/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>
#include <ITwinRealityData.generated.h>


UCLASS()
class ITWINRUNTIME_API AITwinRealityData : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	TSharedPtr<FITwinGeolocation> Geolocation;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString RealityDataId;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	AITwinRealityData();
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateRealityData();
	
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable,
		Meta = (EditCondition = "bGeolocated"))
	void UseAsGeolocation();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void Reset();

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	bool bGeolocated;

	class FImpl;
	TPimplPtr<FImpl> Impl;


	bool HasTileset() const;
	void DestroyTileset();

	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;
};
