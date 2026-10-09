/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Population/ITwinPopulationToolEnum.h>
#include <Population/ITwinPopulationHelper.h>
#include <Templates/PimplPtr.h>
#include <ITwinInteractiveTool.h>
#include <Math/MathFwd.h>
#include <Misc/EnumRange.h>
#include <Engine/HitResult.h>
#include <Containers/Array.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>

#include "ITwinPopulationTool.generated.h"

class UObject;
class USplineComponent;
class AITwinPopulation;
class AITwinDecorationHelper;
class AITwinSplineHelper;

namespace AdvViz::SDK
{
	class IPopulationManager;
}

struct FFeatureEventProperties;

USTRUCT()
struct FPopulationIdentifier
{
	GENERATED_BODY()
public:
	EITwinPopulationType PopulationType;
	int32 PopulationIndex;

	FPopulationIdentifier() : PopulationType(EITwinPopulationType::Count), PopulationIndex(INDEX_NONE)
	{
	}

	FPopulationIdentifier(EITwinPopulationType InPopulationType, int32 InPopulationIndex)
		: PopulationType(InPopulationType), PopulationIndex(InPopulationIndex)
	{
	}

	bool operator==(const FPopulationIdentifier& Other) const
	{
		return PopulationType == Other.PopulationType && PopulationIndex == Other.PopulationIndex;
	}

	bool IsValid(std::optional<int32> ArraySize = {}) const
	{
		return PopulationType != EITwinPopulationType::Count && PopulationIndex != INDEX_NONE
			&& (!ArraySize || PopulationIndex >= 0 && PopulationIndex < ArraySize.value());
	}
};

class FUESplineCurve : public BeUtils::SplineCurve
{
public:
	FUESplineCurve(USplineComponent const& InSpline);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
};

/// Can be used to customize the effect of the gizmo on the selected instance.
class IITwinPopulationInstanceTransformProxy
{
public:
	virtual ~IITwinPopulationInstanceTransformProxy() = default;
	virtual FTransform GetTransform() const = 0;
	virtual void OnTransformModificationStarted(ETransformationMode) = 0;
	virtual void SetTransform(const FTransform& Transform) = 0;
};

using IITwinPopulationInstanceTransformProxyPtr = TSharedPtr<IITwinPopulationInstanceTransformProxy>;

UCLASS()
class ITWINRUNTIME_API AITwinPopulationTool : public AITwinInteractiveTool
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPopulationChangedEvent, const FFeatureEventProperties&, Properties);
	UPROPERTY()
	FPopulationChangedEvent PopulationChangedEvent;
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSelectionChangedEvent);
	UPROPERTY()
	FSelectionChangedEvent SelectionChangedEvent;
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FModeChangedEvent);
	UPROPERTY()
	FModeChangedEvent ModeChangedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPopulationSelectedEvent, FPopulationIdentifier, PopulationHandle);
	UPROPERTY()
	FPopulationSelectedEvent PopulationSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNonSplinePopulationSelectedEvent);
	UPROPERTY()
	FNonSplinePopulationSelectedEvent NonSplinePopulationSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPopulationListModifiedEvent);
	UPROPERTY()
	FPopulationListModifiedEvent PopulationListModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSelectedPopulationModifiedEvent);
	UPROPERTY()
	FSelectedPopulationModifiedEvent SelectedPopulationModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPopulationRemovedEvent, FPopulationIdentifier, PopulationHandle, bool, bTriggeredFromITS);
	UPROPERTY()
	FPopulationRemovedEvent PopulationRemovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPopulationAddedEvent, FPopulationIdentifier, PopulationHandle);
	UPROPERTY()
	FPopulationAddedEvent PopulationAddedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationEvent, bool, bActivated);
	UPROPERTY()
	FActivationEvent ActivationEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplinePointSelectedEvent);
	UPROPERTY()
	FSplinePointSelectedEvent SplinePointSelectedEvent;

	AITwinPopulationTool();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	EPopulationToolMode GetMode() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetMode(EPopulationToolMode mode);

	// Abort current population creation, if any
	void AbortInteractiveCreation(bool bTriggeredFromITS);

	bool IsVisible(FPopulationIdentifier PopHandle) const;
	void SetVisible(FPopulationIdentifier PopHandle, bool isVisible, bool bForceHiddenSpline);
	void SetAllVisible(bool isVisible, bool bForceHiddenSpline);

	void SetIsSpacingRandomized(FPopulationIdentifier PopHandle, bool isSpacingRandomized);
	bool IsSpacingRandomized(FPopulationIdentifier PopHandle) const;

	void SetIsScaleRandomized(FPopulationIdentifier PopHandle, bool isScaleRandomized);
	bool IsScaleRandomized(FPopulationIdentifier PopHandle) const;

	void SetIsRotationRandomized(FPopulationIdentifier PopHandle, bool isRotationRandomized);
	bool IsRotationRandomized(FPopulationIdentifier PopHandle) const;

	bool IsAvoidOverlapping(FPopulationIdentifier PopHandle) const;
	void SetAvoidOverlapping(FPopulationIdentifier PopHandle, bool isAvoidOverlapping);

	// Deactivate the spline drawing tool and abort population creation, if any
	void Deactivate();

	// Resume/pause the interactive creation of the existing (selected) path population
	void ToggleSplineToolForSelectedPath();

	std::optional<FPopulationIdentifier> GetSelectedSplinePopulation() const;
	// Select the given spline population
	// \param bEnterIsolationMode When true, we enter isolation mode, by hiding all the other spline population proxies.
	// \return True if the spline population could be selected.
	bool SelectSplinePopulation(FPopulationIdentifier PopHandle, bool bEnterIsolationMode = true);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	ETransformationMode GetTransformationMode() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetTransformationMode(ETransformationMode mode);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	AITwinPopulation* GetSelectedPopulation() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	int32 GetSelectedInstanceIndex() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedPopulation(AITwinPopulation* population);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedInstanceIndex(int32 instanceIndex);
	
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelectedPopulation() const;
	
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelectedInstance() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelectedInstance();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationModeActivated() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsBrushModeActivated() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void StartBrushingInstances();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void EndBrushingInstances();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ShowBrushSphere();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void HideBrushSphere();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	float GetBrushFlow() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetBrushFlow(float flow);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	float GetBrushSize() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetBrushSize(float size);

	float GetDensity(FPopulationIdentifier PopHandle) const;
	void SetDensity(FPopulationIdentifier PopHandle, float Density);

	float GetScale(FPopulationIdentifier PopHandle) const;
	void SetScale(FPopulationIdentifier PopHandle, float Scale);

	float GetRotation(FPopulationIdentifier PopHandle) const;
	void SetRotation(FPopulationIdentifier PopHandle, float Rotation);

	float GetGridRotation(FPopulationIdentifier PopHandle) const;
	void SetGridRotation(FPopulationIdentifier PopHandle, float Rotation);

	EITwinSplinePopulationMode GetSplinePopulationMode(FPopulationIdentifier PopHandle) const;
	void SetSplinePopulationMode(FPopulationIdentifier PopHandle, EITwinSplinePopulationMode Mode);

	EITwinPathPopulationRotationMode GetPathPopulationRotationMode(FPopulationIdentifier PopHandle) const;
	void SetPathPopulationRotationMode(FPopulationIdentifier PopHandle, EITwinPathPopulationRotationMode RotationMode);

	float GetDistance(FPopulationIdentifier PopHandle) const;
	void SetDistance(FPopulationIdentifier PopHandle, float Distance);

	FFloatRange GetRangeDistance(FPopulationIdentifier PopHandle) const;
	void SetRangeDistance(FPopulationIdentifier PopHandle, FFloatRange Range);

	FFloatRange GetRangeScale(FPopulationIdentifier PopHandle) const;
	void SetRangeScale(FPopulationIdentifier PopHandle, FFloatRange Range);

	FFloatRange GetRangeRotation(FPopulationIdentifier PopHandle) const;
	void SetRangeRotation(FPopulationIdentifier PopHandle, FFloatRange Range);

	// Tangent tightness (in [0,1]) of the given point of a population spline.
	float GetTightness(FPopulationIdentifier PopHandle, int32 PointIndex) const;
	void SetTightness(FPopulationIdentifier PopHandle, int32 PointIndex, float Tightness);
	// Returns the selected point of the population spline, or INDEX_NONE.
	// CanEditTangents is set to false for the end points of an open spline.
	int32 GetSelectedSplinePoint(FPopulationIdentifier PopHandle, bool& CanEditTangents) const;

	// Functions accessing the color variation of the selected instance
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FLinearColor GetSelectionColorVariation() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectionColorVariation(const FLinearColor& color);

	void SetDecorationHelper(AITwinDecorationHelper* decoHelper);

	// Set the Population Manager to manage population metadata persistence
	void SetPopulationManager(const std::shared_ptr<AdvViz::SDK::IPopulationManager>& InPopulationManager);

	// Load existing population metadata from the server for the current scene
	void LoadPopulations();

	// Returns whether the tool is currently loading population metadata from the server
	// (as opposed to interactive creation). Used to avoid tracking analytics events for
	// populations restored while loading a scene.
	bool IsLoadingPopulations() const;

	bool DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath);
	void ReleaseDraggedAssetInstance();
	void DestroyDraggedAssetInstance();

	void SetUsedAsset(const FString& assetPath, bool used);
	void ClearUsedAssets();
	void ReplaceUsedAssets(const TArray<FString>& AssetPaths);

	//! Sets the Gizmo actor class (to be ignored in zone/path population).
	void SetGizmoActorClass(TSubclassOf<AActor> ActorClass);

	/// Pre-load the given asset in a population.
	AITwinPopulation* PreLoadPopulation(const FString& AssetPath);

	/// Switch the tool usage to cutout mode on or off.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetUsedOnCutout(bool bForCutout);

	// Called when we activate/deactivate picking of spline populations in the viewport
	void OnActivatePicking(bool bActivate);
	// Try to select a population (splines or otherwise like brushes or single placement objects) from a mouse click event
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	// Return the total number of spline populations
	int32 NumPopulations() const;

	void SetInstanceTransformProxy(IITwinPopulationInstanceTransformProxyPtr InTransformProxy);

	/// Returns whether some instances can be added - ie. there is one (or more) selected assets.
	/// \param bOutAllowBrush Will be set to true if the paint brush is compatible with the selection.
	bool IsAdditionOfInstancesAllowed(bool* bOutAllowBrush = nullptr) const;

	int32 GetInstanceCount(const FString& assetPath) const;

	bool GetForcePerpendicularToSurface() const;
	void SetForcePerpendicularToSurface(bool b);

	bool GetIsEditingBrushSize() const;
	void SetIsEditingBrushSize(bool b);

	// For communication with iTwin Studio
	// Returns the unique identifier of a spline population from its index
	AdvViz::SDK::RefID GetPopulationRefId(FPopulationIdentifier PopulationHandle) const;
	// Returns the index of a given spline population from its unique identifier
	FPopulationIdentifier GetPopulationIdentifier(AdvViz::SDK::RefID const& RefID) const;
	AdvViz::SDK::RefID GetPopulationId(EITwinPopulationType PopulationType, int32 PopulationIndex) const;

	/// Sets the spline controlling the population.
	UFUNCTION()
	void SetSelectedSpline(AITwinSplineHelper* Spline);

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
	void OnPopulationPolygonSelected();

	// Reset current selection to none.
	// \param bExitIsolationMode When true, and if there was currently an isolation mode, we exit it by
	// restoring the normal visibility of population proxies.
	void DeSelectAll(bool bExitIsolationMode = true);
	/// Requests the (asynchronous) population of the given spline. The spline is sampled on a worker
	/// thread, and instances are created on the game thread once the sampling is done (see Tick).
	/// \param bFinalEdit True (default) when the spline is in a stable state: any computation in progress
	///        for this spline is discarded and a new one is started at once from the current state.
	///        Pass false during interactive edition: the current computation is kept running, and a new
	///        one is scheduled right after it (all intermediate requests are coalesced).
	void PopulateSpline(AITwinSplineHelper const& TargetSpline, bool bFinalEdit = true);

	/// Returns whether a population computation is running or pending for the given spline.
	bool IsPopulatingSpline(AITwinSplineHelper const& TargetSpline) const;

	// Connect the Spline Tool to manage population splines
	void ConnectSplineTool(class AITwinSplineTool* SplineTool);

	/// Return the number of spline populations for the given type.
	int32 NumPopulations(EITwinPopulationType Type) const;

	// Change the view camera so that spline populations can be edited from top
	// If SpecificSpline is provided, only the corresponding spline population will be framed
	UFUNCTION()
	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	void Get3DObjects(FPopulationIdentifier PopHandle, TArray<FString>& Assets) const;
	void Set3DObjects(FPopulationIdentifier PopHandle, const TArray<FString>& Assets);

	// Remove population
	bool RemovePopulation(FPopulationIdentifier PopHandle, bool bTriggeredFromITS);

	// Zoom in on the given spline population
	void ZoomOnPopulation(FPopulationIdentifier PopHandle);

	UFUNCTION()
	void OnSplineHelperAdded(AITwinSplineHelper* NewSpline);

	UFUNCTION()
	void OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);

	class [[nodiscard]] FPickingContext
	{
	public:
		FPickingContext(AITwinPopulationTool& InTool, bool bRestrictPickingOnClipping);
		~FPickingContext();
	private:
		AITwinPopulationTool& Tool;
		const bool bRestrictPickingOnClipping_Old;
	};
	bool GetRestrictPickingOnClippingPrimitives() const;
	void RestrictPickingOnClippingPrimitives(bool bRestrictPickingOnClipping = true);

	/// Overridden from AITwinInteractiveTool
	virtual TUniquePtr<IActiveStateRecord> MakeStateRecord() const override;
	virtual bool RestoreState(IActiveStateRecord const& State) override;
	virtual TUniquePtr<ISelectionRecord> MakeSelectionRecord() const override;
	virtual bool HasSameSelection(ISelectionRecord const& Selection) const override;
	virtual bool RestoreSelection(ISelectionRecord const& Selection) override;
	virtual TUniquePtr<IItemBackup> MakeSelectedItemBackup() const override;
	virtual bool RestoreItem(IItemBackup const& ItemBackup) override;

	/// Undo/Redo of brushing.
	class ITWINRUNTIME_API IBrushUndoEntry
	{
	public:
		virtual ~IBrushUndoEntry();
		virtual void Undo(AITwinPopulationTool& Tool) = 0;
		virtual void Redo(AITwinPopulationTool& Tool) = 0;
		virtual FString GetDescription() const = 0;
	};
	TUniquePtr<IBrushUndoEntry> MakeBrushUndoEntry();

	virtual TSharedPtr<FToolDisabler> MakeToolDisabler() override;

	bool IsSplineToolActive(FPopulationIdentifier PathHandle) const;

protected:
	/// Overridden from AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	/// Overridden from AITwinInteractiveTool
	virtual bool IsPopulationToolImpl() const override { return true; }
	virtual void SetUsedOnCutoutPrimitiveImpl(bool bForCutout) override;
	virtual bool IsUsedOnCutoutPrimitiveImpl() const override;
	virtual void SetEnabledImpl(bool bValue) override;
	virtual bool IsEnabledImpl() const override;
	virtual bool DoMouseClickActionImpl() override;
	virtual bool HasSelectionImpl() const override;
	virtual FTransform GetSelectionTransformImpl() const override;
	virtual void OnSelectionTransformStartedImpl() override;
	virtual void OnSelectionTransformCompletedImpl() override;
	virtual void SetSelectionTransformImpl(const FTransform& Transform) override;
	virtual void DeleteSelectionImpl() override;
	virtual void ResetToDefaultImpl() override;
	virtual bool StartInteractiveCreationImpl() override;
	virtual bool IsInteractiveCreationModeImpl() const override;
	virtual void AbortInteractiveCreationImpl(bool bTriggeredFromITS) override;
	virtual void ValidateInteractiveCreationImpl(bool bTriggeredFromITS) override;
	virtual bool ShowOnlyTranslationZGizmoImpl() const override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	UPROPERTY()
	TWeakObjectPtr<AITwinSplineHelper> SelectedSpline;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> AreaPopulation;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> PathPopulation;

	UPROPERTY()
	TSubclassOf<AActor> GizmoActorClass;

	UFUNCTION()
	void BroadcastSelection();
};

// ------------------------------------------

class ACesium3DTileset;

namespace ITwin
{
	void Gather3DMapTilesets(const UWorld* World, TArray<ACesium3DTileset*>& Out3DMapTilesets);
}