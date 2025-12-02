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

struct FITwinPickingResult;


/// Base class for interactive tools such as ITwin Population / Spline / Annotation Tool.

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

	class ITWINRUNTIME_API IActiveStateRecord
	{
	public:
		virtual ~IActiveStateRecord();
	};
	virtual TUniquePtr<IActiveStateRecord> MakeStateRecord() const;
	virtual bool RestoreState(IActiveStateRecord const& State);

	/// Enable the tool, while deactivating the others if needed.
	bool MakeActiveTool(IActiveStateRecord const& State);

	/// Disable all existing tools.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	static void DisableAll(UWorld* World);

	/// Initiates the interactive creation of a new item (its position following the mouse cursor).
	/// \return True if a new item could be created and is ready to be positioned.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool StartInteractiveCreation();

	/// Returns true if the tool is currently creating a new item interactively (its position following the mouse cursor).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsInteractiveCreationMode() const;

	/// Function handling the click action (LMB) for the tool. Returns whether a significant action was done.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool DoMouseClickAction();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelection() const;

	// Record/restore selection (for undo/redo management).
	class ITWINRUNTIME_API ISelectionRecord
	{
	public:
		virtual ~ISelectionRecord();
	};
	virtual TUniquePtr<ISelectionRecord> MakeSelectionRecord() const PURE_VIRTUAL(AITwinInteractiveTool::MakeSelectionRecord, return {}; );
	virtual bool HasSameSelection(ISelectionRecord const& Selection) const PURE_VIRTUAL(AITwinInteractiveTool::HasSameSelection, return false; );
	virtual bool RestoreSelection(ISelectionRecord const& Selection) PURE_VIRTUAL(AITwinInteractiveTool::RestoreSelection, return false; );

	/// Returns the transformation of the selected element, if any.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FTransform GetSelectionTransform() const;

	/// Called before the selection is modified (ie. when one clicks the mouse button on the interactive gizmo).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void OnSelectionTransformStarted();

	/// Called at the end of an interactive modification (typically when one releases the mouse button).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void OnSelectionTransformCompleted();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectionTransform(const FTransform& Transform);

	// Record/restore full copy of selected item (for undo/redo management).
	class ITWINRUNTIME_API IItemBackup
	{
	public:
		virtual ~IItemBackup();
		virtual FString GetGenericName() const = 0;
	};
	virtual TUniquePtr<IItemBackup> MakeSelectedItemBackup() const PURE_VIRTUAL(AITwinInteractiveTool::MakeSelectedItemBackup, return {}; );
	virtual bool RestoreItem(IItemBackup const& ItemBackup) PURE_VIRTUAL(AITwinInteractiveTool::RestoreItem, return false; );

	// Function deleting the selection, if any.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelection();

	// Reset the tool to its default state.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ResetToDefault();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationTool() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsCompatibleWithGizmo() const;

	void SetCustomPickingExtentInMeters(float PickingExtent);
	float GetCustomPickingExtentInMeters() const;

	FHitResult DoPickingAtMousePosition(FITwinPickingResult* OutPickingResult = nullptr,
		TArray<const AActor*>&& IgnoredActors = {},
		TArray<UPrimitiveComponent*>&& IgnoredComponents = {}) const;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FInteractiveCreationCompletedEvent);
	UPROPERTY()
	FInteractiveCreationCompletedEvent InteractiveCreationCompletedEvent;

protected:
	virtual void SetEnabledImpl(bool bValue) PURE_VIRTUAL(AITwinInteractiveTool::SetEnabledImpl);
	virtual bool IsEnabledImpl() const PURE_VIRTUAL(AITwinInteractiveTool::IsEnabledImpl, return false; );

	virtual bool DoMouseClickActionImpl() PURE_VIRTUAL(AITwinInteractiveTool::DoMouseClickActionImpl, return false; );

	virtual bool HasSelectionImpl() const PURE_VIRTUAL(AITwinInteractiveTool::HasSelectionImpl, return false; );
	virtual FTransform GetSelectionTransformImpl() const PURE_VIRTUAL(AITwinInteractiveTool::GetSelectionTransformImpl, return {}; );

	virtual void OnSelectionTransformStartedImpl() {}
	virtual void OnSelectionTransformCompletedImpl() {}
	virtual void SetSelectionTransformImpl(const FTransform& Transform) PURE_VIRTUAL(AITwinInteractiveTool::SetSelectionTransformImpl);

	virtual void DeleteSelectionImpl() PURE_VIRTUAL(AITwinInteractiveTool::DeleteSelectionImpl);

	virtual void ResetToDefaultImpl() PURE_VIRTUAL(AITwinInteractiveTool::ResetToDefaultImpl);

	virtual bool IsPopulationToolImpl() const { return false; }

	virtual bool IsCompatibleWithGizmoImpl() const { return true; }

	virtual bool StartInteractiveCreationImpl() { return false; }
	virtual bool IsInteractiveCreationModeImpl() const { return false; }

private:
	TOptional<float> CustomPickingExtentInMeters = {};
};
