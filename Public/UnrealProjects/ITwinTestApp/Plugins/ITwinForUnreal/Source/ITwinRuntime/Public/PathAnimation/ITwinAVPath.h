/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAVPath.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include "ITwinAVPath.generated.h"


UCLASS()
class ITWINRUNTIME_API AITwinAVPath : public AActor
{
	GENERATED_BODY()

public:
	virtual FTransform GetPathPosition(int32 /*HandleIndex*/, float /*DistanceCm*/) const { return FTransform(); }
	virtual float GetDistanceFromTime(int32 /*HandleIndex*/, float DeltaTime) const { return SpeedCmPerSec*DeltaTime; }
	virtual float GetSpeed(int32 /*HandleIndex*/) const { return SpeedCmPerSec; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float SpeedCmPerSec = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float WheelRadiusCm = 45.f;

	/** Distance along path between consecutive section reference pivots (e.g. V0_Pivot to V0_V1_Pivot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	float HitchDistanceCm = 225.f;
};