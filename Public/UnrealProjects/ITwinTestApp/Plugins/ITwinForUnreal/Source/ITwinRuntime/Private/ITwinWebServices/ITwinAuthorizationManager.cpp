/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationManager.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "ITwinAuthorizationManager.h"

#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <PlatformHttp.h>
#include <EncryptionContextOpenSSL.h>
#include <HttpServerModule.h>
#include <IHttpRouter.h>
#include <HttpModule.h>

#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <Misc/App.h>
#include <Misc/Base64.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>


#define USE_REFRESH_TOKEN() 1

#if USE_REFRESH_TOKEN()
#define OPTIONAL_OFFLINE_ACCESS_SCOPE " offline_access"
#else
#define OPTIONAL_OFFLINE_ACCESS_SCOPE ""
#endif


struct FAuthorizationCredentials
{
	static constexpr int LocalhostPort = 3000;
	static constexpr auto RedirectUriEndpoint = TEXT("/signin-callback");
	static constexpr auto Scope = TEXT("itwins:read imodels:read realitydata:read") \
		" savedviews:read savedviews:modify" \
		" mesh-export:read mesh-export:modify" \
		OPTIONAL_OFFLINE_ACCESS_SCOPE \
		;

	static ITwin::AppIDArray ITwinAppIDs;

	static FString GetRedirectUri()
	{
		return FString::Printf(TEXT("http://localhost:%d%s"), LocalhostPort, RedirectUriEndpoint);
	}

	static FString GetITwinAppId(EITwinEnvironment Env)
	{
		checkf(!ITwinAppIDs[(size_t)Env].IsEmpty(), TEXT("iTwin App ID not initialized for current env"));
		return ITwinAppIDs[(size_t)Env];
	}

	static FString GetITwinIMSRootUrl(EITwinEnvironment Env)
	{
		// Dev env must use QA ims.
		const FString imsUrlPrefix = (Env == EITwinEnvironment::Prod) ? TEXT("") : TEXT("qa-");
		return TEXT("https://") + imsUrlPrefix + TEXT("ims.bentley.com");
	}
};


namespace ITwin
{
	FString GetITwinAppId(EITwinEnvironment Env)
	{
		return FAuthorizationCredentials::GetITwinAppId(Env);
	}
}

/*static*/ ITwin::AppIDArray FAuthorizationCredentials::ITwinAppIDs;

/*static*/
void FITwinAuthorizationManager::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs)
{
	FAuthorizationCredentials::ITwinAppIDs = ITwinAppIDs;
}

/*static*/
bool FITwinAuthorizationManager::HasITwinAppID(EITwinEnvironment Env)
{
	return ensure((size_t)Env < FAuthorizationCredentials::ITwinAppIDs.size())
		&& !FAuthorizationCredentials::ITwinAppIDs[(size_t)Env].IsEmpty();
}

/*static*/
FITwinAuthorizationManager::Pool FITwinAuthorizationManager::Instances;

/*static*/
FITwinAuthorizationManager::SharedInstance& FITwinAuthorizationManager::GetInstance(EITwinEnvironment Env)
{
	static std::mutex PoolMutex;

	const size_t EnvIndex = (size_t)Env;
	checkf(EnvIndex < Instances.size(), TEXT("Invalid environement (index: %u)"), EnvIndex);

	std::unique_lock<std::mutex> lock(PoolMutex);
	if (!Instances[EnvIndex])
	{
		Instances[EnvIndex].reset(new FITwinAuthorizationManager(Env));
	}
	return Instances[EnvIndex];
}


FITwinAuthorizationManager::FITwinAuthorizationManager(EITwinEnvironment Env)
	: Environment(Env)
{
	IsThisValid = std::make_shared<std::atomic_bool>(true);
}

FITwinAuthorizationManager::~FITwinAuthorizationManager()
{
	ResetAllTickers();
	*IsThisValid = false;
}

void FITwinAuthorizationManager::ResetAllTickers()
{
	ResetRefreshTicker();
	ResetRestartTicker();
}

void FITwinAuthorizationManager::ResetRefreshTicker()
{
	if (RefreshAuthDelegate.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshAuthDelegate);
		RefreshAuthDelegate.Reset();
	}
}

void FITwinAuthorizationManager::ResetRestartTicker()
{
	if (RestartAuthDelegate.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RestartAuthDelegate);
		RestartAuthDelegate.Reset();
	}
}

bool FITwinAuthorizationManager::HasAccessToken() const
{
	FLock Lock(this->Mutex);
	return !this->AccessToken.IsEmpty();
}

void FITwinAuthorizationManager::GetAccessToken(FString& OutAccessToken) const
{
	FLock Lock(this->Mutex);
	OutAccessToken = this->AccessToken;
}

bool FITwinAuthorizationManager::HasRefreshToken() const
{
	FLock Lock(this->Mutex);
	return !AuthInfo.RefreshToken.IsEmpty();
}

void FITwinAuthorizationManager::GetRefreshToken(FString& OutRefreshToken) const
{
	FLock Lock(this->Mutex);
	OutRefreshToken = AuthInfo.RefreshToken;
}

double FITwinAuthorizationManager::GetExpirationTime() const
{
	FLock Lock(this->Mutex);
	return AuthInfo.GetExpirationTime();
}

bool FITwinAuthorizationManager::TryLoadRefreshToken()
{
	FLock Lock(this->Mutex);
	if (LoadRefreshTokenAttempts > 0)
	{
		// Only load the refresh token once.
		return false;
	}
	FString RefreshToken;
	bool const bHasRefreshToken = UITwinWebServices::LoadToken(RefreshToken, Environment);
	LoadRefreshTokenAttempts++;
	if (bHasRefreshToken)
	{
		// Fill AuthInfo
		AuthInfo.RefreshToken = RefreshToken;
	}
	return bHasRefreshToken;
}

void FITwinAuthorizationManager::ResetRefreshToken()
{
	FLock Lock(this->Mutex);
	AuthInfo.RefreshToken.Reset();
	UITwinWebServices::DeleteTokenFile(Environment);
}

void FITwinAuthorizationManager::SetAuthorizationInfo(
	FString const& InAccessToken,
	FITwinAuthorizationInfo const& InAuthInfo,
	bool bAutoRefresh /*= true*/)
{
	FLock Lock(this->Mutex);

	AccessToken = InAccessToken;

	bool const bSameRefreshToken = (AuthInfo.RefreshToken == InAuthInfo.RefreshToken);
	AuthInfo = InAuthInfo;

	if (!bSameRefreshToken)
	{
		// save new information to enable refresh upon future sessions (if a new refresh token was
		// retrieved) or avoid reusing and expired one if none was newly fetched.
		UITwinWebServices::SaveToken(AuthInfo.RefreshToken, Environment);
	}
	ResetRefreshTicker();
	if (bAutoRefresh && !InAuthInfo.RefreshToken.IsEmpty())
	{
		// usually, iTwin access tokens expire after 3600 seconds
		// let's try to refresh it *before* its actual expiration
		float TickerDelay = (AuthInfo.ExpiresIn > 0)
			? 0.90f * AuthInfo.ExpiresIn
			: 60.f * 30;
		RefreshAuthDelegate = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([this, IsValidLambda = this->IsThisValid]
			(float Delta) -> bool
		{
			if (*IsValidLambda)
			{
				this->ProcessTokenRequest(AuthInfo.CodeVerifier, AuthInfo.AuthorizationCode,
					ETokenMode::TM_Refresh, true /*automatic_refresh*/);
			}
			return false; // One tick
		}), TickerDelay);
	}
}



namespace // imported from unreal-engine-3dft-plugin
{
	FString GenerateRandomCharacters(uint32 AmountOfCharacters)
	{
		const char* Values = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
		FString Result = "";
		Result.Reserve(AmountOfCharacters);
		for (uint32 i = 0; i < AmountOfCharacters; ++i)
		{
			Result.AppendChar(Values[FMath::RandRange(0, 61)]);
		}
		return Result;
	}

	FString LaunchWebBrowser(const FString& State, const FString& CodeVerifier,
		EITwinEnvironment Env, bool bUsingRefreshMode)
	{
		TArray<uint8> verifierSha;
		FEncryptionContextOpenSSL().CalcSHA256(TArrayView<const uint8>(
			(const uint8*)StringCast<ANSICHAR>(*CodeVerifier).Get(), CodeVerifier.Len()),
			verifierSha);

		auto CodeChallenge = FBase64::Encode(verifierSha, EBase64Mode::UrlSafe).Replace(TEXT("="), TEXT(""));

		auto RedirectUri = FAuthorizationCredentials::GetRedirectUri();

		FString const PromptParam = bUsingRefreshMode ? TEXT("&prompt=none") : TEXT("");

		FString const LaunchURL = FAuthorizationCredentials::GetITwinIMSRootUrl(Env)
			+ "/connect/authorize?response_type=code"
			+ "&client_id=" + FAuthorizationCredentials::GetITwinAppId(Env)
			+ "&redirect_uri=" + FPlatformHttp::UrlEncode(RedirectUri)
			+ "&scope=" + FPlatformHttp::UrlEncode(FAuthorizationCredentials::Scope)
			+ PromptParam
			+ "&state=" + State
			+ "&code_challenge=" + CodeChallenge
			+ "&code_challenge_method=S256";
		FString Error;
		FPlatformProcess::LaunchURL(*LaunchURL, nullptr, &Error);
		if (!Error.IsEmpty())
		{
			return FString::Printf(TEXT("Could not launch web browser! %s"), *Error);
		}
		return "";
	}
}

void FITwinAuthorizationManager::AddObserver(IITwinAuthorizationObserver* Observer)
{
	FLock Lock(this->Mutex);
	if (Observers.Find(Observer) == INDEX_NONE)
	{
		Observers.Add(Observer);
	}
}

void FITwinAuthorizationManager::RemoveObserver(IITwinAuthorizationObserver* Observer)
{
	FLock Lock(this->Mutex);
	Observers.Remove(Observer);
}

void FITwinAuthorizationManager::NotifyResult(bool bSuccess, FString const& Error)
{
	FLock Lock(this->Mutex);
	for (auto* Observer : this->Observers)
	{
		Observer->OnAuthorizationDone(bSuccess, Error);
	}
}

void FITwinAuthorizationManager::ProcessTokenRequest(
	FString const verifier,
	FString const authorizationCode,
	ETokenMode tokenMode, bool bIsAutomaticRefresh /*= false*/)
{
	const FString clientId = FAuthorizationCredentials::GetITwinAppId(Environment);
	if (clientId.IsEmpty())
	{
		UE_LOG(LrtuServer, Error, TEXT("The iTwin App ID is missing. Please refer to the plugin documentation."));
		return;
	}

	const FString redirectUri = FAuthorizationCredentials::GetRedirectUri();

	const auto tokenRequest = FHttpModule::Get().CreateRequest();
	tokenRequest->SetVerb("POST");
	tokenRequest->SetURL(FAuthorizationCredentials::GetITwinIMSRootUrl(Environment)
		+ TEXT("/connect/token"));
	tokenRequest->SetHeader("Content-Type", "application/x-www-form-urlencoded");

	FString grantType = TEXT("authorization_code");
	FString refreshParams;

	if (tokenMode == ETokenMode::TM_Refresh)
	{
		FLock Lock(this->Mutex);
		if (!AuthInfo.RefreshToken.IsEmpty())
		{
			grantType = TEXT("refresh_token");
			refreshParams = TEXT("&refresh_token=") + AuthInfo.RefreshToken;
		}
	}

	tokenRequest->SetContentAsString("grant_type=" + grantType
		+ "&client_id=" + clientId
		+ "&redirect_uri=" + FPlatformHttp::UrlEncode(redirectUri)
		+ refreshParams
		+ "&code=" + authorizationCode
		+ "&code_verifier=" + verifier
		+ "&scope=" + FPlatformHttp::UrlEncode(FAuthorizationCredentials::Scope));
	tokenRequest->OnProcessRequestComplete().BindLambda(
		[=, this, IsValidLambda = this->IsThisValid]
		(FHttpRequestPtr request, FHttpResponsePtr response, bool connectedSuccessfully)
	{
		if (!(*IsValidLambda))
		{
			// see comments in #ReusableJsonQueries.cpp
			return;
		}

		bool bHasAuthToken = false;
		FString requestError;
		Be::CleanUpGuard resultGuard([this, &requestError, &bHasAuthToken, tokenMode, bIsAutomaticRefresh]
		{
			if (tokenMode == ETokenMode::TM_Refresh && !bHasAuthToken)
			{
				// reset the refresh token (probably wrong or expired)
				this->ResetRefreshToken();
			}

			if (bIsAutomaticRefresh)
			{
				// automatic refresh attempt through a timer => just log the result of the refresh request
				if (bHasAuthToken)
				{
					UE_LOG(LrtuServer, Display, TEXT("iTwin authorization successfully refreshed"));
				}
				else
				{
					const double remainingTime = this->GetExpirationTime() - FApp::GetCurrentTime();
					const int remainingSec = static_cast<int>(std::max(0., remainingTime));
					UE_LOG(LrtuServer, Error, TEXT("Could not refresh the authorization (expiring in %d seconds):\n%s"),
						remainingSec, *requestError);
				}
			}
			else
			{
				// This is the initial authorization.

				// If the refresh token read from the user settings was wrong or expired, restart the
				// authorization process from scratch, without broadcasting the initial failure (the user
				// will have to allow permissions again).
				if (!bHasAuthToken && tokenMode == ETokenMode::TM_Refresh)
				{
					this->RestartAuthorizationLater();
				}
				else
				{
					this->NotifyResult(bHasAuthToken, requestError);
				}
			}
		});

		if (!AITwinServerConnection::CheckRequest(request, response, connectedSuccessfully,
			&requestError))
		{
			return;
		}
		TSharedPtr<FJsonObject> responseJson;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(response->GetContentAsString()), responseJson);
		FString const authToken = responseJson->GetStringField("access_token");

		// store expiration and automatic refresh info
		FITwinAuthorizationInfo authInfo;
		authInfo.CreationTime = FApp::GetCurrentTime();
		authInfo.AuthorizationCode = authorizationCode;
		authInfo.CodeVerifier = verifier;
		authInfo.RefreshToken = responseJson->GetStringField(TEXT("refresh_token"));
		FString const strExpiresIn = responseJson->GetStringField(TEXT("expires_in"));
		if (strExpiresIn.IsEmpty())
		{
			authInfo.ExpiresIn = 0;
		}
		else
		{
			authInfo.ExpiresIn = FCString::Atoi(*strExpiresIn);
		}

		bHasAuthToken = !authToken.IsEmpty();
		if (bHasAuthToken)
		{
			this->SetAuthorizationInfo(authToken, authInfo);
		}
		else
		{
			requestError = TEXT("No access token");
		}
		// emphasize the handling of the result (even though it would be done automatically)
		resultGuard.cleanup();
	});
	tokenRequest->ProcessRequest();
}

void FITwinAuthorizationManager::CheckAuthorization()
{
	if (HasAccessToken())
	{
		return;
	}

	const FString clientId = FAuthorizationCredentials::GetITwinAppId(Environment);
	if (clientId.IsEmpty())
	{
		FString const Error = TEXT("The iTwin App ID is missing. Please refer to the plugin documentation.");
		this->NotifyResult(false, Error);
		return;
	}

	checkf(!bHasBoundAuthPort, TEXT("Authorization process already in progress..."));

	const FString state = GenerateRandomCharacters(10);
	const FString verifier = GenerateRandomCharacters(128);

	TryLoadRefreshToken();

	auto routeHandle = MakeShared<FHttpRouteHandle>();
	*routeHandle = FHttpServerModule::Get().GetHttpRouter(FAuthorizationCredentials::LocalhostPort)
		->BindRoute(FHttpPath(FAuthorizationCredentials::RedirectUriEndpoint),
			EHttpServerRequestVerbs::VERB_GET,
			[=, this, IsValidRequestHandler = this->IsThisValid]
			(const FHttpServerRequest& request, const FHttpResultCallback& onComplete)
	{
		FString HtmlText;
		if (*IsValidRequestHandler
			&& request.QueryParams.Contains("code")
			&& request.QueryParams.Contains("state") && request.QueryParams["state"] == state)
		{
			ProcessTokenRequest(verifier, request.QueryParams["code"],
				HasRefreshToken() ? ETokenMode::TM_Refresh : ETokenMode::TM_Standard);
			HtmlText = TEXT("<h1>Sign in was successful!</h1>You can close this browser window and return to the application.");
		}
		else if (request.QueryParams.Contains("error"))
		{
			if (HasRefreshToken())
			{
				// the refresh token read from user config has probably expired
				// => try again after resetting the refresh token
				ResetRefreshToken();
				RestartAuthorizationLater();
			}
			else
			{
				FString const HtmlError = request.QueryParams["error_description"].Replace(TEXT("+"), TEXT(" "));
				HtmlText = FString::Printf(TEXT("<h1>Error signin in!</h1><br/>%s<br/><br/>You can close this browser window and return to the application."),
					*HtmlError);
			}
		}
		onComplete(FHttpServerResponse::Create(HtmlText, TEXT("text/html")));
		FHttpServerModule::Get().StopAllListeners();
		FHttpServerModule::Get().GetHttpRouter(FAuthorizationCredentials::LocalhostPort)
			->UnbindRoute(*routeHandle);
		bHasBoundAuthPort = false;
		return true;
	});
	if (*routeHandle)
	{
		bHasBoundAuthPort = true;
	}
	FHttpServerModule::Get().StartAllListeners();

	// Open Web Browser
	FString const Error = LaunchWebBrowser(state, verifier, Environment, HasRefreshToken());
	if (!Error.IsEmpty())
	{
		this->NotifyResult(false, Error);
	}
}


void FITwinAuthorizationManager::RestartAuthorizationLater()
{
	// We cannot just call CheckAuthorization in the middle of the process, because we must ensure we can
	// rebind our router on the same port, which requires we have unbounded the previous instance...
	// Therefore the use of a ticker here
	ResetRestartTicker();
	RestartAuthDelegate = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this, IsValidLambda = this->IsThisValid]
		(float Delta) -> bool
	{
		if (*IsValidLambda)
		{
			if (!bHasBoundAuthPort)
			{
				CheckAuthorization();
				return false; // stop ticking
			}
			return true;
		}
		else
		{
			return false; // stop ticking
		}
	}), 0.200 /*TickerDelay: 200 ms*/);
}
