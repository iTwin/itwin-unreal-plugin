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
#include <memory>
#include <mutex>


class IITwinAuthorizationObserver;

/// Implementation details for handing of authorization tokens.
/// The aim is to manage only one token per environment (the access token *and* the refresh token,
/// if applicable).

class FITwinAuthorizationManager
{
public:
	using SharedInstance = std::shared_ptr<FITwinAuthorizationManager>;

	static void SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs);
	static bool HasITwinAppID(EITwinEnvironment Env);

	static SharedInstance& GetInstance(EITwinEnvironment Env);

	~FITwinAuthorizationManager();

	void AddObserver(IITwinAuthorizationObserver* Observer);
	void RemoveObserver(IITwinAuthorizationObserver* Observer);

	/// The main entry point
	void CheckAuthorization();

	bool HasAccessToken() const;
	void GetAccessToken(FString& OutAccessToken) const;


private:
	FITwinAuthorizationManager(EITwinEnvironment Env);
	void ResetAllTickers();

	bool HasRefreshToken() const;
	void GetRefreshToken(FString& OutRefreshToken) const;
	double GetExpirationTime() const;

	bool TryLoadRefreshToken();
	void ResetRefreshToken();

	/// Update the authorization information upon successful server response
	void SetAuthorizationInfo(FString const& InAccessToken,
		FITwinAuthorizationInfo const& InAuthInfo, bool bAutoRefresh = true);

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
	FITwinAuthorizationInfo AuthInfo;
	int LoadRefreshTokenAttempts = 0;

	std::shared_ptr< std::atomic_bool > IsThisValid; // same principle as in #FReusableJsonQueries::FImpl

	TArray<IITwinAuthorizationObserver*> Observers;

	FTSTicker::FDelegateHandle RefreshAuthDelegate;
	FTSTicker::FDelegateHandle RestartAuthDelegate;
	std::atomic_bool bHasBoundAuthPort = false;

	using Pool = std::array<SharedInstance, (size_t)EITwinEnvironment::Invalid>;
	static Pool Instances;
};
