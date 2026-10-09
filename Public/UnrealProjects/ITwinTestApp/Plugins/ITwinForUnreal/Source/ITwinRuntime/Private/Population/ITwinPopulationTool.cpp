/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Population/ITwinPopulationTool.h>

#include <Decoration/ITwinDecorationHelper.h>
#include <Helpers/ITwinBrushHelper.h>
#include <Helpers/ITwinTracingHelper.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinRealityData.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinFeatureChange.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulation.inl>
#include <Population/ITwinPopulationHelper.h>
#include <Spline/ITwinSplineGeometry.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinUESplineCurve.h>
#include <Spline/ITwinSplineTool.h>

#include <Async/Async.h>
#include <Components/SplineComponent.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/EngineTypes.h>
#include <Engine/GameViewportClient.h>
#include <Engine/StaticMesh.h>
#include <Engine/StaticMeshActor.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetSystemLibrary.h>
#include <Materials/Material.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Slate/SceneViewport.h>
#include <SceneView.h>

#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#	include <SDK/Core/Visualization/PopulationPersistence.h>
#	include "SDK/Core/Tools/DelayedCall.h"
#	include "SDK/Core/Tools/Log.h"
#include <Compil/AfterNonUnrealIncludes.h>

namespace
{
	EITwinPopulationType GetPopulationTypeFromSplineUsage(EITwinSplineUsage SplineUsage)
	{
		switch (SplineUsage)
		{
		case EITwinSplineUsage::PopulationZone:
			return EITwinPopulationType::Area;
		case EITwinSplineUsage::PopulationPath:
			return EITwinPopulationType::Path;
		default:
			ensureMsgf(false, TEXT("Unknown spline usage"));
			return EITwinPopulationType::Count;
		}
	}
}

namespace ITwin
{
	inline bool Is3DMapTileset(const ACesium3DTileset* tileset)
	{
		// Detect both Google3D tilesets and iTwin reality data.
		return IsGoogle3DTileset(tileset)
			||
			(
				tileset->Owner.Get() &&
				tileset->Owner->IsA(AITwinRealityData::StaticClass())
			);
	}

	void Gather3DMapTilesets(const UWorld* World, TArray<ACesium3DTileset*>& Out3DMapTilesets)
	{
		GatherGoogle3DTilesets(World, Out3DMapTilesets);
		// Append iTwin Reality-Data tilesets.
		for (TActorIterator<AITwinRealityData> ItwRealDataIter(World); ItwRealDataIter; ++ItwRealDataIter)
		{
			if ((*ItwRealDataIter)->GetTileset())
			{
				Out3DMapTilesets.Push((*ItwRealDataIter)->GetMutableTileset());
			}
		}
	}

	void AppendSplineHelpers(TArray<const AActor*>& OutActors, const UWorld* World,
		std::optional<EITwinSplineUsage> const& RestrictToUsage = std::nullopt)
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(World); SplineIter; ++SplineIter)
		{
			if (!RestrictToUsage.has_value() || RestrictToUsage.value() == (*SplineIter)->GetUsage())
			{
				OutActors.Push(*SplineIter);
			}
		}
	}

	ITWINRUNTIME_API TOptional<FVector> GetNewObjectDefaultPosition(const UObject* WorldContextObject,
																	EITwinSplineUsage SplineUsage,
																	FHitResult& OutHitResult)
	{
		FITwinRayTraceInput TraceInput;
		if (!FITwinTracingHelper::GetRayToTraceFromScreenCenter(WorldContextObject, TraceInput))
		{
			return {};
		}
		FVector Default3DPos = TraceInput.TraceStart + (TraceInput.TraceDirection * 2 * 1e4f); // default position

		// For cutout polygon, we will create the spline slightly above the hit impact, so that the polygon
		// remains visible.
		const float DistFactor = (SplineUsage == EITwinSplineUsage::MapCutout) ? 0.90f : 0.9999f;

		// Make an intersection test to adjust the position on the tileset
		FHitResult HitResult;
		FVector TraceEnd = TraceInput.TraceStart + (TraceInput.TraceDirection * 1e8f);
		if (UKismetSystemLibrary::LineTraceSingle(
			WorldContextObject,
			TraceInput.TraceStart,
			TraceEnd,
			ETraceTypeQuery::TraceTypeQuery1,
			true,
			{} /*ActorsToIgnore*/, EDrawDebugTrace::None, HitResult, true))
		{
			if (HitResult.Distance > 0.f)
			{
				Default3DPos = TraceInput.TraceStart + (TraceInput.TraceDirection * HitResult.Distance * DistFactor);
			}
			else
			{
				Default3DPos = FVector(HitResult.Location);
			}
		}
		OutHitResult = HitResult;
		return Default3DPos;
	}

	namespace Clipping
	{
		void ConfigureNewInstance(const AdvViz::SDK::IInstancePtr& AVizInstance, AActor& HitActor);
	}
}


namespace
{

	class FPopulationBaseRecord
	{
	public:
		FPopulationBaseRecord(AITwinPopulation* InPopulation, AITwinDecorationHelper* InPopulationFactory)
			: PopulationPtr(InPopulation)
			, PopulationFactory(InPopulationFactory)
		{
			if (ensure(InPopulation))
			{
				AssetPath = UTF8_TO_TCHAR(InPopulation->GetObjectRef().c_str());
				InstancesGroupId = InPopulation->GetInstanceGroupId();
			}
		}

		AITwinPopulation* GetPopulation() const
		{
			if (!PopulationPtr.IsValid() && ensure(PopulationFactory))
			{
				PopulationPtr = PopulationFactory->GetOrCreatePopulation(AssetPath, InstancesGroupId);
				ensureMsgf(PopulationPtr.IsValid(), TEXT("unable to reload population from %s"), *AssetPath);
			}
			return PopulationPtr.Get();
		}

		AITwinDecorationHelper* const GetPopulationFactory() const { return PopulationFactory; }

	private:
		mutable TWeakObjectPtr<AITwinPopulation> PopulationPtr;
		AITwinDecorationHelper* const PopulationFactory = nullptr;
		// Populations are deleted when the instance count comes down to zero, so we need a way to reload it
		// if the pointer is invalidated.
		FString AssetPath;
		AdvViz::SDK::RefID InstancesGroupId;
	};

	// Backup of a population instance, used for undo/redo (of both single mode and brushing operations).
	class FPopulationInstanceBackup : public AITwinInteractiveTool::IItemBackup
		, public FPopulationBaseRecord
	{
	public:
		FPopulationInstanceBackup(AITwinPopulation* InPopulation,
			int32 InInstanceIndex,
			AITwinDecorationHelper* InPopulationFactory)
			: FPopulationBaseRecord(InPopulation, InPopulationFactory)
		{
			if (ensure(InPopulation))
			{
				InstanceID = InPopulation->GetInstanceRefId(InInstanceIndex);
				InstanceTransform = InPopulation->GetInstanceTransform(InInstanceIndex);
				InstanceColorVariation = InPopulation->GetInstanceColorVariation(InInstanceIndex);
				auto const AVizInstPtr = InPopulation->GetAVizInstance(InInstanceIndex);
				if (AVizInstPtr)
				{
					auto inst = AVizInstPtr->GetRAutoLock();
					AvizInstanceName = inst->GetName();
				}
			}
		}

		virtual FString GetGenericName() const override
		{
			const AITwinPopulation* Population = GetPopulation();
			if (Population)
				return Population->GetObjectTypeName();
			else
				return TEXT("object");
		}

		AdvViz::SDK::RefID InstanceID = AdvViz::SDK::RefID::Invalid();
		FTransform InstanceTransform = FTransform::Identity;
		FVector InstanceColorVariation = FVector::ZeroVector;
		std::string AvizInstanceName; // sometimes used to encode information...
	};


	struct FBrushAddedPopulationInfo
	{
		int32 FirstAddedInstanceIndex = INDEX_NONE;
#ifndef RELEASE_CONFIG
		int32 NumAddedInstances = 0;
#endif

		FBrushAddedPopulationInfo() = default;
		FBrushAddedPopulationInfo(int32 InFirstAddedInstanceIndex)
			: FirstAddedInstanceIndex(InFirstAddedInstanceIndex)
		{
		}
	};

	struct FBrushRemovedPopulationInfo
	{
		std::vector<FPopulationInstanceBackup> Backups;
	};
}

class AITwinPopulationTool::FImpl : public FITwinBrushHelper
{
public:
	AITwinPopulationTool& owner;
	AITwinDecorationHelper* decorationHelper = nullptr;
	std::shared_ptr<AdvViz::SDK::IPopulationManager> populationManager;

	TArray<TStrongObjectPtr<UITwinAreaPopulationHelper>> AreaInfos;
	TArray<TStrongObjectPtr<UITwinPathPopulationHelper>> PathInfos;

	bool enabled = false; // boolean used to switch on or off the population tool

	EPopulationToolMode toolMode = EPopulationToolMode::Select;
	ETransformationMode transformationMode = ETransformationMode::Move;

	inline const UITwinPopulationHelper* GetPopulationInfo(FPopulationIdentifier PopHandle) const;
	inline UITwinPopulationHelper* GetMutablePopulationInfo(FPopulationIdentifier PopHandle);

	void LoadPopulations();
	void ApplyRotationRangeToPopulationInstances(
		UITwinPopulationHelper* PopHelper, const FFloatRange& NewRangeDeg);
	void ApplyScaleRangeToPopulationInstances(
		UITwinPopulationHelper* PopHelper, const FFloatRange& NewRange);
	void ApplyRotationModeChange(
		UITwinPopulationHelper* PopHelper,
		EITwinPathPopulationRotationMode OldMode,
		EITwinPathPopulationRotationMode NewMode);
	// Called when loading a scene, it checks whether the path asset instances have finished loading.
	bool ArePopulationsFullyLoaded(UITwinPopulationHelper* PopHelper);

	void RemovePopulationObjects(FPopulationIdentifier PopHandle);
	void RemovePopulationObjects(UITwinPopulationHelper* PopHelper);

	AITwinPopulation const* GetSelectedPopulation(int32& OutSelectedInstanceIndex) const;

	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	AITwinPopulation* selectedPopulation = nullptr;
	std::optional<AITwinPopulation::FInteractiveTransformationScope> InteractiveTransformationScope;
	int32 selectedInstanceIndex = INDEX_NONE;

	inline int32 NumPopulations(EITwinPopulationType Type) const;

	void OnActivatePicking(bool bActivate);

	void OnSplineEditedInTool(bool bFinalEdit);

	struct FCreatedInstance
	{
		AITwinPopulation* Population = nullptr;
		int32 NewInstanceIndex = INDEX_NONE;
	};

	struct [[nodiscard]] FScopedInstanceSelection
	{
		FScopedInstanceSelection(FImpl& InImpl, const FCreatedInstance& NewInstance);
		~FScopedInstanceSelection();

		FImpl& Impl;
		AITwinPopulation* const PrevSelectedPopulation;
		int32 const PrevSelectedInstanceIndex;
	};

	AITwinPopulation* draggedAssetPopulation = nullptr;
	std::optional<AITwinPopulation::FAutoRebuildTreeDisabler> DraggedPopTreeUpdateDisabler;
	int32 draggedAssetInstanceIndex = -1;

	FVector::FReal InstancesScaleVariation = 0.2;
	FVector::FReal InstancesRotationVariation = UE_DOUBLE_PI;
	FVector::FReal InstancesRotationVariationMin = -InstancesRotationVariation;
	FVector::FReal InstancesRotationVariationMax = InstancesRotationVariation;
	FVector::FReal InstancesScaleVariationMin = -InstancesScaleVariation;
	FVector::FReal InstancesScaleVariationMax = InstancesScaleVariation;
	bool forcePerpendicularToSurface = false;

	bool isEditingBrushSize = false;
	// Information on current brushing operation.
	std::map<AITwinPopulation*, FBrushAddedPopulationInfo> BrushAddedInstancesInfo;
	std::map<AITwinPopulation*, FBrushRemovedPopulationInfo> BrushRemovedInstancesInfo;

	struct FSavedTransformInfo
	{
		FTransform Transform = FTransform::Identity;
		FQuat::FReal AngleZ = 0.;
		FVector::FReal ScaleVariation = 0.;
		bool bChanged = true;
	};
	std::array<FSavedTransformInfo, static_cast<size_t>(EITwinInstantiatedObjectType::Count)> SavedTransformsByType;

	std::map<FString, bool> usedAssets;
	std::vector<AITwinPopulation*> EditedPopulations;
	mutable bool bNeedsUpdateEditedPopulations = false;
	TArray<AActor*> AllPopulations;
	mutable bool bNeedsUpdateAllPopulations = false;
	AdvViz::SDK::RefID instanceGroupId = AdvViz::SDK::RefID::Invalid(); // destination group for instances
	std::map<AITwinSplineHelper const*, AdvViz::SDK::RefID> splineToGroupId;

	// For the addition of an instance from the browser
	FQuat::FReal DraggingRotVar = 0.;
	FVector::FReal DraggingScaleVar = 1.;

	// Interactive placement mode
	bool bInteractivePlacement = false;
	enum class EInteractivePlacementState
	{
		None,
		JustStarted,
		DelayTransform,
		Transforming,
	};
	EInteractivePlacementState InteractivePlacementState = EInteractivePlacementState::None;

	bool bRestrictPickingOnClipping = false;
	TWeakObjectPtr<AActor> LastHitActor_InteractivePlacement;
	// Preview of the object being placed in interactive placement mode (AzDev#2085006)
	bool bPreviewPlacedObject = false;

	bool bEnabledForCutout = false;

	IITwinPopulationInstanceTransformProxyPtr TransformProxy;


	FImpl(AITwinPopulationTool& inOwner);

	// Implementation of AITwinPopulationTool functions
	EPopulationToolMode GetMode() const { return toolMode; }
	void SetMode(EPopulationToolMode mode);

	ETransformationMode GetTransformationMode() const { return transformationMode; }
	void SetTransformationMode(ETransformationMode mode);

	std::optional<FPopulationIdentifier> GetSelectedSplinePopulation() const;
	bool SelectSplinePopulation(FPopulationIdentifier PopHandle, bool bEnterIsolationMode = true);

	AITwinPopulation* GetSelectedPopulation() const { return selectedPopulation; }
	int32 GetSelectedInstanceIndex() const { return selectedInstanceIndex; }
	void SetSelectedPopulation(AITwinPopulation* population);
	void SetSelectedInstanceIndex(int32 instanceIndex);
	bool HasSelectedPopulation() const { return selectedPopulation != nullptr; }
	inline bool HasSelectedInstance() const;

	void DeleteSelectedInstance();

	bool IsPopulationModeActivated() const;
	bool IsBrushModeActivated() const;

	bool StartBrushingInstances();
	void EndBrushingInstances();
	void ShowInstanceBrush();

	void ComputeBrushFlow();
	void SetBrushSize(float size);

	FTransform GetSelectionTransform() const;
	void OnSelectionTransformStarted(bool bForInteractivePlacement = false);
	void OnSelectionTransformCompleted(bool bForInteractivePlacement = false);
	void SetSelectionTransform(const FTransform& transform);
	FLinearColor GetSelectionColorVariation() const;
	void SetSelectionColorVariation(const FLinearColor& c);

	void SetEnabled(bool value);
	inline bool IsEnabled() const;

	void ResetToDefault();
	void SetDecorationHelper(AITwinDecorationHelper* decoHelper);

	bool DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath);
	void ReleaseDraggedAssetInstance();
	void DestroyDraggedAssetInstance();

	void SetUsedAsset(const FString& assetPath, bool used);
	void ClearUsedAssets();
	void ReplaceUsedAssets(const TArray<FString>& AssetPaths);

	bool IsAdditionOfInstancesAllowed(bool* bOutAllowBrush = nullptr) const;
	int32 GetInstanceCount(const FString& assetPath) const;
	bool GetForcePerpendicularToSurface() const;
	void SetForcePerpendicularToSurface(bool b);
	bool GetIsEditingBrushSize() const;
	void SetIsEditingBrushSize(bool b);
	bool DoMouseClickAction();

	void SetInteractivePlacement(bool bInInteractivePlacement);
	bool StartInteractiveCreation();

	enum class EAbortContext
	{
		UserInput_ITS, // user input triggered from iTwin Studio
		UserInput_Unreal, // user input triggered from Unreal (Escape key...)
		Internal, // internal reason that prevents from adding instances
	};
	void AbortInteractiveCreation(EAbortContext Context);

	void FinalizeInteractiveCreation(const FHitResult* HitResult, bool bTriggeredFromITS);

	//! Set whether the tool is in preview mode for placing an object.
	void SetPreviewPlacedObject(bool bPreview);
	void UpdatePlacedObjectPreview();
	bool CheckInteractivePlacementAllowance();

	void Tick(float DeltaTime);

	// Additional internal functions
	bool ComputeTransformFromHitResult(const FHitResult& HitResult,	FTransform& OutTransform,
		const AITwinPopulation* Population,
		bool bIsDraggingInstance = false,
		bool bStartingInteractiveCreation = false);
	FHitResult LineTraceFromMousePos();
	FVector LineTraceToSetBrushSize();
	void MultiLineTraceFromMousePos(int32 traceCount, std::vector<AITwinPopulation*> const& populations);
	bool AddSingleInstanceFromHitResult(const FHitResult& hitResult,
		FCreatedInstance* OutCreatedInstance = nullptr, bool bStartingInteractiveCreation = false);

	/// Try to add one instance, using the center of the view as reference position.
	bool AddSingleInstanceAtViewCenter(bool bStartingInteractiveCreation);

	void InvalidatePopulations() {
		bNeedsUpdateAllPopulations = true;
		bNeedsUpdateEditedPopulations = !EditedPopulations.empty();
	}
	inline TArray<AActor*> const& GetAllPopulations() const;
	void AppendAllPopulationConstActors(TArray<AActor const*>& ActorsToIgnore) const;
	inline std::vector<AITwinPopulation*> const& GetEditedPopulations(bool bForceUpdateArray = false);

	size_t CollectEditedPopulations();
	void UpdatePopulationsCollisionType() const;
	void UpdatePopulationsArray();
	void StartDragging(AITwinPopulation* population);
	void DeleteInstanceFromPopulation(AITwinPopulation*& population, int32& instanceIndex);

	void RestrictPickingOnClippingPrimitives(bool bInRestrictPickingOnClipping = true);
	bool ShowOnlyTranslationZGizmo() const;

	AITwinPopulation* PreLoadPopulation(const FString& AssetPath);
	void SetUsedOnCutout(bool bForCutout);
	void SetInstanceTransformProxy(IITwinPopulationInstanceTransformProxyPtr InTransformProxy);

	void UpdateGroupId(AITwinSplineHelper const* CurSpline);

	FString GetTransformationName();

	void PopulationChanged(AITwinPopulation &population, EChangeType change, const FString &eventSource);

	// Track a modification event for a spline population (Area/Path), when there is no specific
	// AITwinPopulation instance to report (eg. spline point added/removed).
	void SplinePopulationChanged(EITwinPopulationType PopulationType, EChangeType change,
		const FString &eventSource = FString(), const FString &modificationKind = FString());

	void OnSplineChanged(const FString &eventSource, const FString &modificationKind);

	// Store whether the removal event was initiated by Unreal (delete key in 3D viewport) or
	// iTwin Studio (trash icon in path property widget)
	enum class ERemovalInitiator : uint8_t
	{
		Unreal,
		ITS
	};
	std::optional<ERemovalInitiator> RemovalInitiatorOpt;

	struct [[nodiscard]] FScopedRemovalContext
	{
		FImpl& Impl;

		FScopedRemovalContext(FImpl& InImpl, ERemovalInitiator RemovalInitiator) : Impl(InImpl)
		{
			Impl.RemovalInitiatorOpt.emplace(RemovalInitiator);
		}

		~FScopedRemovalContext()
		{
			Impl.RemovalInitiatorOpt.reset();
		}
	};

	// Population along / inside a spline
	/// Make the Spline Tool the active tool, with usage restricted to spline populations.
	TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool(UWorld* World, EITwinPopulationType PopulationType);
	// Select a spline, or reset selection if InSplineHelper is null
	void SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World);
	/// Show/Hide population proxies for the given population type.
	void SetPopulationProxyVisibility(EITwinPopulationType PopulationType, bool bVisibleInGame, bool bIsolationMode = false);
	void ShowOnlyPopulationProxiesOfType(EITwinPopulationType SelectedType, bool bIsolationMode);
	// Change all spline populations visibility in the viewport (without deactivating them)
	void SetAllPopulationProxiesVisibility(bool bVisibleInGame);
	void HideAllPopulationProxies() { SetAllPopulationProxiesVisibility(false); }
	void PopulateSpline();
	/// Requests an (asynchronous) population of the given spline.
	/// \param bFinalEdit True when the spline reached a stable state (end of edition, parameter change...).
	///        In such case any computation in progress for this spline is discarded and a new one is
	///        started at once. When false (interactive edition in progress), the current computation is
	///        kept, and a new one is scheduled right after it (coalescing all intermediate requests).
	void PopulateSpline(AITwinSplineHelper const& TargetSpline, bool bFinalEdit = true);
	bool RegisterPopulationSpline(AITwinSplineHelper* SplineHelper);

	// ---- Asynchronous spline population ----
	// The population is split in 3 phases:
	//  1. (game thread) snapshot the spline and the sampling parameters,
	//  2. (worker thread) sample the spline => list of positions,
	//  3. (game thread) project positions onto the scene and create the instances.
	struct FSplinePopulationJob
	{
		TWeakObjectPtr<AITwinSplineHelper const> Spline;
		TSharedPtr<FITwinSplineSnapshotCurve> Curve;
		BeUtils::SplineSamplingParameters SamplingParams;
		BeUtils::BoundingBox SamplingBox;
		glm::dvec3 AverageInstanceDims = glm::dvec3(0.0);
		double BoundsMaxZ = 0.;
		std::vector<glm::dvec3> Positions; // output of the worker thread
		std::atomic<bool> bDone = false;
		std::atomic<bool> bCancelled = false;
	};
	struct FSplinePopulationState
	{
		std::shared_ptr<FSplinePopulationJob> RunningJob;
		bool bPendingRequest = false;
	};
	std::map<AdvViz::SDK::RefID, FSplinePopulationState> SplinePopulationStates;

	bool StartSplinePopulationJob(AITwinSplineHelper const& TargetSpline, FSplinePopulationState& State);
	void ApplySplinePopulationJob(FSplinePopulationJob& Job);
	void TickSplinePopulationJobs();
	void CancelSplinePopulationJobs(AdvViz::SDK::RefID const* SplineId = nullptr);

	bool UnregisterPopulationSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);
	bool RemovePopulation(FPopulationIdentifier PopHandle, bool bTriggeredFromITS);
	void ZoomOnPopulation(FPopulationIdentifier PopHandle);
	// For communication with iTwin Studio
	AdvViz::SDK::RefID GetPopulationRefId(FPopulationIdentifier PopulationHandle) const;
	FPopulationIdentifier GetPopulationIdentifier(AdvViz::SDK::RefID const& RefID) const;
	/// Returns the unique identifier of a population from its index.
	AdvViz::SDK::RefID GetPopulationId(EITwinPopulationType PopulationType, int32 PopulationIndex) const;
	void ToggleSplineToolForSelectedPath();
private:
	FPopulationIdentifier GetPopulationIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const;
	UITwinPopulationHelper* CreatePop(EITwinPopulationType PopType);
};

AITwinPopulationTool::FImpl::FImpl(AITwinPopulationTool& inOwner)
	: owner(inOwner)
{
}

inline TArray<AActor*> const& AITwinPopulationTool::FImpl::GetAllPopulations() const
{
	if (bNeedsUpdateAllPopulations)
	{
		const_cast<FImpl*>(this)->UpdatePopulationsArray();
	}
	return AllPopulations;
}

void AITwinPopulationTool::FImpl::AppendAllPopulationConstActors(TArray<AActor const*>& ActorsToIgnore) const
{
	TArray<AActor*> const& AllPopulationActors = GetAllPopulations();
	// Due to constness considerations we cannot just write ActorsToIgnore.Append(AllPopulationActors)...
	ActorsToIgnore.Reserve(ActorsToIgnore.Num() + AllPopulationActors.Num());
	for (auto PopulationActor : AllPopulationActors)
		ActorsToIgnore.Push(PopulationActor);
}

inline std::vector<AITwinPopulation*> const& AITwinPopulationTool::FImpl::GetEditedPopulations(bool bForceUpdateArray /*= false*/)
{
	if (bForceUpdateArray || bNeedsUpdateEditedPopulations)
	{
		CollectEditedPopulations();
	}
	return EditedPopulations;
}

void AITwinPopulationTool::FImpl::SetMode(EPopulationToolMode mode)
{
	if (!IsEnabled())
		return;

	const bool bChangingMode = (toolMode != mode);
	if (bChangingMode && mode != EPopulationToolMode::Select && HasSelectedInstance())
	{
		// Reset current selection.
		SetSelectedInstanceIndex(INDEX_NONE);
		owner.SelectionChangedEvent.Broadcast();
	}

	toolMode = mode;

	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();

	// Show or hide brush depending on active mode
	if (toolMode == EPopulationToolMode::InstantiateN || toolMode == EPopulationToolMode::RemoveInstances)
	{
		ShowInstanceBrush();

		if (toolMode == EPopulationToolMode::InstantiateN)
			ComputeBrushFlow();
	}
	else
	{
		HideBrushSphere();
		if (toolMode == EPopulationToolMode::Area || toolMode == EPopulationToolMode::Path)
		{
			// Abort current spline population creation, if any
			owner.AbortInteractiveCreation(true);

			// Make sure we hide all spline population proxies (only the new item will be visible).
			HideAllPopulationProxies();
			// Start interactive drawing.
			SplineTool = ActivateSplineTool(owner.GetWorld(), toolMode == EPopulationToolMode::Area? EITwinPopulationType::Area : EITwinPopulationType::Path);
			if (SplineTool.IsValid())
			{
				SplineTool->StartInteractiveCreation();
			}
		}
	}

	if (bChangingMode)
	{
		owner.ModeChangedEvent.Broadcast();
	}

	// Use interactive placement for the single instance mode.
	// AzDev#2085006.
	SetPreviewPlacedObject(toolMode == EPopulationToolMode::Instantiate);
}

void AITwinPopulationTool::FImpl::SetPopulationProxyVisibility(EITwinPopulationType PopulationType, bool bVisibleInGame, bool bIsolationMode/* = false*/)
{
	switch (PopulationType)
	{
		case EITwinPopulationType::Area:
		case EITwinPopulationType::Path:
		{
			for (int32 i(0); i < NumPopulations(PopulationType); i++)
			{
				auto PopHelper = GetPopulationInfo(FPopulationIdentifier(PopulationType, i));
				if (PopHelper && PopHelper->SplineHelper.IsValid())
				{
					const bool bShowSpline = bVisibleInGame
						&& (!bIsolationMode || PopHelper->SplineHelper->IsSelected())
						&& PopHelper->IsVisible();
					PopHelper->SplineHelper->SetActorHiddenInGame(!bShowSpline);
				}
			}
			break;
		}
	}
}

void AITwinPopulationTool::FImpl::ShowOnlyPopulationProxiesOfType(EITwinPopulationType SelectedType, bool bIsolationMode)
{
	for (EITwinPopulationType PopulationType : TEnumRange<EITwinPopulationType>())
	{
		SetPopulationProxyVisibility(PopulationType, PopulationType == SelectedType, bIsolationMode);
	}
}

bool AITwinPopulationTool::FImpl::SelectSplinePopulation(FPopulationIdentifier PopHandle, bool bEnterIsolationMode)
{
	if (!ensure(PopHandle.IsValid(NumPopulations(PopHandle.PopulationType))))
		return false;

	bool bHasSetSelection = false;
	auto PopHelper = GetPopulationInfo(PopHandle);
	switch (PopHandle.PopulationType)
	{
		case EITwinPopulationType::Area:
		case EITwinPopulationType::Path:
		{
			if (PopHelper && PopHelper->SplineHelper.IsValid())
			{
				SelectSpline(PopHelper->SplineHelper.Get(), owner.GetWorld());

				bHasSetSelection = PopHelper->SplineHelper->IsSelected();
			}
			break;
		}
		BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinPopulationType::Count:);
	}
	if (bEnterIsolationMode && bHasSetSelection)
	{
		ShowOnlyPopulationProxiesOfType(PopHandle.PopulationType, true);
	}
	return bHasSetSelection;
}

bool AITwinPopulationTool::SelectSplinePopulation(FPopulationIdentifier PopHandle, bool bEnterIsolationMode)
{
	return Impl->SelectSplinePopulation(PopHandle, bEnterIsolationMode);
}

void AITwinPopulationTool::FImpl::SetTransformationMode(ETransformationMode mode)
{
	transformationMode = mode;
}

void AITwinPopulationTool::FImpl::SetSelectedPopulation(AITwinPopulation* population)
{
	if (selectedPopulation && selectedPopulation != population)
	{
		selectedPopulation->SetSelectedInstanceIndex(INDEX_NONE);
	}
	selectedPopulation = population;
	SetSelectedInstanceIndex(INDEX_NONE);
}

void AITwinPopulationTool::FImpl::SetSelectedInstanceIndex(int32 instanceIndex)
{
	if (TransformProxy && instanceIndex != selectedInstanceIndex)
	{
		// Reset transformation proxy whenever the selected instance is changed.
		SetInstanceTransformProxy({});
	}

	selectedInstanceIndex = instanceIndex;

	if (selectedPopulation)
	{
		selectedPopulation->SetSelectedInstanceIndex(instanceIndex);
	}
}

inline bool AITwinPopulationTool::FImpl::HasSelectedInstance() const
{
	return selectedPopulation && selectedInstanceIndex >= 0;
}

void AITwinPopulationTool::FImpl::DeleteSelectedInstance()
{
	if (selectedPopulation)
	{
		DeleteInstanceFromPopulation(selectedPopulation, selectedInstanceIndex);
		// Reset transformation proxy as the selection has been reset.
		SetInstanceTransformProxy({});
		owner.SelectionChangedEvent.Broadcast();
	}
}

bool AITwinPopulationTool::FImpl::ShowOnlyTranslationZGizmo() const
{
	// Cut-out plane can only be moved along Z axis.
	return selectedPopulation
		&& selectedPopulation->GetObjectType() == EITwinInstantiatedObjectType::ClippingPlane;
}

bool AITwinPopulationTool::FImpl::IsPopulationModeActivated() const
{
	return toolMode == EPopulationToolMode::Instantiate || IsBrushModeActivated() || 
		   toolMode == EPopulationToolMode::Area || toolMode == EPopulationToolMode::Path;
}

bool AITwinPopulationTool::FImpl::IsBrushModeActivated() const
{
	return toolMode == EPopulationToolMode::InstantiateN ||
		   toolMode == EPopulationToolMode::RemoveInstances;
}

bool AITwinPopulationTool::FImpl::StartBrushingInstances()
{
	if (!IsEnabled())
		return false;

	BrushAddedInstancesInfo.clear();
	BrushRemovedInstancesInfo.clear();
	StartBrushing(owner.GetGameTimeSinceCreation());

	const bool bForceUpdateEditedPopulations = EditedPopulations.empty();
	GetEditedPopulations(bForceUpdateEditedPopulations);
	return true;
}

void AITwinPopulationTool::FImpl::EndBrushingInstances()
{
	APlayerController* PlayerControler = owner.GetWorld()->GetFirstPlayerController();
	if (ensure(PlayerControler))
	{
		PlayerControler->SetIgnoreLookInput(false);
	}
	EndBrushing();
}

void AITwinPopulationTool::FImpl::ShowInstanceBrush()
{
	if (IsEnabled())
	{
		ShowBrushSphere();
	}
}

void AITwinPopulationTool::FImpl::ComputeBrushFlow()
{
	// Compute an appropriate brush Flow for the selected actor that may be instantiated.
	if (selectedPopulation)
	{
		FVector selSize = Cast<AITwinPopulation const>(selectedPopulation)->GetMasterMeshBounds().GetBox().GetSize();
		float selArea = selSize.X * selSize.Y * 1e-4f; // convert cm^2 to m^2
		// Limit the value to 1 instance per m^2 per second (it's enough for characters
		// which are the smallest assets at the moment).
		SetBrushComputedValue(selArea > 1.f ? 1.f/selArea : 1.f);
	}
}

void AITwinPopulationTool::FImpl::SetBrushSize(float size)
{
	if (IsBrushModeActivated())
	{
		isEditingBrushSize = true;
		SetBrushRadius(size);
	}
}

FTransform AITwinPopulationTool::FImpl::GetSelectionTransform() const
{
	if (HasSelectedInstance())
	{
		if (TransformProxy)
		{
			return TransformProxy->GetTransform();
		}
		else
		{
			return selectedPopulation->GetInstanceTransform(selectedInstanceIndex);
		}
	}
	else if (SplineTool.IsValid() && SplineTool->HasSelection())
	{
		return SplineTool->GetSelectionTransform();
	}

	return FTransform();
}

void AITwinPopulationTool::FImpl::OnSelectionTransformStarted(bool bForInteractivePlacement /*= false*/)
{
	if (bInteractivePlacement && !bForInteractivePlacement)
	{
		// Quick fix to avoid a false positive assert: when the user clicks to validate the instance position
		// we do come here but we don't want to recreate the disabler...
		BE_ASSERT(InteractiveTransformationScope.has_value());
		return;
	}
	if (InteractiveTransformationScope)
	{
		BE_ISSUE("missing call to OnSelectionTransformCompleted?");
		InteractiveTransformationScope.reset();
	}
	if (TransformProxy)
	{
		TransformProxy->OnTransformModificationStarted(transformationMode);
	}
	if (selectedPopulation)
	{
		// Optimization: suspend automatic tree rebuild for the edited population.
		InteractiveTransformationScope.emplace(*selectedPopulation);

		if (!bInteractivePlacement)
		{
			PopulationChanged(*selectedPopulation, EChangeType::Modified, TEXT("gizmo"));
		}
	}
}

void AITwinPopulationTool::FImpl::OnSelectionTransformCompleted(bool bForInteractivePlacement /*= false*/)
{
	if (bInteractivePlacement && !bForInteractivePlacement)
	{
		return;
	}
	// Restore the suspended tree update, if any.
	InteractiveTransformationScope.reset();
}

void AITwinPopulationTool::FImpl::SetSelectionTransform(const FTransform& Transform)
{
	if (HasSelectedInstance())
	{
		if (TransformProxy)
		{
			TransformProxy->SetTransform(Transform);
		}
		else
		{
			selectedPopulation->SetInstanceTransform(selectedInstanceIndex, Transform);
		}
		if (!selectedPopulation->IsRotationVariationEnabled())
		{
			auto& SavedTransform = SavedTransformsByType[static_cast<size_t>(selectedPopulation->GetObjectType())];
			SavedTransform.Transform = Transform;
			SavedTransform.bChanged = true;
		}
	}
}

FLinearColor AITwinPopulationTool::FImpl::GetSelectionColorVariation() const
{
	FLinearColor color(0.5, 0.5, 0.5);

	if (HasSelectedInstance())
	{
		FVector v = selectedPopulation->GetInstanceColorVariation(selectedInstanceIndex);

		color.R = v.X + 0.5;
		color.G = v.Y + 0.5;
		color.B = v.Z + 0.5;
	}

	return color;
}

void AITwinPopulationTool::FImpl::SetSelectionColorVariation(const FLinearColor& c)
{
	if (HasSelectedInstance())
	{
		FVector v(c.R - 0.5, c.G - 0.5, c.B - 0.5);
		selectedPopulation->SetInstanceColorVariation(selectedInstanceIndex, v);
	}
}

void AITwinPopulationTool::FImpl::SetEnabled(bool value)
{
	if (value != enabled)
	{
		enabled = value;

		UpdatePopulationsArray();
		UpdatePopulationsCollisionType();
		UpdatePlacedObjectPreview();

		if (!enabled)
		{
			SetSelectedPopulation(nullptr);
		}

		if (IsBrushModeActivated())
		{
			if (enabled)
			{
				ShowInstanceBrush();
			}
			else
			{
				HideBrushSphere();
			}
		}
	}
}

inline bool AITwinPopulationTool::FImpl::IsEnabled() const
{
	return enabled && decorationHelper && decorationHelper->IsPopulationEnabled();
}

void AITwinPopulationTool::FImpl::ResetToDefault()
{
	toolMode = EPopulationToolMode::Select;
	transformationMode = ETransformationMode::Move;
	usedAssets.clear();
	EditedPopulations.clear();
	SetPreviewPlacedObject(false);
}

void AITwinPopulationTool::FImpl::SetDecorationHelper(AITwinDecorationHelper* decoHelper)
{
	decorationHelper = decoHelper;
}

namespace ITwin
{
	// Converts a screen position (retrieved from drag and drop information) into a mouse position.
	ITWINRUNTIME_API std::optional<FVector2D> GetDragDropMousePosition(
		const FVector2D& ScreenPosition, UWorld* World)
	{
		UGameViewportClient* gameViewportClient = World->GetGameViewport();
		if (!gameViewportClient)
		{
			return std::nullopt;
		}
		FSceneViewport* sceneViewport = gameViewportClient->GetGameViewport();
		if (!sceneViewport)
		{
			return std::nullopt;
		}

		// The conversion from absolute to local coordinates below is done like
		// in FSceneViewport::UpdateCachedCursorPos.
		const FGeometry& cachedGeom = sceneViewport->GetCachedGeometry();
		FVector2D localPixelMousePos = cachedGeom.AbsoluteToLocal(ScreenPosition);
		localPixelMousePos.X = FMath::Clamp(localPixelMousePos.X * cachedGeom.Scale,
			(double)TNumericLimits<int32>::Min(), (double)TNumericLimits<int32>::Max());
		localPixelMousePos.Y = FMath::Clamp(localPixelMousePos.Y * cachedGeom.Scale,
			(double)TNumericLimits<int32>::Min(), (double)TNumericLimits<int32>::Max());
		return localPixelMousePos;
	}
}

bool AITwinPopulationTool::FImpl::DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath)
{
	if (!IsEnabled())
	{
		return false;
	}
	UWorld* World = owner.GetWorld();

	auto PixelMousePos = ITwin::GetDragDropMousePosition(screenPosition, World);
	if (!PixelMousePos)
	{
		return false;
	}
	if (!instanceGroupId.IsValid() && decorationHelper)
	{
		instanceGroupId = decorationHelper->GetStaticInstancesGroupId();
	}

	APlayerController* playerController = World->GetFirstPlayerController();

	FVector traceStart, traceEnd, traceDir;
	if (UGameplayStatics::DeprojectScreenToWorld(
		playerController, *PixelMousePos, traceStart, traceDir))
	{
		if (draggedAssetPopulation == nullptr)
		{
			StartDragging(decorationHelper->GetOrCreatePopulation(assetPath, instanceGroupId));

			if (!ensure(draggedAssetPopulation))
				return false;
		}

		// Do the intersection test to place the instance
		FHitResult HitResult;
		traceEnd = traceStart + traceDir * 1e8f;

		FITwinTracingHelper TracingHelper;
		TracingHelper.AddIgnoredActors(GetAllPopulations());
		TracingHelper.FindNearestImpact(HitResult, World, traceStart, traceEnd);

		FTransform instTransform;
		if (!ComputeTransformFromHitResult(HitResult, instTransform, draggedAssetPopulation, true))
		{
			instTransform.SetTranslation(traceStart + traceDir * 1000);
		}
		
		if (draggedAssetInstanceIndex == -1)
		{
			draggedAssetPopulation->AddInstance(instTransform);
			draggedAssetInstanceIndex = draggedAssetPopulation->GetNumberOfInstances() - 1;
		}
		else
		{
			draggedAssetPopulation->SetInstanceTransform(draggedAssetInstanceIndex, instTransform);
		}

		return true;
	}
	return false;
}

void AITwinPopulationTool::FImpl::ReleaseDraggedAssetInstance()
{
	// Restore automatic tree update for the dragged population.
	DraggedPopTreeUpdateDisabler.reset();
	draggedAssetPopulation = nullptr;
	draggedAssetInstanceIndex = -1;
}

void AITwinPopulationTool::FImpl::DestroyDraggedAssetInstance()
{ 
	DeleteInstanceFromPopulation(draggedAssetPopulation, draggedAssetInstanceIndex);
}

void AITwinPopulationTool::FImpl::SetUsedAsset(const FString& assetPath, bool b)
{
	usedAssets[assetPath] = b;

	// Empty the vector of edited populations so that it is updated the next time
	// instances will be added.
	EditedPopulations.clear();

	UpdatePlacedObjectPreview();
}

void AITwinPopulationTool::FImpl::ClearUsedAssets()
{
	usedAssets.clear();
	EditedPopulations.clear();
	UpdatePlacedObjectPreview();
}

void AITwinPopulationTool::FImpl::ReplaceUsedAssets(const TArray<FString>& AssetPaths)
{
	FString AssetBeingPlaced;
	if (bInteractivePlacement && HasSelectedPopulation())
	{
		AssetBeingPlaced = UTF8_TO_TCHAR(selectedPopulation->GetObjectRef().c_str());
	}
	usedAssets.clear();
	for (const FString& AssetPath : AssetPaths)
	{
		BE_ASSERT(!AssetPath.IsEmpty());
		usedAssets[AssetPath] = true;
	}

	// Same comment as in SetUsedAsset: the vector of edited populations should be updated the next time
	// instances will be added.
	EditedPopulations.clear();

	if (!AssetBeingPlaced.IsEmpty())
	{
		// If the asset being placed is not in the new list, we need to stop the interactive placement.
		if (!usedAssets.contains(AssetBeingPlaced))
		{
			AbortInteractiveCreation(EAbortContext::Internal);
		}
	}
	UpdatePlacedObjectPreview();
}

bool AITwinPopulationTool::FImpl::IsAdditionOfInstancesAllowed(bool* bOutAllowBrush /*= nullptr*/) const
{
	bool bHasSelectedAsset = false;
	bool bForbidBrush = false;
	for (auto const& [AssetPath, bSelected] : usedAssets)
	{
		if (bSelected)
		{
			bHasSelectedAsset = true;
			if (AssetPath.Contains(TEXT("Clipping")))
			{
				// Brushing clipping planes or boxes would make no sense...
				bForbidBrush = true;
			}
		}
	}
	if (bOutAllowBrush)
	{
		*bOutAllowBrush = bHasSelectedAsset && !bForbidBrush;
	}
	return bHasSelectedAsset;
}

int32 AITwinPopulationTool::FImpl::GetInstanceCount(const FString& assetPath) const
{
	return IsEnabled() ? decorationHelper->GetPopulationInstanceCount(assetPath, instanceGroupId) : 0;
}

bool AITwinPopulationTool::FImpl::GetForcePerpendicularToSurface() const
{
	return forcePerpendicularToSurface;
}

void AITwinPopulationTool::FImpl::SetForcePerpendicularToSurface(bool b)
{ 
	forcePerpendicularToSurface = b;
}

bool AITwinPopulationTool::FImpl::GetIsEditingBrushSize() const
{ 
	return isEditingBrushSize;
}

void AITwinPopulationTool::FImpl::SetIsEditingBrushSize(bool b)
{ 
	isEditingBrushSize = b;
}

AITwinPopulationTool::FImpl::FScopedInstanceSelection::FScopedInstanceSelection(
	FImpl& InImpl, const FCreatedInstance& NewInstance)
	: Impl(InImpl)
	, PrevSelectedPopulation(InImpl.GetSelectedPopulation())
	, PrevSelectedInstanceIndex(InImpl.GetSelectedInstanceIndex())
{
	Impl.SetSelectedPopulation(NewInstance.Population);
	Impl.SetSelectedInstanceIndex(NewInstance.NewInstanceIndex);
}

AITwinPopulationTool::FImpl::FScopedInstanceSelection::~FScopedInstanceSelection()
{
	Impl.SetSelectedPopulation(PrevSelectedPopulation);
	Impl.SetSelectedInstanceIndex(PrevSelectedInstanceIndex);
}

void AITwinPopulationTool::FImpl::SetInteractivePlacement(bool bInInteractivePlacement)
{
	bInteractivePlacement = bInInteractivePlacement;
	if (!bInteractivePlacement)
	{
		InteractivePlacementState = EInteractivePlacementState::None;
	}
}

bool AITwinPopulationTool::IsSplineToolActive(FPopulationIdentifier PopHandle) const
{
	if (auto PopHelper = Impl->GetPopulationInfo(PopHandle))
	{
		if (Impl->SplineTool.IsValid() && Impl->SplineTool->GetSelectedSpline() == PopHelper->SplineHelper.Get())
			return Impl->SplineTool->IsInteractiveCreationMode();
	}
	return false;
}

bool AITwinPopulationTool::FImpl::StartInteractiveCreation()
{
	Be::CleanUpGuard RestoreStateCleanup([this]
	{
		SetInteractivePlacement(false);
	});
	SetInteractivePlacement(true);
	// Reset group ID to static instances group, in case it was left pointing to a spline group
	// after creating a spline population (area/path). Without this, the previewed instance would
	// be created in the spline group and marked as a spline population, making it unselectable.
	UpdateGroupId(nullptr);
	if (!AddSingleInstanceAtViewCenter(true))
	{
		return false;
	}
	RestoreStateCleanup.release();
	return true;
}

void AITwinPopulationTool::FImpl::AbortInteractiveCreation(EAbortContext Context)
{
	if (bInteractivePlacement)
	{
		// Remove selected instance, if any, as it was never validated by the user.
		if (HasSelectedInstance())
		{
			// Mark the transform as completed to avoid asserts related to SelectionTreeUpdateDisabler.
			OnSelectionTransformCompleted(/*bForInteractivePopulation*/true);

			DeleteSelectedInstance();
		}
		SetSelectedPopulation(nullptr);
		owner.SelectionChangedEvent.Broadcast();
		SetInteractivePlacement(false);

		// The boolean bTriggeredFromITS is used to relay the event to iTwin Studio or not ; in the case of
		// an internal abortal, we don't want to notify iTwin Studio, as it would trigger a deactivation of
		// the population tool, which we do not want. Therefore we pass bTriggeredFromITS as true in such
		// case.
		// TODO_JDE: replace 'bTriggeredFromITS' by 'bNotifyITS' everywhere, to make things clearer (be
		// careful with all usages in blueprints, such as BP_CarrotViewerController...)
		const bool bTriggeredFromITS = Context == EAbortContext::UserInput_ITS
			|| Context == EAbortContext::Internal;
		owner.InteractiveCreationAbortedEvent.Broadcast(&owner, bTriggeredFromITS);
	}
}

void AITwinPopulationTool::FImpl::FinalizeInteractiveCreation(const FHitResult* HitResult, bool bTriggeredFromITS)
{
	if (!ensure(bInteractivePlacement && HasSelectedInstance()))
	{
		return;
	}

	// Finalize the instance being moved interactively.
	FTransform FinalTransform, transform;
	if (HitResult && ComputeTransformFromHitResult(*HitResult, transform, selectedPopulation))
	{
		// Same comment as in FImpl::Tick
		FinalTransform = selectedPopulation->GetBaseTransform() * transform;
	}
	else
	{
		// Keep current position.
		FinalTransform = selectedPopulation->GetInstanceTransform(selectedInstanceIndex);
	}
	AActor* HitActor = HitResult ? HitResult->GetActor() : LastHitActor_InteractivePlacement.Get();
	const bool bCutoutCreated = HitActor && selectedPopulation->IsClippingPrimitive();
	if (bCutoutCreated)
	{
		// Configure initial cutout properties, depending on the hit layer (encoding them in the AdvViz
		// instance).
		auto const& AVizInst = selectedPopulation->GetAVizInstance(selectedInstanceIndex);
		if (AVizInst)
		{
			ITwin::Clipping::ConfigureNewInstance(AVizInst, *HitActor);
		}
	}
	selectedPopulation->FinalizeAddedInstance(selectedInstanceIndex, &FinalTransform);
	OnSelectionTransformCompleted(/*bForInteractivePopulation*/true);

	owner.InteractiveCreationCompletedEvent.Broadcast(&owner, bTriggeredFromITS);

	// Track Amplitude event
	PopulationChanged(*selectedPopulation, EChangeType::Added, TEXT("single_placement"));

	SetInteractivePlacement(false);

	// In single placement mode, create a new instance to place at once.
	UpdatePlacedObjectPreview();
}

void AITwinPopulationTool::FImpl::UpdatePlacedObjectPreview()
{
	if (!bPreviewPlacedObject)
	{
		return;
	}
	// Use interactive placement for the "single instance" mode.
	// AzDev#2085006.
	const bool bCanPreviewPlacedInstance = GetMode() == EPopulationToolMode::Instantiate
		&& IsEnabled()
		&& IsAdditionOfInstancesAllowed();

	if (bCanPreviewPlacedInstance)
	{
		if (!bInteractivePlacement)
		{
			StartInteractiveCreation();
		}
	}
	else
	{
		if (bInteractivePlacement)
		{
			AbortInteractiveCreation(EAbortContext::Internal);
		}
	}
}

void AITwinPopulationTool::FImpl::SetPreviewPlacedObject(bool bPreview)
{
	const bool bWasPreview = bPreviewPlacedObject;
	bPreviewPlacedObject = bPreview;

	if (bPreviewPlacedObject)
	{
		UpdatePlacedObjectPreview();
	}
	else if (bWasPreview && bInteractivePlacement)
	{
		AbortInteractiveCreation(EAbortContext::Internal);
	}
}

bool AITwinPopulationTool::FImpl::DoMouseClickAction()
{
	if (IsBrushModeActivated())
	{
		// Start brushing.
		APlayerController* PlayerControler = owner.GetWorld()->GetFirstPlayerController();
		if (ensure(PlayerControler))
		{
			PlayerControler->SetIgnoreLookInput(true);
			return StartBrushingInstances();
		}
		else
		{
			return false;
		}
	}

	bool bRelevantAction = false;
	TArray<const AActor*> ActorsToIgnore;

	const bool bFinalizingInteractivePlacement = bInteractivePlacement && HasSelectedInstance();
	if (bFinalizingInteractivePlacement)
	{
		// When clicking to validate the final position of the created instance, we should ignore all
		// populations (including the one being selected, which would much probably be hit as the selected
		// instance is following the mouse...), as done in FImpl::Tick (see #LineTraceFromMousePos).
		AppendAllPopulationConstActors(ActorsToIgnore);
		// Ignore all splines.
		ITwin::AppendSplineHelpers(ActorsToIgnore, owner.GetWorld());
	}
	else if (bRestrictPickingOnClipping)
	{
		// In cutout selection mode, we should ignore impacts on all tilesets, as they can be hidden by the
		// cutout effect (but still seen by ray tracing).
		ITwin::IterateAllITwinTilesets([&ActorsToIgnore](FITwinTilesetAccess const& TilesetAccess)
		{
			const AActor* TilesetActor = TilesetAccess.GetTileset();
			if (TilesetActor)
			{
				ActorsToIgnore.Push(TilesetActor);
			}
		}, owner.GetWorld());
		// Also ignore splines added for the display of cutout edges.
		ITwin::AppendSplineHelpers(ActorsToIgnore, owner.GetWorld(), EITwinSplineUsage::EdgeDisplayHelper);
	}
	else
	{
		// When we are in 'select objects' mode, cutout primitives are hidden, so they should not prevent us
		// from picking an instance located behind the plane/cube.
		// AzDev#1996939
		//
		// Note that this fix is generic: if we allow the user to hide populations by hand in the future, it
		// will be useful as well.
		// 
		// We should also not be able to select instances of path or region populations
		TArray<AActor*> const& AllPopulationActors = GetAllPopulations();
		for (auto PopulationActor : AllPopulationActors)
		{
			if (Cast<AITwinPopulation const>(PopulationActor)->IsHiddenInGame()
			 ||	Cast<AITwinPopulation const>(PopulationActor)->IsSplinePopulation() 
			 || Cast<AITwinPopulation const>(PopulationActor)->IsPartOfPathAnimation())
				ActorsToIgnore.Push(PopulationActor);
		}
	}
	FHitResult hitResult = owner.DoPickingAtMousePosition(nullptr, std::move(ActorsToIgnore));
	AActor* hitActor = hitResult.GetActor();
	auto prevSelectedPopulation = selectedPopulation;
	auto prevSelectedInstanceIndex = selectedInstanceIndex;

	if (bFinalizingInteractivePlacement)
	{
		// Finalize the instance being moved interactively.
		FinalizeInteractiveCreation(&hitResult, /*bTriggeredFromITS*/false);
	}
	else
	{
		bool bHasSelectedPopulation = false;
		if (hitActor)
		{
			if (hitActor->IsA(AITwinPopulation::StaticClass()) &&
				hitResult.Item >= 0 && toolMode == EPopulationToolMode::Select)
			{
				AITwinPopulation* HitPopulation = Cast<AITwinPopulation>(hitActor);
				// Don't select a hidden population/instance.
				bool bVisibleInstance = !HitPopulation->IsHiddenInGame();
				// Beware the special case of cutout primitives: we use an isolation mode when an instance is
				// selected:
				if (bVisibleInstance
					&& HitPopulation->IsClippingPrimitive()
					&& HitPopulation->HasSelectedInstance())
				{
					bVisibleInstance = (HitPopulation->GetSelectedInstanceIndex() == hitResult.Item);
				}
				// We also handle restriction on clipping primitives, if applicable.
				if (bVisibleInstance
					&& (!bRestrictPickingOnClipping || HitPopulation->IsClippingPrimitive()))
				{
					SetSelectedPopulation(HitPopulation);
					SetSelectedInstanceIndex(hitResult.Item);
					bHasSelectedPopulation = bRelevantAction = true;
				}
			}
			else if (toolMode == EPopulationToolMode::Instantiate ||
				toolMode == EPopulationToolMode::InstantiateN)
			{
				SetSelectedPopulation(nullptr);
				FCreatedInstance CreatedInstance;
				bRelevantAction = AddSingleInstanceFromHitResult(hitResult, &CreatedInstance);
				if (bRelevantAction)
				{
					// Track Amplitude event
					PopulationChanged(*CreatedInstance.Population, EChangeType::Added, TEXT("single_placement"));

					// Temporarily select the added instance (for backup system).
					{
						FScopedInstanceSelection TempSelection(*this, CreatedInstance);
						owner.InteractiveCreationCompletedEvent.Broadcast(&owner, /*bTriggeredFromITS*/false);
					}
				}
			}
		}
		if (!bHasSelectedPopulation && toolMode == EPopulationToolMode::Select)
		{
			SetSelectedPopulation(nullptr);
		}
	}

	if (selectedPopulation != prevSelectedPopulation || selectedInstanceIndex != prevSelectedInstanceIndex)
		owner.SelectionChangedEvent.Broadcast();

	return bRelevantAction;
}

void AITwinPopulationTool::FImpl::Tick(float DeltaTime)
{
	// Async spline populations must progress even when the tool is not enabled (eg. after leaving the
	// population tool while a computation was still running).
	TickSplinePopulationJobs();

	if (!enabled)
		return;
	if (IsBrushModeActivated())
	{
		// Place the brush sphere.
		FHitResult hitResult;
		if (isEditingBrushSize)
		{
			SetBrushPosition(LineTraceToSetBrushSize());
		}
		else
		{
			hitResult = LineTraceFromMousePos();
			if (hitResult.GetActor())
			{
				SetBrushPosition(hitResult.Location);
			}
		}

		// Add/remove instances in the brush zone.
		if (hitResult.GetActor() && IsBrushing())
		{
			UpdateGroupId(nullptr);
			auto const& EditedPopulationsActors = GetEditedPopulations();
			if (!EditedPopulationsActors.empty() && toolMode == EPopulationToolMode::InstantiateN)
			{
				float currentTime = owner.GetGameTimeSinceCreation();
				float brushDeltaTime = currentTime - BrushLastTime;
				float brushRadiusInMeters = GetBrushRadius() * 1e-2f;
				float brushDiskArea = brushRadiusInMeters * brushRadiusInMeters * UE_PI;
				float traceCount = BrushFlow.GetFlow() * brushDeltaTime * brushDiskArea;
				int32 traceCountInt = static_cast<int32>(traceCount);

				if (traceCountInt > 0)
				{
					MultiLineTraceFromMousePos(traceCountInt, EditedPopulationsActors);

					BrushLastTime = currentTime;
					BrushLastPos = GetBrushPosition();
				}
			}
			else if (toolMode == EPopulationToolMode::RemoveInstances)
			{
				TArray<AActor*> actorsToIgnore;
				TArray<FHitResult> hitResults;

				if (UKismetSystemLibrary::SphereTraceMulti(
						&owner, hitResult.Location, hitResult.Location, GetBrushRadius(),
						ETraceTypeQuery::TraceTypeQuery1, false, actorsToIgnore,
						EDrawDebugTrace::None, hitResults, true))
				{
					std::map<AITwinPopulation*, TArray<int32>> hitsByPopulation;
					for (auto& hitRes : hitResults)
					{
						AActor* hitActor = hitRes.GetActor();
						if (hitActor && hitActor->IsA(AITwinPopulation::StaticClass()) && hitRes.Item >= 0)
						{
							AITwinPopulation* HitPopulation = Cast<AITwinPopulation>(hitActor);
							// Never remove hidden instances (clipping primitives, typically).
							// Also don't remove instances of path and region populations or path animation.
							if (!HitPopulation->IsHiddenInGame() 
							&& !HitPopulation->IsSplinePopulation() 
							&& !HitPopulation->IsPartOfPathAnimation())
								hitsByPopulation[HitPopulation].AddUnique(hitRes.Item);
						}
					}

					for (auto& hits : hitsByPopulation)
					{
						if (!BrushRemovedInstancesInfo.contains(hits.first))
						{
							// Track amplitude event
							PopulationChanged(*hits.first, EChangeType::Deleted, TEXT("brush"));
						}

						// Backup instances to be removed for undo/redo system.
						auto& Backups = BrushRemovedInstancesInfo[hits.first].Backups;
						Backups.reserve(Backups.size() + hits.second.Num());
						for (int32 InstanceIndex : hits.second)
						{
							Backups.emplace_back(hits.first, InstanceIndex, decorationHelper);
						}

						// Actually remove instances. The indices will be sorted in decreasing order.
						hits.first->RemoveInstances(hits.second);
					}
				}
			}
		}
	}
	else if (bInteractivePlacement && HasSelectedInstance())
	{
		// If this is the first instance being placed, we need to delay its transformation until the instance
		// is fully loaded on GPU. Otherwise, the instance may not appear at all, or only its shadow will be
		// visible, depending on the GPU load. This was a problem with cutout (see ADO#1973505) until we
		// changed the instanced type for those objects.
		if (!CheckInteractivePlacementAllowance())
		{
			return;
		}
		FHitResult hitResult = LineTraceFromMousePos();
		FTransform transform;
		if (ComputeTransformFromHitResult(hitResult, transform, selectedPopulation))
		{
			// Do not forget to multiply by BaseTransform, as done in AITwinPopulation::AddInstance
			selectedPopulation->SetInstanceTransformUEOnly(selectedInstanceIndex,
				selectedPopulation->GetBaseTransform() * transform);
			if (selectedPopulation->IsClippingPrimitive())
			{
				selectedPopulation->NotifyClippingToolOfTransform(selectedInstanceIndex);
			}
			// Store the last hit actor, in case the user validates the instance through the 'Enter' key (see
			// #ValidateInteractiveCreationImpl and #FinalizeInteractiveCreation)
			LastHitActor_InteractivePlacement = hitResult.GetActor();
		}
	}
}

bool AITwinPopulationTool::FImpl::ComputeTransformFromHitResult(
	const FHitResult& hitResult, FTransform& transform,
	const AITwinPopulation* population,
	bool isDraggingInstance /*= false*/,
	bool bStartingInteractiveCreation /*= false*/)
{
	// In some cases (interactive creation of a cutout primitive), we want to create the instance even when
	// no hit was found: it will float in the air as long as the user does not hover a valid area.
	bool const bRequiresValidHit = !bStartingInteractiveCreation;
	if (bRequiresValidHit)
	{
		if (!hitResult.HasValidHitObjectHandle())
		{
			return false;
		}
		AActor* hitActor = hitResult.GetActor();
		if (!hitActor)
		{
			return false;
		}
	}

	FMatrix hitMat(FMatrix::Identity);

	FQuat::FReal RotVar = 0.;

	auto& SavedTransform = SavedTransformsByType[static_cast<size_t>(population->GetObjectType())];

	if (!population->IsRotationVariationEnabled())
	{
		// Reuse the last saved angle for this population type, if any.
		// Angles are now saved by object type, and for cutouts, we ignore it (AzDev#2082180).
		if (!population->IsClippingPrimitive() && SavedTransform.bChanged)
		{
			FVector eulerAngles = SavedTransform.Transform.GetRotation().Euler();
			SavedTransform.AngleZ = FMath::DegreesToRadians(eulerAngles.Z);
			SavedTransform.bChanged = false;
		}
		RotVar = SavedTransform.AngleZ;
	}
	else if (isDraggingInstance)
	{
		RotVar = DraggingRotVar;
	}
	else if (InstancesRotationVariation != 0.)
	{
		// Now that we use interactive placement for single instance mode, we should make sure the random
		// rotation is computed only once for the previewed instance, or else the object with change its
		// rotation at each and every tick!
		if (bInteractivePlacement && !bStartingInteractiveCreation)
		{
			RotVar = SavedTransform.AngleZ;
		}
		else
		{
			RotVar = FMath::FRandRange(
				InstancesRotationVariationMin, InstancesRotationVariationMax);

			if (bStartingInteractiveCreation)
			{
				// Store the random rotation for the previewed instance (see above).
				SavedTransform.AngleZ = RotVar;
			}
		}
	}

	// NB: for cutout cube/plane, we never want the instance to be perpendicular to the hit surface.
	bool const bMakePerpendicularToHitSurface =
		(population->IsPerpendicularToSurface() || this->forcePerpendicularToSurface)
		&& !population->IsClippingPrimitive();

	if (bMakePerpendicularToHitSurface)
	{
		FVector sZ = hitResult.Normal;
		FVector sX = FVector::XAxisVector;
		if (sX.Dot(sZ) > 0.8f)
		{
			sX = FVector::YAxisVector;
		}
		FVector sY = sZ ^ sX;
		sY.Normalize();
		sX = sY ^ sZ;
		hitMat = FMatrix(sX, sY, sZ, FVector(0.));

		if (RotVar != 0.)
		{
			FQuat hitQuat(hitMat);
			hitQuat = FQuat(sZ, RotVar) * hitQuat;
			hitMat = hitQuat.ToMatrix();
		}
	}
	else if (RotVar != 0.)
	{
		FQuat hitQuat(FVector::ZAxisVector, RotVar);
		hitMat = hitQuat.ToMatrix();
	}
	hitMat.SetOrigin(FVector(hitResult.Location));

	if (population->IsScaleVariationEnabled() && InstancesScaleVariation > 0.)
	{
		FVector::FReal ScaleVar = 0.;
		if (isDraggingInstance)
		{
			ScaleVar = DraggingScaleVar;
		}
		else if (bInteractivePlacement && !bStartingInteractiveCreation)
		{
			// Same reasoning as for rotation variation: we want to keep the same scale for the previewed
			// instance until the user validates its position.
			ScaleVar = SavedTransform.ScaleVariation;
		}
		else
		{
			ScaleVar = FMath::FRandRange(InstancesScaleVariationMin, InstancesScaleVariationMax);

			if (bStartingInteractiveCreation)
			{
				// Store the random rotation for the previewed instance (see above).
				SavedTransform.ScaleVariation = ScaleVar;
			}
		}
		hitMat = hitMat.ApplyScale(population->IsSplinePopulation()? ScaleVar : 1. + ScaleVar);
	}

	transform.SetFromMatrix(hitMat);

	return true;
}

FHitResult AITwinPopulationTool::FImpl::LineTraceFromMousePos()
{
	FHitResult HitResult;

	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return HitResult;

	FVector traceStart, traceEnd, traceDir;
	if (!playerController->DeprojectMousePositionToWorld(traceStart, traceDir))
		return HitResult;

	traceEnd = traceStart + traceDir * 1e8f;

	TArray<AActor const*> ActorsToIgnore;
	if (toolMode == EPopulationToolMode::RemoveInstances ||	draggedAssetPopulation || bInteractivePlacement)
	{
		// When erasing instances, collisions are enabled. When dragging an instance from the
		// browser, collisions may be enabled depending on the current mode. Existing populations
		// must be explicitly ignored here so that the brush sphere is placed like when painting
		// instances (it avoids rapid jumps).
		AppendAllPopulationConstActors(ActorsToIgnore);
	}
	if (bInteractivePlacement)
	{
		// Splines added for the display of box edges should also be ignored during interactive placement.
		// (In fact all splines, as they are not real physical objects...)
		ITwin::AppendSplineHelpers(ActorsToIgnore, owner.GetWorld());
	}

	FITwinTracingHelper TracingHelper;
	TracingHelper.AddIgnoredActors(ActorsToIgnore);
	TracingHelper.FindNearestImpact(HitResult, owner.GetWorld(), traceStart, traceEnd);

	return HitResult;
}

FVector AITwinPopulationTool::FImpl::LineTraceToSetBrushSize()
{
	FITwinTracingHelper TracingHelper;
	FITwinRayTraceInput TraceInput;
	if (!TracingHelper.GetRayToTraceFromScreenCenter(&owner, TraceInput))
	{
		return FVector::ZeroVector;
	}
	const FVector TraceEnd = TraceInput.TraceStart + (TraceInput.TraceDirection * 1e8f);

	FHitResult HitResult;
	TracingHelper.FindNearestImpact(HitResult, owner.GetWorld(), TraceInput.TraceStart, TraceEnd);

	if (HitResult.HasValidHitObjectHandle() && HitResult.GetActor())
	{
		return HitResult.Location;
	}
	else
	{
		return TraceInput.TraceStart + (TraceInput.TraceDirection * 1e4f);
	}
}

void AITwinPopulationTool::FImpl::MultiLineTraceFromMousePos(
	int32 traceCount, std::vector<AITwinPopulation*> const& populations)
{
	UWorld* World = owner.GetWorld();
	APlayerController* playerController = World ? World->GetFirstPlayerController() : nullptr;

	if (!playerController)
		return;

	FVector traceStart, traceEnd, traceDir;
	if (!playerController->DeprojectMousePositionToWorld(traceStart, traceDir))
		return;

	const ULocalPlayer* LP = playerController->GetLocalPlayer();

	if (!LP || !LP->ViewportClient)
		return;

	FSceneViewProjectionData projectionData;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, projectionData))
		return;

	FMatrix inverseViewRotMat = projectionData.ViewRotationMatrix.Inverse();
	FVector camRight, camUp, camForward;
	inverseViewRotMat.GetUnitAxes(camRight, camUp, camForward);

	// Build a basis
	FVector vZ = traceDir;
	vZ.Normalize();
	FVector vY = camUp;
	FVector vX = vY ^ vZ;
	vZ = vX ^ vY;

	// Estimate the normal of the intersection between the brush sphere and the scene.
	FVector brushPos = GetBrushPosition();
	constexpr int32 numCircles = 3;
	const float brushRadius = GetBrushRadius();
	const float radiusStep = brushRadius / static_cast<float>(numCircles);
	const float SquaredBrushRadius = brushRadius * brushRadius;
	
	FVector averageNormal(0);

	FITwinTracingHelper TracingHelper;

	for (int32 c = 1; c <= numCircles; ++c)
	{
		float currentRadius = radiusStep*static_cast<float>(c);

		int32 numSamples = static_cast<int32>(UE_TWO_PI * static_cast<float>(c));
		float angleStep = UE_TWO_PI / static_cast<float>(numSamples);

		FVector diskAverageNormal(0);
		for (int32 s = 0; s < numSamples; ++s)
		{
			float currentAngle = angleStep*static_cast<float>(s);

			// Compute traceEnd (on the apparent disk of the brush)
			traceEnd = brushPos + (vX*cosf(currentAngle) + vY*sinf(currentAngle))*currentRadius;
			traceDir = (traceEnd - traceStart);
			traceDir.Normalize();
			traceEnd += (traceDir*brushRadius);

			FHitResult HitResult;
			if (!TracingHelper.FindNearestImpact(HitResult, World, traceStart, traceEnd))
				continue;

			if ((HitResult.Location - brushPos).SquaredLength() > SquaredBrushRadius)
				continue;

			diskAverageNormal += HitResult.Normal;
		}

		if (diskAverageNormal.Normalize(1e-6))
		{
			averageNormal += diskAverageNormal;
		}
	}

	if (!averageNormal.Normalize(1e-6))
		return;

	traceDir = -averageNormal;

	// Build a new basis with the average normal as Z
	vZ = averageNormal;
	vX = camRight - (camRight*averageNormal)*averageNormal;
	if (!vX.Normalize(1e-6f))
		return;
	vY = vZ ^ vX;
	if (!vY.Normalize(1e-6f))
		return;

	// Increase the value of traceCount to compensate for the test below
	// which checks if random coordinates are inside the brush disk.
	traceCount = static_cast<int32>(static_cast<float>(traceCount) * 4.f/UE_PI);

	double brushPosStep = 1./static_cast<double>(traceCount);

	for (int32 i = 1; i <= traceCount; ++i)
	{
		float rx = FMath::FRandRange(-1.f, 1.f);
		float ry = FMath::FRandRange(-1.f, 1.f);
		if (sqrtf(rx*rx + ry*ry) > 1.f)
		{
			continue;
		}

		double t = static_cast<double>(i)*brushPosStep;
		FVector interpolatedBrushPos = BrushLastPos*(1.-t) + brushPos*t;

		// Compute traceEnd
		FVector diskPos = interpolatedBrushPos + (vX*rx + vY*ry)*brushRadius;
		traceEnd = diskPos - (averageNormal*brushRadius*2.f);
		traceStart = diskPos + averageNormal*brushRadius;

		FHitResult HitResult;
		if (!TracingHelper.FindNearestImpact(HitResult, World, traceStart, traceEnd))
			continue;

		const float SquaredDistToBrush = (HitResult.Location - interpolatedBrushPos).SquaredLength();
		if (SquaredDistToBrush > SquaredBrushRadius)
		{
			continue;
		}

		int32 popIndex = populations.size() > 1 ?
			FMath::RandRange((int32)0, (int32)populations.size() - 1) : 0;
		AITwinPopulation* Population = populations[popIndex];

		FTransform transform;
		if (ComputeTransformFromHitResult(HitResult, transform, Population))
		{
			int32 const Index = Population->AddInstance(transform);
			// Record first instance index for each population, for undo/redo system.
			if (Index != INDEX_NONE && !BrushAddedInstancesInfo.contains(Population))
			{
				//Track Amplitude event
				PopulationChanged(*Population, EChangeType::Added, TEXT("brush"));

				BrushAddedInstancesInfo.emplace(Population, Index);
			}
#ifndef RELEASE_CONFIG
			BrushAddedInstancesInfo[Population].NumAddedInstances++;
#endif
		}
	}
}

bool AITwinPopulationTool::FImpl::CheckInteractivePlacementAllowance()
{
	if (!ensure(selectedPopulation))
	{
		return false;
	}

	if (InteractivePlacementState != EInteractivePlacementState::Transforming)
	{
		// For the 1st instance, we need to wait for the tree to be fully built: if we freeze the tree update
		// whereas the tree has never been built, it would result in invisible instances most of the time
		// (see ADO#1973505).
		if (selectedInstanceIndex == 0 && !selectedPopulation->IsTreeFullyBuilt())
		{
			InteractivePlacementState = EInteractivePlacementState::DelayTransform;
			return false;
		}
	}

	if (InteractivePlacementState == EInteractivePlacementState::JustStarted
		|| InteractivePlacementState == EInteractivePlacementState::DelayTransform)
	{
		// The tree is now built, we can start the interactive placement.
		InteractivePlacementState = EInteractivePlacementState::Transforming;
		// Disable automatic UE tree rebuild for this population as long as the position is not
		// validated.
		OnSelectionTransformStarted(/*bForInteractivePlacement*/true);
	}
	return true;
}


bool AITwinPopulationTool::FImpl::AddSingleInstanceFromHitResult(const FHitResult& hitResult,
	FCreatedInstance* OutCreatedInstance /*= nullptr*/,
	bool bStartingInteractiveCreation /*= false*/)
{
	using EAddInstanceContext = AITwinPopulation::EAddInstanceContext;

	const bool bForceUpdateEditedPopulations = EditedPopulations.empty();
	auto const& EditedPopulationsActors = GetEditedPopulations(bForceUpdateEditedPopulations);

	if (!EditedPopulationsActors.empty())
	{
		int32 popIndex = EditedPopulationsActors.size() > 1 ?
			FMath::RandRange((int32)0, (int32)EditedPopulationsActors.size() - 1) : 0;
		FTransform tm;
		if (ComputeTransformFromHitResult(hitResult, tm, EditedPopulationsActors[popIndex],
										  false/*bIsDraggingInstance*/,
										  bStartingInteractiveCreation))
		{
			const int32 InstIndex = EditedPopulationsActors[popIndex]->AddInstance(tm,
				bInteractivePlacement ? EAddInstanceContext::InteractivePlacement : EAddInstanceContext::Default);

			if (bInteractivePlacement)
			{
				// Select the new instance to adjust its position interactively.
				SetSelectedPopulation(EditedPopulationsActors[popIndex]);
				SetSelectedInstanceIndex(InstIndex);
				// Disable automatic UE tree rebuild for this population as long as the position is not
				// validated. If the tree is not yet built (for the first instance of a foliage), we delay
				// the start of the transform until the tree is built.
				InteractivePlacementState = EInteractivePlacementState::JustStarted;
				CheckInteractivePlacementAllowance();
			}
			if (OutCreatedInstance)
			{
				OutCreatedInstance->Population = EditedPopulationsActors[popIndex];
				OutCreatedInstance->NewInstanceIndex = InstIndex;
			}
			return true;
		}
	}

	return false;
}

bool AITwinPopulationTool::FImpl::AddSingleInstanceAtViewCenter(bool bStartingInteractiveCreation)
{
	FHitResult HitResult;
	TOptional<FVector> InstancePos = ITwin::GetNewObjectDefaultPosition(&owner, EITwinSplineUsage::Undefined, HitResult);
	if (!InstancePos)
		return false;
	return AddSingleInstanceFromHitResult(HitResult, nullptr, bStartingInteractiveCreation);
}

size_t AITwinPopulationTool::FImpl::CollectEditedPopulations()
{
	if (!decorationHelper)
	{
		return 0;
	}
	if (!instanceGroupId.IsValid())
	{
		instanceGroupId = decorationHelper->GetStaticInstancesGroupId();
	}

	EditedPopulations.clear();

	for (auto& asset : usedAssets)
	{
		if (asset.second)
		{
			AITwinPopulation* population = decorationHelper->GetOrCreatePopulation(asset.first, instanceGroupId);
			if (population)
				EditedPopulations.push_back(population);
		}
	}

	return EditedPopulations.size();
}


void AITwinPopulationTool::FImpl::UpdatePopulationsCollisionType() const
{
	ECollisionEnabled::Type collisionType = ECollisionEnabled::NoCollision;

	if (enabled &&
		(toolMode == EPopulationToolMode::Select ||
		 toolMode == EPopulationToolMode::RemoveInstances))
	{
		collisionType = ECollisionEnabled::QueryOnly;
	}

	for (auto actor : GetAllPopulations())
	{
		Cast<AITwinPopulation>(actor)->SetCollisionEnabled(collisionType);
	}
}

void AITwinPopulationTool::FImpl::RestrictPickingOnClippingPrimitives(bool bInRestrictPickingOnClipping /*= true*/)
{
	bRestrictPickingOnClipping = bInRestrictPickingOnClipping;

	if (bRestrictPickingOnClipping)
	{
		// Make sure all clipping primitives can be picked.
		UpdatePopulationsArray();
		for (auto actor : GetAllPopulations())
		{
			AITwinPopulation* Population = Cast<AITwinPopulation>(actor);
			if (Population->IsClippingPrimitive())
				Population->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}
}

AITwinPopulation* AITwinPopulationTool::FImpl::PreLoadPopulation(const FString& AssetPath)
{
	if (!decorationHelper)
	{
		return nullptr;
	}
	if (!instanceGroupId.IsValid())
	{
		instanceGroupId = decorationHelper->GetStaticInstancesGroupId();
	}
	return decorationHelper->GetOrCreatePopulation(AssetPath, instanceGroupId);
}

void AITwinPopulationTool::FImpl::SetUsedOnCutout(bool bForCutout)
{
	bEnabledForCutout = bForCutout;
}

void AITwinPopulationTool::FImpl::SetInstanceTransformProxy(IITwinPopulationInstanceTransformProxyPtr InTransformProxy)
{
	TransformProxy = InTransformProxy;
}


void AITwinPopulationTool::FImpl::UpdatePopulationsArray()
{
	AllPopulations.Empty();
	UGameplayStatics::GetAllActorsOfClass(
		owner.GetWorld(), AITwinPopulation::StaticClass(), AllPopulations);
	bNeedsUpdateAllPopulations = false;
}

void AITwinPopulationTool::FImpl::StartDragging(AITwinPopulation* population)
{
	draggedAssetPopulation = population;
	if (draggedAssetPopulation)
	{
		// Optimization: avoid rebuilding UE hierarchical tree while adjusting the new instance position.
		BE_ASSERT(!DraggedPopTreeUpdateDisabler.has_value(), "Lost ReleaseDraggedAssetInstance event?");
		DraggedPopTreeUpdateDisabler.reset();
		DraggedPopTreeUpdateDisabler.emplace(*draggedAssetPopulation);
	}
	DraggingRotVar = FMath::FRandRange(InstancesRotationVariationMin, InstancesRotationVariationMax);
	DraggingScaleVar = FMath::FRandRange(InstancesScaleVariationMin, InstancesScaleVariationMax);
	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();
}

void AITwinPopulationTool::FImpl::DeleteInstanceFromPopulation(
	AITwinPopulation*& population, int32& instanceIndex)
{
	if (population)
	{
		PopulationChanged(*population, EChangeType::Deleted, TEXT("key_down"));

		if (instanceIndex >= 0)
		{
			population->RemoveInstance(instanceIndex);
		}
		if (population->GetNumberOfInstances() == 0)
		{
			population->Destroy();
			InvalidatePopulations();
		}
		population = nullptr;
		instanceIndex = INDEX_NONE;
	}
}

namespace
{
	bool IsSplineUsedForPopulation(AITwinSplineHelper* SplineHelper)
	{
		return SplineHelper && ITwinSpline::IsPopulation(SplineHelper->GetUsage());
	}
}

TWeakObjectPtr<AITwinSplineTool> AITwinPopulationTool::FImpl::ActivateSplineTool(UWorld* World, EITwinPopulationType PopulationType)
{
	ensure(SplineTool.IsValid());
	return ITwin::ActivateSplineTool(World, PopulationType == EITwinPopulationType::Area? EITwinSplineUsage::PopulationZone : EITwinSplineUsage::PopulationPath, SplineTool);
}

void AITwinPopulationTool::FImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	ensure(SplineTool.IsValid());
	ITwin::SelectSpline(SplineHelper, INDEX_NONE, World, SplineTool);
}

void AITwinPopulationTool::FImpl::SetAllPopulationProxiesVisibility(bool bVisibleInGame)
{
	for (EITwinPopulationType PopType : TEnumRange<EITwinPopulationType>())
	{
		SetPopulationProxyVisibility(PopType, bVisibleInGame);
	}
}

void AITwinPopulationTool::FImpl::PopulateSpline()
{
	if (!owner.SelectedSpline.IsValid())
		return;
	PopulateSpline(*owner.SelectedSpline, /*bFinalEdit*/true);
}

void AITwinPopulationTool::FImpl::UpdateGroupId(AITwinSplineHelper const* CurSpline)
{
	if (!ensure(decorationHelper))
		return;
	
	AdvViz::SDK::RefID newGroupId = AdvViz::SDK::RefID::Invalid();
	if (CurSpline == nullptr)
	{
		newGroupId = decorationHelper->GetStaticInstancesGroupId();
	}
	else
	{
		auto itGroupId = splineToGroupId.find(CurSpline);
		if (itGroupId == splineToGroupId.end())
		{
			// Initiate a new group for this spline
			newGroupId = decorationHelper->GetInstancesGroupIdForSpline(*CurSpline);
			splineToGroupId.emplace(CurSpline, newGroupId); 
		}
		else
		{
			newGroupId = itGroupId->second;
		}
	}

	// If a change is detected, we must collect populations again (as they depend the group ID.
	if (instanceGroupId != newGroupId)
	{
		instanceGroupId = newGroupId;
		CollectEditedPopulations();
	}
}

FString AITwinPopulationTool::FImpl::GetTransformationName()
{
	switch (transformationMode)
	{
	case ETransformationMode::Move:
		return TEXT("position");
	case ETransformationMode::Rotate:
		return TEXT("rotation");
	case ETransformationMode::Scale:
		return TEXT("scale");
	}
	return TEXT("");
}

void AITwinPopulationTool::FImpl::PopulationChanged(AITwinPopulation& population, EChangeType change, const FString& eventSource)
{
	FFeatureEventProperties properties;
	properties.ChangeType = change;

	if (population.IsClippingPrimitive())
	{
		properties.FeatureType = EFeatureType::Clipping;
		properties.AddProperty(TEXT("cutout_type"), population.GetObjectTypeName());
		if (change == EChangeType::Modified)
		{
			properties.AddProperty(TEXT("cutout_setting"), GetTransformationName());
		}
	}
	else
	{
		properties.FeatureType = EFeatureType::PopulationObject;
		std::string componentName = population.GetObjectRef();
		componentName = componentName.substr(componentName.find_last_of("/") + 1);
		properties.AddProperty(TEXT("component_name"), UTF8_TO_TCHAR(componentName.c_str()));
		properties.AddProperty(TEXT("component_category"), population.GetObjectTypeName());
		if (change == EChangeType::Modified)
		{
			properties.AddProperty(TEXT("object_setting"), GetTransformationName());
		}
	}
	properties.AddProperty(TEXT("event_source"), eventSource);
	owner.PopulationChangedEvent.Broadcast(properties);
}

void AITwinPopulationTool::FImpl::SplinePopulationChanged(EITwinPopulationType PopulationType, EChangeType change,
	const FString& eventSource /*= FString()*/, const FString& modificationKind /*= FString()*/)
{
	FFeatureEventProperties properties;
	properties.ChangeType = change;
	properties.FeatureType = EFeatureType::PopulationSpline;

	FString population_type;
	switch (PopulationType)
	{
	case EITwinPopulationType::Area: population_type = TEXT("area"); break;
	case EITwinPopulationType::Path: population_type = TEXT("path"); break;
	default: break;
	}
	properties.AddProperty(TEXT("population_type"), population_type);
	if (!eventSource.IsEmpty())
	{
		properties.AddProperty(TEXT("event_source"), eventSource);
	}
	if (!modificationKind.IsEmpty())
	{
		properties.AddProperty(TEXT("population_setting"), modificationKind);
	}

	owner.PopulationChangedEvent.Broadcast(properties);
}

void AITwinPopulationTool::FImpl::PopulateSpline(AITwinSplineHelper const& TargetSpline, bool bFinalEdit /*= true*/)
{
	if (!TargetSpline.GetSplineComponent())
		return;

	FSplinePopulationState& State = SplinePopulationStates[TargetSpline.GetAVizSplineId()];

	if (State.RunningJob && !State.RunningJob->bDone)
	{
		if (bFinalEdit)
		{
			// The edition is finished: discard the computation in progress (its result would be obsolete
			// anyway) and force a new computation from the final state of the spline.
			State.RunningJob->bCancelled = true;
			State.RunningJob.reset();
			State.bPendingRequest = false;
		}
		else
		{
			// Interactive edition: keep the current computation running, and remember that a new one should
			// be launched as soon as it is over (all intermediate requests are coalesced into one).
			State.bPendingRequest = true;
			return;
		}
	}
	State.bPendingRequest = false;
	StartSplinePopulationJob(TargetSpline, State);
}

bool AITwinPopulationTool::FImpl::StartSplinePopulationJob(AITwinSplineHelper const& TargetSpline,
	FSplinePopulationState& State)
{
	UpdateGroupId(&TargetSpline);

	FPopulationIdentifier popHandle = GetPopulationIdentifierFromSpline(TargetSpline.GetAVizSplineId());
	auto popHelper = GetMutablePopulationInfo(popHandle);
	if (!popHelper)
		return false;

	// Note: the existing instances are kept visible until the new result is ready (they are removed in
	// ApplySplinePopulationJob).

	ClearUsedAssets();
	for (auto assetPath : popHelper->Get3DObjects())
	{
		if (!assetPath.IsEmpty())
			SetUsedAsset(assetPath, true);
	}

	const bool bForceUpdateEditedPopulations = EditedPopulations.empty();
	auto const& EditedPopulationsActors = GetEditedPopulations(bForceUpdateEditedPopulations);

	// When an asset is used for the first time in the application, its component may still be
	// downloading/mounting: in such case, AITwinDecorationHelper::GetOrCreatePopulation returns null
	// (the population actor is created later, once the component is available), and the asset is thus
	// missing from EditedPopulationsActors. Schedule a retry of the whole population as soon as all
	// population actors exist, so that the user does not have to trigger a new computation by hand.
	{
		TArray<FString> PendingAssets;
		for (auto const& assetPath : popHelper->Get3DObjects())
		{
			if (!assetPath.IsEmpty() && !decorationHelper->GetPopulation(assetPath, instanceGroupId))
				PendingAssets.Add(assetPath);
		}
		if (!PendingAssets.IsEmpty())
		{
			TWeakObjectPtr<AITwinPopulationTool> weakOwner(&owner);
			TWeakObjectPtr<AITwinSplineHelper const> weakSpline(&TargetSpline);
			AdvViz::SDK::RefID const GroupId = instanceGroupId;
			std::string const delayedCallId =
				"RetryPopulateSplineOnAssetLoaded_" + std::to_string(TargetSpline.GetAVizSplineId().ID());
			AdvViz::SDK::UniqueDelayedCall(delayedCallId,
				[weakOwner, weakSpline, GroupId, PendingAssets = MoveTemp(PendingAssets)]()
				-> AdvViz::SDK::DelayedCall::EReturnedValue
				{
					if (!weakOwner.IsValid() || !weakSpline.IsValid() || !weakOwner->Impl->decorationHelper)
						return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
					auto& Impl = *weakOwner->Impl;
					for (FString const& assetPath : PendingAssets)
					{
						// Use GetPopulation (and not GetOrCreatePopulation) to avoid re-triggering downloads.
						if (!Impl.decorationHelper->GetPopulation(assetPath, GroupId))
							return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
					}
					// Force re-collection of edited populations, so that the newly created ones are used.
					Impl.EditedPopulations.clear();
					Impl.PopulateSpline(*weakSpline, /*bFinalEdit*/true);
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
				},
				0.25f);
		}
	}

	if (EditedPopulationsActors.empty())
	{
		// Nothing can be populated (yet): clear obsolete instances, if any.
		if (popHelper->Populations.Num() > 0)
			RemovePopulationObjects(popHelper);
		return false;
	}

	auto Job = std::make_shared<FSplinePopulationJob>();
	Job->Spline = &TargetSpline;

	BeUtils::SplineSamplingParameters& SamplingParams = Job->SamplingParams;
	SamplingParams.samplingMode = (TargetSpline.GetUsage() == EITwinSplineUsage::PopulationZone)
		? BeUtils::ESplineSamplingMode::Interior
		: BeUtils::ESplineSamplingMode::AlongPath;
	SamplingParams.density = popHelper->GetDensity();
	SamplingParams.forceAligned = popHelper->GetMode() == EITwinSplinePopulationMode::Regular 
								  || TargetSpline.GetUsage() == EITwinSplineUsage::PopulationPath;
	SamplingParams.gridRotAngle = FMath::DegreesToRadians(popHelper->GetGridRotation());
	SamplingParams.forbidOverlap = popHelper->IsAvoidOverlapping();

	// Use CalcBounds directly, it works in all build configurations 
	// (unlike GetActorBounds which doesn't seem to work properly in shipping, only works in editor for some reason).
	USplineComponent const* SplineComp = TargetSpline.GetSplineComponent();
	FBoxSphereBounds SplineBounds = SplineComp->CalcBounds(SplineComp->GetComponentTransform());
	FVector BoundsMin = SplineBounds.Origin - SplineBounds.BoxExtent;
	FVector BoundsMax = SplineBounds.Origin + SplineBounds.BoxExtent;

	BeUtils::BoundingBox& SamplingBox = Job->SamplingBox;
	SamplingBox.min[0] = BoundsMin.X;
	SamplingBox.min[1] = BoundsMin.Y;
	SamplingBox.min[2] = BoundsMin.Z;
	SamplingBox.max[0] = BoundsMax.X;
	SamplingBox.max[1] = BoundsMax.Y;
	SamplingBox.max[2] = BoundsMax.Z;
	Job->BoundsMaxZ = BoundsMax.Z;

	glm::dvec3 AccumBBoxDims(0.0);
	for (AITwinPopulation const* Population : EditedPopulationsActors)
	{
		const FVector BoxSize = Population->GetMasterMeshBounds().GetBox().GetSize();
		AccumBBoxDims += glm::dvec3(BoxSize.X, BoxSize.Y, BoxSize.Z);
	}
	glm::dvec3 const AverageInstanceDims = AccumBBoxDims / (double)EditedPopulationsActors.size();
	Job->AverageInstanceDims = AverageInstanceDims;

	if (SamplingParams.forceAligned) {
		if (!popHelper->IsSpacingRandomized())
		{
			const double distance = popHelper->GetDistance() * 100.0;
			if (distance > 0.)
				SamplingParams.fixedSpacing = glm::dvec2(distance);
			else if (SamplingParams.samplingMode == BeUtils::ESplineSamplingMode::AlongPath)
				SamplingParams.fixedSpacing = glm::dvec2(AverageInstanceDims.x);
		}
		else if (popHelper->IsSpacingRandomized())
			SamplingParams.randomSpacing = glm::dvec2(popHelper->GetDistanceRange().GetLowerBoundValue(), popHelper->GetDistanceRange().GetUpperBoundValue()) * 100.0;
	}

	// Snapshot the spline (the live USplineComponent must not be accessed from the worker thread).
	Job->Curve = MakeShared<FITwinSplineSnapshotCurve>(*SplineComp);

	State.RunningJob = Job;

	BE_LOGD("ITwinPopulation", "Starting async population of spline " << TargetSpline.GetAVizSplineId().ID());

	// Phase 2: sampling on a worker thread. The job is kept alive by the lambda; it is only read from
	// the game thread once bDone is set.
	Async(EAsyncExecution::ThreadPool, [Job]()
	{
		if (!Job->bCancelled)
		{
			// The transformation to world is "baked" in the snapshot curve.
			BeUtils::TransformHolder const IdentityTsf;
			BeUtils::SampleSpline(*Job->Curve, IdentityTsf, Job->SamplingBox, Job->AverageInstanceDims,
				Job->SamplingParams, Job->Positions);
		}
		Job->bDone = true;
	});
	return true;
}

void AITwinPopulationTool::FImpl::TickSplinePopulationJobs()
{
	if (SplinePopulationStates.empty())
		return;

	for (auto It = SplinePopulationStates.begin(); It != SplinePopulationStates.end(); )
	{
		FSplinePopulationState& State = It->second;
		if (!State.RunningJob)
		{
			It = SplinePopulationStates.erase(It);
			continue;
		}
		if (!State.RunningJob->bDone)
		{
			++It;
			continue;
		}
		std::shared_ptr<FSplinePopulationJob> Job = std::move(State.RunningJob);
		State.RunningJob.reset();

		if (!Job->bCancelled && Job->Spline.IsValid())
		{
			ApplySplinePopulationJob(*Job);
		}

		if (State.bPendingRequest && Job->Spline.IsValid())
		{
			// An edition occurred while we were computing: launch the coalesced request now.
			State.bPendingRequest = false;
			StartSplinePopulationJob(*Job->Spline, State);
			++It;
		}
		else
		{
			It = SplinePopulationStates.erase(It);
		}
	}
}

void AITwinPopulationTool::FImpl::CancelSplinePopulationJobs(AdvViz::SDK::RefID const* SplineId /*= nullptr*/)
{
	auto CancelState = [](FSplinePopulationState& State)
	{
		if (State.RunningJob)
			State.RunningJob->bCancelled = true;
		State.RunningJob.reset();
		State.bPendingRequest = false;
	};
	if (SplineId)
	{
		auto It = SplinePopulationStates.find(*SplineId);
		if (It != SplinePopulationStates.end())
		{
			CancelState(It->second);
			SplinePopulationStates.erase(It);
		}
	}
	else
	{
		for (auto& [Id, State] : SplinePopulationStates)
			CancelState(State);
		SplinePopulationStates.clear();
	}
}

void AITwinPopulationTool::FImpl::ApplySplinePopulationJob(FSplinePopulationJob& Job)
{
	AITwinSplineHelper const& TargetSpline = *Job.Spline;
	USplineComponent const* SplineComp = TargetSpline.GetSplineComponent();
	if (!SplineComp)
		return;

	UpdateGroupId(&TargetSpline);

	FPopulationIdentifier popHandle = GetPopulationIdentifierFromSpline(TargetSpline.GetAVizSplineId());
	auto popHelper = GetMutablePopulationInfo(popHandle);
	if (!popHelper)
		return;

	// Now that the new result is ready, remove all instances previously populated on this spline.
	if (popHelper->Populations.Num() > 0)
		RemovePopulationObjects(popHelper);

	// The used assets may have changed while the job was running (eg. another spline was selected):
	// re-target them to this spline's objects.
	ClearUsedAssets();
	for (auto const& assetPath : popHelper->Get3DObjects())
	{
		if (!assetPath.IsEmpty())
			SetUsedAsset(assetPath, true);
	}
	auto const& EditedPopulationsActors = GetEditedPopulations(/*bForceUpdateArray*/true);
	if (EditedPopulationsActors.empty())
		return;

	std::vector<glm::dvec3> const& Positions = Job.Positions;
	if (Positions.empty())
		return;

	uint32 NumAddedInstances = 0;
	// Project sampled spline position onto scene.
	UWorld const* World = owner.GetWorld();
	float const ZStart = static_cast<float>(Job.BoundsMaxZ + 1e5);
	FVector const TraceDir = FVector::DownVector;
	FITwinTracingHelper TracingHelper;
	UpdatePopulationsArray();
	TracingHelper.AddIgnoredActors(GetAllPopulations());
	TArray<AActor const*> ActorsToIgnore;
	if (owner.GizmoActorClass)
	{
		ActorsToIgnore.Add(UGameplayStatics::GetActorOfClass(World, owner.GizmoActorClass));
	}
	ITwin::AppendSplineHelpers(ActorsToIgnore, World);
	TracingHelper.AddIgnoredActors(ActorsToIgnore);
	InstancesRotationVariation = FMath::DegreesToRadians(popHelper->GetRotation());
	InstancesScaleVariation = popHelper->GetScale();
	InstancesRotationVariationMin = InstancesRotationVariation;
	InstancesRotationVariationMax = InstancesRotationVariation;
	InstancesScaleVariationMin = InstancesScaleVariation;
	InstancesScaleVariationMax = InstancesScaleVariation;
	if (popHelper->IsRotationRandomized())
	{
		InstancesRotationVariationMin = FMath::DegreesToRadians(popHelper->GetRotationRange().GetLowerBoundValue());
		InstancesRotationVariationMax = FMath::DegreesToRadians(popHelper->GetRotationRange().GetUpperBoundValue());
		InstancesRotationVariation = FMath::Abs(InstancesRotationVariationMax - InstancesRotationVariationMin);
	}
	if (popHelper->IsScaleRandomized())
	{
		InstancesScaleVariationMin = popHelper->GetScaleRange().GetLowerBoundValue();
		InstancesScaleVariationMax = popHelper->GetScaleRange().GetUpperBoundValue();
		InstancesScaleVariation = FMath::Abs(InstancesScaleVariationMax - InstancesScaleVariationMin);
	}
	for (glm::dvec3 const& SplinePos : Positions)
	{
		// Project spline position onto ground
		FVector TraceStart = { SplinePos.x, SplinePos.y, ZStart };
		FVector TraceEnd = TraceStart + TraceDir * 1e8f;
		FHitResult HitResult;
		if (!TracingHelper.FindNearestImpact(HitResult, World, TraceStart, TraceEnd))
			continue;

		int32 popIndex = EditedPopulationsActors.size() > 1 ?
			FMath::RandRange((int32)0, (int32)EditedPopulationsActors.size() - 1) : 0;
		AITwinPopulation* Population = EditedPopulationsActors[popIndex];

		FTransform InstTransform;
		if (TargetSpline.GetUsage() == EITwinSplineUsage::PopulationZone 
			|| TargetSpline.GetUsage() == EITwinSplineUsage::PopulationPath)
			Population->SetSplinePopulation(true);
		if (ComputeTransformFromHitResult(HitResult, InstTransform, Population))
		{
			// For path populations in Relative rotation mode, orient the instance along the spline tangent.
			if (TargetSpline.GetUsage() == EITwinSplineUsage::PopulationPath
				&& popHelper->GetRotationMode() == EITwinPathPopulationRotationMode::Relative)
			{
				// Find the closest input key on the spline to get the tangent direction.
				FVector InstanceLocation = InstTransform.GetTranslation();
				float ClosestInputKey = SplineComp->FindInputKeyClosestToWorldLocation(InstanceLocation);
				FVector SplineTangent = SplineComp->GetTangentAtSplineInputKey(ClosestInputKey, ESplineCoordinateSpace::World);
				SplineTangent.Z = 0.;
				if (SplineTangent.Normalize(1e-6))
				{
					FQuat::FReal SplineYaw = FMath::Atan2(SplineTangent.Y, SplineTangent.X) + UE_HALF_PI;
					FQuat CurrentRotation = InstTransform.GetRotation();
					FQuat SplineRotation(FVector::ZAxisVector, SplineYaw);
					FQuat FinalRotation = SplineRotation * CurrentRotation;
					InstTransform.SetRotation(FinalRotation);
				}
			}
			Population->AddInstance(InstTransform);
			popHelper->Populations.Add(Population);
			NumAddedInstances++;
		}
	}
	InstancesRotationVariation = UE_DOUBLE_PI;
	InstancesScaleVariation = 0.2;
	InstancesRotationVariationMin = -InstancesRotationVariation;
	InstancesRotationVariationMax = InstancesRotationVariation;
	InstancesScaleVariationMin = -InstancesScaleVariation;
	InstancesScaleVariationMax = InstancesScaleVariation;

	BE_LOGD("ITwinPopulation", "Async population of spline " << TargetSpline.GetAVizSplineId().ID()
		<< " applied: " << NumAddedInstances << " instance(s)");

	owner.SelectedPopulationModifiedEvent.Broadcast();
}

UITwinPopulationHelper* AITwinPopulationTool::FImpl::CreatePop(EITwinPopulationType PopType)
{
	switch (PopType)
	{
	case EITwinPopulationType::Area:
	{
		TStrongObjectPtr<UITwinAreaPopulationHelper> AreaInfo(NewObject<UITwinAreaPopulationHelper>(&owner));
		AreaInfos.Add(AreaInfo);
		return AreaInfo.Get();
	}
	case EITwinPopulationType::Path:
	{
		TStrongObjectPtr<UITwinPathPopulationHelper> PathInfo(NewObject<UITwinPathPopulationHelper>(&owner));
		PathInfos.Add(PathInfo);
		return PathInfo.Get();
	}
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinPopulationType::Count:, nullptr);
	}
}

bool AITwinPopulationTool::FImpl::RegisterPopulationSpline(AITwinSplineHelper* SplineHelper)
{
	// This function is called when a new spline is created with the Spline Tool, and also when an existing spline
	// is loaded from the server. If we are loading an existing population spline from server, we should wait until
	// all the populations are loaded before registering them here
	if (SplineHelper->GetAVizSplineId().HasDBIdentifier())
		return false;

	// Check whether it's actually a new spline and not interactive edition of an existing one
	if (GetPopulationIdentifierFromSpline(SplineHelper->GetAVizSplineId()).IsValid())
		return false;

	FPopulationIdentifier PopHandle;
	PopHandle.PopulationType = GetPopulationTypeFromSplineUsage(SplineHelper->GetUsage());
	PopHandle.PopulationIndex = NumPopulations(PopHandle.PopulationType);

	UITwinPopulationHelper* PopHelper = CreatePop(PopHandle.PopulationType);
	if (!PopHelper)
		return false;

	auto PopPropPtr = populationManager->AddPopulationInfo();
	auto PopProp = PopPropPtr->GetAutoLock();
	PopProp->SetSplineId(SplineHelper->GetAVizSplineId());
	PopProp->SetShouldSave(true);
	PopHelper->Init(SplineHelper, PopPropPtr);

	PopulateSpline(*SplineHelper, /*bFinalEdit*/true);

	SplinePopulationChanged(PopHandle.PopulationType, EChangeType::Added);

	owner.PopulationListModifiedEvent.Broadcast();
	owner.PopulationAddedEvent.Broadcast(PopHandle);
	return true;
}

// -----------------------------------------------------------------------------
//                            AITwinPopulationTool

AITwinPopulationTool::AITwinPopulationTool()
	: AITwinInteractiveTool(), Impl(MakePimpl<FImpl>(*this))
{
	PrimaryActorTick.bCanEverTick = true; // needed for the brush
}

EPopulationToolMode AITwinPopulationTool::GetMode() const
{
	return Impl->GetMode();
}

void AITwinPopulationTool::SetMode(EPopulationToolMode mode)
{
	Impl->SetMode(mode);
}

ETransformationMode AITwinPopulationTool::GetTransformationMode() const
{
	if (Impl->GetSelectedSplinePopulation())
	{
		return ETransformationMode::Move;
	}
	return Impl->GetTransformationMode();
}

void AITwinPopulationTool::SetTransformationMode(ETransformationMode mode)
{
	Impl->SetTransformationMode(mode);
}

AITwinPopulation* AITwinPopulationTool::GetSelectedPopulation() const
{
	return Impl->GetSelectedPopulation();
}

int32 AITwinPopulationTool::GetSelectedInstanceIndex() const
{
	return Impl->GetSelectedInstanceIndex();
}

void AITwinPopulationTool::SetSelectedPopulation(AITwinPopulation* population)
{
	Impl->SetSelectedPopulation(population);
}

void AITwinPopulationTool::SetSelectedInstanceIndex(int32 instanceIndex)
{
	Impl->SetSelectedInstanceIndex(instanceIndex);
}

bool AITwinPopulationTool::HasSelectedPopulation() const
{
	return Impl->HasSelectedPopulation();
}

bool AITwinPopulationTool::HasSelectedInstance() const
{
	return Impl->HasSelectedInstance();
}

bool AITwinPopulationTool::HasSelectionImpl() const
{
	return Impl->HasSelectedInstance() || GetSelectedSplinePopulation();
}

void AITwinPopulationTool::DeleteSelectionImpl()
{
	Impl->DeleteSelectedInstance();
}

void AITwinPopulationTool::DeleteSelectedInstance()
{
	Impl->DeleteSelectedInstance();
}

bool AITwinPopulationTool::IsPopulationModeActivated() const
{
	return Impl->IsPopulationModeActivated();
}

bool AITwinPopulationTool::IsBrushModeActivated() const
{
	return Impl->IsBrushModeActivated();
}

void AITwinPopulationTool::StartBrushingInstances()
{
	Impl->StartBrushingInstances();
}

void AITwinPopulationTool::EndBrushingInstances()
{
	Impl->EndBrushingInstances();
}

void AITwinPopulationTool::ShowBrushSphere()
{
	Impl->ShowInstanceBrush();
}

void AITwinPopulationTool::HideBrushSphere()
{
	Impl->HideBrushSphere();
}

float AITwinPopulationTool::GetBrushFlow() const
{
	return Impl->GetBrushFlow();
}

void AITwinPopulationTool::SetBrushFlow(float flow)
{
	if (Impl->IsBrushModeActivated())
		Impl->SetBrushFlow(flow);
}

float AITwinPopulationTool::GetBrushSize() const
{
	return Impl->GetBrushRadius();
}

void AITwinPopulationTool::SetBrushSize(float size)
{
	Impl->SetBrushSize(size);
}

float AITwinPopulationTool::GetDensity(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetDensity();
	return 0.f;
}

void AITwinPopulationTool::SetDensity(FPopulationIdentifier PopHandle, float Density)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetDensity(Density);
		PopulateSpline(*PopulationInfo->SplineHelper);
	}
}

float AITwinPopulationTool::GetScale(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area || PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetScale();
	return 0.f;
}

void AITwinPopulationTool::SetScale(FPopulationIdentifier PopHandle, float Scale)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area || PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetScale(Scale);
		if (PopHandle.PopulationType == EITwinPopulationType::Path)
		{
			Impl->ApplyScaleRangeToPopulationInstances(PopulationInfo, FFloatRange(Scale, Scale));
		}
		else
		{
			PopulateSpline(*PopulationInfo->SplineHelper);
		}
	}
}

float AITwinPopulationTool::GetRotation(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area || PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetRotation();
	return 0.f;
}

void AITwinPopulationTool::FImpl::ApplyScaleRangeToPopulationInstances(
	UITwinPopulationHelper* PopHelper, const FFloatRange& NewRange)
{
	if (!PopHelper)
		return;

	const float NewMin = NewRange.GetLowerBoundValue();
	const float NewMax = NewRange.GetUpperBoundValue();

	for (auto PopulationWeak : PopHelper->Populations)
	{
		AITwinPopulation* Population = PopulationWeak.Get();
		if (!Population)
			continue;

		if (!Population->IsScaleVariationEnabled())
			continue;

		const bool bIsSplinePop = Population->IsSplinePopulation();
		const int32 NumInstances = Population->GetNumberOfInstances();
		for (int32 i = 0; i < NumInstances; ++i)
		{
			FTransform InstTransform = Population->GetInstanceTransform(i);

			const FVector::FReal NewScaleVar = FMath::FRandRange(NewMin, NewMax);
			const FVector::FReal NewScaleValue = bIsSplinePop ? NewScaleVar : 1. + NewScaleVar;

			InstTransform.SetScale3D(FVector(NewScaleValue));
			Population->SetInstanceTransform(i, InstTransform);
		}
	}
}

void AITwinPopulationTool::FImpl::ApplyRotationRangeToPopulationInstances(
	UITwinPopulationHelper* PopHelper, const FFloatRange& NewRangeDeg)
{
	if (!PopHelper)
		return;

	const FQuat::FReal NewMin = FMath::DegreesToRadians(NewRangeDeg.GetLowerBoundValue());
	const FQuat::FReal NewMax = FMath::DegreesToRadians(NewRangeDeg.GetUpperBoundValue());

	const bool bRelativeMode = PopHelper->GetRotationMode() == EITwinPathPopulationRotationMode::Relative;
	USplineComponent const* SplineComp = (bRelativeMode && PopHelper->SplineHelper.IsValid())
		? PopHelper->SplineHelper->GetSplineComponent()
		: nullptr;

	for (auto PopulationWeak : PopHelper->Populations)
	{
		AITwinPopulation* Population = PopulationWeak.Get();
		if (!Population)
			continue;

		if (!Population->IsRotationVariationEnabled())
			continue;

		const int32 NumInstances = Population->GetNumberOfInstances();
		for (int32 i = 0; i < NumInstances; ++i)
		{
			FTransform InstTransform = Population->GetInstanceTransform(i);
			FQuat CurrentRotation = InstTransform.GetRotation();

			const FVector UpAxis = CurrentRotation.GetUpVector();

			FVector sX = FVector::XAxisVector;
			if (FMath::Abs(sX | UpAxis) > 0.8)
				sX = FVector::YAxisVector;
			FVector sY = (UpAxis ^ sX).GetSafeNormal();
			sX = sY ^ UpAxis;
			FQuat SurfaceAlignment(FMatrix(sX, sY, UpAxis, FVector::ZeroVector));

			const FQuat::FReal NewRotVar = FMath::FRandRange(NewMin, NewMax);
			FQuat NewRotation = FQuat(UpAxis, NewRotVar) * SurfaceAlignment;

			if (SplineComp)
			{
				FVector InstanceLocation = InstTransform.GetTranslation();
				float ClosestInputKey = SplineComp->FindInputKeyClosestToWorldLocation(InstanceLocation);
				FVector SplineTangent = SplineComp->GetTangentAtSplineInputKey(ClosestInputKey, ESplineCoordinateSpace::World);
				SplineTangent.Z = 0.;
				if (SplineTangent.Normalize(1e-6))
				{
					FQuat::FReal SplineYaw = FMath::Atan2(SplineTangent.Y, SplineTangent.X) + UE_HALF_PI;
					FQuat SplineRotation(FVector::ZAxisVector, SplineYaw);
					NewRotation = SplineRotation * NewRotation;
				}
			}

			InstTransform.SetRotation(NewRotation.GetNormalized());
			Population->SetInstanceTransform(i, InstTransform);
		}
	}
}

void AITwinPopulationTool::SetRotation(FPopulationIdentifier PopHandle, float Rotation)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area || PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetRotation(Rotation);
		if (PopHandle.PopulationType == EITwinPopulationType::Path)
		{
			Impl->ApplyRotationRangeToPopulationInstances(PopulationInfo, FFloatRange(Rotation, Rotation));
		}
		else
		{
			PopulateSpline(*PopulationInfo->SplineHelper);
		}
	}
}

void AITwinPopulationTool::FImpl::ApplyRotationModeChange(
	UITwinPopulationHelper* PopHelper,
	EITwinPathPopulationRotationMode OldMode,
	EITwinPathPopulationRotationMode NewMode)
{
	if (!PopHelper || !PopHelper->SplineHelper.IsValid())
		return;

	USplineComponent const* SplineComp = PopHelper->SplineHelper->GetSplineComponent();
	if (!SplineComp)
		return;

	const bool bAddTangent = (NewMode == EITwinPathPopulationRotationMode::Relative);

	for (auto PopulationWeak : PopHelper->Populations)
	{
		AITwinPopulation* Population = PopulationWeak.Get();
		if (!Population || !Population->IsRotationVariationEnabled())
			continue;

		const int32 NumInstances = Population->GetNumberOfInstances();
		for (int32 i = 0; i < NumInstances; ++i)
		{
			FTransform InstTransform = Population->GetInstanceTransform(i);

			FVector InstanceLocation = InstTransform.GetTranslation();
			float ClosestInputKey = SplineComp->FindInputKeyClosestToWorldLocation(InstanceLocation);
			FVector SplineTangent = SplineComp->GetTangentAtSplineInputKey(ClosestInputKey, ESplineCoordinateSpace::World);
			SplineTangent.Z = 0.;
			if (!SplineTangent.Normalize(1e-6))
				continue;

			FQuat::FReal SplineYaw = FMath::Atan2(SplineTangent.Y, SplineTangent.X) + UE_HALF_PI;
			FQuat SplineRotation(FVector::ZAxisVector, bAddTangent ? SplineYaw : -SplineYaw);

			FQuat CurrentRotation = InstTransform.GetRotation();
			InstTransform.SetRotation((SplineRotation * CurrentRotation).GetNormalized());
			Population->SetInstanceTransform(i, InstTransform);
		}
	}
}

float AITwinPopulationTool::GetGridRotation(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetGridRotation();
	return 0.f;
}

void AITwinPopulationTool::SetGridRotation(FPopulationIdentifier PopHandle, float GridRotation)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetGridRotation(GridRotation);
		PopulateSpline(*PopulationInfo->SplineHelper);
	}
}

float AITwinPopulationTool::GetDistance(FPopulationIdentifier PopHandle) const
{
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetDistance();
	return 0.f;
}

void AITwinPopulationTool::SetDistance(FPopulationIdentifier PopHandle, float Distance)
{
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetDistance(Distance);
		PopulateSpline(*PopulationInfo->SplineHelper);
	}
}

FFloatRange AITwinPopulationTool::GetRangeDistance(FPopulationIdentifier PopHandle) const
{
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetDistanceRange();
	return FFloatRange();
}

void AITwinPopulationTool::SetRangeDistance(FPopulationIdentifier PopHandle, FFloatRange Range)
{
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetDistanceRange(Range);
		PopulateSpline(*PopulationInfo->SplineHelper);
	}
}

FFloatRange AITwinPopulationTool::GetRangeScale(FPopulationIdentifier PopHandle) const
{
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetScaleRange();
	return FFloatRange();
}

void AITwinPopulationTool::SetRangeScale(FPopulationIdentifier PopHandle, FFloatRange Range)
{
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetScaleRange(Range);
		if (PopHandle.PopulationType == EITwinPopulationType::Path)
		{
			Impl->ApplyScaleRangeToPopulationInstances(PopulationInfo, Range);
		}
		else
		{
			PopulateSpline(*PopulationInfo->SplineHelper);
		}
	}
}

FFloatRange AITwinPopulationTool::GetRangeRotation(FPopulationIdentifier PopHandle) const
{
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
		return PopulationInfo->GetRotationRange();
	return FFloatRange();
}

void AITwinPopulationTool::SetRangeRotation(FPopulationIdentifier PopHandle, FFloatRange Range)
{
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetRotationRange(Range);
		if (PopHandle.PopulationType == EITwinPopulationType::Path)
		{
			Impl->ApplyRotationRangeToPopulationInstances(PopulationInfo, Range);
		}
		else
		{
			PopulateSpline(*PopulationInfo->SplineHelper);
		}
	}
}

float AITwinPopulationTool::GetTightness(FPopulationIdentifier PopHandle, int32 PointIndex) const
{
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
	{
		if (ensure(PopulationInfo->SplineHelper.IsValid()))
		{
			return PopulationInfo->SplineHelper->GetTightness(PointIndex);
		}
	}
	return 0.f;
}

void AITwinPopulationTool::SetTightness(FPopulationIdentifier PopHandle, int32 PointIndex, float Tightness)
{
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		if (ensure(PopulationInfo->SplineHelper.IsValid()))
		{
			// Also updates the AdvViz spline, so that the new tangents are persisted.
			PopulationInfo->SplineHelper->SetTightness(PointIndex, FMath::Clamp(Tightness, 0.f, 1.f));
			PopulateSpline(*PopulationInfo->SplineHelper);
		}
	}
}

int32 AITwinPopulationTool::GetSelectedSplinePoint(FPopulationIdentifier PopHandle, bool& CanEditTangents) const
{
	CanEditTangents = true;
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
	{
		if (PopulationInfo->SplineHelper.IsValid()
			&& Impl->SplineTool.IsValid()
			&& Impl->SplineTool->GetSelectedSpline() == PopulationInfo->SplineHelper.Get()
			&& Impl->SplineTool->HasSelectedPoint())
		{
			const int32 PointIndex = Impl->SplineTool->GetSelectedPointIndex();
			if (!PopulationInfo->SplineHelper->IsClosedLoop() &&
				(PointIndex == 0 || PointIndex == PopulationInfo->SplineHelper->GetNumberOfSplinePoints() - 1))
				CanEditTangents = false;
			return PointIndex;
		}
	}
	return INDEX_NONE;
}

EITwinSplinePopulationMode AITwinPopulationTool::GetSplinePopulationMode(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
	{
		return PopulationInfo->GetMode();
	}
	return EITwinSplinePopulationMode::Regular;
}

void AITwinPopulationTool::SetSplinePopulationMode(FPopulationIdentifier PopHandle, EITwinSplinePopulationMode Mode)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Area);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopulationInfo->SetMode(Mode);
		PopulateSpline(*PopulationInfo->SplineHelper);
	}
}

EITwinPathPopulationRotationMode AITwinPopulationTool::GetPathPopulationRotationMode(FPopulationIdentifier PopHandle) const
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetPopulationInfo(PopHandle))
	{
		return PopulationInfo->GetRotationMode();
	}
	return EITwinPathPopulationRotationMode::Absolute;
}

void AITwinPopulationTool::SetPathPopulationRotationMode(FPopulationIdentifier PopHandle, EITwinPathPopulationRotationMode RotationMode)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Path);
	if (auto PopulationInfo = Impl->GetMutablePopulationInfo(PopHandle))
	{
		const EITwinPathPopulationRotationMode OldMode = PopulationInfo->GetRotationMode();
		PopulationInfo->SetRotationMode(RotationMode);
		Impl->ApplyRotationModeChange(PopulationInfo, OldMode, RotationMode);
	}
}

FTransform AITwinPopulationTool::GetSelectionTransformImpl() const
{
	return Impl->GetSelectionTransform();
}

void AITwinPopulationTool::OnSelectionTransformStartedImpl()
{
	Impl->OnSelectionTransformStarted();
}
void AITwinPopulationTool::OnSelectionTransformCompletedImpl()
{
	Impl->OnSelectionTransformCompleted();
}
void AITwinPopulationTool::SetSelectionTransformImpl(const FTransform& transform)
{
	Impl->SetSelectionTransform(transform);
}

FLinearColor AITwinPopulationTool::GetSelectionColorVariation() const
{
	return Impl->GetSelectionColorVariation();
}

void AITwinPopulationTool::SetSelectionColorVariation(const FLinearColor& color)
{
	Impl->SetSelectionColorVariation(color);
}

namespace
{
	class FPopulationSelectionRecord : public AITwinInteractiveTool::ISelectionRecord
									 , public FPopulationBaseRecord
	{
	public:
		FPopulationSelectionRecord(AITwinPopulation* InPopulation,
								   int32 InInstanceIndex,
								   AITwinDecorationHelper* InPopulationFactory)
			: FPopulationBaseRecord(InPopulation, InPopulationFactory)
		{
			if (ensure(InPopulation))
			{
				InstanceID = InPopulation->GetInstanceRefId(InInstanceIndex);
			}
		}

		int32 GetInstanceIndex() const {
			const AITwinPopulation* Population = GetPopulation();
			return (Population != nullptr)
				? Population->GetInstanceIndexFromRefId(InstanceID)
				: INDEX_NONE;
		}

		AdvViz::SDK::RefID InstanceID = AdvViz::SDK::RefID::Invalid();
	};

	class FPopulationStateRecord : public AITwinInteractiveTool::IActiveStateRecord
	{
	public:
		FPopulationStateRecord(AITwinPopulationTool const& InTool,
							   std::map<FString, bool> const& InUsedAssets)
			: bEnabled(InTool.IsEnabled())
			, bEnabledForCutout(InTool.IsUsedOnCutoutPrimitive())
			, toolMode(InTool.GetMode())
			, transformationMode(InTool.GetTransformationMode())
			, usedAssets(InUsedAssets)
		{}

		bool const bEnabled = false;
		bool const bEnabledForCutout = false;
		EPopulationToolMode const toolMode = EPopulationToolMode::Select;
		ETransformationMode const transformationMode = ETransformationMode::Move;
		std::map<FString, bool> const usedAssets;
	};
}

TUniquePtr<AITwinInteractiveTool::IActiveStateRecord> AITwinPopulationTool::MakeStateRecord() const
{
	return MakeUnique<FPopulationStateRecord>(*this, Impl->usedAssets);
}

bool AITwinPopulationTool::RestoreState(IActiveStateRecord const& State)
{
	FPopulationStateRecord const* PopulationState = static_cast<FPopulationStateRecord const*>(&State);

	if (PopulationState->bEnabled != IsEnabled())
	{
		SetEnabled(PopulationState->bEnabled);
	}
	Impl->bEnabledForCutout = PopulationState->bEnabledForCutout;
	Impl->toolMode = PopulationState->toolMode;
	Impl->transformationMode = PopulationState->transformationMode;
	Impl->usedAssets = PopulationState->usedAssets;
	Impl->UpdatePopulationsArray();
	Impl->CollectEditedPopulations();
	return IsEnabled();
}

TUniquePtr<AITwinInteractiveTool::ISelectionRecord> AITwinPopulationTool::MakeSelectionRecord() const
{
	if (ensure(Impl->HasSelectedInstance()))
	{
		return MakeUnique<FPopulationSelectionRecord>(
			Impl->GetSelectedPopulation(), Impl->GetSelectedInstanceIndex(), Impl->decorationHelper);
	}
	return {};
}

bool AITwinPopulationTool::HasSameSelection(ISelectionRecord const& Selection) const
{
	FPopulationSelectionRecord const* PopSelection = static_cast<FPopulationSelectionRecord const*>(&Selection);
	return Impl->GetSelectedPopulation() == PopSelection->GetPopulation()
		&& Impl->GetSelectedInstanceIndex() == PopSelection->GetInstanceIndex();
}

bool AITwinPopulationTool::RestoreSelection(ISelectionRecord const& Selection)
{
	FPopulationSelectionRecord const* PopSelection = static_cast<FPopulationSelectionRecord const*>(&Selection);
	AITwinPopulation* Population = PopSelection->GetPopulation();
	if (ensure(Population != nullptr) && PopSelection->InstanceID.IsValid())
	{
		if (Impl->GetSelectedPopulation() != Population)
			Impl->SetSelectedPopulation(Population);
		const int32 InstanceIndex = PopSelection->GetInstanceIndex();
		if (ensure(InstanceIndex != INDEX_NONE))
		{
			if (Impl->GetSelectedInstanceIndex() != InstanceIndex)
				Impl->SetSelectedInstanceIndex(InstanceIndex);
			return true;
		}
	}
	return false;
}

AITwinPopulationTool::IBrushUndoEntry::~IBrushUndoEntry()
{

}

namespace
{

	class FBrushAddedInstancesUndoEntry : public AITwinPopulationTool::IBrushUndoEntry
	{
	public:
		FBrushAddedInstancesUndoEntry(std::map<AITwinPopulation*, FBrushAddedPopulationInfo> const& BrushAddedInstancesInfo,
									AITwinDecorationHelper* InPopulationFactory)
		{
			for (const auto& entry : BrushAddedInstancesInfo)
			{
				AddedInstancesData.emplace_back(entry.first, InPopulationFactory, entry.second);
			}
		}
		virtual void Undo(AITwinPopulationTool& /*Tool*/) override
		{
			for (auto& entry : AddedInstancesData)
			{
				AITwinPopulation* Population = entry.PopulationRecord.GetPopulation();
				if (Population)
				{
					const int32 NumInstances = Population->GetNumberOfInstances();

					const int32 FirstAddedInstanceIndex(entry.BrushedInfo.FirstAddedInstanceIndex);
					// For redo system, we will need a copy of the instance.
					if (entry.AddedInstancesBackups.empty())
					{
						auto* PopulationFactory = entry.PopulationRecord.GetPopulationFactory();
						entry.AddedInstancesBackups.reserve(NumInstances - FirstAddedInstanceIndex);
						for (int32 i = FirstAddedInstanceIndex; i < NumInstances; ++i)
						{
							entry.AddedInstancesBackups.emplace_back(Population, i, PopulationFactory);
						}
					}

					// Build array in reverse order.
					TArray<int32> InstancesToRemove;
					InstancesToRemove.Reserve(NumInstances - FirstAddedInstanceIndex);
					for (int32 i = NumInstances - 1; i >= FirstAddedInstanceIndex; --i)
					{
						InstancesToRemove.Push(i);
					}
					BE_ASSERT(InstancesToRemove.Num() == entry.BrushedInfo.NumAddedInstances);
					Population->RemoveInstances(InstancesToRemove);
				}
			}
		}

		virtual void Redo(AITwinPopulationTool& Tool) override
		{
			for (auto const& entry : AddedInstancesData)
			{
				AITwinPopulation* Population = entry.PopulationRecord.GetPopulation();
				if (Population)
				{
					for (const auto& Backup : entry.AddedInstancesBackups)
					{
						Tool.RestoreItem(Backup);
					}
				}
			}
		}

		virtual FString GetDescription() const override
		{
			return TEXT("Add Instances with Brush");
		}

	private:
		struct FPerPopulationData
		{
			FPopulationBaseRecord PopulationRecord;
			FBrushAddedPopulationInfo BrushedInfo;
			// Only filled upon undo, to enable Redo afterwards.
			std::vector<FPopulationInstanceBackup> AddedInstancesBackups;

			FPerPopulationData(AITwinPopulation* InPopulation,
							   AITwinDecorationHelper* InPopulationFactory,
							   FBrushAddedPopulationInfo const& InInfo)
				: PopulationRecord(InPopulation, InPopulationFactory)
				, BrushedInfo(InInfo)
			{
			}
		};
		std::vector<FPerPopulationData> AddedInstancesData;
	};


	class FBrushRemovedInstancesUndoEntry : public AITwinPopulationTool::IBrushUndoEntry
	{
	public:
		FBrushRemovedInstancesUndoEntry(std::map<AITwinPopulation*, FBrushRemovedPopulationInfo>&& BrushRemovedInstancesInfo,
			AITwinDecorationHelper* InPopulationFactory)
		{
			for (auto&& entry : BrushRemovedInstancesInfo)
			{
				RemovedInstancesData.emplace_back(entry.first, InPopulationFactory, std::move(entry.second));
			}
		}
		virtual void Undo(AITwinPopulationTool& Tool) override
		{
			for (auto const& entry : RemovedInstancesData)
			{
				AITwinPopulation* Population = entry.PopulationRecord.GetPopulation();
				if (Population)
				{
					for (const auto& Backup : entry.RemovedInstancesBackups)
					{
						Tool.RestoreItem(Backup);
					}
				}
			}
		}

		virtual void Redo(AITwinPopulationTool& Tool) override
		{
			// Recover instance indices to be removed.
			for (auto const& entry : RemovedInstancesData)
			{
				AITwinPopulation* Population = entry.PopulationRecord.GetPopulation();
				if (Population)
				{
					TArray<int32> InstancesToRemove;
					InstancesToRemove.Reserve(entry.RemovedInstancesBackups.size());
					for (const auto& backup : entry.RemovedInstancesBackups)
					{
						int32 InstanceIndex = Population->GetInstanceIndexFromRefId(backup.InstanceID);
						if (ensure(InstanceIndex != INDEX_NONE))
						{
							InstancesToRemove.Push(InstanceIndex);
						}
					}
					BE_ASSERT(InstancesToRemove.Num() == (int32)entry.RemovedInstancesBackups.size());
					Population->RemoveInstances(InstancesToRemove);
				}
			}
		}

		virtual FString GetDescription() const override
		{
			return TEXT("Erase Instances");
		}

	private:
		struct FPerPopulationData
		{
			FPopulationBaseRecord PopulationRecord;
			std::vector<FPopulationInstanceBackup> RemovedInstancesBackups;

			FPerPopulationData(AITwinPopulation* InPopulation,
							   AITwinDecorationHelper* InPopulationFactory,
							   FBrushRemovedPopulationInfo&& InInfo)
				: PopulationRecord(InPopulation, InPopulationFactory)
				, RemovedInstancesBackups(std::move(InInfo.Backups))
			{

			}
		};
		std::vector<FPerPopulationData> RemovedInstancesData;
	};

}

TUniquePtr<AITwinInteractiveTool::IItemBackup> AITwinPopulationTool::MakeSelectedItemBackup() const
{
	if (ensure(Impl->HasSelectedInstance()))
	{
		return MakeUnique<FPopulationInstanceBackup>(
			Impl->GetSelectedPopulation(), Impl->GetSelectedInstanceIndex(), Impl->decorationHelper);
	}
	return {};
}

bool AITwinPopulationTool::RestoreItem(IItemBackup const& ItemBackup)
{
	FPopulationInstanceBackup const* PopBackup = static_cast<FPopulationInstanceBackup const*>(&ItemBackup);
	AITwinPopulation* Population = PopBackup->GetPopulation();
	if (ensureMsgf(Population != nullptr, TEXT("Unable to recover population")))
	{
		// Use context 'UndoRedo' to make sure the RefID is restored before notifying the Clipping
		// Tool manager.
		// It also avoid troubles with BaseTransform being applied twice...
		const int32 Index = Population->AddInstance(PopBackup->InstanceTransform,
			AITwinPopulation::EAddInstanceContext::UndoRedo);
		if (ensure(Index >= 0))
		{
			if (!PopBackup->AvizInstanceName.empty())
			{
				auto const AVizInst = Population->GetAVizInstance(Index);
				if (AVizInst)
				{
					auto inst = AVizInst->GetAutoLock();
					inst->SetName(PopBackup->AvizInstanceName);
				}
			}
			Population->SetInstanceColorVariation(Index, PopBackup->InstanceColorVariation);
			Population->FinalizeAddedInstance(Index, &PopBackup->InstanceTransform, &PopBackup->InstanceID);
			Population->OnInstanceRestored(PopBackup->InstanceID);
			return true;
		}
	}
	return false;
}

TUniquePtr<AITwinPopulationTool::IBrushUndoEntry> AITwinPopulationTool::MakeBrushUndoEntry()
{
	if (!Impl->BrushAddedInstancesInfo.empty())
	{
		return MakeUnique<FBrushAddedInstancesUndoEntry>(Impl->BrushAddedInstancesInfo, Impl->decorationHelper);
	}
	else if (!Impl->BrushRemovedInstancesInfo.empty())
	{
		return MakeUnique<FBrushRemovedInstancesUndoEntry>(std::move(Impl->BrushRemovedInstancesInfo), Impl->decorationHelper);
	}
	return {};
}


void AITwinPopulationTool::SetEnabledImpl(bool bValue)
{
	if (Impl->SplineTool.IsValid() && Impl->SplineTool->HasSelection())
		return;
	Impl->SetEnabled(bValue);
}

bool AITwinPopulationTool::IsEnabledImpl() const
{
	return Impl->IsEnabled();
}

void AITwinPopulationTool::ResetToDefaultImpl()
{
	Impl->ResetToDefault();
}

void AITwinPopulationTool::SetDecorationHelper(AITwinDecorationHelper* decoHelper)
{
	Impl->SetDecorationHelper(decoHelper);
}

void AITwinPopulationTool::SetPopulationManager(const std::shared_ptr<AdvViz::SDK::IPopulationManager>& InPopulationManager)
{
	Impl->populationManager = InPopulationManager;
}

void AITwinPopulationTool::FImpl::LoadPopulations()
{
	if (!populationManager)
	{
		BE_LOGW("ITwinPopulation", "LoadPopulations: no population manager");
		return;
	}

	std::set<AdvViz::SDK::RefID> populationIds;
	populationManager->GetPopulationIds(populationIds);

	BE_LOGI("ITwinPopulation", "LoadPopulations: " << populationIds.size() << " population(s) to load");

	for (const AdvViz::SDK::RefID& popId : populationIds)
	{
		auto popInfoPtr = populationManager->GetPopulationInfo(popId);
		if (!popInfoPtr)
			continue;

		auto popInfo = popInfoPtr->GetRAutoLock();
		const AdvViz::SDK::RefID& splineRefId = popInfo->GetSplineId();
		if (!splineRefId.IsValid())
			continue;

		// Find the corresponding spline helper in the world.
		AITwinSplineHelper* MatchingSpline = nullptr;
		for (TActorIterator<AITwinSplineHelper> SplineIter(owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetAVizSplineId() == splineRefId)
			{
				MatchingSpline = *SplineIter;
				break;
			}
		}
		if (!MatchingSpline)
		{
			BE_LOGW("ITwinPopulation", "LoadPopulations: could not find spline for population " << popId.ID());
			continue;
		}

		const EITwinPopulationType PopType = GetPopulationTypeFromSplineUsage(MatchingSpline->GetUsage());
		if (PopType != EITwinPopulationType::Area && PopType != EITwinPopulationType::Path)
			continue;

		auto InstGroupId = decorationHelper->GetInstancesGroupIdForSpline(*MatchingSpline);
		if (splineToGroupId.find(MatchingSpline) == splineToGroupId.end())
			splineToGroupId.emplace(MatchingSpline, InstGroupId);

		FPopulationIdentifier PopHandle;
		PopHandle.PopulationType = PopType;
		PopHandle.PopulationIndex = PopType == EITwinPopulationType::Area? AreaInfos.Num() : PathInfos.Num();

		auto PopHelper = CreatePop(PopType);
		PopHelper->Init(MatchingSpline, popInfoPtr);

		std::vector<std::string> objects;
		popInfo->GetObjects(objects);
		TArray<FString> ObjectPaths;
		for (const auto& obj : objects)
			ObjectPaths.Add(UTF8_TO_TCHAR(obj.c_str()));
		PopHelper->Set3DObjectsFromProps();
		if (ObjectPaths.Num() == 0)
			continue;

		bool bAllAssetsLoaded = true;
		for (auto asset : ObjectPaths)
		{
			// We do not want to trigger new population creation when loading an existing scene, just wait for all populations to load and then create the link
			if (!decorationHelper->GetOrCreatePopulation(asset, InstGroupId))
				bAllAssetsLoaded = false;
		}

		TArray<FString> CapturedPaths = ObjectPaths;
		auto LinkPopulationsToSpline = [this, CapturedPaths, PopHelper, InstGroupId]()
		{
			for (const auto& path : CapturedPaths)
			{
				AITwinPopulation* Population = decorationHelper->GetPopulation(path, InstGroupId);
				if (Population)
				{
					Population->SetSplinePopulation(true);
					PopHelper->Populations.Add(Population);
					Population->SetHiddenInGame(!PopHelper->IsVisible());
				}
			}
		};

		if (bAllAssetsLoaded)
		{
			LinkPopulationsToSpline();
		}
		else
		{
			// Delay adding objects to spline until all items have been completely loaded from the component center.
			TWeakObjectPtr<AITwinPopulationTool> weakOwner(&owner);
			std::string const delayedCallId = "RetryPopulateSpline_" + std::to_string(reinterpret_cast<uintptr_t>(PopHelper));
			AdvViz::SDK::UniqueDelayedCall(delayedCallId,
				[weakOwner, PopHelper, LinkPopulationsToSpline = MoveTemp(LinkPopulationsToSpline)]() -> AdvViz::SDK::DelayedCall::EReturnedValue
				{
					if (!weakOwner.IsValid())
						return AdvViz::SDK::DelayedCall::EReturnedValue::Done;

					if (weakOwner->Impl->ArePopulationsFullyLoaded(PopHelper))
					{
						LinkPopulationsToSpline();
						return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
					}

					return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
				},
				0.25f);
		}

		owner.PopulationListModifiedEvent.Broadcast();
		owner.PopulationAddedEvent.Broadcast(PopHandle);
	}
}

void AITwinPopulationTool::LoadPopulations()
{
	Impl->LoadPopulations();
}

bool AITwinPopulationTool::IsLoadingPopulations() const
{
	if (Impl->SplineTool.IsValid() && Impl->SplineTool->IsLoadingSpline())
		return true;
	return false;
}

bool AITwinPopulationTool::DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath)
{
	return Impl->DragActorInLevel(screenPosition, assetPath);
}

void AITwinPopulationTool::ReleaseDraggedAssetInstance()
{ 
	Impl->ReleaseDraggedAssetInstance();
}

void AITwinPopulationTool::DestroyDraggedAssetInstance()
{ 
	Impl->DestroyDraggedAssetInstance();
}

void AITwinPopulationTool::SetUsedAsset(const FString& assetPath, bool used)
{
	Impl->SetUsedAsset(assetPath, used);
}

void AITwinPopulationTool::ClearUsedAssets()
{
	Impl->ClearUsedAssets();
}

void AITwinPopulationTool::ReplaceUsedAssets(const TArray<FString>& AssetPaths)
{
	Impl->ReplaceUsedAssets(AssetPaths);
}

void AITwinPopulationTool::SetGizmoActorClass(TSubclassOf<AActor> ActorClass)
{
	GizmoActorClass = ActorClass;
}

AITwinPopulation* AITwinPopulationTool::PreLoadPopulation(const FString& AssetPath)
{
	return Impl->PreLoadPopulation(AssetPath);
}

void AITwinPopulationTool::FImpl::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects spline population from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedSplinePopulation();
		if (CurrentSelection)
			ShowOnlyPopulationProxiesOfType(CurrentSelection->PopulationType, true);
		else
		{
			if (!SplineTool->IsEnabled() || !SplineTool->IsPopulationTool())
				ITwin::ActivateSplineTool(owner.GetWorld(), EITwinSplineUsage::SplinePopulation, SplineTool);
			SetAllPopulationProxiesVisibility(true);
		}
	}
	else
	{
		HideAllPopulationProxies();
	}
}

void AITwinPopulationTool::OnActivatePicking(bool bActivate)
{
	Impl->OnActivatePicking(bActivate);
}

bool AITwinPopulationTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	UWorld* World = GetWorld();
	if (!World)
		return false;
	bool bRelevantAction = false;
	bOutSelectionGizmoNeeded = false;
	// Test spline populations then brushes and single placement objects.

	// Note that we can only have one active tool at a time, but we don't want the population splines to be
	// hidden just because we temporarily disable the spline tool...
	AITwinSplineTool::FAutomaticVisibilityDisabler AutoVisDisabler;

	if (NumPopulations() > 0)
	{
		auto const ActiveTool = ITwin::ActivateSplineTool(World, EITwinSplineUsage::SplinePopulation, Impl->SplineTool);
		if (ActiveTool.IsValid())
		{
			ActiveTool->SetUsedForPopulation(true);
			bRelevantAction = ActiveTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = ActiveTool->HasSelection();
			auto const NewSelection = GetSelectedSplinePopulation();
			if (NewSelection)
			{
				// Isolation of the selected item, if any.
				Impl->ShowOnlyPopulationProxiesOfType(NewSelection->PopulationType, true);
			}
			else
			{
				// End of isolation mode.
				Impl->SetAllPopulationProxiesVisibility(true);
			}

			// Notify new selection. If nothing is selected, notify it as well
			BroadcastSelection();
		}
	}
	if (!bRelevantAction)
	{
		// Activate population tool
		auto const ActiveTool = this;
		if (!IsEnabled())
		{
			AITwinInteractiveTool::DisableAll(GetWorld());
			SetEnabled(true);
		}
		SetUsedOnCutout(false);
		auto prevTransformationMode = Impl->transformationMode;
		ResetToDefault();
		Impl->transformationMode = prevTransformationMode;
		SetMode(EPopulationToolMode::Select);
		bRelevantAction = ActiveTool->DoMouseClickAction();
		if (bRelevantAction) {
			bOutSelectionGizmoNeeded = ActiveTool->HasSelectedPopulation();
			NonSplinePopulationSelectedEvent.Broadcast();
		}
	}

	return bRelevantAction;
}

void AITwinPopulationTool::SetUsedOnCutout(bool bForCutout)
{
	Impl->SetUsedOnCutout(bForCutout);
}

int32 AITwinPopulationTool::NumPopulations() const
{
	int32 totalPopulations(0);
	for (EITwinPopulationType PopulationType : { EITwinPopulationType::Area,
												 EITwinPopulationType::Path })
	{
		totalPopulations += NumPopulations(PopulationType);
	}
	return totalPopulations;
}


void AITwinPopulationTool::SetInstanceTransformProxy(IITwinPopulationInstanceTransformProxyPtr InTransformProxy)
{
	Impl->SetInstanceTransformProxy(InTransformProxy);
}

bool AITwinPopulationTool::IsAdditionOfInstancesAllowed(bool* bOutAllowBrush /*= nullptr*/) const
{
	return Impl->IsAdditionOfInstancesAllowed(bOutAllowBrush);
}

int32 AITwinPopulationTool::GetInstanceCount(const FString& assetPath) const
{
	return Impl->GetInstanceCount(assetPath);
}

bool AITwinPopulationTool::GetForcePerpendicularToSurface() const
{
	return Impl->GetForcePerpendicularToSurface();
}

void AITwinPopulationTool::SetForcePerpendicularToSurface(bool b)
{ 
	Impl->SetForcePerpendicularToSurface(b);
}

bool AITwinPopulationTool::GetIsEditingBrushSize() const
{ 
	return Impl->GetIsEditingBrushSize();
}

void AITwinPopulationTool::SetIsEditingBrushSize(bool b)
{ 
	Impl->SetIsEditingBrushSize(b);
}

bool AITwinPopulationTool::DoMouseClickActionImpl()
{
	return Impl->DoMouseClickAction();
}

void AITwinPopulationTool::BeginPlay()
{
	Super::BeginPlay();

	Impl->InitBrushSphere(GetWorld());
}

void AITwinPopulationTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	Impl->CancelSplinePopulationJobs();
	Impl->AreaInfos.Empty();
	Impl->PathInfos.Empty();
}

void AITwinPopulationTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->Tick(DeltaTime);
}

inline
int32 AITwinPopulationTool::FImpl::NumPopulations(EITwinPopulationType Type) const
{
	switch (Type)
	{
		case EITwinPopulationType::Area:		return AreaInfos.Num();
		case EITwinPopulationType::Path:		return PathInfos.Num();

	// Those types hold no population list: this is not an error, as an invalid/unknown population
	// identifier (typically EITwinPopulationType::Count) is commonly tested against this count.
	case EITwinPopulationType::Object:
	case EITwinPopulationType::Erase:
	case EITwinPopulationType::Count:
	default:
		return 0;
	}
}

void AITwinPopulationTool::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	AITwinSplineTool::FAutomaticVisibilityDisabler AutoVisDisabler;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(World, GetPopulationTypeFromSplineUsage(SpecificSpline->GetUsage()));
	if (SplineTool.IsValid())
	{
		SplineTool->OnOverviewCamera(SpecificSpline);
	}
}

void AITwinPopulationTool::FImpl::ZoomOnPopulation(FPopulationIdentifier PopHandle)
{
	switch (PopHandle.PopulationType)
	{
		case EITwinPopulationType::Area:
		case EITwinPopulationType::Path:
		{
			auto PopHelper = GetPopulationInfo(PopHandle);
			if (PopHelper && PopHelper->SplineHelper.IsValid())
			{
				// use overview camera for zoom
				owner.OnOverviewCamera(PopHelper->SplineHelper.Get());
			}
			break;
		}
		BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinPopulationType::Count:);
	}
}

void AITwinPopulationTool::ZoomOnPopulation(FPopulationIdentifier PopHandle)
{
	Impl->ZoomOnPopulation(PopHandle);
}

FPopulationIdentifier AITwinPopulationTool::FImpl::GetPopulationIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = AreaInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinAreaPopulationHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FPopulationIdentifier(EITwinPopulationType::Area, Index);

	Index = PathInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinPathPopulationHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FPopulationIdentifier(EITwinPopulationType::Path, Index);

	return FPopulationIdentifier(EITwinPopulationType::Count, INDEX_NONE);
}

bool AITwinPopulationTool::FImpl::UnregisterPopulationSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	auto const SelectedBefore = GetSelectedSplinePopulation();

	auto PopHandle = GetPopulationIdentifierFromSpline(SplineBeingRemoved->GetAVizSplineId());
	if (ensure(PopHandle.IsValid(NumPopulations(PopHandle.PopulationType))))
	{
		// Discard any population computation in progress for this spline.
		AdvViz::SDK::RefID const SplineId = SplineBeingRemoved->GetAVizSplineId();
		CancelSplinePopulationJobs(&SplineId);

		if (!bTriggeredFromITS)
			RemovePopulationObjects(PopHandle);

		// Remove from the population manager for persistence.
		populationManager->RemovePopulationInfo(GetPopulationInfo(PopHandle)->GetPopRefID());

		switch (PopHandle.PopulationType)
		{
		case EITwinPopulationType::Area:
			AreaInfos.RemoveAt(PopHandle.PopulationIndex);
			break;
		case EITwinPopulationType::Path:
			PathInfos.RemoveAt(PopHandle.PopulationIndex);
			break;
		default:
			break;
		}

		if (!bTriggeredFromITS && !SplineBeingRemoved->IsInteractiveCreationInProgress())
		{
			SplinePopulationChanged(PopHandle.PopulationType, EChangeType::Deleted, TEXT("key_down"));
		}

		owner.PopulationRemovedEvent.Broadcast(PopHandle, bTriggeredFromITS);
		owner.PopulationListModifiedEvent.Broadcast();

		// After removing a spline population, we should exit isolation mode or else we'll be in an inconsistent state
		if (SelectedBefore)
		{
			SetAllPopulationProxiesVisibility(true);
		}
		return true;
	}

	return false;
}

void AITwinPopulationTool::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	if (IsSplineUsedForPopulation(SplineBeingRemoved))
	{
		Impl->UnregisterPopulationSpline(SplineBeingRemoved, bTriggeredFromITS);
	}
}

void AITwinPopulationTool::FImpl::RemovePopulationObjects(UITwinPopulationHelper* PopHelper)
{
	if (!PopHelper)
		return;

	for (auto Population : PopHelper->Populations)
	{
		if (Population.IsValid())
			Population->RemoveAllInstances();
	}
	PopHelper->Populations.Empty();
}

bool AITwinPopulationTool::FImpl::ArePopulationsFullyLoaded(UITwinPopulationHelper* PopHelper)
{
	if (!PopHelper || !PopHelper->SplineHelper.IsValid())
		return false;
	auto InstGroupId = decorationHelper->GetInstancesGroupIdForSpline(*(PopHelper->SplineHelper));
	auto Assets = PopHelper->Get3DObjects();
	for (auto asset : Assets)
	{
		AITwinPopulation* Population = decorationHelper->GetPopulation(asset, InstGroupId);
		if (!Population || Population->GetNumberOfInstances() == 0)
			return false;
	}
	return true;
}

void AITwinPopulationTool::FImpl::RemovePopulationObjects(FPopulationIdentifier PopHandle)
{
	RemovePopulationObjects(GetMutablePopulationInfo(PopHandle));
}

bool AITwinPopulationTool::FImpl::RemovePopulation(FPopulationIdentifier PopHandle, bool bTriggeredFromITS)
{
	if (!ensure(PopHandle.IsValid(NumPopulations(PopHandle.PopulationType))))
		return false;

	// Select the spline population if needed (for undo/redo) - the spline is already selected if this event is
	// triggered from iTS spline population properties page, but not if the event is triggered from the list of populations.
	auto const CurrentSelection = GetSelectedSplinePopulation();
	bool bPopulationIsSelected = CurrentSelection
		&& CurrentSelection->PopulationType == PopHandle.PopulationType
		&& CurrentSelection->PopulationIndex == PopHandle.PopulationIndex;
	if (!bPopulationIsSelected)
	{
		bPopulationIsSelected = SelectSplinePopulation(PopHandle, false);
	}

	const int32 NumPopsOld = NumPopulations(PopHandle.PopulationType);
	switch (PopHandle.PopulationType)
	{
		case EITwinPopulationType::Area:
		case EITwinPopulationType::Path:
		{
			if (auto PopHelper = GetPopulationInfo(PopHandle))
			{
				RemovePopulationObjects(PopHandle);
				if (PopHelper->SplineHelper.IsValid() && ensure(SplineTool.IsValid()))
					SplineTool->DeleteSpline(PopHelper->SplineHelper.Get(), bTriggeredFromITS);
			}
		}
	}

	const bool bRemoved = (NumPopulations(PopHandle.PopulationType) == NumPopsOld - 1);
	return bRemoved;
}

bool AITwinPopulationTool::RemovePopulation(FPopulationIdentifier PopHandle, bool bTriggeredFromITS)
{
	const bool bRemoved = Impl->RemovePopulation(PopHandle, bTriggeredFromITS);
	return bRemoved;
}

void AITwinPopulationTool::Get3DObjects(FPopulationIdentifier PopHandle, TArray<FString>& Assets) const
{
	if (auto PopHelper = Impl->GetPopulationInfo(PopHandle))
		Assets = PopHelper->Get3DObjects(/*Assets*/);
	else
		Assets.Empty();
}

void AITwinPopulationTool::Set3DObjects(FPopulationIdentifier PopHandle, const TArray<FString>& Assets)
{
	ensure(PopHandle.PopulationType != EITwinPopulationType::Object || Assets.Num() == 1);
	auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle);
	if (!PopHelper)
		return;
	PopHelper->Set3DObjects(Assets);
	PopulateSpline(*PopHelper->SplineHelper);
}

bool AITwinPopulationTool::IsVisible(FPopulationIdentifier PopHandle) const
{
	if (auto PopInfo = Impl->GetPopulationInfo(PopHandle))
		return PopInfo->IsVisible();
	return false;
}

void AITwinPopulationTool::SetVisible(FPopulationIdentifier PopHandle, bool isVisible, bool bForceHiddenSpline)
{
	if (auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopHelper->SetVisible(isVisible);
		if (!PopHelper)
			return;
		for (auto Population : PopHelper->Populations)
		{
			if (!Population.IsValid())
				continue;
			Population->SetHiddenInGame(!isVisible);
		}
		if (PopHelper->SplineHelper.IsValid())
			PopHelper->SplineHelper->SetActorHiddenInGame(!isVisible || bForceHiddenSpline);
	}
}

bool AITwinPopulationTool::IsAvoidOverlapping(FPopulationIdentifier PopHandle) const
{
	if (auto PopInfo = Impl->GetPopulationInfo(PopHandle))
		return PopInfo->IsAvoidOverlapping();
	return false;
}

void AITwinPopulationTool::SetAvoidOverlapping(FPopulationIdentifier PopHandle, bool isAvoidOverlapping)
{
	if (auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle))
	{
		PopHelper->SetIsAvoidOverlapping(isAvoidOverlapping);
		PopulateSpline(*PopHelper->SplineHelper);
	}
}

void AITwinPopulationTool::SetAllVisible(bool isVisible, bool bForceHiddenSpline)
{
	for (EITwinPopulationType PopType : { EITwinPopulationType::Area,
										   EITwinPopulationType::Path })
	{
		for (int32 Index(0); Index < NumPopulations(PopType); ++Index)
		{
			SetVisible(FPopulationIdentifier(PopType, Index), isVisible, bForceHiddenSpline);
		}
	}
}

void AITwinPopulationTool::SetIsSpacingRandomized(FPopulationIdentifier PopHandle, bool isSpacingRandomized)
{
	ensure(PopHandle.PopulationType == EITwinPopulationType::Path);
	auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle);
	if (!PopHelper)
		return;
	PopHelper->SetIsSpacingRandomized(isSpacingRandomized);
	PopulateSpline(*PopHelper->SplineHelper);
}

bool AITwinPopulationTool::IsSpacingRandomized(FPopulationIdentifier PopHandle) const
{
	if (auto PopInfo = Impl->GetPopulationInfo(PopHandle))
		return PopInfo->IsSpacingRandomized();
	return false;
}

void AITwinPopulationTool::SetIsScaleRandomized(FPopulationIdentifier PopHandle, bool isScaleRandomized)
{
	auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle);
	if (!PopHelper)
		return;
	PopHelper->SetIsScaleRandomized(isScaleRandomized);
	if (PopHandle.PopulationType == EITwinPopulationType::Path)
	{
		const FFloatRange Range = isScaleRandomized
			? PopHelper->GetScaleRange()
			: FFloatRange(PopHelper->GetScale(), PopHelper->GetScale());
		Impl->ApplyScaleRangeToPopulationInstances(PopHelper, Range);
	}
	else
	{
		PopulateSpline(*PopHelper->SplineHelper);
	}
}

bool AITwinPopulationTool::IsScaleRandomized(FPopulationIdentifier PopHandle) const
{
	if (auto PopInfo = Impl->GetPopulationInfo(PopHandle))
		return PopInfo->IsScaleRandomized();
	return false;
}

void AITwinPopulationTool::SetIsRotationRandomized(FPopulationIdentifier PopHandle, bool isRotationRandomized)
{
	auto PopHelper = Impl->GetMutablePopulationInfo(PopHandle);
	if (!PopHelper)
		return;
	PopHelper->SetIsRotationRandomized(isRotationRandomized);
	if (PopHandle.PopulationType == EITwinPopulationType::Path)
	{
		const FFloatRange Range = isRotationRandomized
			? PopHelper->GetRotationRange()
			: FFloatRange(PopHelper->GetRotation(), PopHelper->GetRotation());
		Impl->ApplyRotationRangeToPopulationInstances(PopHelper, Range);
	}
	else
	{
		PopulateSpline(*PopHelper->SplineHelper);
	}
}

bool AITwinPopulationTool::IsRotationRandomized(FPopulationIdentifier PopHandle) const
{
	if (auto PopInfo = Impl->GetPopulationInfo(PopHandle))
		return PopInfo->IsRotationRandomized();
	return false;
}

inline UITwinPopulationHelper* AITwinPopulationTool::FImpl::GetMutablePopulationInfo(FPopulationIdentifier PopHandle)
{
	if (!PopHandle.IsValid(NumPopulations(PopHandle.PopulationType)))
		return nullptr;
	switch (PopHandle.PopulationType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinPopulationType::Count:)
	case EITwinPopulationType::Area:	return AreaInfos[PopHandle.PopulationIndex].Get();
	case EITwinPopulationType::Path:	return PathInfos[PopHandle.PopulationIndex].Get();
	}
}

inline const UITwinPopulationHelper* AITwinPopulationTool::FImpl::GetPopulationInfo(FPopulationIdentifier PopHandle) const
{
	if (!PopHandle.IsValid(NumPopulations(PopHandle.PopulationType)))
		return nullptr;
	switch (PopHandle.PopulationType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinPopulationType::Count:)
	case EITwinPopulationType::Area:	return AreaInfos[PopHandle.PopulationIndex].Get();
	case EITwinPopulationType::Path:	return PathInfos[PopHandle.PopulationIndex].Get();
	}
}

int32 AITwinPopulationTool::NumPopulations(EITwinPopulationType Type) const
{
	return Impl->NumPopulations(Type);
}

void AITwinPopulationTool::SetSelectedSpline(AITwinSplineHelper* Spline)
{
	SelectedSpline = Spline;
}

void AITwinPopulationTool::PopulateSpline(AITwinSplineHelper const& TargetSpline, bool bFinalEdit /*= true*/)
{
	Impl->PopulateSpline(TargetSpline, bFinalEdit);
}

bool AITwinPopulationTool::IsPopulatingSpline(AITwinSplineHelper const& TargetSpline) const
{
	auto It = Impl->SplinePopulationStates.find(TargetSpline.GetAVizSplineId());
	return It != Impl->SplinePopulationStates.end()
		&& (It->second.RunningJob || It->second.bPendingRequest);
}

void AITwinPopulationTool::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	if (IsSplineUsedForPopulation(NewSpline))
	{
		Impl->RegisterPopulationSpline(NewSpline);
	}
}

void AITwinPopulationTool::ConnectSplineTool(AITwinSplineTool* SplineTool)
{
	Impl->SplineTool = SplineTool;
	if (SplineTool)
	{
		SplineTool->SplineAddedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplineHelperAdded);
		SplineTool->SplineBeforeRemovedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplineHelperRemoved);
		SplineTool->SplinePointMovedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplinePointMovedInTool);
		SplineTool->SplineEditionEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplineEditedInTool);
		SplineTool->SplinePointAddedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplinePointAddedInTool);
		SplineTool->SplinePointRemovedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplinePointRemovedInTool);
		SplineTool->SplinePointMovingStartedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplinePointMoveStart);
		SplineTool->SplineMovingStartedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnSplineMoveStart);
		SplineTool->SplineSelectedEvent.AddUniqueDynamic(this, &AITwinPopulationTool::OnPopulationPolygonSelected);
	}
}

void AITwinPopulationTool::FImpl::OnSplineEditedInTool(bool bFinalEdit)
{
	if (!SplineTool.IsValid() || !SplineTool->IsPopulationTool() || !SplineTool->GetSelectedSpline())
		return;
	if (SplineTool->IsInteractiveCreationMode())
	{
		return;
	}
	auto PopHandle = GetPopulationIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
	if (auto PopHelper = GetMutablePopulationInfo(PopHandle))
	{
		if (PopHelper->SplineHelper.IsValid())
			PopulateSpline(*PopHelper->SplineHelper, bFinalEdit);
	}
	owner.SelectedPopulationModifiedEvent.Broadcast();
}


void AITwinPopulationTool::OnSplineEditedInTool()
{
	// End of an edition (point added/removed, interactive edition finished...): force the final state.
	Impl->OnSplineEditedInTool(/*bFinalEdit*/true);
}

void AITwinPopulationTool::OnSplinePointMovedInTool(bool bTriggeredFromITS)
{
	// Interactive move in progress: keep the current computation, and queue the latest state.
	Impl->OnSplineEditedInTool(/*bFinalEdit*/false);
}

void AITwinPopulationTool::OnSplineMoveStart()
{
	Impl->OnSplineChanged(TEXT("gizmo"), TEXT("position"));
}

void AITwinPopulationTool::OnSplinePointMoveStart()
{
	Impl->OnSplineChanged(TEXT("gizmo"), TEXT("point_position"));
}

void AITwinPopulationTool::FImpl::OnSplineChanged(const FString& eventSource, const FString& modificationKind)
{
	if (SplineTool.IsValid() && SplineTool->IsPopulationTool()
		&& SplineTool->GetSelectedSpline()
		&& !SplineTool->IsInteractiveCreationMode()
		&& !owner.IsLoadingPopulations())
	{
		auto PopHandle = GetPopulationIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
		if (PopHandle.IsValid(NumPopulations(PopHandle.PopulationType)))
		{
			SplinePopulationChanged(PopHandle.PopulationType, EChangeType::Modified, eventSource, modificationKind);
		}
	}
}

void AITwinPopulationTool::OnSplinePointAddedInTool()
{
	Impl->OnSplineChanged(TEXT("mouse_click"), TEXT("point_added"));
}

void AITwinPopulationTool::OnSplinePointRemovedInTool()
{
	Impl->OnSplineChanged(TEXT("key_down"), TEXT("point_removed"));
}

bool AITwinPopulationTool::StartInteractiveCreationImpl()
{
	return Impl->StartInteractiveCreation();
}

bool AITwinPopulationTool::IsInteractiveCreationModeImpl() const
{
	return Impl->bInteractivePlacement;
}

bool AITwinPopulationTool::IsUsedOnCutoutPrimitiveImpl() const
{
	return Impl->bEnabledForCutout;
}

void AITwinPopulationTool::SetUsedOnCutoutPrimitiveImpl(bool bForCutout)
{
	SetUsedOnCutout(bForCutout);
}

AITwinPopulation const* AITwinPopulationTool::FImpl::GetSelectedPopulation(int32& OutSelectedInstanceIndex) const
{
	AITwinPopulation const* SelectedPopulation = nullptr;
	OutSelectedInstanceIndex = INDEX_NONE;
	SelectedPopulation = owner.GetSelectedPopulation();
	if (SelectedPopulation)
	{
		OutSelectedInstanceIndex = owner.GetSelectedInstanceIndex();
	}
	return SelectedPopulation;
}

std::optional<FPopulationIdentifier> AITwinPopulationTool::FImpl::GetSelectedSplinePopulation() const
{
	int32 InstanceIndex(INDEX_NONE);
	if (ensure(SplineTool.IsValid()) && SplineTool->IsPopulationTool())
	{
		if (auto SelectedSplinePopulation = SplineTool->GetSelectedSpline())
			return GetPopulationIdentifierFromSpline(SelectedSplinePopulation->GetAVizSplineId());
	}
	return std::nullopt;
}

std::optional<FPopulationIdentifier> AITwinPopulationTool::GetSelectedSplinePopulation() const
{
	return Impl->GetSelectedSplinePopulation();
}

void AITwinPopulationTool::BroadcastSelection()
{
	// Notify new selection (using -1 as index if nothing i selected)
	auto const NewSelection = GetSelectedSplinePopulation();
	if (NewSelection)
	{
		PopulationSelectedEvent.Broadcast(NewSelection.value());
	}
	else
	{
		PopulationSelectedEvent.Broadcast(FPopulationIdentifier());
	}
}

void AITwinPopulationTool::OnPopulationPolygonSelected()
{
	BroadcastSelection();
}

void AITwinPopulationTool::DeSelectAll(bool bExitIsolationMode)
{
	auto CurrentSelection = GetSelectedSplinePopulation();
	if (CurrentSelection)
	{
		Impl->SelectSpline(nullptr, GetWorld());

		if (bExitIsolationMode)
		{
			// Restore visibility of proxies.
			Impl->SetAllPopulationProxiesVisibility(true);
		}
	}
	BroadcastSelection();
}

AdvViz::SDK::RefID AITwinPopulationTool::FImpl::GetPopulationRefId(FPopulationIdentifier PopulationHandle) const
{
	auto PopulationHelper = GetPopulationInfo(PopulationHandle);
	return PopulationHelper && PopulationHelper->SplineHelper.IsValid() ? PopulationHelper->SplineHelper->GetAVizSplineId() : AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinPopulationTool::GetPopulationRefId(FPopulationIdentifier PopulationHandle) const
{
	return Impl->GetPopulationRefId(PopulationHandle);
}

AdvViz::SDK::RefID AITwinPopulationTool::FImpl::GetPopulationId(EITwinPopulationType PopulationType, int32 PopulationIndex) const
{
	switch (PopulationType)
	{
		case EITwinPopulationType::Area:
		{
			if (PopulationIndex >= 0 && PopulationIndex < AreaInfos.Num())
			{
				auto const& AreaInfo = AreaInfos[PopulationIndex].Get();
				if (AreaInfo->SplineHelper.IsValid())
				{
					return AreaInfo->SplineHelper->GetAVizSplineId();
				}
			}
			break;
		}
		case EITwinPopulationType::Path:
		{
			if (PopulationIndex >= 0 && PopulationIndex < PathInfos.Num())
			{
				auto const& PathInfo = PathInfos[PopulationIndex].Get();
				if (PathInfo->SplineHelper.IsValid())
				{
					return PathInfo->SplineHelper->GetAVizSplineId();
				}
			}
			break;
		}
		BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinPopulationType::Count:);
	}
	return AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinPopulationTool::GetPopulationId(EITwinPopulationType PopulationType, int32 PopulationIndex) const
{
		return Impl->GetPopulationId(PopulationType, PopulationIndex);
}

FPopulationIdentifier AITwinPopulationTool::FImpl::GetPopulationIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = AreaInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinAreaPopulationHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid()
				&& InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FPopulationIdentifier(EITwinPopulationType::Area, Index);

	Index = PathInfos.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinPathPopulationHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid()
				&& InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FPopulationIdentifier(EITwinPopulationType::Path, Index);

	return FPopulationIdentifier(EITwinPopulationType::Count, INDEX_NONE);
}

FPopulationIdentifier AITwinPopulationTool::GetPopulationIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	return Impl->GetPopulationIdentifier(RefID);
}


void AITwinPopulationTool::AbortInteractiveCreation(bool bTriggeredFromITS)
{
	// Abort current population creation, if any
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsPopulationTool())
	{
		if (ActiveTool->IsInteractiveCreationMode())
			ActiveTool->AbortInteractiveCreation(bTriggeredFromITS);
		ActiveTool->SetEnabled(false);
		ActiveTool->SetUsedForPopulation(false);
	}
}

void AITwinPopulationTool::Deactivate()
{
	// Abort current population creation, if any.
	AbortInteractiveCreation(true);

	// Deselect all, without changing the visibility (since we will hide all below...)
	DeSelectAll(false);

	// Trigger event to refresh the selection gizmo
	ActivationEvent.Broadcast(false);

	Impl->HideAllPopulationProxies();
}

void AITwinPopulationTool::FImpl::ToggleSplineToolForSelectedPath()
{
	if (SplineTool.IsValid())
	{
		if (SplineTool->GetSelectedSpline() && SplineTool->GetSelectedSpline()->IsUsedForPopulation())
		{
			SplineTool->ToggleInteractiveEditionMode();
		}
	}
}

void AITwinPopulationTool::ToggleSplineToolForSelectedPath()
{
	Impl->ToggleSplineToolForSelectedPath();
}

void AITwinPopulationTool::AbortInteractiveCreationImpl(bool bTriggeredFromITS)
{
	if (Impl->bInteractivePlacement)
	{
		const FImpl::EAbortContext AbortContext = bTriggeredFromITS
			? FImpl::EAbortContext::UserInput_ITS
			: FImpl::EAbortContext::UserInput_Unreal;
		Impl->AbortInteractiveCreation(AbortContext);
	}
	// See discussion in AzDev#2085006: the escape key should also cancel the brushing mode.
	// This is done by changing the mode from iTwin Studio.
	else if (Impl->IsEnabled() && Impl->IsBrushModeActivated())
	{
		InteractiveCreationAbortedEvent.Broadcast(this, bTriggeredFromITS);
	}
}

void AITwinPopulationTool::ValidateInteractiveCreationImpl(bool bTriggeredFromITS)
{
	if (!Impl->bInteractivePlacement)
		return;
	if (HasSelectedInstance())
	{
		Impl->FinalizeInteractiveCreation(nullptr, bTriggeredFromITS);
	}
	else
	{
		// No instance is ready to be validated: just abort the creation.
		Impl->SetInteractivePlacement(false);
		InteractiveCreationAbortedEvent.Broadcast(this, bTriggeredFromITS);
	}
}


AITwinPopulationTool::FPickingContext::FPickingContext(AITwinPopulationTool& InTool, bool bRestrictPickingOnClipping)
	: Tool(InTool)
	, bRestrictPickingOnClipping_Old(InTool.GetRestrictPickingOnClippingPrimitives())
{
	Tool.RestrictPickingOnClippingPrimitives(bRestrictPickingOnClipping);
}
AITwinPopulationTool::FPickingContext::~FPickingContext()
{
	// Restore previous state.
	Tool.RestrictPickingOnClippingPrimitives(bRestrictPickingOnClipping_Old);
}

bool AITwinPopulationTool::GetRestrictPickingOnClippingPrimitives() const
{
	return Impl->bRestrictPickingOnClipping;
}

void AITwinPopulationTool::RestrictPickingOnClippingPrimitives(bool bRestrictPickingOnClipping /*= true*/)
{
	Impl->RestrictPickingOnClippingPrimitives(bRestrictPickingOnClipping);
}

bool AITwinPopulationTool::ShowOnlyTranslationZGizmoImpl() const
{
	return Impl->ShowOnlyTranslationZGizmo();
}

namespace
{
	class [[nodiscard]] FPopulationToolDisabler : public AITwinInteractiveTool::FToolDisabler
	{
		using Super = AITwinInteractiveTool::FToolDisabler;
	public:
		FPopulationToolDisabler(AITwinInteractiveTool* InTool);
		virtual ~FPopulationToolDisabler();
	private:
		TArray<TWeakObjectPtr<AITwinPopulation>> HiddenPopulations;
	};

	FPopulationToolDisabler::FPopulationToolDisabler(AITwinInteractiveTool* InTool)
		: Super(InTool)
	{
		if (InTool && InTool->GetWorld())
		{
			// Hide cutout primitives.
			for (TActorIterator<AITwinPopulation> PopIter(InTool->GetWorld()); PopIter; ++PopIter)
			{
				AITwinPopulation* Population = *PopIter;
				if (Population->IsClippingPrimitive() && !Population->IsHiddenInGame())
				{
					Population->SetHiddenInGame(true);
					HiddenPopulations.Add(Population);
				}
			}
		}
	}

	FPopulationToolDisabler::~FPopulationToolDisabler()
	{
		// Restore hidden state of cutout primitives.
		for (TWeakObjectPtr<AITwinPopulation> const& PopulationPtr : HiddenPopulations)
		{
			if (PopulationPtr.IsValid())
			{
				PopulationPtr->SetHiddenInGame(false);
			}
		}
	}
}

TSharedPtr<AITwinInteractiveTool::FToolDisabler> AITwinPopulationTool::MakeToolDisabler()
{
	return MakeShared<FPopulationToolDisabler>(this);
}
