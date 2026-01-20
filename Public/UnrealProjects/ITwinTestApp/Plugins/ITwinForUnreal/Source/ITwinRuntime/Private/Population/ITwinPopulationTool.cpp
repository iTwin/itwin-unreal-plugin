/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Population/ITwinPopulationTool.h>
#include <Population/ITwinPopulation.h>

#include <BeUtils/SplineSampling/SplineSampling.h>
#include <Components/SplineComponent.h>
#include <Spline/ITwinSplineHelper.h>

#include <Decoration/ITwinDecorationHelper.h>
#include <Helpers/ITwinTracingHelper.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinRealityData.h>

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

#include <map>
#include <vector>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>

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

	ITWINRUNTIME_API TOptional<FVector> GetNewObjectDefaultPosition(const UObject* WorldContextObject,
																	EITwinSplineUsage SplineUsage,
																	FHitResult& OutHitResult)
	{
		UWorld* World = WorldContextObject->GetWorld();
		if (!World)
			return {};
		APlayerController* PlayerController = World->GetFirstPlayerController();

		if (!PlayerController)
			return {};

		int32 Width(0), Height(0);
		PlayerController->GetViewportSize(Width, Height);

		FVector TraceStart, TraceDir;
		FVector2D ScreenPos(Width * 0.5, Height * 0.5);
		if (!UGameplayStatics::DeprojectScreenToWorld(PlayerController, ScreenPos, TraceStart, TraceDir))
			return {};

		FVector Default3DPos = TraceStart + (TraceDir * 2 * 1e4f); // default position

		// For cutout polygon, we will create the spline slightly above the hit impact, so that the polygon
		// remains visible.
		const float DistFactor = (SplineUsage == EITwinSplineUsage::MapCutout) ? 0.90f : 0.9999f;

		// Make an intersection test to adjust the position on the tileset
		FHitResult HitResult;
		FVector TraceEnd = TraceStart + TraceDir * 1e8f;
		if (UKismetSystemLibrary::LineTraceSingle(
			WorldContextObject, TraceStart, TraceEnd, ETraceTypeQuery::TraceTypeQuery1, true,
			{} /*ActorsToIgnore*/, EDrawDebugTrace::None, HitResult, true))
		{
			if (HitResult.Distance > 0.f)
			{
				Default3DPos = TraceStart + (TraceDir * HitResult.Distance * DistFactor);
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
		void ConfigureNewInstance(AdvViz::SDK::IInstance& AVizInstance, AActor& HitActor);
	}
}


FUESplineCurve::FUESplineCurve(USplineComponent const& InSpline)
	: UESpline(InSpline)
{}

glm::dvec3 FUESplineCurve::GetPositionAtCoord(value_type const& u) const
{
	// Directly work in world coordinates
	const float SplineTime = u * UESpline.Duration;
	auto const Pos_World = UESpline.GetLocationAtTime(SplineTime, ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

glm::dvec3 FUESplineCurve::GetTangentAtCoord(value_type const& u) const
{
	const float SplineTime = u * UESpline.Duration;
	auto const Tgte_World = UESpline.GetTangentAtTime(SplineTime, ESplineCoordinateSpace::World);
	return {
		Tgte_World.X,
		Tgte_World.Y,
		Tgte_World.Z
	};
}

size_t FUESplineCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return static_cast<size_t>(UESpline.GetNumberOfSplinePoints());
}

glm::dvec3 FUESplineCurve::GetPositionAtIndex(size_t idx) const
{
	// Directly work in world coordinates
	auto const Pos_World = UESpline.GetLocationAtSplinePoint(static_cast<int32>(idx),
		ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

bool FUESplineCurve::IsCyclic() const
{
	return UESpline.IsClosedLoop();
}




#define BRUSH_MESH_INVERSE_RADIUS 6.25e-3f // = 1/160

class AITwinPopulationTool::FImpl
{
public:
	AITwinPopulationTool& owner;
	AITwinDecorationHelper* decorationHelper = nullptr;

	bool enabled = false; // boolean used to switch on or off the population tool

	EPopulationToolMode toolMode = EPopulationToolMode::Select;
	ETransformationMode transformationMode = ETransformationMode::Move;

	AITwinPopulation* selectedPopulation = nullptr;
	std::optional<AITwinPopulation::FAutoRebuildTreeDisabler> SelectionTreeUpdateDisabler;
	int32 selectedInstanceIndex = -1;

	struct BrushFlow
	{
		float computedValue = 1.f;
		float userFactor = 1.f;
		float GetFlow() const { return computedValue * userFactor; }
	};

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
	AStaticMeshActor* brushSphere = nullptr;
	float brushRadius = 1000.f; // radius in centimeters
	BrushFlow brushFlow; // number of added instances per m^2 per second.
	float brushLastTime = 0.f;
	FVector brushLastPos = FVector(0);
	float instancesScaleVariation = 0.2f;
	float instancesRotationVariation = UE_PI;
	bool forcePerpendicularToSurface = false;
	// Collisions now enabled by default on Reality data (azdev#1737290), so I guess we'll remove this
	bool enableOnRealityData = true;
	bool isBrushingInstances = false;
	bool isEditingBrushSize = false;
	FTransform savedTransform = FTransform::Identity;
	bool savedTransformChanged = true;
	float savedAngleZ = 0.f;
	std::map<FString, bool> usedAssets;
	std::vector<AITwinPopulation*> editedPopulations;
	TArray<AActor*> allPopulations;
	AdvViz::SDK::RefID instanceGroupId = AdvViz::SDK::RefID::Invalid(); // destination group for instances
	std::map<AITwinSplineHelper const*, AdvViz::SDK::RefID> splineToGroupId;

	// For the addition of an instance from the browser
	float draggingRotVar = 0.f;
	float draggingScaleVar = 1.f;

	// Interactive placement mode
	bool bInteractivePlacement = false;
	bool bRestrictPickingOnClipping = false;


	FImpl(AITwinPopulationTool& inOwner);

	// Implementation of AITwinPopulationTool functions
	EPopulationToolMode GetMode() const;
	void SetMode(EPopulationToolMode mode);
	ETransformationMode GetTransformationMode() const;
	void SetTransformationMode(ETransformationMode mode);
	AITwinPopulation* GetSelectedPopulation() const;
	int32 GetSelectedInstanceIndex() const;
	void SetSelectedPopulation(AITwinPopulation* population);
	void SetSelectedInstanceIndex(int32 instanceIndex);
	bool HasSelectedPopulation() const;
	inline bool HasSelectedInstance() const;
	void DeleteSelectedInstance();
	bool IsPopulationModeActivated() const;
	bool IsBrushModeActivated() const;
	void StartBrushingInstances();
	void EndBrushingInstances();
	void ShowBrushSphere();
	void HideBrushSphere();
	void ComputeBrushFlow();
	float GetBrushFlow() const;
	void SetBrushFlow(float flow);
	float GetBrushSize() const;
	void SetBrushSize(float size);
	FTransform GetSelectionTransform() const;
	void OnSelectionTransformStarted(bool bForInteractivePlacement = false);
	void OnSelectionTransformCompleted(bool bForInteractivePlacement = false);
	void SetSelectionTransform(const FTransform& transform);
	FLinearColor GetSelectionColorVariation() const;
	void SetSelectionColorVariation(const FLinearColor& c);
	void SetEnabled(bool value);
	bool IsEnabled() const;
	void ResetToDefault();
	void SetDecorationHelper(AITwinDecorationHelper* decoHelper);
	bool DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath);
	void ReleaseDraggedAssetInstance();
	void DestroyDraggedAssetInstance();
	void SetUsedAsset(const FString& assetPath, bool used);
	void ClearUsedAssets();
	bool IsAdditionOfInstancesAllowed(bool& bOutAllowBrush) const;
	int32 GetInstanceCount(const FString& assetPath) const;
	bool GetForcePerpendicularToSurface() const;
	void SetForcePerpendicularToSurface(bool b);
	bool GetEnableOnRealityData() const;
	void SetEnableOnRealityData(bool b);
	bool GetIsEditingBrushSize() const;
	void SetIsEditingBrushSize(bool b);
	bool DoMouseClickAction();
	void Tick(float DeltaTime);

	// Additional internal functions
	void InitBrushSphere();
	bool ComputeTransformFromHitResult(
		const FHitResult& hitResult, FTransform& transform,
		const AITwinPopulation* population, bool isDraggingInstance = false);
	FHitResult LineTraceFromMousePos();
	FVector LineTraceToSetBrushSize();
	void MultiLineTraceFromMousePos(int32 traceCount, std::vector<AITwinPopulation*> const& populations);
	bool AddSingleInstanceFromHitResult(const FHitResult& hitResult, FCreatedInstance* OutCreatedInstance = nullptr);

	/// Try to add one instance, using the center of the view as reference position.
	bool AddSingleInstanceAtViewCenter();

	size_t CollectEditedPopulations();
	void SetBrushPosition(const FVector& position);
	void UpdatePopulationsCollisionType() const;
	void UpdatePopulationsArray();
	void StartDragging(AITwinPopulation* population);
	void DeleteInstanceFromPopulation(AITwinPopulation*& population, int32& instanceIndex);

	void RestrictPickingOnClippingPrimitives(bool bInRestrictPickingOnClipping = true);

	AITwinPopulation* PreLoadPopulation(const FString& AssetPath);

	void UpdateGroupId(AITwinSplineHelper const* CurSpline);

	// Population along / inside a spline
	uint32 PopulateSpline();
	uint32 PopulateSpline(AITwinSplineHelper const& TargetSpline);
};

AITwinPopulationTool::FImpl::FImpl(AITwinPopulationTool& inOwner)
	: owner(inOwner)
{
}

EPopulationToolMode AITwinPopulationTool::FImpl::GetMode() const
{
	return toolMode;
}

void AITwinPopulationTool::FImpl::SetMode(EPopulationToolMode mode)
{
	if (!IsEnabled())
		return;

	const bool bChangingMode = (toolMode != mode);
	if (bChangingMode && mode != EPopulationToolMode::Select && HasSelectedInstance())
	{
		// Reset current selection.
		SetSelectedInstanceIndex(-1);
		owner.SelectionChangedEvent.Broadcast();
	}

	toolMode = mode;

	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();

	// Show or hide brush depending on active mode
	if (toolMode == EPopulationToolMode::InstantiateN || toolMode == EPopulationToolMode::RemoveInstances)
	{
		ShowBrushSphere();

		if (toolMode == EPopulationToolMode::InstantiateN)
			ComputeBrushFlow();
	}
	else
	{
		HideBrushSphere();
	}

	if (bChangingMode)
	{
		owner.ModeChangedEvent.Broadcast();
	}
}

ETransformationMode AITwinPopulationTool::FImpl::GetTransformationMode() const
{
	return transformationMode;
}

void AITwinPopulationTool::FImpl::SetTransformationMode(ETransformationMode mode)
{
	transformationMode = mode;
}

AITwinPopulation* AITwinPopulationTool::FImpl::GetSelectedPopulation() const
{
	return selectedPopulation;
}

int32 AITwinPopulationTool::FImpl::GetSelectedInstanceIndex() const
{
	return selectedInstanceIndex;
}

void AITwinPopulationTool::FImpl::SetSelectedPopulation(AITwinPopulation* population)
{
	selectedPopulation = population;
	selectedInstanceIndex = -1;
}

void AITwinPopulationTool::FImpl::SetSelectedInstanceIndex(int32 instanceIndex)
{
	selectedInstanceIndex = instanceIndex;
}

bool AITwinPopulationTool::FImpl::HasSelectedPopulation() const
{
	return selectedPopulation != nullptr;
}

inline bool AITwinPopulationTool::FImpl::HasSelectedInstance() const
{
	return selectedPopulation && selectedInstanceIndex >= 0;
}

void AITwinPopulationTool::FImpl::DeleteSelectedInstance()
{
	DeleteInstanceFromPopulation(selectedPopulation, selectedInstanceIndex);
}

bool AITwinPopulationTool::FImpl::IsPopulationModeActivated() const
{
	return toolMode == EPopulationToolMode::Instantiate || IsBrushModeActivated();
}

bool AITwinPopulationTool::FImpl::IsBrushModeActivated() const
{
	return toolMode == EPopulationToolMode::InstantiateN ||
		   toolMode == EPopulationToolMode::RemoveInstances;
}

void AITwinPopulationTool::FImpl::StartBrushingInstances()
{
	if (!IsEnabled())
		return;

	isBrushingInstances = true;
	brushLastTime = owner.GetGameTimeSinceCreation();
	brushLastPos = brushSphere->GetActorLocation();
	
	if (editedPopulations.empty())
	{
		CollectEditedPopulations();
	}
}

void AITwinPopulationTool::FImpl::EndBrushingInstances()
{
	isBrushingInstances = false;
}

void AITwinPopulationTool::FImpl::ShowBrushSphere()
{
	if (brushSphere && IsEnabled())
	{
		brushSphere->SetActorHiddenInGame(false);
	}
}

void AITwinPopulationTool::FImpl::HideBrushSphere()
{
	if (brushSphere)
	{
		brushSphere->SetActorHiddenInGame(true);
	}
}

void AITwinPopulationTool::FImpl::ComputeBrushFlow()
{
	// Compute an appropriate brushFlow for the selected actor that may be instantiated.
	if (selectedPopulation)
	{
		FVector selSize = Cast<AITwinPopulation const>(selectedPopulation)->GetMasterMeshBounds().GetBox().GetSize();
		float selArea = selSize.X * selSize.Y * 1e-4f; // convert cm^2 to m^2
		// Limit the value to 1 instance per m^2 per second (it's enough for characters
		// which are the smallest assets at the moment).
		brushFlow.computedValue = selArea > 1.f ? 1.f/selArea : 1.f;
	}
}

float AITwinPopulationTool::FImpl::GetBrushFlow() const
{
	return brushFlow.userFactor;
}

void AITwinPopulationTool::FImpl::SetBrushFlow(float flow)
{
	if (IsBrushModeActivated())
	{
		brushFlow.userFactor = flow;
	}
}

float AITwinPopulationTool::FImpl::GetBrushSize() const
{
	return brushRadius;
}

void AITwinPopulationTool::FImpl::SetBrushSize(float size)
{
	if (IsBrushModeActivated())
	{
		isEditingBrushSize = true;
		brushRadius = size;
	}
}

FTransform AITwinPopulationTool::FImpl::GetSelectionTransform() const
{
	if (HasSelectedInstance())
	{
		return selectedPopulation->GetInstanceTransform(selectedInstanceIndex);
	}

	return FTransform();
}

void AITwinPopulationTool::FImpl::OnSelectionTransformStarted(bool bForInteractivePlacement /*= false*/)
{
	if (bInteractivePlacement && !bForInteractivePlacement)
	{
		// Quick fix to avoid a false positive assert: when the user clicks to validate the instance position
		// we do come here but we don't want to recreate the disabler...
		BE_ASSERT(SelectionTreeUpdateDisabler.has_value());
		return;
	}
	if (SelectionTreeUpdateDisabler)
	{
		BE_ISSUE("missing call to OnSelectionTransformCompleted?");
		SelectionTreeUpdateDisabler.reset();
	}
	if (selectedPopulation)
	{
		// Optimization: suspend automatic tree rebuild for the edited population.
		SelectionTreeUpdateDisabler.emplace(*selectedPopulation);
	}
}

void AITwinPopulationTool::FImpl::OnSelectionTransformCompleted(bool /*bForInteractivePlacement*/ /*= false*/)
{
	// Restore the suspended tree update, if any.
	SelectionTreeUpdateDisabler.reset();
}

void AITwinPopulationTool::FImpl::SetSelectionTransform(const FTransform& transform)
{
	if (HasSelectedInstance())
	{
		selectedPopulation->SetInstanceTransform(selectedInstanceIndex, transform);
		if (!selectedPopulation->IsRotationVariationEnabled())
		{
			savedTransform = transform;
			savedTransformChanged = true;
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

		if (!enabled)
		{
			SetSelectedPopulation(nullptr);
		}

		if (IsBrushModeActivated())
		{
			if (enabled)
			{
				ShowBrushSphere();
			}
			else
			{
				HideBrushSphere();
			}
		}
	}
}

bool AITwinPopulationTool::FImpl::IsEnabled() const
{
	return enabled && decorationHelper && decorationHelper->IsPopulationEnabled();
}

void AITwinPopulationTool::FImpl::ResetToDefault()
{
	toolMode = EPopulationToolMode::Select;
	transformationMode = ETransformationMode::Move;
	usedAssets.clear();
	editedPopulations.clear();

	// When re-opening the population widget, there is absolutely no reason to change the global options
	// defined by the user!
	// [*******
	//	forcePerpendicularToSurface = false;
	//	enableOnRealityData = false;
	// *******]
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
		TracingHelper.AddIgnoredActors(allPopulations);
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
	editedPopulations.clear();
}

void AITwinPopulationTool::FImpl::ClearUsedAssets()
{
	usedAssets.clear();
	editedPopulations.clear();
}

bool AITwinPopulationTool::FImpl::IsAdditionOfInstancesAllowed(bool& bOutAllowBrush) const
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
	bOutAllowBrush = bHasSelectedAsset && !bForbidBrush;
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

bool AITwinPopulationTool::FImpl::GetEnableOnRealityData() const
{ 
	return enableOnRealityData;
}

void AITwinPopulationTool::FImpl::SetEnableOnRealityData(bool b)
{
	enableOnRealityData = b;

	if (enableOnRealityData)
	{
		TArray<ACesium3DTileset*> tilesets;
		ITwin::Gather3DMapTilesets(owner.GetWorld(), tilesets);
		for (auto tileset : tilesets)
		{
			tileset->SetCreatePhysicsMeshes(true);
		}
	}
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

bool AITwinPopulationTool::FImpl::DoMouseClickAction()
{
	bool bRelevantAction = false;
	TArray<const AActor*> ActorsToIgnore;
	const bool bFinalizingInteractivePlacement = bInteractivePlacement && HasSelectedInstance();
	if (bFinalizingInteractivePlacement)
	{
		// When clicking to validate the final position of the created instance, we should ignore all
		// populations (including the one being selected, which would much probably be hit as the selected
		// instance is following the mouse...), as done in FImpl::Tick (see #LineTraceFromMousePos).
		//
		// Due to constness considerations we cannot just write ActorsToIgnore = allPopulations...
		ActorsToIgnore.Reserve(allPopulations.Num());
		for (auto PopulationActor : allPopulations)
			ActorsToIgnore.Push(PopulationActor);
	}
	FHitResult hitResult = owner.DoPickingAtMousePosition(nullptr, std::move(ActorsToIgnore));
	AActor* hitActor = hitResult.GetActor();
	auto prevSelectedPopulation = selectedPopulation;
	auto prevSelectedInstanceIndex = selectedInstanceIndex;

	if (bFinalizingInteractivePlacement)
	{
		// Finalize the instance being moved interactively.
		FTransform FinalTransform, transform;
		if (ComputeTransformFromHitResult(hitResult, transform, selectedPopulation))
		{
			// Same comment as in FImpl::Tick
			FinalTransform = selectedPopulation->GetBaseTransform() * transform;
		}
		else
		{
			// Keep current position.
			FinalTransform = selectedPopulation->GetInstanceTransform(selectedInstanceIndex);
		}
		if (hitActor && selectedPopulation->IsClippingPrimitive())
		{
			// Configure initial cutout properties, depending on the hit layer (encoding them in the AdvViz
			// instance).
			auto const& AVizInst = selectedPopulation->GetAVizInstance(selectedInstanceIndex);
			if (AVizInst)
			{
				ITwin::Clipping::ConfigureNewInstance(*AVizInst, *hitActor);
			}
		}
		selectedPopulation->FinalizeAddedInstance(selectedInstanceIndex, &FinalTransform);
		OnSelectionTransformCompleted(/*bForInteractivePopulation*/true);
		owner.InteractiveCreationCompletedEvent.Broadcast();
		bInteractivePlacement = false;
	}
	else
	{
		SetSelectedPopulation(nullptr);
		if (hitActor)
		{
			if (hitActor->IsA(AITwinPopulation::StaticClass()) &&
				hitResult.Item >= 0 && toolMode == EPopulationToolMode::Select)
			{
				AITwinPopulation* HitPopulation = Cast<AITwinPopulation>(hitActor);
				// Don't select a hidden population.
				// We also handle restriction on clipping primitives, if applicable.
				if (!HitPopulation->IsHiddenInGame()
					&& (!bRestrictPickingOnClipping || HitPopulation->IsClippingPrimitive()))
				{
					SetSelectedPopulation(HitPopulation);
					SetSelectedInstanceIndex(hitResult.Item);
					bRelevantAction = true;
				}
			}
			else if (toolMode == EPopulationToolMode::Instantiate ||
				toolMode == EPopulationToolMode::InstantiateN)
			{
				FCreatedInstance CreatedInstance;
				bRelevantAction = AddSingleInstanceFromHitResult(hitResult, &CreatedInstance);
				if (bRelevantAction)
				{
					owner.SingleInstanceAddedEvent.Broadcast();

					// Temporarily select the added instance (for backup system).
					{
						FScopedInstanceSelection TempSelection(*this, CreatedInstance);
						owner.InteractiveCreationCompletedEvent.Broadcast();
					}
				}
			}
		}
	}

	if (selectedPopulation != prevSelectedPopulation || selectedInstanceIndex != prevSelectedInstanceIndex)
		owner.SelectionChangedEvent.Broadcast();

	return bRelevantAction;
}

void AITwinPopulationTool::FImpl::Tick(float DeltaTime)
{
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
		if (hitResult.GetActor() && isBrushingInstances)
		{
			if (!editedPopulations.empty() && toolMode == EPopulationToolMode::InstantiateN)
			{
				float currentTime = owner.GetGameTimeSinceCreation();
				float brushDeltaTime = currentTime - brushLastTime;
				float brushRadiusInMeters = brushRadius * 1e-2f;
				float brushDiskArea = brushRadiusInMeters * brushRadiusInMeters * UE_PI;
				float traceCount = brushFlow.GetFlow() * brushDeltaTime * brushDiskArea;
				int32 traceCountInt = static_cast<int32>(traceCount);

				if (traceCountInt > 0)
				{
					MultiLineTraceFromMousePos(traceCountInt, editedPopulations);

					brushLastTime = currentTime;
					brushLastPos = brushSphere->GetActorLocation();
				}
			}
			else if (toolMode == EPopulationToolMode::RemoveInstances)
			{
				TArray<AActor*> actorsToIgnore;
				TArray<FHitResult> hitResults;

				if (UKismetSystemLibrary::SphereTraceMulti(
						&owner, hitResult.Location, hitResult.Location, brushRadius,
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
							if (!HitPopulation->IsHiddenInGame())
								hitsByPopulation[HitPopulation].AddUnique(hitRes.Item);
						}
					}

					for (auto& hits : hitsByPopulation)
					{
						hits.second.Sort([](const int32& a, const int32& b) {return a > b;});
						hits.first->RemoveInstances(hits.second);
					}
				}
			}
		}
	}
	else if (bInteractivePlacement && HasSelectedInstance())
	{
		FHitResult hitResult = LineTraceFromMousePos();
		FTransform transform;
		if (ComputeTransformFromHitResult(hitResult, transform, selectedPopulation))
		{
			// Do not forget to multiply by BaseTransform, as done in AITwinPopulation::AddInstance
			selectedPopulation->SetInstanceTransformUEOnly(selectedInstanceIndex,
				selectedPopulation->GetBaseTransform() * transform);
		}
	}
}

void AITwinPopulationTool::FImpl::InitBrushSphere()
{
	// Create the brush sphere and material (like in FoliageEdMode.cpp in the UE source code)
	UMaterial* brushMaterial = LoadObject<UMaterial>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Materials/BrushMaterial.BrushMaterial"), nullptr, LOAD_None, nullptr);
	UMaterialInstanceDynamic* brushMID = 
		UMaterialInstanceDynamic::Create(brushMaterial, GetTransientPackage());
	check(brushMID != nullptr);
	UStaticMesh* brushSphereMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/BrushSphere.BrushSphere"), nullptr, LOAD_None, nullptr);
	brushSphere = owner.GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass());
	brushSphere->SetMobility(EComponentMobility::Movable);
	brushSphere->SetActorLocation(FVector(0));
	brushSphere->SetActorHiddenInGame(true);
	UStaticMeshComponent* brushSphereComp = brushSphere->GetStaticMeshComponent();
	brushSphereComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	brushSphereComp->SetCollisionObjectType(ECC_WorldDynamic);
	brushSphereComp->SetStaticMesh(brushSphereMesh);
	brushSphereComp->SetMaterial(0, brushMID);
	brushSphereComp->SetAbsolute(true, true, true);
	brushSphereComp->CastShadow = false;
}

bool AITwinPopulationTool::FImpl::ComputeTransformFromHitResult(
	const FHitResult& hitResult, FTransform& transform,
	const AITwinPopulation* population, bool isDraggingInstance /*= false*/)
{
	if (!hitResult.HasValidHitObjectHandle())
	{
		return false;
	}
	AActor* hitActor = hitResult.GetActor();

	if (!hitActor ||
		(!enableOnRealityData && hitActor->IsA(ACesium3DTileset::StaticClass()) &&
		 ITwin::Is3DMapTileset(Cast<ACesium3DTileset>(hitActor))))
	{
		return false;
	}

	FMatrix hitMat(FMatrix::Identity);

	float rotVar = 0.f;
	if (!population->IsRotationVariationEnabled())
	{
		if (savedTransformChanged)
		{
			FVector eulerAngles = savedTransform.GetRotation().Euler();
			savedAngleZ = FMath::DegreesToRadians(static_cast<float>(eulerAngles.Z));
			savedTransformChanged = false;
		}
		rotVar = savedAngleZ;
	}
	else if (isDraggingInstance)
	{
		rotVar = draggingRotVar;
	}
	else if (instancesRotationVariation != 0.f)
	{
		rotVar = FMath::FRandRange(
			-instancesRotationVariation, instancesRotationVariation);
	}

	if (population->IsPerpendicularToSurface() || forcePerpendicularToSurface)
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
		hitMat = FMatrix(sX, sY, sZ, FVector(0.f));

		if (rotVar != 0.f)
		{
			FQuat hitQuat(hitMat);
			hitQuat = FQuat(sZ, rotVar) * hitQuat;
			hitMat = hitQuat.ToMatrix();
		}
	}
	else if (rotVar != 0.f)
	{
		FQuat hitQuat(FVector::ZAxisVector, rotVar);
		hitMat = hitQuat.ToMatrix();
	}
	hitMat.SetOrigin(FVector(hitResult.Location));

	if (population->IsScaleVariationEnabled() && instancesScaleVariation > 0.f)
	{
		float scaleVar = 0.f;
		if (isDraggingInstance)
		{
			scaleVar = draggingScaleVar;
		}
		else
		{
			scaleVar = FMath::FRandRange(-instancesScaleVariation, instancesScaleVariation);
		}

		hitMat = hitMat.ApplyScale(1.f + scaleVar);
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

	TArray<AActor*> actorsToIgnore;
	if (toolMode == EPopulationToolMode::RemoveInstances ||	draggedAssetPopulation || bInteractivePlacement)
	{
		// When erasing instances, collisions are enabled. When dragging an instance from the
		// browser, collisions may be enabled depending on the current mode. Existing populations
		// must be explicitly ignored here so that the brush sphere is placed like when painting
		// instances (it avoids rapid jumps).
		actorsToIgnore = allPopulations;
	}

	FITwinTracingHelper TracingHelper;
	TracingHelper.AddIgnoredActors(actorsToIgnore);
	TracingHelper.FindNearestImpact(HitResult, owner.GetWorld(), traceStart, traceEnd);

	return HitResult;
}

FVector AITwinPopulationTool::FImpl::LineTraceToSetBrushSize()
{
	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return FVector(0, 0, 0);

	int32 width, height;
	playerController->GetViewportSize(width, height);

	FVector traceStart, traceEnd, traceDir;
	FVector2D screenPos(width*0.5, height*0.5);

	if (!UGameplayStatics::DeprojectScreenToWorld(playerController, screenPos, traceStart, traceDir))
		return FVector(0, 0, 0);

	traceEnd = traceStart + traceDir * 1e8f;

	FHitResult HitResult;
	FITwinTracingHelper TracingHelper;
	TracingHelper.FindNearestImpact(HitResult, owner.GetWorld(), traceStart, traceEnd);

	if (HitResult.HasValidHitObjectHandle() && HitResult.GetActor())
	{
		return HitResult.Location;
	}
	else
	{
		return traceStart + traceDir * 1e4f;
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
	FVector brushPos = brushSphere->GetActorLocation();
	constexpr int32 numCircles = 3;
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
		FVector interpolatedBrushPos = brushLastPos*(1.-t) + brushPos*t;

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
		AITwinPopulation* population = populations[popIndex];

		FTransform transform;
		if (ComputeTransformFromHitResult(HitResult, transform, population))
		{
			population->AddInstance(transform);
		}
	}
}

bool AITwinPopulationTool::FImpl::AddSingleInstanceFromHitResult(const FHitResult& hitResult,
	FCreatedInstance* OutCreatedInstance /*= nullptr*/)
{
	if (editedPopulations.empty())
	{
		CollectEditedPopulations();
	}

	if (!editedPopulations.empty())
	{
		int32 popIndex = editedPopulations.size() > 1 ?
			FMath::RandRange((int32)0, (int32)editedPopulations.size() - 1) : 0;
		FTransform tm;
		if (ComputeTransformFromHitResult(hitResult, tm, editedPopulations[popIndex]))
		{
			const int32 InstIndex = editedPopulations[popIndex]->AddInstance(tm, bInteractivePlacement);

			if (bInteractivePlacement)
			{
				// Select the new instance to adjust its position interactively.
				SetSelectedPopulation(editedPopulations[popIndex]);
				SetSelectedInstanceIndex(InstIndex);
				// Disable automatic UE tree rebuild for this population as long as the position is not
				// validated.
				OnSelectionTransformStarted(/*bForInteractivePlacement*/true);
			}
			if (OutCreatedInstance)
			{
				OutCreatedInstance->Population = editedPopulations[popIndex];
				OutCreatedInstance->NewInstanceIndex = InstIndex;
			}
			return true;
		}
	}

	return false;
}

bool AITwinPopulationTool::FImpl::AddSingleInstanceAtViewCenter()
{
	FHitResult HitResult;
	TOptional<FVector> InstancePos = ITwin::GetNewObjectDefaultPosition(&owner, EITwinSplineUsage::Undefined, HitResult);
	if (!InstancePos)
		return false;
	return AddSingleInstanceFromHitResult(HitResult);
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

	editedPopulations.clear();

	for (auto& asset : usedAssets)
	{
		if (asset.second)
		{
			AITwinPopulation* population = decorationHelper->GetOrCreatePopulation(asset.first, instanceGroupId);
			if (population)
				editedPopulations.push_back(population);
		}
	}

	return editedPopulations.size();
}

void AITwinPopulationTool::FImpl::SetBrushPosition(const FVector& position)
{
	if (brushSphere)
	{
		FTransform tm;
		tm.SetTranslation(position);
		tm.SetScale3D(FVector(brushRadius * BRUSH_MESH_INVERSE_RADIUS));
		brushSphere->SetActorTransform(tm);
	}
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

	for (auto actor : allPopulations)
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
		for (auto actor : allPopulations)
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


void AITwinPopulationTool::FImpl::UpdatePopulationsArray()
{
	allPopulations.Empty();
	UGameplayStatics::GetAllActorsOfClass(
		owner.GetWorld(), AITwinPopulation::StaticClass(), allPopulations);
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
	draggingRotVar = FMath::FRandRange(-instancesRotationVariation, instancesRotationVariation);
	draggingScaleVar = FMath::FRandRange(-instancesScaleVariation, instancesScaleVariation);
	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();
}

void AITwinPopulationTool::FImpl::DeleteInstanceFromPopulation(
	AITwinPopulation*& population, int32& instanceIndex)
{
	if (population)
	{
		if (instanceIndex >= 0)
		{
			population->RemoveInstance(instanceIndex);
		}
		if (population->GetNumberOfInstances() == 0)
		{
			population->Destroy();
		}
		population = nullptr;
		instanceIndex = -1;
	}
}

namespace
{

}

uint32 AITwinPopulationTool::FImpl::PopulateSpline()
{
	if (!owner.SelectedSpline.IsValid())
		return 0;
	return PopulateSpline(*owner.SelectedSpline);
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

uint32 AITwinPopulationTool::FImpl::PopulateSpline(AITwinSplineHelper const& TargetSpline)
{
	if (!TargetSpline.GetSplineComponent())
		return 0;

	UpdateGroupId(&TargetSpline);
	if (editedPopulations.empty() && !CollectEditedPopulations())
		return 0;

	// First remove all instances populated on this spline.
	for (AITwinPopulation* Population : editedPopulations)
	{
		Population->RemoveAllInstances();
	}

	FUESplineCurve const Curve(*TargetSpline.GetSplineComponent());

	BeUtils::SplineSamplingParameters SamplingParams;
	SamplingParams.samplingMode = (TargetSpline.GetUsage() == EITwinSplineUsage::PopulationZone)
		? BeUtils::ESplineSamplingMode::Interior
		: BeUtils::ESplineSamplingMode::AlongPath;
	//SamplingParams.fixedNbInstances = 10;
	//SamplingParams.fixedSpacing = glm::dvec2(5. * 100); // 5m

	// The transformation to world is "baked" in FUESplineCurve.
	BeUtils::TransformHolder const IdentityTsf;

	FVector SplineOrigin, SplineExtent;
	TargetSpline.GetActorBounds(false, SplineOrigin, SplineExtent);
	FVector BoundsMin = SplineOrigin - SplineExtent;
	FVector BoundsMax = SplineOrigin + SplineExtent;

	BeUtils::BoundingBox SamplingBox;
	SamplingBox.min[0] = BoundsMin.X;
	SamplingBox.min[1] = BoundsMin.Y;
	SamplingBox.min[2] = BoundsMin.Z;
	SamplingBox.max[0] = BoundsMax.X;
	SamplingBox.max[1] = BoundsMax.Y;
	SamplingBox.max[2] = BoundsMax.Z;

	glm::dvec3 AccumBBoxDims(0.0);
	for (AITwinPopulation const* Population : editedPopulations)
	{
		const FVector BoxSize = Population->GetMasterMeshBounds().GetBox().GetSize();
		AccumBBoxDims += glm::dvec3(BoxSize.X, BoxSize.Y, BoxSize.Z);
	}
	glm::dvec3 const AverageInstanceDims = AccumBBoxDims / (double)editedPopulations.size();

	std::vector<glm::dvec3> Positions;
	BeUtils::SampleSpline(Curve, IdentityTsf, SamplingBox, AverageInstanceDims, SamplingParams, Positions);

	if (Positions.empty())
		return 0;

	uint32 NumAddedInstances = 0;
	// Project sampled spline position onto scene.
	UWorld const* World = owner.GetWorld();
	float const ZStart = static_cast<float>(BoundsMax.Z + 1e5);
	FVector const TraceDir = FVector::DownVector;
	FITwinTracingHelper TracingHelper;
	UpdatePopulationsArray();
	TracingHelper.AddIgnoredActors(allPopulations);
	for (glm::dvec3 const& SplinePos : Positions)
	{
		// Project spline position onto ground
		FVector TraceStart = { SplinePos.x, SplinePos.y, ZStart };
		FVector TraceEnd = TraceStart + TraceDir * 1e8f;
		FHitResult HitResult;
		if (!TracingHelper.FindNearestImpact(HitResult, World, TraceStart, TraceEnd))
			continue;

		int32 popIndex = editedPopulations.size() > 1 ?
			FMath::RandRange((int32)0, (int32)editedPopulations.size() - 1) : 0;
		AITwinPopulation* Population = editedPopulations[popIndex];

		FTransform InstTransform;
		if (ComputeTransformFromHitResult(HitResult, InstTransform, Population))
		{
			Population->AddInstance(InstTransform);
			NumAddedInstances++;
		}
	}
	return NumAddedInstances;
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
	return Impl->HasSelectedInstance();
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
	Impl->ShowBrushSphere();
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
	Impl->SetBrushFlow(flow);
}

float AITwinPopulationTool::GetBrushSize() const
{
	return Impl->GetBrushSize();
}

void AITwinPopulationTool::SetBrushSize(float size)
{
	Impl->SetBrushSize(size);
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

	private:
		mutable TWeakObjectPtr<AITwinPopulation> PopulationPtr;
		AITwinDecorationHelper* const PopulationFactory = nullptr;
		// Populations are deleted when the instance count comes down to zero, so we need a way to reload it
		// it the pointer is invalidated.
		FString AssetPath;
		AdvViz::SDK::RefID InstancesGroupId;
	};

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

namespace
{
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
				auto const AVizInst = InPopulation->GetAVizInstance(InInstanceIndex);
				if (AVizInst)
					AvizInstanceName = AVizInst->GetName();
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
		// Use interactive placement flag to make sure the RefID is restored before notifying the Clipping
		// Tool manager.
		// It also avoid troubles with BaseTransform being applied twice...
		const int32 Index = Population->AddInstance(PopBackup->InstanceTransform, true);
		if (ensure(Index >= 0))
		{
			if (!PopBackup->AvizInstanceName.empty())
			{
				auto const AVizInst = Population->GetAVizInstance(Index);
				if (AVizInst)
					AVizInst->SetName(PopBackup->AvizInstanceName);
			}
			Population->SetInstanceColorVariation(Index, PopBackup->InstanceColorVariation);
			Population->FinalizeAddedInstance(Index, &PopBackup->InstanceTransform, &PopBackup->InstanceID);
			Population->OnInstanceRestored(PopBackup->InstanceID);
			return true;
		}
	}
	return false;
}

void AITwinPopulationTool::SetEnabledImpl(bool bValue)
{
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

AITwinPopulation* AITwinPopulationTool::PreLoadPopulation(const FString& AssetPath)
{
	return Impl->PreLoadPopulation(AssetPath);
}

bool AITwinPopulationTool::IsAdditionOfInstancesAllowed(bool& bOutAllowBrush) const
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

bool AITwinPopulationTool::GetEnableOnRealityData() const
{ 
	return Impl->GetEnableOnRealityData();
}

void AITwinPopulationTool::SetEnableOnRealityData(bool b)
{
	Impl->SetEnableOnRealityData(b);
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
	Impl->InitBrushSphere();
}

void AITwinPopulationTool::Tick(float DeltaTime)
{
	Impl->Tick(DeltaTime);
}

void AITwinPopulationTool::SetSelectedSpline(AITwinSplineHelper* Spline)
{
	SelectedSpline = Spline;
}

uint32 AITwinPopulationTool::PopulateSpline(AITwinSplineHelper const& TargetSpline)
{
	return Impl->PopulateSpline(TargetSpline);
}

bool AITwinPopulationTool::StartInteractiveCreationImpl()
{
	Be::CleanUpGuard RestoreStateCleanup([this]
	{
		Impl->bInteractivePlacement = false;
	});
	Impl->bInteractivePlacement = true;
	if (!Impl->AddSingleInstanceAtViewCenter())
	{
		return false;
	}
	RestoreStateCleanup.release();
	return true;
}

bool AITwinPopulationTool::IsInteractiveCreationModeImpl() const
{
	return Impl->bInteractivePlacement;
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
