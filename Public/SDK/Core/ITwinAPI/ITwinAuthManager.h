/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once


#ifndef SDK_CPPMODULES
#	include <array>
#	include <atomic>
#	include <functional>
#	include <memory>
#	include <mutex>
#	include <optional>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>
#include <Core/Network/http.h>
#include <Core/Network/IHttpRouter.h>

#include "ITwinAuthInfo.h"
#include "ITwinAuthStatus.h"
#include "ITwinEnvironment.h"
#include "ITwinTypes.h"

MODULE_EXPORT namespace AdvViz::SDK
{
	class ITwinAuthObserver;
	class Http;

	enum class EITwinAuthGrantType : uint8_t
	{
		AuthCode,
		ClientCredentials
	};

	enum class EITwinAuthOverrideMode : uint8_t
	{
		/**
		 * @brief No override: we will use the regular token retrieved through the standard iTwin
		 * authorization process.
		 */
		None,

		/**
		 * @brief Access token retrieved from another process (such as iTwinStudio).
		 */
		IPC,

		/**
		 * @brief Authorization temporarily granted to access another user's iTwins.
		 */
		SharedITwins,

		/**
		 * @brief Usually a fake authorization, specific to unit testing.
		 * @remark This does not grant access to anything, obviously.
		 */
		Testing
	};


	/// In the future, the whole authorization process will be moved here.
	/// For now, we only centralize the access token.

	class ITwinAuthManager : public Tools::Factory<ITwinAuthManager, EITwinEnvironment>
	{
	public:
		class Credentials;

		using SharedInstance = std::shared_ptr<ITwinAuthManager>;

		// App ID can be depend on the chosen environment, so we store all possible values
		static constexpr size_t ENV_COUNT = static_cast<size_t>(EITwinEnvironment::Invalid) + 1;
		using AppIDArray = std::array<std::string, ENV_COUNT>;

		//! Sets the iTwin AppID for all environments.
		static void SetAppIDArray(AppIDArray const& ITwinAppIDs, bool bLogIDs = true);
		//! Sets the iTwin AppID for the given environment.
		static void SetAppID(EITwinEnvironment env, std::string const& ITwinAppID, bool bLogID = true);
		static bool HasAppID(EITwinEnvironment env);
		static std::string GetAppID(EITwinEnvironment env);

		static void AddScope(std::string const& extraScope);
		//! Only an extra scope that was added with AddScope can be removed with RemoveScope.
		static bool RemoveScope(std::string const& extraScope);
		static bool HasScope(std::string const& scope);

		static void SetRedirectUriPort(int port);
		static int GetRedirectUriPort();

		//! Redirects the IMS requests (in particular the token endpoint) to the given base URL.
		//! For unit testing only: this is what allows a mock server to answer /connect/token.
		//! Pass std::nullopt to restore the regular, environment-based URL.
		void SetIMSBaseUrlForTesting(std::optional<std::string> const& baseUrl);

		static SharedInstance& GetInstance(EITwinEnvironment env);

		//! Destroys the manager instance of the given environment, so that the next call to
		//! GetInstance builds a fresh one.
		//! For unit testing only: this is what lets two test sections use the same environment
		//! without inheriting each other's state - in particular the lazily created, and never
		//! re-created, http router (see CheckAuthorization).
		//! Any authorization in progress on that instance is cancelled first.
		static void ResetInstanceForTesting(EITwinEnvironment env);

		virtual ~ITwinAuthManager();

		//! Returns the iTwin AppID for this environment.
		std::string GetAppID() const;
		//! Returns the client ID for authorization process (generally the AppID).
		std::string GetClientID() const;

		void AddObserver(ITwinAuthObserver* observer);
		void RemoveObserver(ITwinAuthObserver* observer);

		/// Initiates the authorization process if needed. It is asynchronous.
		/// \return Success if an access token was previously retrieved and is still valid ; Failed in case
		/// of error while trying to initiate the authorization ; InProgress if the authorization has to be
		/// requested, and could be initiated.
		EITwinAuthStatus CheckAuthorization();

		bool HasAccessToken() const;
		std::shared_ptr<ThreadSafeAccessToken> GetAccessToken() const;

		//! Returns the regular access token, ignoring any override.
		std::string const& GetRegularAccessToken() const;

		//! Sets the regular access token for this environment.
		void SetAccessToken(std::string const& accessToken);

		//! Used by the "Open shared iTwin" feature: overrides the regular access token
		//! with the one provided as argument, so that GetAccessToken() returns the
		//! "override" token instead of the regular one.
		//! Pass an empty string to restore the regular token.
		//! The same mechanism was extended to retrieve the authorization from another process, and for unit
		//! testing.
		void SetOverrideAccessToken(std::string const& accessToken, EITwinAuthOverrideMode overrideMode);

		//! Restore the regular access token, by resetting the override one.
		void ResetOverrideAccessToken();

		//! Returns the access token override mode.
		EITwinAuthOverrideMode GetOverrideMode() const;

		/// Returns true as long as the route listening for the authorization callback is bound.
		/// This is what prevents us from rebinding the same port.
		bool IsAuthPortBound() const;

		//! Returns true if an authorization is currently in progress.
		bool IsAuthorizationInProgress() const;

		/// Cancels the authorization currently in progress, if any:
		/// releases the route listening for the authorization callback, cancels any pending
		/// restart, and makes sure a token exchange already in flight will be discarded when it
		/// completes (see authEpoch_).
		/// Does *not* reset the current access token: this only aborts an ongoing sign-in.
		/// Observers are not notified, because the cancellation is a deliberate action of the
		/// caller, not an authorization failure.
		///
		/// \return True if the authorization callback route was still bound, and has been
		/// released - ie. the same condition as IsAuthPortBound().
		/// \remark Beware the returned value is *not* a reliable "something was aborted" flag.
		/// Once the browser redirect has been received, the route is already unbound while the
		/// token exchange is still running: in that window this method returns false, even though
		/// bumping the epoch did abort the sign-in. The cancellation itself is always effective;
		/// only the reporting is narrower.
		bool CancelAuthorization();

		std::string GetCurrentAuthorizationURL() const;

		//! Switch to client credentials grant type (for internal usage only).
		bool SetClientCredentialGrantType(std::string const& clientID, std::string const& clientSecret,
			std::optional<std::string> const& imsName = std::nullopt,
			std::optional<std::string> const& customScope = std::nullopt);

		//! Encode/decode a token.
		virtual bool EncodeToken(std::string const& InToken, std::string const& InKeyRoot, std::string& OutEncode) const  = 0;
		virtual bool DecodeToken(std::string const& InEncoded, std::string const& InKeyRoot, std::string& OutToken) const = 0;

		//! Resets current authorization token, if it was acquired from the SDK, rather than set from
		//! SetOverrideAccessToken, and reset cached data so that the next authorization process triggers a
		//! new login.
		//! If an authorization is currently in progress, it is cancelled (see CancelAuthorization).
		virtual bool Logout();


	protected:
		ITwinAuthManager(EITwinEnvironment Env);
		static std::string GetRedirectUri();

		std::string GetScope() const;
		std::string GetIMSBaseUrl() const;

		bool HasRefreshToken() const;
		void GetRefreshToken(std::string& refreshToken) const;
		std::chrono::system_clock::time_point GetExpirationTime() const;

		void SetAuthorizationURL(std::string const& authorizationURL);


	private:
		enum class EAuthContext
		{
			StdRequest,
			Reload,
			Logout,
		};

		//! Returns the access token to use, it may be the "override" or regular one.
		std::string const& GetCurrentAccessToken() const;

		bool TryLoadRefreshToken();
		bool ResetRefreshToken();

		/// Returns the current authorization epoch - see authEpoch_.
		/// Call this when initiating a token exchange, and pass the returned value to
		/// IsAuthEpochCurrent when the response finally arrives.
		uint64_t GetAuthEpoch() const;

		/// Returns whether the given epoch, obtained from GetAuthEpoch when a token exchange was
		/// initiated, is still the current one - ie. whether the response to that exchange is
		/// still relevant, or must be discarded - see authEpoch_.
		bool IsAuthEpochCurrent(uint64_t epoch) const;

		bool SaveAccessToken(std::string const& accessToken, EAuthContext authContext) const;
		bool ReloadAccessToken(std::string& readAccessToken, ITwinAuthInfo& readAuthInfo) const;

		/// Update the authorization information upon successful server response
		void SetAuthorizationInfo(std::string const& accessToken, ITwinAuthInfo const& authInfo,
			EAuthContext authContext = EAuthContext::StdRequest);

		void NotifyResult(bool bSuccess, std::string const& strError);

		void ResetRefreshTicker();

		enum class ETokenMode
		{
			Standard,
			Refresh
		};
		RequestID ProcessTokenRequest(std::string const& verifier, std::string const& authorizationCode,
			ETokenMode tokenMode, bool isAutomaticRefresh = false);

		/// When using the refresh mode, and the latter is impossible for some reason, call this to ensure the
		/// whole authorization process with restart in a clean way as soon as possible.
		void RestartAuthorizationLater();

		void ResetRestartTicker();

		virtual bool SavePrivateData(std::string const& data, int keyIndex = 0) const = 0;
		virtual bool LoadPrivateData(std::string& data, int keyIndex = 0) const = 0;

		/// Starts the actual authorization. A possible implementation could consist in opening
		/// a web browser.
		virtual bool StartAuthorizationInstance(std::string const& state, std::string const& codeVerifier,
			std::string& error) = 0;

	protected:
		const EITwinEnvironment env_;

	private:
		using Mutex = std::recursive_mutex;
		using Lock = std::lock_guard<std::recursive_mutex>;
		mutable Mutex mutex_;
		std::string accessToken_;
		std::string overrideAccessToken_;
		EITwinAuthOverrideMode overrideMode_ = EITwinAuthOverrideMode::None;
		std::shared_ptr<ThreadSafeAccessToken> currentToken_;
		ITwinAuthInfo authInfo_;

		std::shared_ptr<Http> http_;
		std::shared_ptr<IHttpRouter> httpRouter_;
		/// The route currently bound to receive the authorization callback, if any.
		/// This is the single source of truth for "an authorization is in progress".
		IHttpRouter::RouteHandlePtr routeHandle_; // guarded by mutex_

		/// Incremented every time the authorization process is cancelled (see CancelAuthorization).
		///
		/// Rationale: releasing routeHandle_ is enough to abort a sign-in that is still waiting for
		/// the browser redirect, but *not* one that already received it. In the latter case the
		/// route is already unbound while the /connect/token exchange is still in flight, and its
		/// completion handler would happily log the user back in - typically right after a Logout().
		/// Since an http request cannot be cancelled once sent, we discard its outcome instead: a
		/// token exchange captures the epoch in effect when it is initiated, and its response is
		/// ignored if the epoch has changed in the meantime.
		///
		/// Never reset, and only ever incremented, so that an epoch value can never be mistaken for
		/// a later one. uint64_t makes wrapping a non-issue in practice.
		uint64_t authEpoch_ = 0; // guarded by mutex_

		std::string currentAuthorizationURL_;
		int loadRefreshTokenAttempts_ = 0;

		EITwinAuthGrantType grantType_ = EITwinAuthGrantType::AuthCode;
		std::string clientSecret_; // only used in ClientCredentials mode
		std::optional<std::string> customClientId_;
		std::optional<std::string> customScope_;
		std::optional<std::string> customIMSBaseUrl_; // unit tests only

		std::shared_ptr< std::atomic_bool > stillValid_; // to check lambda validity

		std::vector<ITwinAuthObserver*> observers_;

		using Pool = std::array<SharedInstance, (size_t)EITwinEnvironment::Invalid>;
		struct Globals
		{
			Pool instances_;
		};
		static Globals& GetGlobals();
	};
}
