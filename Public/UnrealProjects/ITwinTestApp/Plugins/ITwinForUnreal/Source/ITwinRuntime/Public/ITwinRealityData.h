/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <Templates/PimplPtr.h>
#include <ITwinRealityData.generated.h>

UCLASS()
class ITWINRUNTIME_API AITwinRealityData : public AActor
{
	GENERATED_BODY()
public:
	TSharedPtr<FITwinGeolocation> Geolocation;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TObjectPtr<AITwinServerConnection> ServerConnection;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString RealityDataId;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	AITwinRealityData();
	virtual void Destroyed() override;
	
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateRealityData();
	
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable,
		Meta = (EditCondition = "bGeolocated"))
	void UseAsGeolocation();
private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	bool bGeolocated;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
