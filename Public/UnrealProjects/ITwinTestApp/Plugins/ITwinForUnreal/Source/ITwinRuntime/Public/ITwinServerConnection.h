/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerConnection.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <Interfaces/IHttpRequest.h>
#include <Interfaces/IHttpResponse.h>
#include <Logging/LogMacros.h>
#include <UObject/Object.h>
#include <array>
#include <string>
#include <memory>
#include <ITwinServerConnection.generated.h>

UENUM()
enum class EITwinEnvironment : uint8
{
	Prod,
	QA,
	Dev,
	Invalid,
};

namespace ITwin
{
	// App ID can be depend on the chosen environment, so we store all possible values
	static constexpr size_t ENV_COUNT = static_cast<size_t>(EITwinEnvironment::Invalid) + 1;
	using AppIDArray = std::array<std::string, ENV_COUNT>;
}

//! Stores common information used to access the iTwin Web APIs.
UCLASS()
class ITWINRUNTIME_API AITwinServerConnection : public AActor
{
	GENERATED_BODY()
public:
	//! Application environment determining the prefix used to access the iTwin APIs
	//! (eg. "qa-", "dev-" or "") as well as other implementation details (exact URLs,
	//! available versions...)
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	EITwinEnvironment Environment = EITwinEnvironment::Invalid;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	FString UrlPrefix() const;

	//! Return the OAuth access token for this environment, if any.
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	FString GetAccessToken() const;

	std::shared_ptr<std::string> GetAccessTokenPtr() const;

	UFUNCTION(BlueprintCallable, Category = "iTwin")
	bool HasAccessToken() const { return !GetAccessToken().IsEmpty(); }

	static bool CheckRequest(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
		bool connectedSuccessfully, FString* pstrError = nullptr, bool const bWillRetry = false);
	//! Sets the app ID for all environments.
	//! This function is useful only for Bentley apps, which may use non-Prod envs.
	static void SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs);
	//! Sets the app ID for the Prod environment.
	//! This function is a convenience for non-Bentley apps, since such apps only use this env.
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	static void SetITwinAppID(const FString& AppID);
};

DECLARE_LOG_CATEGORY_EXTERN(LogITwinHttp, Log, All);
