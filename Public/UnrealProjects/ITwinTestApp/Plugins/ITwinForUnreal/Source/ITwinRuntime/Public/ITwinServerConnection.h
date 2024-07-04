/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerConnection.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <Interfaces/IHttpRequest.h>
#include <Interfaces/IHttpResponse.h>
#include <Logging/LogMacros.h>
#include <UObject/Object.h>
#include <array>
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
	using AppIDArray = std::array<FString, ENV_COUNT>;
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
	
	//! OAuth access token, without any prefix (eg. it should not start with "Bearer ").
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		Transient)
	FString AccessToken;

	FString UrlPrefix() const;

	UFUNCTION(BlueprintCallable, Category = "iTwin")
	bool HasAccessToken() const { return !AccessToken.IsEmpty(); }

	static bool CheckRequest(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
							 bool connectedSuccessfully, FString* pstrError = nullptr);
	//! Sets the app ID for all environments.
	//! This function is useful only for Bentley apps, which may use non-Prod envs.
	static void SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs);
	//! Sets the app ID for the Prod environment.
	//! This function is a convenience for non-Bentley apps, since such apps only use this env.
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	static void SetITwinAppID(const FString& AppID);
};

DECLARE_LOG_CATEGORY_EXTERN(LogITwinHttp, Log, All);
