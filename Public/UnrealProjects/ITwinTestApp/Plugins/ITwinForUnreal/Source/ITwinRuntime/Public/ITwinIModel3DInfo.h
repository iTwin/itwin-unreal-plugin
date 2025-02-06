/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel3DInfo.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

#include "ITwinIModel3DInfo.generated.h"

USTRUCT(BlueprintType)
struct FITwinIModel3DInfo
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "iTwin")
		FVector BoundingBoxMin = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "iTwin")
		FVector BoundingBoxMax = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "iTwin")
		FVector ModelCenter = FVector(0, 0, 0);
};
