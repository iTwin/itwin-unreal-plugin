/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>

#include <ITwinDigitalTwin.generated.h>

struct FIModelInfos;

UCLASS()
class ITWINRUNTIME_API AITwinDigitalTwin : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	AITwinDigitalTwin();
	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
	virtual void Destroyed() override;

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateITwin();

private:
	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info) override;
	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
