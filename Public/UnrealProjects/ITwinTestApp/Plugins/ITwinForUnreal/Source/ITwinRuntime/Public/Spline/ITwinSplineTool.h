/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineTool.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Templates/PimplPtr.h>
#include <GameFramework/Actor.h>
#include <Containers/UnrealString.h>
#include <Math/MathFwd.h>

#include "ITwinSplineTool.generated.h"

class AITwinSplineHelper;

UCLASS()
class ITWINRUNTIME_API AITwinSplineTool : public AActor
{
	GENERATED_BODY()

public:
	AITwinSplineTool();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	AITwinSplineHelper* GetSelectedSpline() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedSpline(AITwinSplineHelper* population);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedPointIndex(int32 pointIndex);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelection() const;

	// Function deleting the selection (either a single point or the whole spline)
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelection();

	// Functions to add/remove spline points
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelectedPoint();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DuplicateSelectedPoint();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void EnableDuplicationWhenMovingPoint(bool value);

	// Functions accessing the transformation of the selected spline
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FTransform GetSelectionTransform() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectionTransform(const FTransform& transform);

	// Enable/Disable the spline tool
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetEnabled(bool value);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsEnabled() const;

	// Reset the tool to its default state.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ResetToDefault();

	// Function handling the click action (LMB) for the spline tool
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DoMouseClickAction();

	// Function adding a cartographic polygon
	void AddCartographicPolygon(const FVector& position);

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};