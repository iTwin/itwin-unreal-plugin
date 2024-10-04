/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingOptions.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "ITwinPickingOptions.generated.h"

USTRUCT(BlueprintType, Category = "iTwin")
struct ITWINRUNTIME_API FITwinPickingOptions
{
	GENERATED_BODY()
public:
	//! Indicates whether the picked iTwin ElementID, if any, should be selected.
	UPROPERTY()
	bool bSelectElement = true;

	//! Indicates whether the picked iTwin MaterialID, if any, should be selected.
	UPROPERTY()
	bool bSelectMaterial = true;
};
