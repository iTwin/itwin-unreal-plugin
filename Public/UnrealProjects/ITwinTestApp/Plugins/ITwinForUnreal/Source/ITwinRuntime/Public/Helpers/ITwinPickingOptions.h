/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingOptions.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

	//! Whether the selection of the iTwin MaterialID, if any, should be broadcast.
	UPROPERTY()
	bool bBroadcastMaterialSelection = true;

	//! Indicates whether the picked iTwin MaterialID, if any, should be highlighted.
	//! In such case, the picked iTwin ElementID will not be highlighted.
	UPROPERTY()
	bool bHighlightSelectedMaterial = false;

	//! Custom trace extent, in meters. Only positive values will be considered.
	UPROPERTY()
	float CustomTraceExtentInMeters = -1.f;

	//! Custom mouse position to use for picking.
	const FVector2D* CustomMousePosition = nullptr;
};
