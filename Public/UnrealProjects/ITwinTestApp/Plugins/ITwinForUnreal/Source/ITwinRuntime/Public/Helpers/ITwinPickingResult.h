/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingResult.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Engine/HitResult.h>

class AITwinIModel;

struct ITWINRUNTIME_API FITwinPickingResult
{
	FString ElementId;
	FVector2D MousePosition = FVector2D::ZeroVector;
	FVector TraceStart = FVector::ZeroVector;
	FVector TraceEnd = FVector::ZeroVector;
	FHitResult HitResult;
	TOptional<uint64> MaterialId;
	AITwinIModel* PickedMaterialIModel = nullptr; // IModel owning the picked material, if any.
};
