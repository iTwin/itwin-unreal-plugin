/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationManager.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "ITwinAuthorizationInfo.h"

#include <ITwinServerConnection.h>
#include <Containers/Ticker.h>
#include <Templates/SharedPointer.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace SDK::Core
{
	class ITwinAuthManager;
}

class IITwinAuthorizationObserver;

/// Implementation details for handing of authorization tokens.
/// The aim is to manage only one token per environment (the access token *and* the refresh token,
/// if applicable).

class ITWINRUNTIME_API FITwinAuthorizationManager
{
public:
	using SharedInstance = std::shared_ptr<FITwinAuthorizationManager>;

	static void SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs);
	static bool HasITwinAppID(EITwinEnvironment Env);
	static void AddScopes(FString const& ExtraScopes);

	static SharedInstance& GetInstance(EITwinEnvironment Env);

	~FITwinAuthorizationManager();

	FString GetITwinAppId() const;
	void AddObserver(IITwinAuthorizationObserver* Observer);
	void RemoveObserver(IITwinAuthorizationObserver* Observer);

	/// The main entry point
	void CheckAuthorization();

	bool HasAccessToken() const;
	void GetAccessToken(FString& OutAccessToken) const;
	//! Used by the "Open shared iTwin" feature: overrides the regular access token
	//! with the one provided as argument, so that GetAccessToken() returns the
	//! "override" token instead of the regular one.
	//! Pass an empty string to restore the regular token.
	void SetOverrideAccessToken(const FString& InAccessToken);

	bool IsAuthorizationInProgress() const;

	/// "Extension" for desktop application.
	using FTokenGrantedObserverFunc = std::function<void(FString const& InToken, FITwinAuthorizationInfo const& InAuthInfo)>;
	using FReloadTokenFunc = std::function<bool(FString& OutToken, FITwinAuthorizationInfo& OutAuthInfo)>;

	void SetTokenGrantedObserverFunc(FTokenGrantedObserverFunc const& Func);
	void SetReloadTokenFunc(FReloadTokenFunc const& Func);

private:
	FITwinAuthorizationManager(EITwinEnvironment Env);
	void ResetAllTickers();

	bool HasRefreshToken() const;
	void GetRefreshToken(FString& OutRefreshToken) const;
	double GetExpirationTime() const;

	bool TryLoadRefreshToken();
	void ResetRefreshToken();

	/// Update the authorization information upon successful server response
	enum class EAuthContext
	{
		StdRequest,
		Reload
	};
	void SetAuthorizationInfo(FString const& InAccessToken,
		FITwinAuthorizationInfo const& InAuthInfo, EAuthContext AuthContext = EAuthContext::StdRequest);

	void NotifyResult(bool bSuccess, FString const& Error);

	void ResetRefreshTicker();

	enum class ETokenMode
	{
		TM_Standard,
		TM_Refresh
	};
	void ProcessTokenRequest(FString const verifier, FString const authorizationCode,
		ETokenMode tokenMode, bool bIsAutomaticRefresh = false);

	/// When using the refresh mode, and the latter is impossible for some reason, call this to ensure the
	/// whole authorization process with restart in a clean way as soon as possible.
	void RestartAuthorizationLater();

	void ResetRestartTicker();

private:
	const EITwinEnvironment Environment;

	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<std::recursive_mutex>;
	mutable FMutex Mutex;
	FString AccessToken;
	FString OverrideAccessToken;
	FITwinAuthorizationInfo AuthInfo;
	int LoadRefreshTokenAttempts = 0;

	std::shared_ptr< std::atomic_bool > IsThisValid; // same principle as in #FReusableJsonQueries::FImpl

	TArray<IITwinAuthorizationObserver*> Observers;

	FTSTicker::FDelegateHandle RefreshAuthDelegate;
	FTSTicker::FDelegateHandle RestartAuthDelegate;
	std::atomic_bool bHasBoundAuthPort = false;

	using CoreManager = SDK::Core::ITwinAuthManager;
	std::shared_ptr<CoreManager> CoreImpl;

	FTokenGrantedObserverFunc OnTokenGrantedFunc;
	FReloadTokenFunc ReloadTokenFunc;

	using Pool = std::array<SharedInstance, (size_t)EITwinEnvironment::Invalid>;
	static Pool Instances;
};
