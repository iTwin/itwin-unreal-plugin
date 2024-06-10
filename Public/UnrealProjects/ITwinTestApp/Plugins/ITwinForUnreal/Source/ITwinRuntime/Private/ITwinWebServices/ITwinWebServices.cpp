/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServices.h>


#include <EncryptionContextOpenSSL.h>
#include <HttpModule.h>

#include "ITwinAuthorizationManager.h"

#include <ITwinServerEnvironment.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <Kismet/GameplayStatics.h>

#include <Serialization/ArchiveProxy.h>
#include <Serialization/MemoryReader.h>
#include <Serialization/MemoryWriter.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <Misc/FileHelper.h>
#include <PlatformCryptoTypes.h>
#include <HAL/FileManager.h>

#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	extern FString GetITwinAppId(EITwinEnvironment Env);
}

namespace
{
	inline FString GetITwinAPIRootUrl(EITwinEnvironment Env)
	{
		return TEXT("https://") + ITwinServerEnvironment::GetUrlPrefix(Env) + TEXT("api.bentley.com");
	}

	static UITwinWebServices* WorkingInstance = nullptr;

	struct [[nodiscard]] ScopedWorkingWebServices
	{
		UITwinWebServices* PreviousInstance = WorkingInstance;

		ScopedWorkingWebServices(UITwinWebServices* CurrentInstance)
		{
			check(!WorkingInstance);
			WorkingInstance = CurrentInstance;
		}

		~ScopedWorkingWebServices()
		{
			WorkingInstance = PreviousInstance;
		}
	};

	// only used for unit tests
	static FString TokenFileSuffix;


	/// Return an AES256 (symmetric) key
	TArray<uint8> GetKey(EITwinEnvironment Env)
	{
		// This handler uses AES256, which has 32-byte keys.
		static const int32 KeySizeInBytes = 32;

		// Build a deterministic key from the application ID and user/computer data. The goal of this
		// encryption is just to secure the token for an external individual not having access to the code of
		// the plugin...
		FString KeyRoot = FString(FPlatformProcess::ComputerName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += FString(FPlatformProcess::UserName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += ITwin::GetITwinAppId(Env).Reverse().Replace(TEXT("-"), TEXT("A"));
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

	FString GetTokenFilename(EITwinEnvironment Env, bool bCreateDir)
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
		return FPaths::Combine(TokenDir,
			ITwinServerEnvironment::GetUrlPrefix(Env) + TEXT("AdvVizCnx") + TokenFileSuffix + TEXT(".dat"));
	}


	//// Json helpers imported from 3dft

	TSharedPtr<FJsonObject> GetChildObject(TSharedPtr<FJsonObject> JsonRoot, FString Element)
	{
		TArray<FString> Names;
		Element.ParseIntoArray(Names, TEXT("/"), true);

		for (const auto& Name : Names)
		{
			auto JsonChild = JsonRoot->GetObjectField(Name);
			if (!JsonChild)
			{
				return nullptr;
			}
			JsonRoot = JsonChild;
		}
		return JsonRoot;
	}

	FVector GetFVector(TSharedPtr<FJsonObject> JsonObject, FString name)
	{
		auto JsonArray = JsonObject->GetArrayField(*name);
		if (!JsonArray.Num())
		{
			return FVector(0, 0, 0);
		}
		return FVector(JsonArray[0]->AsNumber(), JsonArray[1]->AsNumber(), JsonArray[2]->AsNumber());
	}

	FRotator GetFRotator(TSharedPtr<FJsonObject> JsonObject, FString name)
	{
		JsonObject = JsonObject->GetObjectField(*name);
		if (!JsonObject)
		{
			return FRotator(0, 0, 0);
		}
		return FRotator(JsonObject->GetNumberField("pitch"), JsonObject->GetNumberField("yaw"), JsonObject->GetNumberField("roll"));
	}
}

/*static*/
void UITwinWebServices::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs)
{
	FITwinAuthorizationManager::SetITwinAppIDArray(ITwinAppIDs);
}

/*static*/ 
UITwinWebServices* UITwinWebServices::GetWorkingInstance()
{
	return WorkingInstance;
}

#if WITH_TESTS
/*static*/
void UITwinWebServices::SetupTestMode(EITwinEnvironment Env)
{
	// for unit tests, allow running without iTwin App ID, and use a special suffix for filenames to avoid
	// any conflict with the normal run.
	if (!FITwinAuthorizationManager::HasITwinAppID(Env))
	{
		UITwinWebServices::SetITwinAppIDArray({ TEXT("ThisIsADummyAppIDForTesting") });
	}
	TokenFileSuffix = TEXT("_Test");
}
#endif //WITH_TESTS

/*static*/
bool UITwinWebServices::SaveToken(FString const& Token, EITwinEnvironment Env)
{
	const bool bIsDeletingToken = Token.IsEmpty();
	FString OutputFileName = GetTokenFilename(Env, !bIsDeletingToken);
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
	EPlatformCryptoResult EncryptResult = EPlatformCryptoResult::Failure;
	TArray<uint8> OutCiphertext = FEncryptionContextOpenSSL().Encrypt_AES_256_ECB(
		TArrayView<const uint8>(
			(const uint8*)StringCast<ANSICHAR>(*Token).Get(), Token.Len()),
		GetKey(Env), EncryptResult);
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

	if (rawData.Num())
	{
		return FFileHelper::SaveArrayToFile(rawData, *OutputFileName);
	}
	else
	{
		return false;
	}
}

/*static*/
bool UITwinWebServices::LoadToken(FString& OutToken, EITwinEnvironment Env)
{
	FString TokenFileName = GetTokenFilename(Env, false);
	if (!FPaths::FileExists(TokenFileName))
	{
		return false;
	}

	TArray<uint8> rawData;
	if (!FFileHelper::LoadFileToArray(rawData, *TokenFileName))
	{
		return false;
	}

	FMemoryReader memReader(rawData, true);
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

	EPlatformCryptoResult EncryptResult = EPlatformCryptoResult::Failure;
	TArray<uint8> Plaintext = FEncryptionContextOpenSSL().Decrypt_AES_256_ECB(
		Ciphertext, GetKey(Env), EncryptResult);
	if (EncryptResult != EPlatformCryptoResult::Success)
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

/*static*/
void UITwinWebServices::DeleteTokenFile(EITwinEnvironment Env)
{
	SaveToken({}, Env);
}

/*static*/
bool UITwinWebServices::GetErrorDescription(FJsonObject const& responseJson, FString& OutError,
	FString const& Indent)
{
	if (!responseJson.HasField(TEXT("error")))
	{
		return false;
	}

	// see https://developer.bentley.com/apis/issues-v1/operations/get-workflow/
	// (search "error-response" section)

	TSharedPtr<FJsonObject> const ErrorObject = responseJson.GetObjectField(TEXT("error"));
	FString errorCode = ErrorObject->GetStringField("code");
	FString errorMessage = ErrorObject->GetStringField("message");
	FString const newLine = TEXT("\n") + Indent;
	OutError += newLine + FString::Printf(TEXT("Error [%s]: %s"), *errorCode, *errorMessage);

	const TArray<TSharedPtr<FJsonValue>>* detailsJson = nullptr;
	if (ErrorObject->TryGetArrayField(TEXT("details"), detailsJson))
	{
		for (const auto& detailValue : *detailsJson)
		{
			const auto& detailObject = detailValue->AsObject();
			FString strDetail;
			FString strCode, strMsg, strTarget;
			if (detailObject->TryGetStringField(TEXT("code"), strCode))
			{
				strDetail += FString::Printf(TEXT("[%s] "), *strCode);
			}
			if (detailObject->TryGetStringField(TEXT("message"), strMsg))
			{
				strDetail += strMsg;
			}
			if (detailObject->TryGetStringField(TEXT("target"), strTarget))
			{
				strDetail += FString::Printf(TEXT(" (target: %s)"), *strTarget);
			}
			if (!strDetail.IsEmpty())
			{
				OutError += newLine + FString::Printf(TEXT("Details: %s"), *strDetail);
			}
		}
	}
	return true;
}

class UITwinWebServices::FImpl
{
	friend class UITwinWebServices;

	UITwinWebServices& owner_;

	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<std::recursive_mutex>;
	mutable FMutex mutex_;
	std::shared_ptr< std::atomic_bool > isThisValid_ = std::make_shared< std::atomic_bool >(true); // same principle as in #FReusableJsonQueries::FImpl
	IITwinWebServicesObserver* observer_ = nullptr;
	FString lastError_;

	// Some data (mostly tokens) are unique per environment - thus their management is centralized
	using SharedMngrPtr = FITwinAuthorizationManager::SharedInstance;
	SharedMngrPtr authManager_;

public:
	FImpl(UITwinWebServices& Owner)
		: owner_(Owner)
	{

	}

	~FImpl()
	{
		*isThisValid_ = false;

		if (authManager_)
		{
			authManager_->RemoveObserver(&owner_);
		}
	}
};


UITwinWebServices::UITwinWebServices()
	: Impl(MakePimpl<UITwinWebServices::FImpl>(*this))
{

}


bool UITwinWebServices::IsAuthorizationInProgress() const
{
	FImpl::FLock Lock(Impl->mutex_);
	if (ServerConnection && !ServerConnection->AccessToken.IsEmpty())
	{
		// Authorization already proceed.
		return false;
	}
	if (!Impl->authManager_)
	{
		// Never started.
		return false;
	}
	return Impl->authManager_->IsAuthorizationInProgress();
}

void UITwinWebServices::SetServerConnection(TObjectPtr<AITwinServerConnection> const& InConnection)
{
	FImpl::FLock Lock(Impl->mutex_);
	ServerConnection = InConnection;
	if (ServerConnection)
	{
		this->Environment = ServerConnection->Environment;
	}
}

bool UITwinWebServices::TryGetServerConnection(bool bAllowBroadcastAuthResult)
{
	{
		FImpl::FLock Lock(Impl->mutex_);
		if (ServerConnection)
		{
			return true;
		}
	}

	// Initiate the manager handling tokens for current Environment
	if (!Impl->authManager_)
	{
		Impl->authManager_ = FITwinAuthorizationManager::GetInstance(Environment);
		Impl->authManager_->AddObserver(this);
	}

	// First try to use existing access token for current Environment, if any.
	if (Impl->authManager_->HasAccessToken())
	{
		OnAuthDoneImpl(true, {}, bAllowBroadcastAuthResult);
		return true;
	}

	// No valid server connection
	return false;
}

void UITwinWebServices::CheckAuthorization()
{
	if (TryGetServerConnection(true))
	{
		// We could get a valid server connection. No need to do anything more (note that the token
		// will be automatically refreshed when approaching its expiration: no need to check that).
		return;
	}
	Impl->authManager_->CheckAuthorization();
}

void UITwinWebServices::OnAuthDoneImpl(bool bSuccess, FString const& Error, bool bBroadcastResult /*= true*/)
{
	ScopedWorkingWebServices WorkingInstanceSetter(this);

	if (bSuccess)
	{
		FImpl::FLock Lock(Impl->mutex_);
		if (!ServerConnection)
		{
			// First see if an existing connection actor for this environment can be reused
			TArray<AActor*> itwinServerActors;
			UGameplayStatics::GetAllActorsOfClass(
				GetWorld(), AITwinServerConnection::StaticClass(), itwinServerActors);
			for (AActor* servConn : itwinServerActors)
			{
				AITwinServerConnection* existingConnection = Cast<AITwinServerConnection>(servConn);
				if (existingConnection
					&& existingConnection->IsValidLowLevelFast(false)
					&& existingConnection->Environment == this->Environment)
				{
					ServerConnection = existingConnection;
					break;
				}
			}
		}
		if (!ServerConnection)
		{
			ServerConnection = GetWorld()->SpawnActor<AITwinServerConnection>();
		}
		ServerConnection->Environment = this->Environment;
		Impl->authManager_->GetAccessToken(ServerConnection->AccessToken);
		checkf(!ServerConnection->AccessToken.IsEmpty(), TEXT("Upon success, an access token is expected!"));
	}

	if (bBroadcastResult)
	{
		OnAuthorizationChecked.Broadcast(bSuccess, Error);
		if (Impl->observer_)
		{
			Impl->observer_->OnAuthorizationDone(bSuccess, Error);
		}
	}
}

void UITwinWebServices::OnAuthorizationDone(bool bSuccess, FString const& Error)
{
	OnAuthDoneImpl(bSuccess, Error, true);
}

FString UITwinWebServices::GetAuthToken() const
{
	FString authToken;
	FImpl::FLock Lock(Impl->mutex_);
	if (ServerConnection && ServerConnection->IsValidLowLevelFast(false))
	{
		authToken = ServerConnection->AccessToken;
	}
	return authToken;
}

void UITwinWebServices::GetServerConnection(TObjectPtr<AITwinServerConnection>& OutConnection) const
{
	FImpl::FLock Lock(Impl->mutex_);
	if (ServerConnection && ServerConnection->IsValidLowLevelFast(false))
	{
		OutConnection = ServerConnection;
	}
	else
	{
		OutConnection = {};
	}
}

bool UITwinWebServices::HasSameConnection(AITwinServerConnection const* Connection) const
{
	FImpl::FLock Lock(Impl->mutex_);
	return ServerConnection.Get() == Connection;
}

void UITwinWebServices::SetObserver(IITwinWebServicesObserver* InObserver)
{
	Impl->observer_ = InObserver;
}

void UITwinWebServices::SetLastError(FString const& InError)
{
	FImpl::FLock Lock(Impl->mutex_);
	Impl->lastError_ = InError;
}

FString UITwinWebServices::GetLastError() const
{
	FImpl::FLock Lock(Impl->mutex_);
	return Impl->lastError_;
}

bool UITwinWebServices::ConsumeLastError(FString& OutError)
{
	FImpl::FLock Lock(Impl->mutex_);
	OutError = Impl->lastError_;
	Impl->lastError_ = {};
	return !OutError.IsEmpty();
}

struct UITwinWebServices::FITwinAPIRequestInfo
{
	FString Verb; // "GET" or "POST"...
	FString UrlSuffix;
	FString AcceptHeader;

	FString ContentType;
	FString ContentString;

	TMap<FString, FString> CustomHeaders;
};

template <typename ResultDataType, class FunctorType, class DelegateAsFunctor>
void UITwinWebServices::TProcessHttpRequest(
	FITwinAPIRequestInfo const& RequestInfo,
	FunctorType&& InFunctor,
	DelegateAsFunctor&& InResultFunctor)
{
	// We may have no ServerConnection yet (happens if one instantiates UITwinWebServices and uses it at once
	// without calling SetServerConnection or CheckAuthorization...)
	// In such case, if we can find an existing access token, use it instead of failing directly. However we
	// should not broadcast the authorization success in such case, as this is certainly not expected by the
	// client.
	if (!TryGetServerConnection(false))
	{
		return;
	}
	FString const authToken = GetAuthToken();
	if (authToken.IsEmpty())
	{
		return;
	}
	const auto request = FHttpModule::Get().CreateRequest();
	request->SetVerb(RequestInfo.Verb);
	request->SetURL(GetITwinAPIRootUrl(this->Environment) + RequestInfo.UrlSuffix);

	// Fill headers
	if (!RequestInfo.CustomHeaders.Contains(TEXT("Prefer")))
	{
		request->SetHeader(TEXT("Prefer"), TEXT("return=representation"));
	}
	request->SetHeader(TEXT("Accept"), RequestInfo.AcceptHeader);
	if (!RequestInfo.ContentType.IsEmpty())
	{
		// for "POST" requests typically
		request->SetHeader(TEXT("Content-Type"), RequestInfo.ContentType);
	}
	if (!RequestInfo.ContentString.IsEmpty())
	{
		request->SetContentAsString(RequestInfo.ContentString);
	}
	request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + authToken);

	// add custom headers, if any
	for (const auto& header : RequestInfo.CustomHeaders) {
		request->SetHeader(header.Key, header.Value);
	}

	request->OnProcessRequestComplete().BindLambda(
		[this,
		IsValidLambda = this->Impl->isThisValid_,
		ResultCallback = Forward<DelegateAsFunctor>(InResultFunctor),
		ResponseProcessor = Forward<FunctorType>(InFunctor)]
		(FHttpRequestPtr request, FHttpResponsePtr response, bool connectedSuccessfully)
	{
		if (!(*IsValidLambda))
		{
			// see comments in #ReusableJsonQueries.cpp
			return;
		}
		bool bValidResponse = false;
		FString requestError;
		Be::CleanUpGuard setErrorGuard([this, &bValidResponse, &requestError, &ResultCallback]
		{
			this->SetLastError(requestError);
			if (!bValidResponse)
			{
				ResultCallback(false, {});
			}
		});

		if (!AITwinServerConnection::CheckRequest(request, response, connectedSuccessfully, &requestError))
		{
			return;
		}
		TSharedPtr<FJsonObject> responseJson;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(response->GetContentAsString()), responseJson);

		ResultDataType ResultData;
		if (responseJson)
		{
			bValidResponse = ResponseProcessor(ResultData, *responseJson);
		}
		ScopedWorkingWebServices WorkingInstanceSetter(this);
		ResultCallback(bValidResponse, ResultData);
	});
	request->ProcessRequest();
}

void UITwinWebServices::GetITwinInfo(FString ITwinId)
{
	const FITwinAPIRequestInfo iTwinRequestInfo = {
		TEXT("GET"),
		TEXT("/itwins/" + ITwinId),
		TEXT("application/vnd.bentley.itwin-platform.v1+json")
	};
	TProcessHttpRequest<FITwinInfo>(
		iTwinRequestInfo,
		[](FITwinInfo& iTwinInfo, FJsonObject const& responseJson) -> bool
	{
		auto itwinJson = responseJson.GetObjectField("iTwin");
		if (!itwinJson)
		{
			return false;
		}
		iTwinInfo.Id = itwinJson->GetStringField("id");
		iTwinInfo.DisplayName = itwinJson->GetStringField("displayName");
		iTwinInfo.Status = itwinJson->GetStringField("status");
		iTwinInfo.Number = itwinJson->GetStringField("number");
		return true;
	},
		[this](bool bResult, FITwinInfo const& ResultData)
	{
		this->OnGetITwinInfoComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnITwinInfoRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetiTwins()
{
	static const FITwinAPIRequestInfo iTwinsRequestInfo = {
		TEXT("GET"),
		TEXT("/itwins/recents?subClass=Project&status=Active&$top=1000"),
		TEXT("application/vnd.bentley.itwin-platform.v1+json")
	};
	TProcessHttpRequest<FITwinInfos>(
		iTwinsRequestInfo,
		[](FITwinInfos& iTwinInfos, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* itwinsJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("iTwins"), itwinsJson))
		{
			return false;
		}
		for (const auto& iTwinValue : *itwinsJson)
		{
			const auto& iTwinObject = iTwinValue->AsObject();

			FITwinInfo iTwinInfo = {
				iTwinObject->GetStringField("id"),
				iTwinObject->GetStringField("displayName"),
				iTwinObject->GetStringField("status"),
				iTwinObject->GetStringField("number")
			};
			iTwinInfos.iTwins.Push(iTwinInfo);
		}
		return true;
	},
		[this](bool bResult, FITwinInfos const& ResultData)
	{
		this->OnGetiTwinsComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnITwinsRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetiTwiniModels(FString ITwinId)
{
	const FITwinAPIRequestInfo iModelsRequestInfo = {
		TEXT("GET"),
		TEXT("/imodels/?iTwinId=") + ITwinId + TEXT("&$top=100"),
		TEXT("application/vnd.bentley.itwin-platform.v2+json")
	};
	TProcessHttpRequest<FIModelInfos>(
		iModelsRequestInfo,
		[](FIModelInfos& iModelInfos, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* imodelsJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("iModels"), imodelsJson))
		{
			return false;
		}
		for (const auto& IModelValue : *imodelsJson)
		{
			auto const& IModelObject = IModelValue->AsObject();

			FIModelInfo iModel = {
				IModelObject->GetStringField("id"),
				IModelObject->GetStringField("displayName")
			};
			iModelInfos.iModels.Push(iModel);
		}
		return true;
	},
		[this](bool bResult, FIModelInfos const& ResultData)
	{
		this->OnGetiTwiniModelsComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnIModelsRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::DoGetiModelChangesets(FString const& IModelId, bool bRestrictToLatest)
{
	const FITwinAPIRequestInfo changesetsRequestInfo = {
		TEXT("GET"),
		TEXT("/imodels/") + IModelId + TEXT("/changesets?")
		+ (bRestrictToLatest ? TEXT("$top=1&") : TEXT(""))
		+ TEXT("$orderBy=index+desc"),
		TEXT("application/vnd.bentley.itwin-platform.v2+json")
	};
	TProcessHttpRequest<FChangesetInfos>(
		changesetsRequestInfo,
		[](FChangesetInfos& Infos, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* changesetsJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("changesets"), changesetsJson))
		{
			return false;
		}
		for (const auto& ChangesetValue : *changesetsJson)
		{
			auto const& JsonObject = ChangesetValue->AsObject();
			FChangesetInfo Changeset = {
				JsonObject->GetStringField("id"),
				JsonObject->GetStringField("displayName"),
				JsonObject->HasField("description") ? JsonObject->GetStringField("description") : "",
				JsonObject->GetIntegerField("index")
			};
			Infos.Changesets.Push(Changeset);
		}
		return true;
	},
		[this](bool bResult, FChangesetInfos const& ResultData)
	{
		this->OnGetiModelChangesetsComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnChangesetsRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetiModelChangesets(FString IModelId)
{
	DoGetiModelChangesets(IModelId, false);
}

void UITwinWebServices::GetiModelLatestChangeset(FString iModelId)
{
	DoGetiModelChangesets(iModelId, true);
}

void UITwinWebServices::GetExports(FString IModelId, FString ChangesetId)
{
	const FITwinAPIRequestInfo exportsRequestInfo = {
		TEXT("GET"),
		TEXT("/mesh-export/?iModelId=") + IModelId + TEXT("&changesetId=") + ChangesetId,
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),
		{},
		{},
		{
			// The following headers have been added folowing suggestion by Daniel Iborra.
			// This header is supposed to filter exports, but it is not implemented yet on server.
			// Therefore we need to keep our own filter on the response for now.
			{TEXT("exportType"), TEXT("CESIUM")}, 
			{TEXT("cdn"), TEXT("1")}, // Activates CDN, improves performance
			{TEXT("client"), TEXT("Unreal")}, // For stats
			// (end of headers suggested by Daniel Iborra)
		},
	};
	TProcessHttpRequest<FITwinExportInfos>(
		exportsRequestInfo,
		[](FITwinExportInfos& Infos, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* exportsJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("exports"), exportsJson))
		{
			return false;
		}
		for (const auto& ExportValue : *exportsJson)
		{
			const auto& ExportObject = ExportValue->AsObject();
			const auto& RequestObject = ExportObject->GetObjectField("request");
			if (RequestObject->GetStringField("exportType") == "CESIUM" && RequestObject->HasField("exportTypeVersion"))
			{
				FITwinExportInfo Export = {
					ExportObject->GetStringField("id"),
					ExportObject->GetStringField("displayName"),
					ExportObject->GetStringField("status"),
					RequestObject->GetStringField("iModelId"),
					RequestObject->GetStringField("changesetId")
				};
				if (Export.Status == "Complete")
				{
					auto MeshJsonObject = GetChildObject(ExportObject, "_links/mesh");
					if (MeshJsonObject)
					{
						Export.MeshUrl = MeshJsonObject->GetStringField("href").Replace(TEXT("?"), TEXT("/tileset.json?"));
					}
				}
				Infos.ExportInfos.Push(Export);
			}
		}
		return true;
	},
		[this](bool bResult, FITwinExportInfos const& ResultData)
	{
		this->OnGetExportsComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnExportInfosRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetExportInfo(FString ExportId)
{
	const FITwinAPIRequestInfo exportRequestInfo = {
		TEXT("GET"),
		TEXT("/mesh-export/") + ExportId,
		TEXT("application/vnd.bentley.itwin-platform.v1+json")
	};
	TProcessHttpRequest<FITwinExportInfo>(
		exportRequestInfo,
		[](FITwinExportInfo& Export, FJsonObject const& responseJson) -> bool
	{
		auto JsonExport = responseJson.GetObjectField("export");
		if (!JsonExport)
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: Export not defined."));
			return false;
		}

		auto JsonHref = GetChildObject(JsonExport, "request");
		if (!JsonHref)
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: Export request not defined."));
			return false;
		}

		FString const ExportType = JsonHref->GetStringField(TEXT("exportType"));
		if (ExportType != "CESIUM")
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: Export Type is incorrect: %s"), *ExportType);
			return false;
		}
		Export.Id = JsonExport->GetStringField(TEXT("id"));
		Export.DisplayName = JsonExport->GetStringField(TEXT("displayName"));
		Export.Status = JsonExport->GetStringField(TEXT("status"));
		Export.iModelId = JsonHref->GetStringField(TEXT("iModelId"));
		Export.iTwinId = JsonHref->GetStringField(TEXT("contextId"));
		Export.ChangesetId = JsonHref->GetStringField(TEXT("changesetId"));

		if (Export.Status == "Complete")
		{
			JsonHref = GetChildObject(JsonExport, "_links/mesh");
			if (JsonHref)
			{
				Export.MeshUrl = JsonHref->GetStringField("href").Replace(TEXT("?"), TEXT("/tileset.json?"));
			}
		}
		return true;
	},
		[this](bool bResult, FITwinExportInfo const& ResultData)
	{
		this->OnGetExportInfoComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnExportInfoRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::StartExport(FString IModelId, FString ChangesetId)
{
	const FITwinAPIRequestInfo startExportRequest = {
		TEXT("POST"),
		TEXT("/mesh-export"),
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),

		/*** additional settings for POST ***/
		TEXT("application/json"),
		FString::Printf(
			TEXT("{\"iModelId\":\"%s\",\"changesetId\":\"%s\",\"exportType\":\"CESIUM\"}"),
			*IModelId, *ChangesetId)
	};
	TProcessHttpRequest<FString>(
		startExportRequest,
		[IModelId](FString& ExportId, FJsonObject const& responseJson) -> bool
	{
		ExportId = FString();
		auto JsonExport = responseJson.GetObjectField("export");
		if (JsonExport)
		{
			ExportId = JsonExport->GetStringField("id");

			UE_LOG(LogITwinHttp, Display, TEXT("StartExport for %s = OK, export ID:\n%s"),
				*IModelId, *ExportId);
		}
		return !ExportId.IsEmpty();
	},
		[this](bool bResult, FString const& ExportId)
	{
		this->OnStartExportComplete.Broadcast(bResult, ExportId);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnExportStarted(bResult, ExportId);
		}
	});
}

void UITwinWebServices::GetRealityData(FString ITwinId)
{
	const FITwinAPIRequestInfo realDataRequestInfo = {
		TEXT("GET"),
		TEXT("/reality-management/reality-data/?iTwinId=") + ITwinId + TEXT("&types=Cesium3DTiles&$top=100"),
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),
		TEXT(""),
		TEXT(""),
		/* custom headers */
		{
			{ TEXT("Prefer"),	TEXT("return=minimal") },
			{ TEXT("types"),	TEXT("Cesium3DTiles") },
		}
	};
	TProcessHttpRequest<FITwinRealityDataInfos>(
		realDataRequestInfo,
		[ITwinId](FITwinRealityDataInfos& RealityData, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* realityDataJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("realityData"), realityDataJson))
		{
			return false;
		}
		for (const auto& RealityDataValue : *realityDataJson)
		{
			const auto& RealityDataObject = RealityDataValue->AsObject();
			FITwinRealityDataInfo Info;
			Info.DisplayName = RealityDataObject->GetStringField("displayName");
			Info.Id = RealityDataObject->GetStringField("id");
			RealityData.Infos.Push(Info);
		}
		return true;
	},
		[this](bool bResult, FITwinRealityDataInfos const& ResultData)
	{
		this->OnGetRealityDataComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnRealityDataRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetRealityData3DInfo(FString ITwinId, FString RealityDataId)
{
	// two distinct requests are needed here!
	// code moved from ITwinRealityData.cpp)
	const FITwinAPIRequestInfo realDataRequestInfo = {
		TEXT("GET"),
		TEXT("/reality-management/reality-data/") + RealityDataId + TEXT("?iTwinId=") + ITwinId,
		TEXT("application/vnd.bentley.itwin-platform.v1+json")
	};
	TProcessHttpRequest<FITwinRealityData3DInfo>(
		realDataRequestInfo,
		[this, ITwinId, RealityDataId](FITwinRealityData3DInfo& RealityData3DInfo, FJsonObject const& responseJson) -> bool
	{
		auto realitydataJson = responseJson.GetObjectField("realityData");
		if (!realitydataJson)
		{
			return false;
		}
		RealityData3DInfo.Id = RealityDataId;
		RealityData3DInfo.DisplayName = realitydataJson->GetStringField("displayName");

		// Make a second request to retrieve mesh URL
		const FITwinAPIRequestInfo realDataRequestInfo = {
			TEXT("GET"),
			TEXT("/reality-management/reality-data/") + RealityDataId + TEXT("/readaccess?iTwinId=") + ITwinId,
			TEXT("application/vnd.bentley.itwin-platform.v1+json")
		};
		TProcessHttpRequest<FITwinRealityData3DInfo>(
			realDataRequestInfo,
			[this, RealityData3DInfo, realitydataJson]
			(FITwinRealityData3DInfo& FinalRealityData3DInfo, FJsonObject const& responseJson) -> bool
		{
			FinalRealityData3DInfo = RealityData3DInfo;

			const TSharedPtr<FJsonObject>* ExtentJson;
			if (realitydataJson->TryGetObjectField(TEXT("extent"), ExtentJson))
			{
				FinalRealityData3DInfo.bGeolocated = true;
				auto const SW_Json = (*ExtentJson)->GetObjectField("southWest");
				auto const NE_Json = (*ExtentJson)->GetObjectField("northEast");
				FinalRealityData3DInfo.ExtentSouthWest.Latitude = SW_Json->GetNumberField("latitude");
				FinalRealityData3DInfo.ExtentSouthWest.Longitude = SW_Json->GetNumberField("longitude");
				FinalRealityData3DInfo.ExtentNorthEast.Latitude = NE_Json->GetNumberField("latitude");
				FinalRealityData3DInfo.ExtentNorthEast.Longitude = NE_Json->GetNumberField("longitude");
			}

			auto LinksJson = responseJson.GetObjectField("_links");
			if (LinksJson)
			{
				auto MeshJsonObject = LinksJson->GetObjectField("containerUrl");
				if (MeshJsonObject)
				{
					FinalRealityData3DInfo.MeshUrl = MeshJsonObject->GetStringField("href").
						Replace(TEXT("?"), ToCStr("/" + realitydataJson->GetStringField("rootDocument") + "?"));
				}
			}
			return true;
		},
			[this](bool bResult, FITwinRealityData3DInfo const& FinalResultData)
		{
			// This is for the 2nd request: broadcast final result
			this->OnGetRealityData3DInfoComplete.Broadcast(bResult, FinalResultData);
			if (this->Impl->observer_)
			{
				this->Impl->observer_->OnRealityData3DInfoRetrieved(bResult, FinalResultData);
			}
		});

		return true;
	},
		[this](bool bResult, FITwinRealityData3DInfo const& PartialResultData)
	{
		// result of the 1st request: only broadcast it in case of failure
		if (!bResult)
		{
			// the 1st request has failed
			this->OnGetRealityData3DInfoComplete.Broadcast(false, PartialResultData);
			if (this->Impl->observer_)
			{
				this->Impl->observer_->OnRealityData3DInfoRetrieved(false, PartialResultData);
			}
		}
	});
}

void UITwinWebServices::AddSavedView(FString ITwinId, FString IModelId, FSavedView SavedView, FSavedViewInfo SavedViewInfo)
{
	const auto& camPos = SavedView.Origin;
	const auto& camRot = SavedView.Angles;
	const FITwinAPIRequestInfo savedViewsRequestInfo = {
		TEXT("POST"),
		TEXT("/savedviews/"),
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),

		/*** additional settings for POST ***/
		TEXT("application/json"),
		FString::Printf(TEXT("{\"iTwinId\":\"%s\",\"iModelId\":\"%s\",\"savedViewData\":{\"itwin3dView\":{\"origin\":[%.2f,%.2f,%.2f],\"extents\":[0.00,0.00,0.00],\"angles\":{\"yaw\":%.2f,\"pitch\":%.2f,\"roll\":%.2f},\"camera\":{\"lens\":0.0,\"focusDist\":0.0,\"eye\":[%.2f,%.2f,%.2f]}}},\"displayName\":\"%s\",\"shared\":true,\"tagIds\":[]}"),
		*ITwinId, *IModelId, camPos.X, camPos.Y, camPos.Z, camRot.Yaw, camRot.Pitch, camRot.Roll, camPos.X, camPos.Y, camPos.Z, *SavedViewInfo.DisplayName)
	};
	TProcessHttpRequest<FSavedViewInfo>(
		savedViewsRequestInfo,
		[](FSavedViewInfo& Info, FJsonObject const& responseJson) -> bool
		{
			FString DetailedError;
			if (UITwinWebServices::GetErrorDescription(responseJson, DetailedError))
			{
				UE_LOG(LogITwinHttp, Error, TEXT("Error while adding SavedView: %s"), *DetailedError);
				Info = { "","",true };
				return false;
			}
			Info = { responseJson.GetObjectField("savedView")->GetStringField("id"),
					 responseJson.GetObjectField("savedView")->GetStringField("displayName"), true };
			return true;
		},
		[this](bool bResult, FSavedViewInfo const& ResultData)
		{
			this->OnAddSavedViewComplete.Broadcast(bResult, ResultData);
			if (this->Impl->observer_)
			{
				this->Impl->observer_->OnSavedViewAdded(bResult, ResultData);
			}
		}
	);
}

void UITwinWebServices::DeleteSavedView(FString SavedViewId)
{
	const FITwinAPIRequestInfo savedViewsRequestInfo = {
		TEXT("DELETE"),
		TEXT("/savedviews/" + SavedViewId),
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),
	};
	TProcessHttpRequest<FString>(
		savedViewsRequestInfo,
		[](FString& ErrorCode, FJsonObject const& responseJson) -> bool
		{
			FString DetailedError;
			if (UITwinWebServices::GetErrorDescription(responseJson, DetailedError))
			{
				UE_LOG(LogITwinHttp, Error, TEXT("Error while deleting SavedView: %s"), *DetailedError);
				ErrorCode = DetailedError;
				return false;
			}
			ErrorCode = {};
			return true;
		},
		[this](bool bResult, FString const& strResponse)
		{
			this->OnDeleteSavedViewComplete.Broadcast(bResult, strResponse);
			if (this->Impl->observer_)
			{
				this->Impl->observer_->OnSavedViewDeleted(bResult, strResponse);
			}
		}
	);
}

void UITwinWebServices::EditSavedView(FSavedView SavedView, FSavedViewInfo SavedViewInfo)
{
	const auto Request = FHttpModule::Get().CreateRequest();
	const auto& camPos = SavedView.Origin;
	const auto& camRot = SavedView.Angles;
	const FITwinAPIRequestInfo savedViewsRequestInfo = {
		TEXT("PATCH"),
		TEXT("/savedviews/" + SavedViewInfo.Id),
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),

		/*** additional settings for PATCH ***/
		TEXT("application/json"),
		FString::Printf(TEXT("{\"savedViewData\":{\"itwin3dView\":{\"origin\":[%.2f,%.2f,%.2f],\"extents\":[0.00,0.00,0.00],\"angles\":{\"yaw\":%.2f,\"pitch\":%.2f,\"roll\":%.2f},\"camera\":{\"lens\":0.0,\"focusDist\":0.0,\"eye\":[%.2f,%.2f,%.2f]}}},\"displayName\":\"%s\",\"shared\":true,\"tagIds\":[]}"),
		camPos.X, camPos.Y, camPos.Z, camRot.Yaw, camRot.Pitch, camRot.Roll, camPos.X, camPos.Y, camPos.Z, *SavedViewInfo.DisplayName)
	};

	struct FEditSavedViewData
	{
		FSavedView SavedView;
		FSavedViewInfo SavedViewInfo;
	};
	TProcessHttpRequest<FEditSavedViewData>(
		savedViewsRequestInfo,
		[](FEditSavedViewData& EditSVData, FJsonObject const& responseJson) -> bool
		{
			FString DetailedError;
			if (UITwinWebServices::GetErrorDescription(responseJson, DetailedError))
			{
				UE_LOG(LogITwinHttp, Error, TEXT("Error while editing SavedView: %s"), *DetailedError);
				return false;
			}

			const auto& view = responseJson.GetObjectField("savedView");
			const auto& JsonView = GetChildObject(view, "savedViewData/itwin3dView");

			const auto& JsonEye = JsonView->GetObjectField("camera");

			EditSVData.SavedView = { GetFVector(JsonEye, "eye"), GetFVector(JsonView, "extents"), GetFRotator(JsonView, "angles") };
			EditSVData.SavedViewInfo = { view->GetStringField("id"), view->GetStringField("displayName"), view->GetBoolField("shared") };
			return true;
		},
		[this](bool bResult, FEditSavedViewData const& EditSVData)
		{
			this->OnEditSavedViewComplete.Broadcast(bResult, EditSVData.SavedView, EditSVData.SavedViewInfo);
			if (this->Impl->observer_)
			{
				this->Impl->observer_->OnSavedViewEdited(bResult, EditSVData.SavedView, EditSVData.SavedViewInfo);
			}
		}
	);
}

void UITwinWebServices::GetAllSavedViews(FString iTwinId, FString iModelId)
{
	const FITwinAPIRequestInfo savedViewsRequestInfo = {
		TEXT("GET"),
		TEXT("/savedviews?iTwinId=") + iTwinId + TEXT("&iModelId=") + iModelId,
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),
		TEXT("application/json"),
	};
	TProcessHttpRequest<FSavedViewInfos>(
		savedViewsRequestInfo,
		[](FSavedViewInfos& Infos, FJsonObject const& responseJson) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* savedViewsJson = nullptr;
		if (!responseJson.TryGetArrayField(TEXT("savedViews"), savedViewsJson))
		{
			return false;
		}
		for (const auto savedViewValue : *savedViewsJson)
		{
			const auto savedViewObject = savedViewValue->AsObject();
			FSavedViewInfo SavedView = {
				savedViewObject->GetStringField("id"),
				savedViewObject->GetStringField("displayName"),
				savedViewObject->GetBoolField("shared")
			};
			Infos.SavedViews.Push(SavedView);
		}
		return true;
	},
		[this](bool bResult, FSavedViewInfos const& ResultData)
	{
		this->OnGetSavedViewsComplete.Broadcast(bResult, ResultData);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnSavedViewInfosRetrieved(bResult, ResultData);
		}
	});
}

void UITwinWebServices::GetSavedView(FString SavedViewId)
{
	const FITwinAPIRequestInfo savedViewsRequestInfo = {
		TEXT("GET"),
		TEXT("/savedviews/") + SavedViewId,
		TEXT("application/vnd.bentley.itwin-platform.v1+json"),
		TEXT("application/json"),
	};

	struct FSavedViewData
	{
		FSavedView SavedView;
		FSavedViewInfo SavedViewInfo;
	};
	TProcessHttpRequest<FSavedViewData>(
		savedViewsRequestInfo,
		[](FSavedViewData& SVData, FJsonObject const& responseJson) -> bool
	{
		auto JsonSavedView = responseJson.GetObjectField("savedView");
		if (!JsonSavedView)
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: savedView not defined."));
			return false;
		}
		auto JsonView = GetChildObject(JsonSavedView, "savedViewData/itwin3dView");
		if (!JsonView)
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: itwin3dView not defined."));
			return false;
		}
		auto JsonEye = JsonView->GetObjectField("camera");
		if (!JsonEye)
		{
			UE_LOG(LogITwinHttp, Error, TEXT("Invalid Reply: camera not defined."));
			return false;
		}
		SVData.SavedView.Origin = GetFVector(JsonEye, "eye");
		SVData.SavedView.Extents = GetFVector(JsonView, "extents");
		SVData.SavedView.Angles = GetFRotator(JsonView, "angles");
		SVData.SavedViewInfo.Id = JsonSavedView->GetStringField("id");
		SVData.SavedViewInfo.DisplayName = JsonSavedView->GetStringField("displayName");
		SVData.SavedViewInfo.bShared = JsonSavedView->GetBoolField("shared");
		return true;
	},
		[this](bool bResult, FSavedViewData const& SVData)
	{
		this->OnGetSavedViewComplete.Broadcast(bResult, SVData.SavedView, SVData.SavedViewInfo);
		if (this->Impl->observer_)
		{
			this->Impl->observer_->OnSavedViewRetrieved(bResult, SVData.SavedView, SVData.SavedViewInfo);
		}
	});
}
