/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPathAnimTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Containers/Array.h>
#include <Containers/Map.h>
#include <GameFramework/Actor.h>
#include <Misc/EnumRange.h>
#include <Misc/Optional.h>
#include <Templates/PimplPtr.h>
#include <Spline/ITwinSplineHelper.h>
#include <PathAnimation/ITwinAnimPathHelper.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinPathAnimTool.generated.h"

class AITwinInteractiveTool;
class AITwinPopulation;
class AITwinSplineHelper;
class AITwinAnimPathHelper;
class AITwinAVConnector;

namespace AdvViz::SDK
{
	class RefID;
}

struct FFeatureEventProperties;

USTRUCT()
struct FAnimPathIdentifier 
{
	GENERATED_BODY()
public:
	EITwinAnimPathType PathType;
	int32 PathIndex;

	FAnimPathIdentifier() : PathType(EITwinAnimPathType::Count), PathIndex(INDEX_NONE)
	{}

	FAnimPathIdentifier(EITwinAnimPathType InPathType, int32 InPathIndex)
		: PathType(InPathType), PathIndex(InPathIndex)
	{}

	bool operator==(const FAnimPathIdentifier& Other) const
	{
		return PathType == Other.PathType && PathIndex == Other.PathIndex;
	}

	bool IsValid(std::optional<int32> ArraySize = {}) const
	{
		return PathType != EITwinAnimPathType::Count && PathIndex != INDEX_NONE 
			&& (!ArraySize || PathIndex >= 0 && PathIndex < ArraySize.value());
	}
};



// Class managing path animation creation
UCLASS()
class ITWINRUNTIME_API AITwinPathAnimTool : public AActor
{
	GENERATED_BODY()

public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAnimPathListModifiedEvent);
	UPROPERTY()
	FAnimPathListModifiedEvent AnimPathListModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSelectedAnimPathModifiedEvent);
	UPROPERTY()
	FSelectedAnimPathModifiedEvent SelectedAnimPathModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAnimPathAddedEvent, FAnimPathIdentifier, PathHandle);
	UPROPERTY()
	FAnimPathAddedEvent AnimPathAddedEvent;

	//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveStartedEvent);
	//UPROPERTY()
	//FRemoveStartedEvent RemoveStartedEvent;

	//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveCompletedEvent);
	//UPROPERTY()
	//FRemoveCompletedEvent RemoveCompletedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAnimPathRemovedEvent, FAnimPathIdentifier, PathHandle, bool, bTriggeredFromITS);
	UPROPERTY()
	FAnimPathRemovedEvent AnimPathRemovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAnimPathSelectedEvent, FAnimPathIdentifier, PathHandle);
	UPROPERTY()
	FAnimPathSelectedEvent AnimPathSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplinePointSelectedEvent);
	UPROPERTY()
	FSplinePointSelectedEvent SplinePointSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSplinePointMovedEvent, bool, bMovedInITS);
	UPROPERTY()
	FSplinePointMovedEvent SplinePointMovedEvent;

	// Structured tracking of path animation modifications (mirrors ClippingModified/PopulationChanged),
	// used for spline point add/remove analytics when there is no specific instance to report.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPathAnimChangedEvent, const FFeatureEventProperties&, Properties);
	UPROPERTY()
	FPathAnimChangedEvent PathAnimChangedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationEvent, bool, bActivated);
	UPROPERTY()
	FActivationEvent ActivationEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteractiveCreationAbortedEvent, bool, bTriggeredFromITS);
	UPROPERTY()
	FInteractiveCreationAbortedEvent InteractiveCreationAbortedEvent;

	AITwinPathAnimTool();

	virtual void Tick(float DeltaTime) override;

	// Connect the Population Tool to manage objects to animate
	void ConnectPopulationTool(class AITwinPopulationTool* PopulationTool);

	// Connect the Spline Tool to manage path animation splines
	void ConnectSplineTool(class AITwinSplineTool* SplineTool);

	// Set the Decoration Helper to manage instance groups
	void SetDecorationHelper(class AITwinDecorationHelper* InDecoHelper);

	// Set the Path Animation Manager to manage the creation and update of path animations on the server
	void SetPathAnimManager(const std::shared_ptr<AdvViz::SDK::IPathAnimManager>& InPathAnimManager);

	void SetArticulatedVehiclesConnector(AITwinAVConnector* AVConnector);

	// Manage synchronization of animation playback with the camera timeline
	DECLARE_DELEGATE_RetVal(TOptional<float>, FGetTimelineFixedTime);
	FGetTimelineFixedTime GetTimelineFixedTime;

	// Load existing animation paths from the server for the current scene
	void LoadAnimationPaths();

	// Returns whether the tool is currently loading animation paths from the server
	// (as opposed to interactive creation). Used to avoid tracking analytics events for
	// paths restored while loading a scene.
	bool IsLoadingAnimationPaths() const;

	// Initiate the interactive creation of a new animation path
	bool StartInteractiveCreation(EITwinAnimPathType PathType);

	// Abort current animation path creation, if any
	void AbortInteractiveCreation(bool bTriggeredFromITS);

	// Resume/pause the interactive creation of the existing (selected) animation path
	void ToggleSplineToolForSelectedPath();

	// Deactivate the path drawing tool and abort animation path creation, if any
	void Deactivate();

	// Return the total number of animation paths of given type
	int32 NumPaths(EITwinAnimPathType PathType) const;

	// Return the total number of animation paths of given type
	int32 NumPaths() const;

	// Remove animation path
	bool RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS);

	// Select the given animation path
	// \param bEnterIsolationMode When true, we enter isolation mode, by hiding all the other path proxies.
	// \return True if the path could be selected.
	bool SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode = true);
	// Returns selected path handle, if any
	std::optional<FAnimPathIdentifier> GetSelectedPath() const;
	// Reset current selection to none.
	// \param bExitIsolationMode When true, and if there was currently an isolation mode, we exit it by
	// restoring the normal visibility of path proxies.
	void DeSelectAll(bool bExitIsolationMode = true);

	// Zoom in on the given animation path
	void ZoomOnPath(FAnimPathIdentifier PathHandle);

	void ResetAnimation(FAnimPathIdentifier PathHandle);
	void PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay);

	// Called when we activate/deactivate picking of animation paths in the viewport
	void OnActivatePicking(bool bActivate);
	// Try to select an animation path from a mouse click event
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	// Change the view camera so that animation paths can be edited from top
	// If SpecificSpline is provided, only the corresponding path will be framed
	UFUNCTION(Category = "iTwinUX", BlueprintCallable)
	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	// For communication with iTwin Studio
	// Returns the unique identifier of an animation path from its index
	AdvViz::SDK::RefID GetPathRefId(FAnimPathIdentifier PathHandle) const;
	// Returns the index of a given animation path from its unique identifier
	FAnimPathIdentifier GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const;

	// Getters and setters for animation path properties

	//FString GetName(FAnimPathIdentifier PathHandle) const;
	//void SetName(FAnimPathIdentifier PathHandle, const FString& Name);

	void Get3DObjects(FAnimPathIdentifier PathHandle, TArray<FString>& Assets) const;
	void Set3DObjects(FAnimPathIdentifier PathHandle, const TArray<FString>& Assets);

	bool IsVisible(FAnimPathIdentifier PathHandle) const;
	void SetVisible(FAnimPathIdentifier PathHandle, bool isVisible);
	void SetAllVisible(bool isVisible);

	bool HasInvDirection(FAnimPathIdentifier PathHandle) const;
	void SetInvDirection(FAnimPathIdentifier PathHandle, bool bInvDirection);
	void SetInvDirectionAll(EITwinAnimPathType PathType, bool bInvDirection);

	bool IsLoop(FAnimPathIdentifier PathHandle) const;
	void SetIsLoop(FAnimPathIdentifier PathHandle, bool isLoop);

	float GetSpeed(FAnimPathIdentifier PathHandle) const;
	void SetSpeed(FAnimPathIdentifier PathHandle, float Speed);

	float GetDelay(FAnimPathIdentifier PathHandle) const;
	void SetDelay(FAnimPathIdentifier PathHandle, float Delay);

	EITwinAnimPathRepeatMode GetRepeatMode(FAnimPathIdentifier PathHandle) const;
	void SetRepeatMode(FAnimPathIdentifier PathHandle, EITwinAnimPathRepeatMode RepeatMode);

	bool IsOneWay(FAnimPathIdentifier PathHandle) const;
	void SetOneWay(FAnimPathIdentifier PathHandle, bool bOneWay);

	int GetLaneCount(FAnimPathIdentifier PathHandle) const;
	void SetLaneCount(FAnimPathIdentifier PathHandle, int LaneCount);

	float GetLaneWidth(FAnimPathIdentifier PathHandle) const;
	void SetLaneWidth(FAnimPathIdentifier PathHandle, float LaneWidth);

	float GetDensity(FAnimPathIdentifier PathHandle) const;
	void SetDensity(FAnimPathIdentifier PathHandle, float Density);

	float GetSeparatorWidth(FAnimPathIdentifier PathHandle) const;
	void SetSeparatorWidth(FAnimPathIdentifier PathHandle, float SeparatorWidth);

	float GetMinSpeed(FAnimPathIdentifier PathHandle) const;
	void SetMinSpeed(FAnimPathIdentifier PathHandle, float MinSpeed);

	float GetMaxSpeed(FAnimPathIdentifier PathHandle) const;
	void SetMaxSpeed(FAnimPathIdentifier PathHandle, float MaxSpeed);

	float GetTightness(FAnimPathIdentifier PathHandle, int32 PointIndex) const;
	void SetTightness(FAnimPathIdentifier PathHandle, int32 PointIndex, float Tightness);

	float GetPathLength(FAnimPathIdentifier PathHandle) const;
	int32 GetInstancesNum(FAnimPathIdentifier PathHandle) const;

	int32 GetSelectedSplinePoint(FAnimPathIdentifier PathHandle, bool& CanEditTangents) const;
	bool IsSplineToolActive(FAnimPathIdentifier PathHandle) const;

	UFUNCTION()
	void OnSplineHelperAdded(AITwinSplineHelper* NewSpline);

	UFUNCTION()
	void OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);

	UFUNCTION()
	void OnItemCreationAbortedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS);

	UFUNCTION()
	void OnSplineEditedInTool();

	UFUNCTION()
	void OnSplinePointMovedInTool(bool bTriggeredFromITS);

	UFUNCTION()
	void OnSplineMoveStart();

	UFUNCTION()
	void OnSplinePointMoveStart();

	UFUNCTION()
	void OnSplinePointAddedInTool();

	UFUNCTION()
	void OnSplinePointRemovedInTool();

	UFUNCTION()
	void OnPathAnimPolygonSelected();

protected:
	//virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BroadcastSelection();
	void TriggerRebake(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly);
	void TriggerRepopulate(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly);

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
