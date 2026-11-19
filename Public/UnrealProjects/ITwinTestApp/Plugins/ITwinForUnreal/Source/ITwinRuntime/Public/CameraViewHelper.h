/*--------------------------------------------------------------------------------------+
|
|     $Source: CameraViewHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <Engine/World.h>
#include <TimerManager.h>
#include "CameraViewHelper.generated.h"

class ACameraActor;

namespace ITwin {
	void StartBlendedCameraMovement(UWorld* World, const FTransform& NewCameraTransform, float BlendTime = 3.f);
}

UCLASS()
class ITWINRUNTIME_API ACameraViewHelper : public AActor
{
	GENERATED_BODY()

public:
	void StartMovement(const FTransform& NewCameraTransform, float BlendTime);
	void EndMovement();

private:
	FTimerHandle TimerHandle;
	FTransform TargetTransform;
	TObjectPtr<ACameraActor> OverviewCamera = nullptr;
};
