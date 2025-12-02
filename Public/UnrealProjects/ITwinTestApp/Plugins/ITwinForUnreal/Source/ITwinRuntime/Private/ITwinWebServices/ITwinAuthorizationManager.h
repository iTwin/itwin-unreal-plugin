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


/// Implementation details for handing of authorization tokens.
/// The aim is to manage only one token per environment (the access token *and* the refresh token,
/// if applicable).

class FITwinAuthorizationManager : public AdvViz::SDK::ITwinAuthManager, public AdvViz::SDK::Tools::TypeId<FITwinAuthorizationManager>
{
public:
	/// Must be called at the beginning of the execution, to connect stuff in the SDK.
	static void OnStartup();

	static bool SaveToken(FString const& InInfo, AdvViz::SDK::EITwinEnvironment Env);
	static bool LoadToken(FString& OutInfo, AdvViz::SDK::EITwinEnvironment Env);
	static void DeleteTokenFile(AdvViz::SDK::EITwinEnvironment Env);

	static void SetUseExternalBrowser(bool bInUseExternalBrowser);
	static bool UseExternalBrowser();

#if WITH_TESTS
	static void SetupTestMode(AdvViz::SDK::EITwinEnvironment Env, FString const& TokenFileSuffix);
#endif

	using AdvViz::SDK::Tools::TypeId<FITwinAuthorizationManager>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::ITwinAuthManager::IsTypeOf(i); }
	
	FITwinAuthorizationManager(AdvViz::SDK::EITwinEnvironment Env);
	~FITwinAuthorizationManager();

	virtual bool EncodeToken(std::string const& InToken, std::string const& InKeyRoot, std::string& OutEncoded) const override;
	virtual bool DecodeToken(std::string const& InEncoded, std::string const& InKeyRoot, std::string& OutToken) const override;

	/// Helper to disable the use of external browser for a given scope.
	class [[nodiscard]] FExternalBrowserDisabler
	{
	public:
		FExternalBrowserDisabler();
		~FExternalBrowserDisabler();
	private:
		const bool bOldUseExternalBrowser = UseExternalBrowser();
	};

private:
	static bool SavePrivateData(FString const& InInfo, AdvViz::SDK::EITwinEnvironment Env, int KeyIndex, FString const& FileSuffix);
	static bool LoadPrivateData(FString& OutInfo, AdvViz::SDK::EITwinEnvironment Env, int KeyIndex, FString const& FileSuffix);

	static bool EncodeTokenData(FString const& InToken, FString const& InKeyRoot, FString& OutEncoded);
	static bool DecodeTokenData(FString const& InEncoded, FString const& InKeyRoot, FString& OutToken);


	virtual bool SavePrivateData(std::string const& data, int keyIndex = 0) const override;
	virtual bool LoadPrivateData(std::string& data, int keyIndex = 0) const override;

	virtual bool StartAuthorizationInstance(std::string const& state, std::string const& codeVerifier, std::string& error) override;


	//! Whether the authorization process should open an external web browser (default mode in plugin).
	static bool bUseExternalBrowser;
};
