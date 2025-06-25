/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServiceActor.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Logging/LogMacros.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthStatus.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <ITwinServiceActor.generated.h>

class UITwinWebServices;

ITWINRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogITwin, Log, All);


//! Base class for all actors interacting with an iTwin Web Service.
//! (and thus using an iTwin server connection at some point)
UCLASS()
class ITWINRUNTIME_API AITwinServiceActor : public AActor, public FITwinDefaultWebServicesObserver
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TObjectPtr<AITwinServerConnection> ServerConnection;


	AITwinServiceActor();
	virtual void BeginDestroy() override;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	const UITwinWebServices* GetWebServices() const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	UITwinWebServices* GetMutableWebServices();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	FString GetAccessToken() const;


#if WITH_TESTS
	//! Used in automated tests, to enable mocking of web services.
	//! \param ServerUrl The url of the mock server to use (eg "http://localhost:1234").
	void SetTestMode(FString const& ServerUrl);
#endif

protected:
	UPROPERTY()
	TObjectPtr<UITwinWebServices> WebServices;


	void UpdateWebServices();


	/// Returns current connection status
	/// If bRequestAuthorisationIfNeeded is true and no valid connection currently exists, triggers an
	/// authorization request.
	AdvViz::SDK::EITwinAuthStatus CheckServerConnection(bool bRequestAuthorisationIfNeeded = true);

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual const TCHAR* GetObserverName() const override;


private:
	virtual void UpdateOnSuccessfulAuthorization();

	/// overridden from AdvViz::SDK::ITwinAuthObserver:
	virtual void OnAuthorizationDone(bool bSuccess, std::string const& Error) override;
};
