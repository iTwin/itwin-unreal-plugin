/*--------------------------------------------------------------------------------------+
|
|     $Source: DigitalTwinManagerTestHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/TestHelperSingleton.h>
#include <CoreMinimal.h>
#include <memory>

#include "DigitalTwinManagerTestHelper.generated.h"


class AITwinDigitalTwinManager;
struct FIModelTestOptions;

UCLASS()
class UDigitalTwinManagerTestHelper : public UObject, public TTestHelperSingleton<UDigitalTwinManagerTestHelper>
{
	GENERATED_BODY()
public:
	UDigitalTwinManagerTestHelper();

	bool Init(FIModelTestOptions const& Options);
	void RegisterEvents();
	bool PostCondition() const;
	void OnReset();

	UFUNCTION()
	void OnITwinInfoRetrieved();

	UFUNCTION()
	void OnLoadComponent(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId);

	UFUNCTION()
	void OnWillRemoveComponent(AActor* ComponentWillBeRemoved, EITwinModelType ModelType);

	UFUNCTION()
	void OnRemoveComponent(FString ModelId, EITwinModelType ModelType);

	UFUNCTION()
	void OnGeoLocationSet(bool bFromScene, bool bFromElevationRequest);

	UFUNCTION()
	void OnGeoLocationGapInMeters(double DistanceMeters, const FString& LayerId);

	UFUNCTION()
	void OnAtmosphereLoaded(bool bSuccess);

	class FImpl;
	FImpl& GetImpl() { return *Impl; }


public:
	UPROPERTY()
	TObjectPtr<AITwinDigitalTwinManager> ITwinManager;

private:
	TPimplPtr<FImpl> Impl;
};

#endif // WITH_TESTS
