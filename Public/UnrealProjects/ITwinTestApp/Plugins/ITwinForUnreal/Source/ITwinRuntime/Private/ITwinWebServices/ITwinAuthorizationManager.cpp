/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationManager.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinAuthorizationManager.h"

#include <EncryptionContextOpenSSL.h>

#include <ITwinServerEnvironment.h>
#include <PlatformHttp.h>
#include <HttpServerModule.h>
#include <IHttpRouter.h>
#include <HttpModule.h>

#include <Misc/App.h>
#include <Misc/Base64.h>
#include <Misc/EngineVersionComparison.h>

#include <Serialization/ArchiveProxy.h>
#include <Serialization/MemoryReader.h>
#include <Serialization/MemoryWriter.h>

#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <PlatformCryptoTypes.h>
#include <HAL/FileManager.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Network/HttpRequest.h>
#	include <Core/Network/IHttpRouter.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace
{
	// only used for unit tests
	static FString TokenFileSuffix;


	TArray<uint8> ConvertToKey(FString KeyRoot)
	{
		// This handler uses AES256, which has 32-byte keys.
		static const int32 KeySizeInBytes = 32;
		if (!ensure(KeyRoot.Len() > 0))
		{
			KeyRoot += "0";
		}
		while (KeyRoot.Len() < KeySizeInBytes)
		{
			KeyRoot.Append(*KeyRoot.Reverse());
		}
		TArray<uint8> Key;
		Key.Reset(KeySizeInBytes);
		Key.Append(
			TArrayView<const uint8>((const uint8*)StringCast<ANSICHAR>(*KeyRoot).Get(), KeySizeInBytes).GetData(), KeySizeInBytes);
		return Key;
	}

	/// Return an AES256 (symmetric) key
	TArray<uint8> GetKey(AdvViz::SDK::EITwinEnvironment Env, int KeyIndex = 0)
	{
		FString SepChar;
		if (KeyIndex > 0)
		{
			SepChar.AppendChar(TCHAR('0') + KeyIndex);
		}
		// Build a deterministic key from the application ID and user/computer data. The goal of this
		// encryption is just to secure the token for an external individual not having access to the code of
		// the plugin...
		FString KeyRoot = FString(FPlatformProcess::ComputerName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += SepChar;
		KeyRoot += FString(FPlatformProcess::UserName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += SepChar;
		KeyRoot += FString(AdvViz::SDK::ITwinAuthManager::GetAppID(Env).c_str()).Reverse().Replace(TEXT("-"), TEXT("A"));

		return ConvertToKey(KeyRoot);
	}

	bool EncryptToken(FString const& Token, TArray<uint8> const& Key, TArray<uint8>& OutCryptedData)
	{
		if (Key.Num() != 32)
		{
			ensureMsgf(false, TEXT("wrong key"));
			return false;
		}
		EPlatformCryptoResult EncryptResult = EPlatformCryptoResult::Failure;
		TArray<uint8> OutCiphertext = FEncryptionContextOpenSSL().Encrypt_AES_256_ECB(
			TArrayView<const uint8>(
				(const uint8*)StringCast<ANSICHAR>(*Token).Get(), Token.Len()),
			Key, EncryptResult);
		if (EncryptResult != EPlatformCryptoResult::Success)
		{
			return false;
		}
		TArray<uint8> rawData;
		FMemoryWriter memWriter(rawData, true);
		FArchiveProxy archive(memWriter);
		memWriter.SetIsSaving(true);

		uint32 TokenLen = Token.Len();
		archive << TokenLen;
		archive << OutCiphertext;

		OutCryptedData = rawData;
		return true;
	}

	bool DecryptToken(TArray<uint8> const& InCryptedData, TArray<uint8> const& Key, FString& OutToken)
	{
		FMemoryReader memReader(InCryptedData, true);
		FArchiveProxy archive(memReader);
		memReader.SetIsLoading(true);

		uint32 TokenLen = 0;
		TArray<uint8> Ciphertext;
		archive << TokenLen;
		archive << Ciphertext;

		if (TokenLen == 0)
		{
			return false;
		}

		EPlatformCryptoResult DecryptResult = EPlatformCryptoResult::Failure;
		TArray<uint8> Plaintext = FEncryptionContextOpenSSL().Decrypt_AES_256_ECB(
			Ciphertext, Key, DecryptResult);
		if (DecryptResult != EPlatformCryptoResult::Success)
		{
			return false;
		}
		if (Plaintext.Num() < (int32)TokenLen)
		{
			return false;
		}
		OutToken.Reset(TokenLen);
		for (uint32 i = 0; i < TokenLen; ++i)
		{
			OutToken.AppendChar(static_cast<TCHAR>(Plaintext[i]));
		}
		return true;
	}

	FString GetTokenFilename(AdvViz::SDK::EITwinEnvironment Env, FString const& FileSuffix, bool bCreateDir)
	{
		FString OutDir = FPlatformProcess::UserSettingsDir();
		if (OutDir.IsEmpty())
		{
			return {};
		}
		FString const TokenDir = FPaths::Combine(OutDir, TEXT("Bentley"), TEXT("Cache"));
		if (bCreateDir && !IFileManager::Get().DirectoryExists(*TokenDir))
		{
			IFileManager::Get().MakeDirectory(*TokenDir, true);
		}
		::EITwinEnvironment const eEnv = static_cast<::EITwinEnvironment>(Env);
		return FPaths::Combine(TokenDir,
			ITwinServerEnvironment::GetUrlPrefix(eEnv) + TEXT("AdvVizCnx") + FileSuffix + TokenFileSuffix + TEXT(".dat"));
	}

	/// Connect UE HttpRouter implementation to ITwin SDK.
	class FUEHttpRouter : public AdvViz::SDK::IHttpRouter, public AdvViz::SDK::Tools::TypeId<FUEHttpRouter>
	{
	public:
		struct FUERouteHandle : public AdvViz::SDK::IHttpRouter::RouteHandle
		{
			// Beware we have a shared pointer of a shared pointer here...
			TSharedPtr<FHttpRouteHandle> Ptr = MakeShared<FHttpRouteHandle>();

			FHttpRouteHandle& Get() const {
				return *Ptr;
			}
			virtual bool IsValid() const override
			{
				return Ptr && Ptr->Get() != nullptr;
			}
		};
		virtual RouteHandlePtr MakeRouteHandler() const override
		{
			return std::make_shared<FUERouteHandle>();
		}

		virtual bool BindRoute(
			RouteHandlePtr& routeHandlePtr,
			int Port, std::string const& redirectUriEndpoint, AdvViz::SDK::EVerb eVerb,
			RequestHandlerCallback const& requestHandlerCB) const override
		{
			auto routeHandle = std::static_pointer_cast<FUERouteHandle>(routeHandlePtr);
			if (!routeHandle)
				return false;
			EHttpServerRequestVerbs requestsVerb = EHttpServerRequestVerbs::VERB_NONE;
			switch (eVerb)
			{
			case AdvViz::SDK::EVerb::Delete:	requestsVerb = EHttpServerRequestVerbs::VERB_DELETE; break;
			case AdvViz::SDK::EVerb::Get:		requestsVerb = EHttpServerRequestVerbs::VERB_GET; break;
			case AdvViz::SDK::EVerb::Patch:	requestsVerb = EHttpServerRequestVerbs::VERB_PATCH; break;
			case AdvViz::SDK::EVerb::Post:	requestsVerb = EHttpServerRequestVerbs::VERB_POST; break;
			case AdvViz::SDK::EVerb::Put:		requestsVerb = EHttpServerRequestVerbs::VERB_PUT; break;
			default:
				BE_ISSUE("unknown verb", eVerb);
				break;
			}
			routeHandle->Get() = FHttpServerModule::Get().GetHttpRouter(Port)
				->BindRoute(FHttpPath(redirectUriEndpoint.c_str()), requestsVerb,
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
					FHttpRequestHandler::CreateLambda(
#endif
						[routeHandlePtr, Port, coreRequestHandler = requestHandlerCB]
						(const FHttpServerRequest& request, const FHttpResultCallback& onComplete)
			{
				std::map<std::string, std::string> queryParams;
				for (auto const& [Key, Value] : request.QueryParams)
				{
					queryParams[TCHAR_TO_UTF8(*Key)] = TCHAR_TO_UTF8(*Value);
				}
				std::string HtmlText;
				if (coreRequestHandler)
				{
					coreRequestHandler(queryParams, HtmlText);
				}

				onComplete(FHttpServerResponse::Create(HtmlText.c_str(), TEXT("text/html")));

				FHttpServerModule::Get().StopAllListeners();

				auto routeHandle = std::static_pointer_cast<FUERouteHandle>(routeHandlePtr);
				if (routeHandle && routeHandle->Get())
				{
					FHttpServerModule::Get().GetHttpRouter(Port)->UnbindRoute(routeHandle->Get());
				}
				return true;
			}
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
			)
#endif
			);
			FHttpServerModule::Get().StartAllListeners();
			return true;
		}

		using AdvViz::SDK::Tools::TypeId<FUEHttpRouter>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::IHttpRouter::IsTypeOf(i); }
	};
}

/*static*/
bool FITwinAuthorizationManager::bUseExternalBrowser = true;

/*static*/
void FITwinAuthorizationManager::SetUseExternalBrowser(bool bInUseExternalBrowser)
{
	bUseExternalBrowser = bInUseExternalBrowser;
}

/*static*/
bool FITwinAuthorizationManager::UseExternalBrowser()
{
	return bUseExternalBrowser;
}

FITwinAuthorizationManager::FExternalBrowserDisabler::FExternalBrowserDisabler()
	: bOldUseExternalBrowser(UseExternalBrowser())
{
	SetUseExternalBrowser(false);
}

FITwinAuthorizationManager::FExternalBrowserDisabler::~FExternalBrowserDisabler()
{
	// Restore previous state.
	SetUseExternalBrowser(bOldUseExternalBrowser);
}

/*static*/
void FITwinAuthorizationManager::OnStartup()
{
	// Adapt Unreal to SDK Core's authentication management

	using namespace AdvViz::SDK;

	ITwinAuthManager::SetNewFct([](AdvViz::SDK::EITwinEnvironment Env) {
		ITwinAuthManager* p(static_cast<ITwinAuthManager*>(new FITwinAuthorizationManager(Env)));
		return p;
	});

	AdvViz::SDK::IHttpRouter::SetNewFct([]() {
		AdvViz::SDK::IHttpRouter* p(static_cast<AdvViz::SDK::IHttpRouter*>(new FUEHttpRouter()));
		return p;
	});
}

FITwinAuthorizationManager::FITwinAuthorizationManager(AdvViz::SDK::EITwinEnvironment Env)
	: AdvViz::SDK::ITwinAuthManager(Env)
{

}

FITwinAuthorizationManager::~FITwinAuthorizationManager()
{

}

bool FITwinAuthorizationManager::SavePrivateData(std::string const& data, int keyIndex /*= 0*/) const
{
	FString FileSuffix;
	if (keyIndex > 0)
	{
		FileSuffix = FString::Printf(TEXT("_%d"), keyIndex);
	}
	return SavePrivateData(FString(data.c_str()), env_, keyIndex, FileSuffix);
}

bool FITwinAuthorizationManager::LoadPrivateData(std::string& data, int keyIndex /*= 0*/) const
{
	FString FileSuffix;
	if (keyIndex > 0)
	{
		FileSuffix = FString::Printf(TEXT("_%d"), keyIndex);
	}
	FString Data;
	if (!LoadPrivateData(Data, env_, keyIndex, FileSuffix))
	{
		return false;
	}
	data = TCHAR_TO_UTF8(*Data);
	return true;
}

bool FITwinAuthorizationManager::StartAuthorizationInstance(std::string const& state, std::string const& codeVerifier, std::string& error)
{
	TArray<uint8> verifierSha;
	FEncryptionContextOpenSSL().CalcSHA256(TArrayView<const uint8>(
		(const uint8*)codeVerifier.c_str(), codeVerifier.length()),
		verifierSha);

	auto CodeChallenge = FBase64::Encode(verifierSha, EBase64Mode::UrlSafe).Replace(TEXT("="), TEXT(""));

	FString const RedirectUri = FString(AdvViz::SDK::ITwinAuthManager::GetRedirectUri().c_str());

	FString const PromptParam = HasRefreshToken() ? TEXT("&prompt=none") : TEXT("");

	FString const LaunchURL = FString(GetIMSBaseUrl().c_str())
		+ "/connect/authorize?response_type=code"
		+ "&client_id=" + GetAppID().c_str()
		+ "&redirect_uri=" + FPlatformHttp::UrlEncode(RedirectUri)
		+ "&scope=" + FPlatformHttp::UrlEncode(FString(GetScope().c_str()))
		+ PromptParam
		+ "&state=" + state.c_str()
		+ "&code_challenge=" + CodeChallenge
		+ "&code_challenge_method=S256";
	// Store the authorization URL (useful in case we do not use the external web browser...)
	this->SetAuthorizationURL(TCHAR_TO_UTF8(*LaunchURL));

	if (UseExternalBrowser())
	{
		FString Error;
		FPlatformProcess::LaunchURL(*LaunchURL, nullptr, &Error);
		if (!Error.IsEmpty())
		{
			error = TCHAR_TO_UTF8(*FString::Printf(TEXT("Could not launch web browser! %s"), *Error));
			return false;
		}
	}
	return true;
}


#if WITH_TESTS

/*static*/
void FITwinAuthorizationManager::SetupTestMode(AdvViz::SDK::EITwinEnvironment Env, FString const& InTokenFileSuffix)
{
	// for unit tests, allow running without iTwin App ID, and use a special suffix for filenames to avoid
	// any conflict with the normal run.
	if (!ITwinAuthManager::HasAppID(Env))
	{
		ITwinAuthManager::SetAppIDArray({ "ThisIsADummyAppIDForTesting" });
	}
	checkf(!InTokenFileSuffix.IsEmpty(), TEXT("a unique suffix is required to avoid conflicts"));
	TokenFileSuffix = InTokenFileSuffix;
}

#endif // WITH_TESTS

/*static*/
bool FITwinAuthorizationManager::SavePrivateData(FString const& Token, AdvViz::SDK::EITwinEnvironment Env, int KeyIndex,
	FString const& FileSuffix)
{
	const bool bIsDeletingToken = Token.IsEmpty();
	FString OutputFileName = GetTokenFilename(Env, FileSuffix, !bIsDeletingToken);
	if (OutputFileName.IsEmpty())
	{
		return false;
	}
	if (bIsDeletingToken)
	{
		// just remove the file, if it exists: this will discard any old refresh token
		if (IFileManager::Get().FileExists(*OutputFileName))
		{
			IFileManager::Get().Delete(*OutputFileName);
		}
		return true;
	}

	auto const Key = GetKey(Env, KeyIndex);

	TArray<uint8> CryptedData;
	if (!EncryptToken(Token, Key, CryptedData))
	{
		return false;
	}
	if (CryptedData.Num())
	{
		return FFileHelper::SaveArrayToFile(CryptedData, *OutputFileName);
	}
	else
	{
		return false;
	}
}

/*static*/
bool FITwinAuthorizationManager::SaveToken(FString const& Token, AdvViz::SDK::EITwinEnvironment Env)
{
	return SavePrivateData(Token, Env, 0, {});
}

/*static*/
bool FITwinAuthorizationManager::LoadPrivateData(FString& OutToken, AdvViz::SDK::EITwinEnvironment Env, int KeyIndex,
	FString const& FileSuffix)
{
	auto const Key = GetKey(Env, KeyIndex);
	if (Key.Num() != 32)
	{
		ensureMsgf(false, TEXT("wrong key"));
		return false;
	}
	FString TokenFileName = GetTokenFilename(Env, FileSuffix, false);
	if (!FPaths::FileExists(TokenFileName))
	{
		return false;
	}

	TArray<uint8> rawData;
	if (!FFileHelper::LoadFileToArray(rawData, *TokenFileName))
	{
		return false;
	}
	return DecryptToken(rawData, Key, OutToken);
}

/*static*/
bool FITwinAuthorizationManager::LoadToken(FString& OutToken, AdvViz::SDK::EITwinEnvironment Env)
{
	return LoadPrivateData(OutToken, Env, 0, {});
}

/*static*/
void FITwinAuthorizationManager::DeleteTokenFile(AdvViz::SDK::EITwinEnvironment Env)
{
	SaveToken({}, Env);
}

/*static*/
bool FITwinAuthorizationManager::EncodeTokenData(FString const& InToken, FString const& InKeyRoot, FString& OutEncoded)
{
	auto const Key = ConvertToKey(InKeyRoot);
	TArray<uint8> CryptedData;
	if (!EncryptToken(InToken, Key, CryptedData) || CryptedData.IsEmpty())
	{
		return false;
	}
	auto const Crypted_B64 = FBase64::Encode(CryptedData, EBase64Mode::UrlSafe);
	if (Crypted_B64.IsEmpty())
	{
		return false;
	}
	OutEncoded = Crypted_B64;
	return true;
}

bool FITwinAuthorizationManager::EncodeToken(std::string const& InToken, std::string const& InKeyRoot, std::string& OutEncoded) const
{
	FString Encoded;
	if (!EncodeTokenData(ANSI_TO_TCHAR(InToken.c_str()), ANSI_TO_TCHAR(InKeyRoot.c_str()), Encoded))
	{
		return false;
	}
	OutEncoded = TCHAR_TO_ANSI(*Encoded);
	return true;
}

/*static*/
bool FITwinAuthorizationManager::DecodeTokenData(FString const& InEncoded, FString const& InKeyRoot, FString& OutToken)
{
	TArray<uint8> Crypted;
	if (!FBase64::Decode(InEncoded, Crypted, EBase64Mode::UrlSafe))
	{
		return false;
	}
	auto const Key = ConvertToKey(InKeyRoot);
	FString DecodedToken;
	if (!DecryptToken(Crypted, Key, DecodedToken))
	{
		return false;
	}
	OutToken = TCHAR_TO_ANSI(*DecodedToken);
	return true;
}

bool FITwinAuthorizationManager::DecodeToken(std::string const& InEncoded, std::string const& InKeyRoot, std::string& OutToken) const
{
	FString DecodedToken;
	if (!DecodeTokenData(ANSI_TO_TCHAR(InEncoded.c_str()), ANSI_TO_TCHAR(InKeyRoot.c_str()), DecodedToken))
	{
		return false;
	}
	OutToken = TCHAR_TO_ANSI(*DecodedToken);
	return true;
}
