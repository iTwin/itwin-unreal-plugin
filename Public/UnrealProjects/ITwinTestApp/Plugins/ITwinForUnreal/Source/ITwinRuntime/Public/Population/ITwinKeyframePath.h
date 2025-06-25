/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinKeyframePath.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include <memory>
#include <string>

#include "ITwinKeyframePath.generated.h"

namespace AdvViz::SDK
{
	class IKeyframeAnimator;
}
class FSceneView;

UCLASS()
class ITWINRUNTIME_API AITwinKeyframePath : public AActor
{
	GENERATED_BODY()
public:
	AITwinKeyframePath();

	void Tick(float DeltaTime);
	void SetKeyframeAnimator(std::shared_ptr<AdvViz::SDK::IKeyframeAnimator> &KeyframeAnimator);

	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	bool CameraFreeze = false;
	
	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	bool FreezeTime = false;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	bool DisplayBBox = false;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	bool DisplayInfo = false;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	float LoopTime = FLT_MAX;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FSceneView* GetSceneView();

	struct FImpl;
	TPimplPtr<FImpl> Impl;
};
