/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinAuthManager.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


#include <Containers/Ticker.h>
#include <map>


/// Implementation details for handing of authorization tokens.
/// The aim is to manage only one token per environment (the access token *and* the refresh token,
/// if applicable).

class FITwinAuthorizationManager : public SDK::Core::ITwinAuthManager
{
public:
	/// Must be called at the beginning of the execution, to connect stuff in the SDK.
	static void OnStartup();

	static bool SaveToken(FString const& InInfo, SDK::Core::EITwinEnvironment Env);
	static bool LoadToken(FString& OutInfo, SDK::Core::EITwinEnvironment Env);
	static void DeleteTokenFile(SDK::Core::EITwinEnvironment Env);

#if WITH_TESTS
	static void SetupTestMode(SDK::Core::EITwinEnvironment Env, FString const& TokenFileSuffix);
#endif

	
	FITwinAuthorizationManager(SDK::Core::EITwinEnvironment Env);
	~FITwinAuthorizationManager();


private:
	static bool SavePrivateData(FString const& InInfo, SDK::Core::EITwinEnvironment Env, int KeyIndex, FString const& FileSuffix);
	static bool LoadPrivateData(FString& OutInfo, SDK::Core::EITwinEnvironment Env, int KeyIndex, FString const& FileSuffix);

	virtual bool SavePrivateData(std::string const& data, int keyIndex = 0) const override;
	virtual bool LoadPrivateData(std::string& data, int keyIndex = 0) const override;

	virtual bool LaunchWebBrowser(std::string const& state, std::string const& codeVerifier, std::string& error) const override;

	virtual void UniqueDelayedCall(std::string const& uniqueId, std::function<bool()> const& func, float delayInSeconds) override;


private:
	std::map<std::string, FTSTicker::FDelegateHandle> tickerHandlesMap_;
};
