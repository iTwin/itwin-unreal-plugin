/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once


#ifndef SDK_CPPMODULES
#	include <array>
#	include <atomic>
#	include <functional>
#	include <memory>
#	include <mutex>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>

#include "ITwinAuthInfo.h"
#include "ITwinAuthStatus.h"
#include "ITwinEnvironment.h"
#include "ITwinTypes.h"

MODULE_EXPORT namespace SDK::Core
{
	class ITwinAuthObserver;
	class Http;
	class IHttpRouter;

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

		static void SetAppIDArray(AppIDArray const& ITwinAppIDs);
		static bool HasAppID(EITwinEnvironment env);
		static std::string GetAppID(EITwinEnvironment env);
		static void AddScope(std::string const& extraScope);
		static bool HasScope(std::string const& scope);

		static SharedInstance& GetInstance(EITwinEnvironment env);

		virtual ~ITwinAuthManager();

		//! Returns the iTwin AppID for this environment.
		std::string GetAppID() const;

		void AddObserver(ITwinAuthObserver* observer);
		void RemoveObserver(ITwinAuthObserver* observer);

		/// Initiates the authorization process if needed. It is asynchronous.
		/// \return Success if an access token was previously retrieved and is still valid ; Failed in case
		/// of error while trying to initiate the authorization ; InProgress if the authorization has to be
		/// requested, and could be initiated.
		EITwinAuthStatus CheckAuthorization();

		bool HasAccessToken() const;
		void GetAccessToken(std::string& outAccessToken) const;

		//! Sets the regular access token for this environment.
		void SetAccessToken(std::string const& accessToken);

		//! Used by the "Open shared iTwin" feature: overrides the regular access token
		//! with the one provided as argument, so that GetAccessToken() returns the
		//! "override" token instead of the regular one.
		//! Pass an empty string to restore the regular token.
		void SetOverrideAccessToken(std::string const& accessToken);

		bool IsAuthorizationInProgress() const;

		//! Delay a function call to a certain delay
		//! (could be moved to an independent class in the future TODO_JDE...)
		virtual void UniqueDelayedCall(std::string const& uniqueId, std::function<bool()> const& func, float delayInSeconds) = 0;


	protected:
		ITwinAuthManager(EITwinEnvironment Env);
		static std::string GetRedirectUri();

		std::string GetScope() const;
		std::string GetIMSBaseUrl() const;


		bool HasRefreshToken() const;
		void GetRefreshToken(std::string& refreshToken) const;
		std::chrono::system_clock::time_point GetExpirationTime() const;


	private:
		//! Returns the access token to use, it may be the "override" or regular one.
		std::string const& GetCurrentAccessToken() const;

		bool TryLoadRefreshToken();
		void ResetRefreshToken();

		bool SaveAccessToken(std::string const& accessToken) const;
		bool ReloadAccessToken(std::string& readAccessToken, ITwinAuthInfo& readAuthInfo) const;

		/// Update the authorization information upon successful server response
		enum class EAuthContext
		{
			StdRequest,
			Reload
		};
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

		virtual bool LaunchWebBrowser(std::string const& state, std::string const& codeVerifier, std::string& error) const = 0;


	protected:
		const EITwinEnvironment env_;

	private:
		using Mutex = std::recursive_mutex;
		using Lock = std::lock_guard<std::recursive_mutex>;
		mutable Mutex mutex_;
		std::string accessToken_;
		std::string overrideAccessToken_;
		ITwinAuthInfo authInfo_;

		std::shared_ptr<Http> http_;
		std::shared_ptr<IHttpRouter> httpRouter_;
		std::atomic_bool hasBoundAuthPort_ = false;
		int loadRefreshTokenAttempts_ = 0;

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
