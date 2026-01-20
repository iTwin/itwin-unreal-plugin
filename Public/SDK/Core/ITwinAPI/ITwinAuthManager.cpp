/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinAuthManager.h"

#include "ITwinAuthObserver.h"
#include "ITwinWebServices.h"
#include <../BeHeaders/Util/CleanUpGuard.h>

#include <Core/Tools/Assert.h>

#include <fmt/format.h>

#include <Core/Json/Json.h>
#include <Core/Network/HttpRequest.h>
#include <Core/Network/IHttpRouter.h>
#include <Core/Tools/DelayedCall.h>

#include "../Singleton/singleton.h"
#include <random>


namespace AdvViz::SDK
{
	template<>
	Tools::Factory<ITwinAuthManager, EITwinEnvironment>::Globals::Globals()
	{
		newFct_ = [](EITwinEnvironment /*env*/) {
			BE_ISSUE("ITwinAuthManager cannot be instantiated directly - need platform specific overrides");
			return nullptr;
			};
	}

	template<>
	Tools::Factory<ITwinAuthManager, EITwinEnvironment>::Globals& Tools::Factory<ITwinAuthManager, EITwinEnvironment>::GetGlobals()
	{
		return singleton<Tools::Factory<ITwinAuthManager, EITwinEnvironment>::Globals>();
	}

#define USE_REFRESH_TOKEN() 1

#if USE_REFRESH_TOKEN()
#define OPTIONAL_OFFLINE_ACCESS_SCOPE " offline_access"
#else
#define OPTIONAL_OFFLINE_ACCESS_SCOPE ""
#endif


	[[nodiscard]] std::string trim(const std::string& source)
	{
		std::string s(source);
		s.erase(0, s.find_first_not_of(" \n\r\t"));
		s.erase(s.find_last_not_of(" \n\r\t") + 1);
		return s;
	}

	class ITwinAuthManager::Credentials
	{
	public:
		static constexpr auto RedirectUriEndpoint = "/signin-callback";

		static AppIDArray AppIDs;

	private:
		static int RedirectUriPort;

		static constexpr auto minimalScope_ = "itwin-platform" \
			OPTIONAL_OFFLINE_ACCESS_SCOPE \
			;
		// Additional scopes may be added by the client application (this is the case in Carrot currently).
		static std::string extraScopes_;

	public:
		static std::string GetRedirectUri()
		{
			return fmt::format("http://127.0.0.1:{}{}", RedirectUriPort, RedirectUriEndpoint);
		}
		static std::string GetScope()
		{
			return extraScopes_ + minimalScope_;
		}
		static bool AddScope(std::string const& extraScope)
		{
			// Make sure we add a separator if needed.
			std::string const scopeToAdd = trim(extraScope);
			if (scopeToAdd.empty())
				return false;
			if (GetScope().find(scopeToAdd) != std::string::npos)
				return false; // already there
			extraScopes_ += scopeToAdd + " ";
			return true;
		}

		static std::string const& GetAppID(EITwinEnvironment env)
		{
			// Use "ensure" instead of "check" here, so that the app will not stop (crash) if the user
			// did not correctly set the app ID, which is likely to happen if user just wants to try
			// the ITwinTestApp without having read the doc completely.
			// In this case, a more "friendly" error message is displayed by the app.
			BE_ASSERT(!AppIDs[(size_t)env].empty(), "iTwin App ID not initialized for current env");
			return AppIDs[(size_t)env];
		}

		static inline std::string GetEnvPrefix(EITwinEnvironment env)
		{
			// Dev env must use QA ims.
			return ((env == EITwinEnvironment::Prod) ? "" : "qa-");
		}
		static inline std::string GetBeIMSUrl(std::string const& imsName, EITwinEnvironment env)
		{
			return std::string("https://") + GetEnvPrefix(env) + imsName + ".bentley.com";
		}
		static std::string GetITwinIMSRootUrl(EITwinEnvironment env)
		{
			return GetBeIMSUrl("ims", env);
		}

		static void SetRedirectUriPort(int port)
		{
			RedirectUriPort = port;
		}
		static int GetRedirectUriPort()
		{
			return RedirectUriPort;
		}
	};

	/*static*/
	ITwinAuthManager::AppIDArray ITwinAuthManager::Credentials::AppIDs;

	/*static*/
	std::string ITwinAuthManager::Credentials::extraScopes_;

	/*static*/
	int ITwinAuthManager::Credentials::RedirectUriPort = 3000;

	namespace
	{
		static std::string HideString(std::string const& strId)
		{
			std::string hiddenID;
			size_t const maxVisibleChars = strId.length() / 4;
			size_t displayedChars = 0;
			for (auto const& c : strId)
			{
				if (c == '-' || c == '_')
				{
					hiddenID += c;
				}
				else if (displayedChars < maxVisibleChars)
				{
					hiddenID += c;
					displayedChars++;
				}
				else
				{
					hiddenID += '?';
				}
			}
			return hiddenID;
		}

		static std::string HideAppID(std::string const& strId)
		{
			if (strId.length() <= 4)
			{
				return HideString(strId);
			}
			std::string strLeft = HideString(strId.substr(0, strId.length() / 2));
			std::string strRight = strId.substr(strId.length() / 2);
			std::reverse(strRight.begin(), strRight.end());
			strRight = HideString(strRight);
			std::reverse(strRight.begin(), strRight.end());
			return strLeft + strRight;
		}
	}

	/*static*/
	void ITwinAuthManager::SetAppIDArray(AppIDArray const& ITwinAppIDs, bool bLogIDs /*= true*/)
	{
		std::ostringstream oss;
		oss << "Setting AppID -";

		// Do not write clear AppID in logs.
		size_t nbValidIDs(0);
		for (size_t envId(0); envId < ITwinAppIDs.size(); envId++)
		{
			if (!ITwinAppIDs[envId].empty())
			{
				if (nbValidIDs > 0)
				{
					oss << ",";
				}
				oss << " " << ITwinServerEnvironment::ToString(
					static_cast<EITwinEnvironment>(envId))
					<< ": " << HideAppID(ITwinAppIDs[envId]);
				nbValidIDs++;
			}
		}
		if (bLogIDs && AdvViz::SDK::Tools::IsLogInitialized())
		{
			BE_LOGI("ITwinAPI", oss.str());
		}
		Credentials::AppIDs = ITwinAppIDs;
	}

	/*static*/
	bool ITwinAuthManager::HasAppID(EITwinEnvironment env)
	{
		if ((size_t)env < Credentials::AppIDs.size())
		{
			return !Credentials::AppIDs[(size_t)env].empty();
		}
		BE_ISSUE("invalid env", (size_t)env);
		return false;
	}

	/*static*/
	std::string ITwinAuthManager::GetAppID(EITwinEnvironment env)
	{
		if ((size_t)env < Credentials::AppIDs.size())
		{
			return Credentials::GetAppID(env);
		}
		BE_ISSUE("invalid env", (size_t)env);
		return {};
	}

	/*static*/
	void ITwinAuthManager::AddScope(std::string const& extraScope)
	{
		Credentials::AddScope(extraScope);
	}

	/*static*/
	bool ITwinAuthManager::HasScope(std::string const& scope)
	{
		return Credentials::GetScope().find(scope) != std::string::npos;
	}

	/*static*/
	void ITwinAuthManager::SetRedirectUriPort(int port)
	{
		Credentials::SetRedirectUriPort(port);
	}

	/*static*/
	int ITwinAuthManager::GetRedirectUriPort()
	{
		return Credentials::GetRedirectUriPort();
	}

	/*static*/
	std::string ITwinAuthManager::GetRedirectUri()
	{
		return Credentials::GetRedirectUri();
	}

	/*static*/
	ITwinAuthManager::Globals& ITwinAuthManager::GetGlobals()
	{
		return singleton<ITwinAuthManager::Globals>();
	}

	/*static*/
	ITwinAuthManager::SharedInstance& ITwinAuthManager::GetInstance(EITwinEnvironment env)
	{
		static std::mutex PoolMutex;

		const size_t envIndex = (size_t)env;
		Globals& globals = GetGlobals();

		BE_ASSERT(envIndex < globals.instances_.size(), "Invalid environment", envIndex);

		std::unique_lock<std::mutex> lock(PoolMutex);
		
		if (!globals.instances_[envIndex])
		{
			globals.instances_[envIndex].reset(ITwinAuthManager::New(env));
		}
		return globals.instances_[envIndex];
	}


	ITwinAuthManager::ITwinAuthManager(EITwinEnvironment env)
		: env_(env)
	{
		stillValid_ = std::make_shared<std::atomic_bool>(true);

		http_.reset(Http::New());
		http_->SetBaseUrl(Credentials::GetITwinIMSRootUrl(env_).c_str());
		currentToken_.reset(new std::string);
	}

	ITwinAuthManager::~ITwinAuthManager()
	{
		*stillValid_ = false;
	}

	void ITwinAuthManager::ResetRefreshTicker()
	{
		UniqueDelayedCall("refreshAuth", {}, -1.f);
	}

	void ITwinAuthManager::ResetRestartTicker()
	{
		UniqueDelayedCall("restartAuth", {}, -1.f);
	}


	bool ITwinAuthManager::HasAccessToken() const
	{
		Lock lock(mutex_);
		return !currentToken_->empty();
	}

	std::shared_ptr<std::string> ITwinAuthManager::GetAccessToken() const
	{
		Lock lock(mutex_);
		return currentToken_;
	}

	void ITwinAuthManager::SetAccessToken(std::string const& accessToken)
	{
		Lock lock(mutex_);
		accessToken_ = accessToken;
		*currentToken_ = GetCurrentAccessToken();
	}

	void ITwinAuthManager::SetOverrideAccessToken(std::string const& accessToken, EITwinAuthOverrideMode overrideMode)
	{
		Lock lock(mutex_);
		overrideAccessToken_ = accessToken;
		overrideMode_ = overrideMode;
		if (accessToken.empty() && overrideMode != EITwinAuthOverrideMode::None)
		{
			BE_ISSUE("inconsistent override mode (will revert to None)", (size_t)overrideMode);
			overrideMode_ = EITwinAuthOverrideMode::None;
		}
		*currentToken_ = GetCurrentAccessToken();
	}

	void ITwinAuthManager::ResetOverrideAccessToken()
	{
		SetOverrideAccessToken({}, EITwinAuthOverrideMode::None);
	}

	EITwinAuthOverrideMode ITwinAuthManager::GetOverrideMode() const
	{
		Lock lock(mutex_);
		return overrideMode_;
	}

	std::string const& ITwinAuthManager::GetCurrentAccessToken() const
	{
		return !overrideAccessToken_.empty() ? overrideAccessToken_ : accessToken_;
	}

	std::string const& ITwinAuthManager::GetRegularAccessToken() const
	{
		Lock lock(mutex_);
		return accessToken_;
	}

	bool ITwinAuthManager::HasRefreshToken() const
	{
		Lock lock(mutex_);
		return !authInfo_.RefreshToken.empty();
	}

	void ITwinAuthManager::GetRefreshToken(std::string& OutRefreshToken) const
	{
		Lock lock(mutex_);
		OutRefreshToken = authInfo_.RefreshToken;
	}

	std::chrono::system_clock::time_point ITwinAuthManager::GetExpirationTime() const
	{
		Lock lock(mutex_);
		return authInfo_.GetExpirationTime();
	}

	bool ITwinAuthManager::TryLoadRefreshToken()
	{
		Lock lock(mutex_);
		if (loadRefreshTokenAttempts_ > 0)
		{
			// Only load the refresh token once.
			return false;
		}
		std::string refreshToken;
		bool const bHasRefreshToken = LoadPrivateData(refreshToken);
		loadRefreshTokenAttempts_++;
		if (bHasRefreshToken)
		{
			// Fill AuthInfo
			authInfo_.RefreshToken = refreshToken;
		}
		if (bHasRefreshToken)
		{
			// Try to reload a non-expired token.
			std::string readAccessToken;
			ITwinAuthInfo readAuthInfo;
			if (ReloadAccessToken(readAccessToken, readAuthInfo))
			{
				readAuthInfo.RefreshToken = refreshToken;
				readAuthInfo.CreationTime = std::chrono::system_clock::now();
				SetAuthorizationInfo(readAccessToken, readAuthInfo, EAuthContext::Reload);
			}
		}
		return bHasRefreshToken;
	}

	void ITwinAuthManager::ResetRefreshToken()
	{
		Lock lock(mutex_);
		authInfo_.RefreshToken.clear();
		SavePrivateData({});
	}

	void ITwinAuthManager::SetAuthorizationInfo(
		std::string const& accessToken,
		ITwinAuthInfo const& authInfo,
		EAuthContext AuthContext /*= EAuthContext::StdRequest*/)
	{
		SetAccessToken(accessToken);

		Lock lock(mutex_);

		bool const bSameRefreshToken = (authInfo_.RefreshToken == authInfo.RefreshToken);
		authInfo_ = authInfo;

		if (!bSameRefreshToken)
		{
			// Save new information to enable refresh upon future sessions (if a new refresh token was
			// retrieved) or avoid reusing an expired one if none was newly fetched.
			SavePrivateData(authInfo_.RefreshToken);
		}

		if (AuthContext == EAuthContext::StdRequest
			&& !accessToken.empty())
		{
			// Also save the access token to minimize the need for interactive login when we relaunch the
			// same application/plugin before the expiration time of the token.
			SaveAccessToken(accessToken);
		}

		ResetRefreshTicker();

#if USE_REFRESH_TOKEN()
		if (!authInfo.RefreshToken.empty())
		{
			// usually, iTwin access tokens expire after 3600 seconds
			// let's try to refresh it *before* its actual expiration
			float fDelay = (authInfo_.ExpiresIn > 0)
				? 0.90f * authInfo_.ExpiresIn
				: 60.f * 30;
			UniqueDelayedCall("refreshAuth",
				[this, IsValidLambda = this->stillValid_]() -> DelayedCall::EReturnedValue
			{
				if (*IsValidLambda)
				{
					this->ProcessTokenRequest(authInfo_.CodeVerifier, authInfo_.AuthorizationCode,
						ETokenMode::Refresh, true /*automatic_refresh*/);
				}
				return DelayedCall::EReturnedValue::Done;
			}, fDelay);
		}
#endif //USE_REFRESH_TOKEN
	}

	std::string ITwinAuthManager::GetAppID() const
	{
		if (HasAppID(env_))
		{
			return Credentials::GetAppID(env_);
		}
		BE_ISSUE("no AppID for environment", (size_t)env_);
		return {};
	}

	std::string ITwinAuthManager::GetClientID() const
	{
		return customClientId_.value_or(GetAppID());
	}

	std::string ITwinAuthManager::GetScope() const
	{
		return customScope_.value_or(Credentials::GetScope());
	}
	std::string ITwinAuthManager::GetIMSBaseUrl() const
	{
		return Credentials::GetITwinIMSRootUrl(env_);
	}

	void ITwinAuthManager::AddObserver(ITwinAuthObserver* observer)
	{
		Lock lock(mutex_);
		auto it = std::find(observers_.begin(), observers_.end(), observer);
		if (it == observers_.end())
		{
			observers_.push_back(observer);
		}
	}

	void ITwinAuthManager::RemoveObserver(ITwinAuthObserver* observer)
	{
		Lock lock(mutex_);
		std::erase(observers_, observer);
	}

	void ITwinAuthManager::NotifyResult(bool bSuccess, std::string const& strError)
	{
		Lock lock(mutex_);
		for (auto* observer : this->observers_)
		{
			observer->OnAuthorizationDone(bSuccess, strError);
		}
	}

	std::string ITwinAuthManager::GetCurrentAuthorizationURL() const
	{
		Lock lock(mutex_);
		return currentAuthorizationURL_;
	}

	void ITwinAuthManager::SetAuthorizationURL(std::string const& authorizationURL)
	{
		Lock lock(mutex_);
		currentAuthorizationURL_ = authorizationURL;
	}

	namespace
	{
		std::string GenerateRandomCharacters(size_t amountOfCharacters)
		{
			const char* Values = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
			constexpr size_t nbValues = sizeof(Values);
			std::string result;
			result.reserve(amountOfCharacters);

			std::mt19937 rng;

			const std::time_t curTime = std::chrono::system_clock::to_time_t(
				std::chrono::system_clock::now());
			rng.seed((int)curTime);
			for (size_t i = 0; i < amountOfCharacters; ++i)
			{
				result.append(1u, Values[rng() % nbValues]);
			}
			return result;
		}
	}


	RequestID ITwinAuthManager::ProcessTokenRequest(
		std::string const& verifier,
		std::string const& authorizationCode,
		ETokenMode tokenMode, bool bIsAutomaticRefresh /*= false*/)
	{
		const std::string clientId = GetClientID();
		if (clientId.empty())
		{
			BE_LOGE("ITwinAPI", "The iTwin App ID is missing. Please refer to the plugin documentation.");
			return HttpRequest::NO_REQUEST;
		}
		if (grantType_ == EITwinAuthGrantType::ClientCredentials && clientSecret_.empty())
		{
			BE_LOGE("ITwinAPI", "Missing data for client_credentials mode.");
			return HttpRequest::NO_REQUEST;
		}
		const auto request = std::shared_ptr<HttpRequest>(HttpRequest::New(), [](HttpRequest* p) {delete p; });
		if (!request)
		{
			return HttpRequest::NO_REQUEST;
		}
		request->SetVerb(EVerb::Post);

		Http::Headers headers;
		headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
		headers.emplace_back("X-Correlation-ID", request->GetRequestID());

		std::string grantType = "authorization_code";
		std::string refreshParams;
		std::string codeParams;
		std::string clientSecretParams;

		if (tokenMode == ETokenMode::Refresh)
		{
			Lock lock(mutex_);
			if (!authInfo_.RefreshToken.empty())
			{
				grantType = "refresh_token";
				refreshParams = std::string("&refresh_token=") + authInfo_.RefreshToken;
			}
		}
		if (grantType_ == EITwinAuthGrantType::ClientCredentials)
		{
			grantType = "client_credentials";
			refreshParams = "";
			clientSecretParams = std::string("&client_secret=") + AdvViz::SDK::EncodeForUrl(clientSecret_);
		}
		else
		{
			codeParams = std::string("&code=") + authorizationCode + "&code_verifier=" + verifier;
		}

		const std::string redirectUri = Credentials::GetRedirectUri();
		std::string const requestBody = std::string("grant_type=") + grantType
			+ "&client_id=" + clientId
			+ "&redirect_uri=" + AdvViz::SDK::EncodeForUrl(redirectUri)
			+ refreshParams
			+ codeParams
			+ clientSecretParams
			+ "&scope=" + AdvViz::SDK::EncodeForUrl(this->GetScope());


		using RequestPtr = HttpRequest::RequestPtr;
		using Response = HttpRequest::Response;
		request->SetResponseCallback(
			[this, IsValidLambda = this->stillValid_, authorizationCode, verifier, tokenMode, bIsAutomaticRefresh]
			(RequestPtr const& request, Response const& response)
		{
			if (!(*IsValidLambda))
			{
				// see comments in #ReusableJsonQueries.cpp
				return;
			}

			bool bHasAuthToken = false;
			std::string requestError;
			Be::CleanUpGuard resultGuard([this, &requestError, &bHasAuthToken, tokenMode, bIsAutomaticRefresh]
			{
				if (tokenMode == ETokenMode::Refresh && !bHasAuthToken)
				{
					// reset the refresh token (probably wrong or expired)
					this->ResetRefreshToken();
				}

				if (bIsAutomaticRefresh)
				{
					// automatic refresh attempt through a timer => just log the result of the refresh request
					if (bHasAuthToken)
					{
						BE_LOGI("ITwinAPI", "iTwin authorization successfully refreshed");
					}
					else
					{
						int remainingSeconds = 0;
						auto const expTime = this->GetExpirationTime();
						auto const now = std::chrono::system_clock::now();
						if (expTime > now)
						{
							remainingSeconds = static_cast<int>(
								std::chrono::duration_cast<std::chrono::seconds>(expTime - now).count());
						}
						BE_LOGE("ITwinAPI", "Could not refresh the authorization (expiring in "
							<< remainingSeconds << " seconds) - error: " << requestError);
					}
				}
				else
				{
					// This is the initial authorization.

					// If the refresh token read from the user settings was wrong or expired, restart the
					// authorization process from scratch, without broadcasting the initial failure (the user
					// will have to allow permissions again).
					if (!bHasAuthToken && tokenMode == ETokenMode::Refresh)
					{
						this->RestartAuthorizationLater();
					}
					else
					{
						this->NotifyResult(bHasAuthToken, requestError);
					}
				}
			});

			if (!request->CheckResponse(response, requestError))
			{
				if (!response.second.empty())
				{
					// Try to parse iTwin error
					requestError += ITwinWebServices::GetErrorDescriptionFromJson(response.second,
						requestError.empty() ? "" : "\t");
				}
				return;
			}

			struct ITwinAuthData
			{
				std::string access_token;
				std::optional<std::string> refresh_token;
				std::optional<std::string> token_type;
				std::optional<int> expires_in;
			} authData;
			if (!Json::FromString(authData, response.second, requestError))
			{
				return;
			}

			// store expiration and automatic refresh info
			ITwinAuthInfo authInfo;
			authInfo.AuthorizationCode = authorizationCode;
			authInfo.CodeVerifier = verifier;
			authInfo.RefreshToken = authData.refresh_token.value_or("");
			authInfo.ExpiresIn = authData.expires_in.value_or(0);

			bHasAuthToken = !authData.access_token.empty();
			if (bHasAuthToken)
			{
				this->SetAuthorizationInfo(authData.access_token, authInfo);
			}
			else
			{
				requestError = "No access token";
			}

			// emphasize the handling of the result (even though it would be done automatically)
			resultGuard.cleanup();
		});
		request->Process(*http_, "/connect/token",
			HttpRequest::BodyParams(requestBody, AdvViz::SDK::Tools::EStringEncoding::Ansi),
			headers);

		return request->GetRequestID();
	}

	EITwinAuthStatus ITwinAuthManager::CheckAuthorization()
	{
		if (HasAccessToken())
		{
			return EITwinAuthStatus::Success;
		}
		if (IsAuthorizationInProgress())
		{
			// Do not accumulate authorization requests! (see itwin-unreal-plugin/issues/7)
			return EITwinAuthStatus::InProgress;
		}
		if (GetClientID().empty())
		{
			std::string const Error = "The iTwin App ID is missing. Please refer to the plugin documentation.";
			this->NotifyResult(false, Error);
			return EITwinAuthStatus::Failed;
		}

		if (grantType_ == EITwinAuthGrantType::ClientCredentials)
		{
			ProcessTokenRequest("", "", ETokenMode::Standard);
			return EITwinAuthStatus::InProgress;
		}

		BE_ASSERT(!hasBoundAuthPort_, "Authorization process already in progress...");

		const std::string state = GenerateRandomCharacters(10);
		const std::string verifier = GenerateRandomCharacters(128);

		const bool bHasLoadedRefreshTok = TryLoadRefreshToken();
		if (bHasLoadedRefreshTok && HasAccessToken())
		{
			// We could reload a non-expired token.
			this->NotifyResult(true, {});
			return EITwinAuthStatus::Success;
		}

		if (!httpRouter_)
		{
			httpRouter_.reset(IHttpRouter::New());
		}
		if (!httpRouter_)
		{
			std::string const Error = "No support for Http Router. Cannot request access.";
			this->NotifyResult(false, Error);
			return EITwinAuthStatus::Failed;
		}
		auto routeHandle = httpRouter_->MakeRouteHandler();
		httpRouter_->BindRoute(routeHandle,
			Credentials::GetRedirectUriPort(),
			Credentials::RedirectUriEndpoint,
			EVerb::Get,
			[=, this, httpRouter = this->httpRouter_, IsValidRequestHandler = this->stillValid_]
			(std::map<std::string, std::string> const& queryParams, std::string& outHtmlText)
		{
			if (!(*IsValidRequestHandler))
			{
				return;
			}
			if (queryParams.contains("code")
				&& queryParams.contains("state") && queryParams.at("state") == state)
			{
				ProcessTokenRequest(verifier, queryParams.at("code"),
					HasRefreshToken() ? ETokenMode::Refresh : ETokenMode::Standard);
				outHtmlText = "<h1>Sign in was successful!</h1>You can close this browser window and return to the application.";
			}
			else if (queryParams.contains("error"))
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
					std::string htmlError;
					auto itDesc = queryParams.find("error_description");
					if (itDesc != queryParams.end())
					{
						htmlError = itDesc->second;
						std::replace(htmlError.begin(), htmlError.end(), '+', ' ');
					}
					outHtmlText = fmt::format("<h1>Error signin in!</h1><br/>{}<br/><br/>You can close this browser window and return to the application.",
						htmlError);
				}
			}
			hasBoundAuthPort_ = false;
		});
		if (routeHandle && routeHandle->IsValid())
		{
			hasBoundAuthPort_ = true;
		}

		// Start the actual authorization (typically by opening a Web Browser).
		std::string brwError;
		if (!StartAuthorizationInstance(state, verifier, brwError))
		{
			this->NotifyResult(false, brwError);
			return EITwinAuthStatus::Failed;
		}
		return EITwinAuthStatus::InProgress;
	}


	void ITwinAuthManager::RestartAuthorizationLater()
	{
		// We cannot just call CheckAuthorization in the middle of the process, because we must ensure we can
		// rebind our router on the same port, which requires we have unbounded the previous instance...
		// Therefore the use of a ticker here
		ResetRestartTicker();
		UniqueDelayedCall("restartAuth",
			[this, IsValidLambda = this->stillValid_]() -> DelayedCall::EReturnedValue
		{
			if (*IsValidLambda)
			{
				if (!hasBoundAuthPort_)
				{
					CheckAuthorization();
					return DelayedCall::EReturnedValue::Done; // stop ticking
				}
				return DelayedCall::EReturnedValue::Repeat;
			}
			else
			{
				return DelayedCall::EReturnedValue::Done; // stop ticking
			}
		}, 0.200f /*TickerDelay: 200 ms*/);
	}


	bool ITwinAuthManager::IsAuthorizationInProgress() const
	{
		if (HasAccessToken())
			return false;
		return hasBoundAuthPort_.load();
	}

	bool ITwinAuthManager::SaveAccessToken(std::string const& accessToken) const
	{
		if (!accessToken.empty()
			&& authInfo_.ExpiresIn > 0
			&& !authInfo_.CodeVerifier.empty()
			&& !authInfo_.AuthorizationCode.empty()
			&& !authInfo_.RefreshToken.empty())
		{
			// Also save the access token to minimize the need for interactive login when we relaunch the
			// same application/plugin before the expiration time of the token.
			std::chrono::system_clock::time_point ExpirationTimePoint = std::chrono::system_clock::now()
				+ std::chrono::seconds(authInfo_.ExpiresIn);
			const std::time_t ExpirationTime = std::chrono::system_clock::to_time_t(ExpirationTimePoint);
			return SavePrivateData(
				fmt::format("{} + {} + {} + {}",
					accessToken, authInfo_.CodeVerifier, authInfo_.AuthorizationCode,
					static_cast<int64_t>(ExpirationTime)),
				1 /*keyIndex*/);
		}
		return false;
	}

	namespace
	{
		std::vector<std::string> Tokenize(std::string const& src, std::string const& separator)
		{
			const size_t sepLen = separator.length();
			std::vector<std::string> res;
			size_t posStart = 0;
			size_t posEnd = 0;
			do {
				posEnd = src.find(separator, posStart);
				if (posEnd != std::string::npos)
				{
					res.push_back(src.substr(posStart, posEnd - posStart));
					posStart = posEnd + sepLen;
				}
			} while (posEnd != std::string::npos);
			res.push_back(src.substr(posStart));
			return res;
		}
	}

	bool ITwinAuthManager::ReloadAccessToken(std::string& readAccessToken, ITwinAuthInfo& readAuthInfo) const
	{
		std::string fullInfo;
		if (!LoadPrivateData(fullInfo, 1 /*keyIndex*/))
		{
			return false;
		}
		bool bStillValidToken = false;
		std::vector<std::string> tokens = Tokenize(fullInfo, " + ");
		if (tokens.size() == 4)
		{
			readAccessToken = tokens[0];
			readAuthInfo.CodeVerifier = tokens[1];
			readAuthInfo.AuthorizationCode = tokens[2];
			int64_t expTime = std::stoll(tokens[3].c_str());
			if (expTime > 0
				&& !readAuthInfo.CodeVerifier.empty()
				&& !readAuthInfo.AuthorizationCode.empty())
			{
				auto const ExpirationTime = std::chrono::system_clock::from_time_t(std::time_t{ expTime });
				auto const Now = std::chrono::system_clock::now();
				if (ExpirationTime > Now)
				{
					auto NbSeconds = std::chrono::duration_cast<std::chrono::seconds>(ExpirationTime - Now).count();
					if (NbSeconds > 60 && NbSeconds < 3600 * 24)
					{
						readAuthInfo.ExpiresIn = static_cast<int>(NbSeconds);
						BE_LOGI("ITwinAPI", "Authorization found - expires in " << readAuthInfo.ExpiresIn << " seconds");
						bStillValidToken = true;
					}
				}
			}
		}
		return bStillValidToken && !readAccessToken.empty();
	}

	bool ITwinAuthManager::SetClientCredentialGrantType(std::string const& clientID, std::string const& clientSecret,
		std::optional<std::string> const& imsName /*= std::nullopt*/,
		std::optional<std::string> const& customScope /*= std::nullopt*/)
	{
		if (clientSecret.empty())
		{
			BE_ISSUE("client secret is required for client_credentials mode");
			return false;
		}
		if (!clientID.empty())
		{
			// Override client (=App) ID
			customClientId_ = clientID;
		}
		if (imsName)
		{
			http_.reset(Http::New());
			http_->SetBaseUrl(Credentials::GetBeIMSUrl(*imsName, env_).c_str());
		}
		clientSecret_ = clientSecret;
		grantType_ = EITwinAuthGrantType::ClientCredentials;
		customScope_ = customScope;
		return true;
	}
}
