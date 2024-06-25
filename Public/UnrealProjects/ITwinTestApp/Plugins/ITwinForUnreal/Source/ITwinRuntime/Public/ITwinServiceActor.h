/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServiceActor.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Logging/LogMacros.h>

#include <ITwinServiceActor.generated.h>

class UITwinWebServices;

DECLARE_LOG_CATEGORY_EXTERN(LogITwin, Log, All);


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
	virtual void Destroyed() override;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	const UITwinWebServices* GetWebServices() const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	UITwinWebServices* GetMutableWebServices();

protected:
	UPROPERTY()
	TObjectPtr<UITwinWebServices> WebServices;


	void UpdateWebServices();

	enum class EConnectionStatus : uint8
	{
		NotConnected,
		InProgress,
		Connected
	};

	/// Returns current connection status
	/// If bRequestAuthorisationIfNeeded is true and no valid connection currently exists, triggers an
	/// authorization request.
	EConnectionStatus CheckServerConnection(bool bRequestAuthorisationIfNeeded = true);

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual const TCHAR* GetObserverName() const override;


private:
	virtual void UpdateOnSuccessfulAuthorization();

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) override;
};
