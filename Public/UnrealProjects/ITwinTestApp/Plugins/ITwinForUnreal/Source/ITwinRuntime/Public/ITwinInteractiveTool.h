/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinInteractiveTool.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Engine/HitResult.h>
#include <GameFramework/Actor.h>

#include "ITwinInteractiveTool.generated.h"

/// Base class for interactive tools such as ITwin Population Tool or Spline Tool.

UCLASS()
class ITWINRUNTIME_API AITwinInteractiveTool : public AActor
{
	GENERATED_BODY()

public:
	AITwinInteractiveTool();

	/// Enable/Disable the tool.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetEnabled(bool bValue);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsEnabled() const;

	/// Function handling the click action (LMB) for the tool. Returns whether a significant action was done.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool DoMouseClickAction();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelection() const;

	/// Returns the transformation of the selected element, if any.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FTransform GetSelectionTransform() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectionTransform(const FTransform& Transform);

	// Function deleting the selection, if any.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelection();

	// Reset the tool to its default state.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ResetToDefault();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationTool() const;

	void SetCustomPickingExtentInMeters(float PickingExtent);
	float GetCustomPickingExtentInMeters() const;

	FHitResult DoPickingAtMousePosition() const;

protected:
	virtual void SetEnabledImpl(bool bValue) PURE_VIRTUAL(AITwinInteractiveTool::SetEnabledImpl);
	virtual bool IsEnabledImpl() const PURE_VIRTUAL(AITwinInteractiveTool::IsEnabledImpl, return false; );

	virtual bool DoMouseClickActionImpl() PURE_VIRTUAL(AITwinInteractiveTool::DoMouseClickActionImpl, return false; );

	virtual bool HasSelectionImpl() const PURE_VIRTUAL(AITwinInteractiveTool::HasSelectionImpl, return false; );
	virtual FTransform GetSelectionTransformImpl() const PURE_VIRTUAL(AITwinInteractiveTool::GetSelectionTransformImpl, return {}; );
	virtual void SetSelectionTransformImpl(const FTransform& Transform) PURE_VIRTUAL(AITwinInteractiveTool::SetSelectionTransformImpl);

	virtual void DeleteSelectionImpl() PURE_VIRTUAL(AITwinInteractiveTool::DeleteSelectionImpl);

	virtual void ResetToDefaultImpl() PURE_VIRTUAL(AITwinInteractiveTool::ResetToDefaultImpl);

	virtual bool IsPopulationToolImpl() const { return false; }

private:
	TOptional<float> CustomPickingExtentInMeters = {};
};
