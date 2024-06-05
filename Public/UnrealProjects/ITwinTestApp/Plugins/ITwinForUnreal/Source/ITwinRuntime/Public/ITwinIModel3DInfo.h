/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel3DInfo.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
	UPROPERTY(BlueprintReadOnly)
		FVector BoundingBoxMin = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly)
		FVector BoundingBoxMax = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly)
		FVector ModelCenter = FVector(0, 0, 0);
};
