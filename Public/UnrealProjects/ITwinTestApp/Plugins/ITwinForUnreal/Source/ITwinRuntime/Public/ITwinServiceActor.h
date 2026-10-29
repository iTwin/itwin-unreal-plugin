/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServiceActor.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <ITwinEnvironmentEnum.h>
#include <ITwinLogCategory.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthStatus.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <ITwinServiceActor.generated.h>

class UITwinWebServices;


UENUM(BlueprintType)
enum class EEnvironmentSelectionMethod : uint8
{
	ESM_Automatic UMETA(DisplayName = "Automatic"),
	ESM_Manual UMETA(DisplayName = "Manual")
};

//! Base class for all actors interacting with an iTwin Web Service.
//! (and thus using an iTwin server connection at some point)
UCLASS()
class ITWINRUNTIME_API AITwinServiceActor : public AActor, public FITwinDefaultWebServicesObserver
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	EEnvironmentSelectionMethod EnvironmentSelectionMethod = EEnvironmentSelectionMethod::ESM_Automatic;

	UPROPERTY(Category = "iTwin",
		meta = (EditCondition = "EnvironmentSelectionMethod == EEnvironmentSelectionMethod::ESM_Manual"),
		EditAnywhere)
	EITwinEnvironment Environment = EITwinEnvironment::Invalid;


	AITwinServiceActor();
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(Category = "iTwin",
		BlueprintGetter)
	EITwinEnvironment GetEnvironment() const { return Environment; }

	UFUNCTION(Category = "iTwin",
		BlueprintSetter)
	void SetEnvironment(EITwinEnvironment InEnvironment);

	//! Copy environment-related settings from another iTwinServiceActor (eg a manager) to this one.
	void CopyEnvironmentFrom(AITwinServiceActor const& Other);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	const UITwinWebServices* GetWebServices() const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	UITwinWebServices* GetMutableWebServices();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	bool HasAccessToken() const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	FString GetAccessToken() const;


#if WITH_TESTS
	//! Used in automated tests, to enable mocking of web services.
	//! \param ServerUrl The url of the mock server to use (eg "http://localhost:1234").
	void SetTestMode(FString const& ServerUrl);
	//! Returns true if the actor is currently in test mode (ie using a mock server).
	//! In such case, ServerUrl is filled with the url of the mock server.
	bool IsInTestMode(FString& OutServerUrl) const;
#endif

protected:
	UPROPERTY()
	TObjectPtr<UITwinWebServices> WebServices;


	void UpdateWebServices();


	/// Returns current authorization status
	/// If bRequestAuthorisationIfNeeded is true and no valid connection currently exists, triggers an
	/// authorization request.
	AdvViz::SDK::EITwinAuthStatus CheckAuthorizationStatus(bool bRequestAuthorisationIfNeeded = true);

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual const TCHAR* GetObserverName() const override;


private:
	virtual void UpdateOnSuccessfulAuthorization();

	/// overridden from AdvViz::SDK::ITwinAuthObserver:
	virtual void OnAuthorizationDone(bool bSuccess, std::string const& Error) override;
};
