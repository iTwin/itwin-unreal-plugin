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

class FITwinSynchro4DAnimator;
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

	/// Determine what Elements are under the mouse cursor and log information about them. Only the first few
	/// encountered Elements are processed to avoid overflowing the logs.
	/// \param pMaxUniqueElementsHit Pointer to a variable storing the maximum number of Elements to identify
	///		(passing nullptr is allowed and means "1")
	void IdentifyElementsUnderCursor(uint32 const* pMaxUniqueElementsHit);

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
