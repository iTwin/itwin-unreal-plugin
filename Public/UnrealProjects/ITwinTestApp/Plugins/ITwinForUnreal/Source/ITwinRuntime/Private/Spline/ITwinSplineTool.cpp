/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineHelper.h>

#include <Math/UEMathConversion.h>
#include <IncludeCesium3DTileset.h>
#include <CesiumCartographicPolygon.h>
#include <CesiumPolygonRasterOverlay.h>
#include <Helpers/ITwinPickingResult.h>
#include <Helpers/WorldSingleton.h>
#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinTilesetAccess.h>
#include <Population/ITwinPopulationTool.h>


// UE headers
#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <Components/SplineComponent.h>
#include <Components/SplineMeshComponent.h>
#include <Components/StaticMeshComponent.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/GameplayStatics.h>
#include <TimerManager.h>
#include <UObject/StrongObjectPtr.h>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Visualization/SplinesManager.h>
#include <Compil/AfterNonUnrealIncludes.h>

class AITwinSplineTool::FImpl
{
public:
	AITwinSplineTool& owner;

	bool bIsEnabled = false; // boolean used to switch on or off the spline tool

	AITwinSplineHelper* selectedSplineHelper = nullptr;
	int32 selectedPointIndex = -1;
	bool duplicateWhenMovingPoint = false;
	EITwinSplineToolMode ToolMode = EITwinSplineToolMode::Undefined;
	EITwinSplineUsage ToolUsage = EITwinSplineUsage::Undefined;
	std::shared_ptr<AdvViz::SDK::ISplinesManager> splinesManager;
	std::set<ITwin::ModelDecorationIdentifier> CutoutTargetIdentifiers;
	bool bAutoSelectCutoutTarget = false;
	bool bIsLoadingSpline = false;

	TArray<TWeakObjectPtr<const AActor>> ActorsExcludedFromPicking; // for interactive cut-out polygon creation
	TArray<TWeakObjectPtr<const AActor>> ActorsExcludedFromPicking_PointInsertion;

	FTimerHandle RefreshTimerHandle;
	FTimerHandle TimerHandle;
	ACameraActor* OverviewCamera = nullptr;

	bool bPreventDeletion = false; // workaround for duplicated events in iTS + shipping
	FTimerHandle PreventDeletionTimerHandle;

	static bool bAutomaticSplineVisibility;


	FImpl(AITwinSplineTool& inOwner);

	// Implementation of AITwinSplineTool functions
	AITwinSplineHelper* GetSelectedSpline() const;
	void SetSelectedSpline(AITwinSplineHelper* splineHelper);
	void SetSelectedPointIndex(int32 pointIndex);
	int32 GetSelectedPointIndex() const;
	bool HasSelection() const;
	void DeleteSelection();
	bool HasSelectedPoint() const;
	void DeleteSelectedSpline();
	void DeleteSpline(AITwinSplineHelper* SplineHelper);
	bool CanDeletePoint() const;
	void DeleteSelectedPoint();
	void DuplicateSelectedPoint();
	void EnableDuplicationWhenMovingPoint(bool value);
	FTransform GetSelectionTransform() const;
	void SetSelectionTransform(const FTransform& transform);
	void SetEnabled(bool value);
	bool IsEnabled() const;
	void ResetToDefault();
	void BuildListOfActorsToExclude();
	void BuildListOfActorsToExcludeForPointInsertion();
	bool DoMouseClickAction();

	inline FHitResult DoPickingAtMousePosition(const TArray<TWeakObjectPtr<const AActor>>& ExcludedActors) const;
	bool ActionOnTick(float DeltaTime);

	AITwinSplineHelper* AddSpline(FVector const& Position);

	bool LoadSpline(AdvViz::SDK::SharedSpline const& Spline);


	bool GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const;
	bool GetOverviewCameraReferencePosition(FVector& RefLocation, FBox& OutBox) const;

	EITwinSplineToolMode GetMode() const { return ToolMode; }
	void SetMode(EITwinSplineToolMode NewMode) { ToolMode = NewMode; }

	//! Toggle interactive creation mode on/off. If the creation is toggled on for cut-out usage and
	//! bAutoSelectCutoutTarget is true, the cut-out target will be determined by the layer intersected
	//! upon the first click.
	void ToggleInteractiveCreationMode(bool bInAutoSelectCutoutTarget = false);
	void AbortInteractiveCreation();

	EITwinSplineUsage GetUsage() const { return ToolUsage; }
	void SetUsage(EITwinSplineUsage NewUsage) { ToolUsage = NewUsage; }

	EITwinTangentMode GetTangentMode() const;
	void SetTangentMode(EITwinTangentMode TangentMode);

	void RefreshScene(AITwinSplineHelper const* TargetSpline = nullptr);

	void TriggerDelayedRefresh();

	bool GetInvertSelectedSplineEffect() const;
	void SetInvertSelectedSplineEffect(bool bInvertEffect);

	// Inspired from ASavedViewsController
	void StartCameraMovement(FTransform const& CamTransform, float BlendTime);
	void EndCameraMovement(FTransform const& CamTransform);

private:
	template <typename TFunc>
	void ForEachTargetTileset(TFunc const& Func) const;

	AITwinSplineHelper* CreateSpline(EITwinSplineUsage SplineUsage,
		std::optional<FVector> const& PositionOpt, AdvViz::SDK::SharedSpline const& LoadedSpline);

	void RemoveCartographicPolygon(ACesiumCartographicPolygon* Polygon);
};

/*static*/ bool AITwinSplineTool::FImpl::bAutomaticSplineVisibility = true;

AITwinSplineTool::FImpl::FImpl(AITwinSplineTool& inOwner)
	: owner(inOwner)
{
}

AITwinSplineHelper* AITwinSplineTool::FImpl::GetSelectedSpline() const
{
	return selectedSplineHelper;
}

void AITwinSplineTool::FImpl::SetSelectedSpline(AITwinSplineHelper* splineHelper)
{
	selectedSplineHelper = splineHelper;
	selectedPointIndex = -1;

	owner.SplineSelectionEvent.Broadcast();
	owner.SplineEditionEvent.Broadcast();
}

void AITwinSplineTool::FImpl::SetSelectedPointIndex(int32 pointIndex)
{
	selectedPointIndex = pointIndex;

	owner.SplineEditionEvent.Broadcast();
}

int32 AITwinSplineTool::FImpl::GetSelectedPointIndex() const
{
	if (selectedSplineHelper)
		return selectedPointIndex;
	else
		return INDEX_NONE;
}

bool AITwinSplineTool::FImpl::HasSelection() const
{
	return IsValid(selectedSplineHelper);
}

void AITwinSplineTool::FImpl::DeleteSelection()
{
	if (HasSelection() && !bPreventDeletion)
	{
		// For some reason, at least on my machine, the DEL key event was triggered more than once, leading
		// to delete progressively the whole spline.
		// As I already experienced some strange behaviors specific to my machine, I prefer adding a basic
		// protection rather than modifying the blueprint managing those events.
		bPreventDeletion = true;

		BE_LOGI("ITwinAdvViz", "[SplineTool] Deleting selection (selected point: "
			<< selectedPointIndex << " / total number of points: " << selectedSplineHelper->GetNumberOfSplinePoints()
			<< ")");

		if (CanDeletePoint())
		{
			DeleteSelectedPoint();
		}
		else
		{
			DeleteSelectedSpline();
		}
		owner.SplineEditionEvent.Broadcast();

		// Re-enable deletion after a short delay.
		AITwinSplineTool* SplineToolActor = &owner;
		owner.GetWorld()->GetTimerManager().SetTimer(PreventDeletionTimerHandle,
			FTimerDelegate::CreateLambda([this, SplineToolActor, _ = TStrongObjectPtr<AITwinSplineTool>(SplineToolActor)]
		{
			if (IsValid(SplineToolActor))
				bPreventDeletion = false;
		}),
			0.5f /* in seconds*/, false);
	}
}

void AITwinSplineTool::FImpl::DeleteSpline(AITwinSplineHelper* SplineHelper)
{
	const bool bIsSelectedSpline = (SplineHelper == selectedSplineHelper);

	owner.SplineBeforeRemovedEvent.Broadcast(SplineHelper);

	SplineHelper->DeleteCartographicPolygons([this](ACesiumCartographicPolygon* Polygon)
	{
		RemoveCartographicPolygon(Polygon);
	});

	if (splinesManager)
	{
		splinesManager->RemoveSpline(SplineHelper->GetAVizSpline());
	}
	SplineHelper->Destroy();
	if (bIsSelectedSpline)
	{
		selectedSplineHelper = nullptr;
		selectedPointIndex = -1;
	}

	owner.SplineRemovedEvent.Broadcast();
	owner.SplineEditionEvent.Broadcast();
}

void AITwinSplineTool::FImpl::DeleteSelectedSpline()
{
	if (HasSelection())
	{
		DeleteSpline(selectedSplineHelper);
	}
}

bool AITwinSplineTool::FImpl::HasSelectedPoint() const
{
	return HasSelection() && selectedPointIndex >= 0;
}

bool AITwinSplineTool::FImpl::CanDeletePoint() const
{
	// The minimum number of spline points for a cut-out polygon is 3, because we can't cut anything
	// with a segment...
	return HasSelectedPoint() && selectedSplineHelper->CanDeletePoint();
}

void AITwinSplineTool::FImpl::DeleteSelectedPoint()
{
	if (CanDeletePoint())
	{
		selectedSplineHelper->DeletePoint(selectedPointIndex);

		// Select the next point in the spline (most of the time, we can just keep current index unchanged,
		// since the point was just removed. The only exception is when we deleted the last one => then we
		// loop...)
		if (selectedPointIndex >= selectedSplineHelper->GetNumberOfSplinePoints())
		{
			selectedPointIndex = 0;
		}
		if (ensure(selectedPointIndex < selectedSplineHelper->GetNumberOfSplinePoints()))
		{
			SetSelectedPointIndex(selectedPointIndex);

			owner.SplinePointRemovedEvent.Broadcast();
		}
		else
		{
			// Should not happen as we enforce keeping at least 3 points...
			selectedPointIndex = -1;

			owner.SplinePointRemovedEvent.Broadcast();
			owner.SplineEditionEvent.Broadcast();
		}
		TriggerDelayedRefresh();
	}
}

void AITwinSplineTool::FImpl::DuplicateSelectedPoint()
{
	if (HasSelectedPoint())
	{
		selectedSplineHelper->DuplicatePoint(selectedPointIndex);
	}
}

void AITwinSplineTool::FImpl::EnableDuplicationWhenMovingPoint(bool value)
{
	duplicateWhenMovingPoint = value;
}

FTransform AITwinSplineTool::FImpl::GetSelectionTransform() const
{
	if (IsValid(selectedSplineHelper))
	{
		if (selectedPointIndex >= 0 &&
			selectedPointIndex < selectedSplineHelper->GetNumberOfSplinePoints())
		{
			FVector pos = selectedSplineHelper->GetLocationAtSplinePoint(selectedPointIndex);
			FTransform tm = selectedSplineHelper->GetActorTransform();
			tm.SetTranslation(pos);
			return tm;
		}
		else
		{
			return selectedSplineHelper->GetTransformForUserInteraction();
		}
	}

	return FTransform();
}

void AITwinSplineTool::FImpl::SetSelectionTransform(const FTransform& transform)
{
	if (IsValid(selectedSplineHelper))
	{
		if (selectedPointIndex >= 0)
		{
			FVector position = transform.GetLocation();

			if (selectedSplineHelper->GetLocationAtSplinePoint(selectedPointIndex) !=
				position)
			{
				if (duplicateWhenMovingPoint)
				{
					selectedSplineHelper->DuplicatePoint(selectedPointIndex, position);
					duplicateWhenMovingPoint = false;
				}

				selectedSplineHelper->SetLocationAtSplinePoint(selectedPointIndex, position);
			}
		}
		else
		{
			selectedSplineHelper->SetTransformFromUserInteraction(transform);
		}
		owner.SplinePointMovedEvent.Broadcast(false /*bMovedInITS*/);

		TriggerDelayedRefresh();
	}
}

void AITwinSplineTool::FImpl::SetEnabled(bool value)
{
	if (value != this->bIsEnabled)
	{
		this->bIsEnabled = value;

		if (FImpl::bAutomaticSplineVisibility)
		{
			// Show/hide splines helpers matching the current usage
			TArray<AActor*> splineHelpers;
			UGameplayStatics::GetAllActorsOfClass(
				owner.GetWorld(), AITwinSplineHelper::StaticClass(), splineHelpers);
			for (auto splineHelper : splineHelpers)
			{
				bool bHideSpline = !this->bIsEnabled;
				if (this->bIsEnabled)
				{
					// Only display splines of the selected usage, and linked to the current model, if any.
					auto const SplHelper = Cast<AITwinSplineHelper>(splineHelper);
					bHideSpline = SplHelper->GetUsage() != this->GetUsage()
						|| SplHelper->GetLinkedModels() != this->CutoutTargetIdentifiers;
				}
				splineHelper->SetActorHiddenInGame(bHideSpline);
			}
		}

		if (this->bIsEnabled)
		{
			BuildListOfActorsToExcludeForPointInsertion();
		}
		else
		{
			SetSelectedSpline(nullptr);
		}
	}
}

bool AITwinSplineTool::FImpl::IsEnabled() const
{
	return bIsEnabled;
}

void AITwinSplineTool::FImpl::ResetToDefault()
{
}

void AITwinSplineTool::FImpl::BuildListOfActorsToExclude()
{
	ActorsExcludedFromPicking.Reset();
	if (GetUsage() == EITwinSplineUsage::MapCutout
		&& GetMode() == EITwinSplineToolMode::InteractiveCreation)
	{
		// Exclude all tilesets from picking, except those matching the selected layer.
		ITwin::IterateAllITwinTilesets([this](FITwinTilesetAccess const& TilesetAccess)
		{
			if (!CutoutTargetIdentifiers.contains(TilesetAccess.GetDecorationKey()))
			{
				const AActor* TilesetActor = TilesetAccess.GetTileset();
				if (TilesetActor)
				{
					ActorsExcludedFromPicking.Push(TilesetActor);
				}
			}
		}, owner.GetWorld());
	}
}

void AITwinSplineTool::FImpl::BuildListOfActorsToExcludeForPointInsertion()
{
	// For interactive point insertion, we exclude all tilesets
	ActorsExcludedFromPicking_PointInsertion.Reset();
	ITwin::IterateAllITwinTilesets([this](FITwinTilesetAccess const& TilesetAccess)
	{
		const AActor* TilesetActor = TilesetAccess.GetTileset();
		if (TilesetActor)
		{
			ActorsExcludedFromPicking_PointInsertion.Push(TilesetActor);
		}
	}, owner.GetWorld());
}

bool AITwinSplineTool::FImpl::DoMouseClickAction()
{
	// During interactive creation, we should ignore the last point, which always follows the mouse and thus
	// could "hide" the last validated spline point, preventing us from stopping the polygon though a
	// double-click or click on last point.
	TArray<UPrimitiveComponent*> ComponentsToIgnore;
	if (GetMode() == EITwinSplineToolMode::InteractiveCreation && HasSelection())
	{
		const int32 NumPoints = selectedSplineHelper->GetNumberOfSplinePoints();
		if (NumPoints > 0)
		{
			ComponentsToIgnore.Push(selectedSplineHelper->GetPointMeshComponent(NumPoints - 1));
		}
	}

	TArray<const AActor*> ActorsToToIgnore;
	// For point insertion, we should ignore tilesets (as done in Tick to change the cursor).
	if (GetMode() != EITwinSplineToolMode::InteractiveCreation)
	{
		for (auto const& Excluded : ActorsExcludedFromPicking_PointInsertion)
		{
			if (Excluded.IsValid())
				ActorsToToIgnore.Push(Excluded.Get());
		}
	}

	FITwinPickingResult PickingResult;
	FHitResult hitResult = owner.DoPickingAtMousePosition(&PickingResult, std::move(ActorsToToIgnore), std::move(ComponentsToIgnore));
	AActor* hitActor = hitResult.GetActor();

	if (GetMode() == EITwinSplineToolMode::InteractiveCreation)
	{
		if (hitActor)
		{
			if (!HasSelection())
			{
				// Start a new spline.
				if (bAutoSelectCutoutTarget)
				{
					// Determine the cut-out target from the hit layer.
					TilesetAccessArray Targets;
					auto TilesetAccess = ITwin::GetTilesetAccess(hitActor);
					if (TilesetAccess)
						Targets.Push(std::move(TilesetAccess));
					owner.SetCutoutTargets(std::move(Targets));

					BuildListOfActorsToExclude();
				}
				AITwinSplineHelper* NewSplineHelper = CreateSpline(ToolUsage, hitResult.ImpactPoint, {});
				SetSelectedSpline(NewSplineHelper);

				// Select the last point of the created spline
				if (NewSplineHelper)
				{
					SetSelectedPointIndex(NewSplineHelper->GetNumberOfSplinePoints() - 1);
				}
			}
			// If the user clicks on the 1st or last point of current polygon, stop the drawing.
			bool bStopDrawing = false;
			if (hitActor == selectedSplineHelper
				&& hitResult.GetComponent()
				&& hitResult.GetComponent()->IsA(UStaticMeshComponent::StaticClass()))
			{
				const int32 NumPoints = selectedSplineHelper->GetNumberOfSplinePoints();
				const int32 PointIndex = selectedSplineHelper->FindPointIndexFromMeshComponent(
					Cast<UStaticMeshComponent>(hitResult.GetComponent()));
				// NB: we test n-2 below, as this is the last point actually validated by the user ; the last
				// point, during interactive drawing, is always following the mouse...
				if (PointIndex != INDEX_NONE &&
					(PointIndex == 0 || PointIndex == NumPoints - 2))
				{
					bStopDrawing = true;
					ToggleInteractiveCreationMode();
				}
			}

			if (!bStopDrawing && HasSelectedPoint())
			{
				// Immediately duplicate the current point, so that the user can start moving the next one
				// interactively.
				DuplicateSelectedPoint();

				SetSelectedPointIndex(selectedSplineHelper->GetNumberOfSplinePoints() - 1);
			}
		}
	}
	else
	{
		SetSelectedSpline(nullptr);
		if (hitActor && hitActor->IsA(AITwinSplineHelper::StaticClass()))
		{
			AITwinSplineHelper* splineHelper = Cast<AITwinSplineHelper>(hitActor);
			SetSelectedSpline(splineHelper);

			if (hitResult.GetComponent())
			{
				// NB test USplineMeshComponent *before* UStaticMeshComponent, as USplineMeshComponent
				// inherits UStaticMeshComponent...
				if (hitResult.GetComponent()->IsA(USplineMeshComponent::StaticClass()))
				{
					// Insert new point when clicking on an existing segment.
					const int32 HitSegmentIndex = splineHelper->FindSegmentIndexFromSplineComponent(
						Cast<USplineMeshComponent>(hitResult.GetComponent()));
					if (HitSegmentIndex != INDEX_NONE)
					{
						int32 NewPointIndex = splineHelper->InsertPointAt(HitSegmentIndex + 1, hitResult.ImpactPoint);
						if (NewPointIndex != INDEX_NONE)
						{
							SetSelectedPointIndex(NewPointIndex);
							owner.SplinePointSelectedEvent.Broadcast();
							owner.InteractiveCreationCompletedEvent.Broadcast();
						}
					}
				}
				else if (hitResult.GetComponent()->IsA(UStaticMeshComponent::StaticClass()))
				{
					int32 pointIndex = splineHelper->FindPointIndexFromMeshComponent(
						Cast<UStaticMeshComponent>(hitResult.GetComponent()));

					if (pointIndex != INDEX_NONE)
					{
						SetSelectedPointIndex(pointIndex);
						owner.SplinePointSelectedEvent.Broadcast();
					}
				}
			}
			return true;
		}
		// Test if the line intersects the interior of one of the splines.
		// We use a coarse approximation (polygon composed of the control points), which is correct for
		// cartographic polygons...
		for (TActorIterator<AITwinSplineHelper> SplineIter(owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if (SplineIter->GetUsage() == this->GetUsage())
			{
				if (SplineIter->DoesLineIntersectSplinePolygon(PickingResult.TraceStart, PickingResult.TraceEnd))
				{
					SetSelectedSpline(*SplineIter);
					return true;
				}
			}
		}
	}
	return false;
}

inline
FHitResult AITwinSplineTool::FImpl::DoPickingAtMousePosition(const TArray<TWeakObjectPtr<const AActor>>& ExcludedActors) const
{
	TArray<const AActor*> ActorsToExclude;
	for (auto const& Excluded : ExcludedActors)
	{
		if (Excluded.IsValid())
			ActorsToExclude.Push(Excluded.Get());
	}
	return owner.DoPickingAtMousePosition(nullptr, std::move(ActorsToExclude));
}

bool AITwinSplineTool::FImpl::ActionOnTick(float DeltaTime)
{
	if (GetMode() != EITwinSplineToolMode::InteractiveCreation)
	{
		if (IsEnabled())
		{
			// Change the cursor if the mouse is hovering one spline segment (in this case, a click will
			// insert a point).
			int32 HitSegmentIndex = INDEX_NONE;
			FHitResult HitResult = DoPickingAtMousePosition(ActorsExcludedFromPicking_PointInsertion);
			AActor* HitActor = HitResult.GetActor();
			if (HitActor && HitActor->IsA(AITwinSplineHelper::StaticClass())
				&& HitResult.GetComponent()
				&& HitResult.GetComponent()->IsA(USplineMeshComponent::StaticClass()))
			{
				HitSegmentIndex = Cast<AITwinSplineHelper>(HitActor)->FindSegmentIndexFromSplineComponent(
					Cast<USplineMeshComponent>(HitResult.GetComponent()));
			}
			APlayerController* PlayerController = owner.GetWorld()->GetFirstPlayerController();
			if (PlayerController)
			{
				if (HitSegmentIndex != INDEX_NONE)
				{
					PlayerController->CurrentMouseCursor = EMouseCursor::Crosshairs;
				}
				else
				{
					PlayerController->CurrentMouseCursor = PlayerController->DefaultMouseCursor;
				}
			}
		}

		return false;
	}
	if (!HasSelectedPoint())
		return false;

	// Try to project the selected point on the object below it, under the mouse.
	// Note that we restrict the picking on the layer currently selected for polygon creation:
	FHitResult HitResult = DoPickingAtMousePosition(ActorsExcludedFromPicking);
	AActor* HitActor = HitResult.GetActor();
	if (HitActor && !HitActor->IsA(AITwinSplineHelper::StaticClass()))
	{
		FTransform TM = selectedSplineHelper->GetActorTransform();
		TM.SetTranslation(HitResult.ImpactPoint);
		SetSelectionTransform(TM);
		return true;
	}
	return false;
}


namespace ITwin
{
	bool RemoveCutoutPolygon(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon)
	{
		if (!TilesetAccess.HasTileset())
			return false;
		ACesium3DTileset& Tileset = *TilesetAccess.GetMutableTileset();
		bool bRemoved = false;
		UCesiumPolygonRasterOverlay* RasterOverlay = GetCutoutOverlay(Tileset);
		if (RasterOverlay)
		{
			const int32 index = RasterOverlay->Polygons.Find(Polygon);
			if (index != INDEX_NONE)
			{
				RasterOverlay->Polygons.RemoveAt(index);
				if (RasterOverlay->InvertSelection &&
					RasterOverlay->Polygons.IsEmpty())
				{
					// If no more polygons are left in an inverted raster overlay, one should totally disable
					// it or the tileset will be totally invisible!
					RasterOverlay->ExcludeSelectedTiles = false;
				}
				bRemoved = true;
			}
		}
		if (bRemoved)
		{
			TilesetAccess.RefreshTileset();
		}
		return bRemoved;
	}

	inline bool AddCutoutPolygonToOverlay(UCesiumPolygonRasterOverlay& RasterOverlay, ACesiumCartographicPolygon* Polygon)
	{
		if (RasterOverlay.Polygons.Find(Polygon) != INDEX_NONE)
			return false;

		RasterOverlay.Polygons.Push(Polygon);
		// If we have previously flipped this polygon effect, we may have deactivated the raster overlay
		// (see #RemoveCutoutPolygon above) which contained it before => ensure we always reactivate overlays
		// with active polygons:
		RasterOverlay.ExcludeSelectedTiles = true;

		return true;
	}

	bool AddCutoutPolygon(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon)
	{
		ACesium3DTileset* Tileset = TilesetAccess.GetMutableTileset();
		if (!Tileset)
			return false;
		bool bAdded = false;
		UCesiumPolygonRasterOverlay* RasterOverlay = GetCutoutOverlay(*Tileset);
		if (!RasterOverlay)
		{
			InitCutoutOverlay(*Tileset);
			RasterOverlay = GetCutoutOverlay(*Tileset);
		}
		if (ensure(RasterOverlay))
		{
			bAdded = AddCutoutPolygonToOverlay(*RasterOverlay, Polygon);
		}
		if (bAdded)
		{
			TilesetAccess.RefreshTileset();
		}
		return bAdded;
	}

	void InvertCutoutPolygonEffect(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon, bool bInvertEffect)
	{
		ACesium3DTileset* Tileset = TilesetAccess.GetMutableTileset();
		if (!Tileset)
			return;
		bool bModified = false;
		// We cannot just invert an individual polygon, as it quickly hides everything (having an overlay
		// that hides all but the polygon and a second one that shows only other polygons it contain).
		UCesiumPolygonRasterOverlay* RasterOverlay = GetCutoutOverlay(*Tileset);
		if (!RasterOverlay)
		{
			InitCutoutOverlay(*Tileset);
			RasterOverlay = GetCutoutOverlay(*Tileset);
			if (ensure(RasterOverlay))
			{
				bModified = AddCutoutPolygonToOverlay(*RasterOverlay, Polygon);
			}
		}
		if (ensure(RasterOverlay))
		{
			bModified |= (RasterOverlay->InvertSelection != bInvertEffect);
			bModified |= !RasterOverlay->ExcludeSelectedTiles;
			RasterOverlay->InvertSelection = bInvertEffect;
			RasterOverlay->ExcludeSelectedTiles = true;
		}
		if (bModified)
		{
			TilesetAccess.RefreshTileset();
		}
	}
}

namespace
{
	/// Helper class to instantiate AITwinSplineHelper, either from a spline loaded from the decoration
	/// service, or from scratch, with a new reference position.
	/// This class is abstract, as the implementation will depend on the specific spline usage.
	class ISplineHelperMaker
	{
	public:
		virtual ~ISplineHelperMaker();

		using ModelIdentifier = ITwin::ModelDecorationIdentifier;

		AITwinSplineHelper* MakeSplineHelper(UWorld& World, bool bCreateAsHidden,
			AdvViz::SDK::ISplinesManager& SplineManager,
			std::set<ModelIdentifier> const& LinkedModels);

		bool IsLoadingFromAdvViz() const { return (bool)LoadedSpline; }

	protected:
		ISplineHelperMaker(
			TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InLoadedSpline,
			EITwinSplineUsage const InUsage);

		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) = 0;

		virtual void OnSplineCreated(AITwinSplineHelper& SplineHelper) {}


		TSoftObjectPtr<ACesiumGeoreference> const Georeference;
		EITwinSplineToolMode const ToolMode;
		std::optional<FVector> const PositionOpt;
		AdvViz::SDK::SharedSpline const LoadedSpline;
		EITwinSplineUsage const SplineUsage;

		EITwinTangentMode TangentMode = EITwinTangentMode::Custom;
	};

	ISplineHelperMaker::ISplineHelperMaker(
		TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InLoadedSpline,
		EITwinSplineUsage const InUsage)
		: Georeference(InGeoreference)
		, ToolMode(InToolMode)
		, PositionOpt(InPositionOpt)
		, LoadedSpline(InLoadedSpline)
		, SplineUsage(InUsage)
	{}

	ISplineHelperMaker::~ISplineHelperMaker()
	{

	}

	AITwinSplineHelper* ISplineHelperMaker::MakeSplineHelper(UWorld& World, bool bCreateAsHidden,
		AdvViz::SDK::ISplinesManager& SplineManager,
		std::set<ModelIdentifier> const& LinkedModels)
	{
		ACesiumCartographicPolygon* CartographicPolygon = nullptr;
		USplineComponent* SplineComp = GetSplineComponent(World, CartographicPolygon);

		const bool bUseCartographicSplineComponent = (SplineUsage == EITwinSplineUsage::MapCutout);
		if (bUseCartographicSplineComponent && !ensure(SplineComp))
		{
			return nullptr;
		}

		AITwinSplineHelper* SplineHelper = nullptr;
		{
			const AITwinSplineHelper::FSpawnContext UsageContext(SplineUsage);

			SplineHelper = World.SpawnActor<AITwinSplineHelper>();
		}

		// For a generic spline, the spline component should have been created during the spawning of the
		// helper (thanks to the FSpawnContext above).
		if (!bUseCartographicSplineComponent)
		{
			SplineComp = SplineHelper->GetSplineComponent();
		}
		if (!ensure(SplineComp))
		{
			return nullptr;
		}
		const bool bCreateDefaultSpline = !LoadedSpline;
		if (bCreateDefaultSpline)
		{
			// Reduce the area of the default polygon
			for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
			{
				FVector pos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
				SplineComp->SetLocationAtSplinePoint(i, pos * 0.25, ESplineCoordinateSpace::Local);
			}
		}
		else
		{
			// Set number of points to 0, it will be synced with the loaded spline later
			// (The default 1 point "breaks" the sync - see AITwinSplineHelper::Initialize())
			SplineComp->SetSplinePoints(TArray<FVector>{}, ESplineCoordinateSpace::Local);
		}

		SplineHelper->GlobeAnchor->SetGeoreference(Georeference);
		if (PositionOpt)
		{
			SplineHelper->SetActorLocation(*PositionOpt);
		}
		else
		{
			// Disable automatic orientation, as we want to impose the transformation from the points just
			// loaded from the decoration service.
			SplineHelper->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
		}

		if (CartographicPolygon)
		{
			SplineHelper->SetCartographicPolygonForGeoref(CartographicPolygon, Georeference);
		}

		AdvViz::SDK::SharedSpline Spline = LoadedSpline;
		if (!Spline)
		{
			// Instantiate the AdvViz spline for this new polygon.
			Spline = SplineManager.AddSpline();
			Spline->SetUsage(static_cast<AdvViz::SDK::ESplineUsage>(SplineUsage));

			std::vector<AdvViz::SDK::SplineLinkedModel> AdvVizLinkedModels;
			for (auto const& LinkedModel : LinkedModels)
			{
				AdvVizLinkedModels.push_back({
					ITwin::ModelTypeToString(LinkedModel.first),
					TCHAR_TO_UTF8(*LinkedModel.second)
				});
			}
			Spline->SetLinkedModels(AdvVizLinkedModels);
		}
		SplineHelper->Initialize(SplineComp, Spline);

		if (bCreateDefaultSpline)
		{
			SplineHelper->SetTangentMode(this->TangentMode);
		}
		else
		{
			SplineHelper->SetActorHiddenInGame(bCreateAsHidden);

			// After the initialization, since the transformation of the AdvViz::SDK spline has been
			// applied to UE actors, the automatic orientation can be re-enabled.
			if (CartographicPolygon)
			{
				CartographicPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
			}
			SplineHelper->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
		}

		OnSplineCreated(*SplineHelper);

		return SplineHelper;
	}

	/// Special implementation for Cutout polygon
	class FCutoutPolygonMaker : public ISplineHelperMaker
	{
	public:
		struct FTilesetData
		{
			FITwinTilesetAccess const* TilesetAccess = nullptr;
			UCesiumPolygonRasterOverlay* const RasterOverlay = nullptr;

			bool HasTileset() const {
				return TilesetAccess && TilesetAccess->HasTileset();
			}
		};
		FCutoutPolygonMaker(
			TArray<FTilesetData> const& InTilesetData,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InSpline);

	protected:
		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) override;

		virtual void OnSplineCreated(AITwinSplineHelper& SplineHelper) override;

		static std::vector<ACesiumGeoreference*> GetUniqueGeoRefs(TArray<FTilesetData> const& InTilesetData);
		static TSoftObjectPtr<ACesiumGeoreference> GetGeoRef(FTilesetData const& InTilesetData);
		static TSoftObjectPtr<ACesiumGeoreference> GetFirstGeoRef(TArray<FTilesetData> const& InTilesetData);

	private:
		TArray<FTilesetData> TilesetData;
		ACesiumCartographicPolygon* CartographicPolygon = nullptr;
	};

	/*static*/
	TSoftObjectPtr<ACesiumGeoreference> FCutoutPolygonMaker::GetGeoRef(FTilesetData const& InData)
	{
		if (InData.HasTileset())
		{
			return InData.TilesetAccess->GetTileset()->GetGeoreference();
		}
		return nullptr;
	}

	/*static*/
	std::vector<ACesiumGeoreference*> FCutoutPolygonMaker::GetUniqueGeoRefs(TArray<FTilesetData> const& InTilesetData)
	{
		std::vector<ACesiumGeoreference*> UniqueGeorefs;
		std::set<ACesiumGeoreference*> GeorefsSet;
		for (auto const& Data : InTilesetData)
		{
			auto const GeoRef = GetGeoRef(Data);
			if (GeoRef && GeorefsSet.insert(GeoRef.Get()).second)
			{
				UniqueGeorefs.push_back(GeoRef.Get());
			}
		}
		return UniqueGeorefs;
	}

	/*static*/
	TSoftObjectPtr<ACesiumGeoreference> FCutoutPolygonMaker::GetFirstGeoRef(TArray<FTilesetData> const& InTilesetData)
	{
		auto const Georefs = GetUniqueGeoRefs(InTilesetData);
		if (!Georefs.empty())
		{
			return *Georefs.begin();
		}
		else
		{
			return nullptr;
		}
	}

	FCutoutPolygonMaker::FCutoutPolygonMaker(TArray<FTilesetData> const& InTilesetData,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InSpline)
		: ISplineHelperMaker(GetFirstGeoRef(InTilesetData), InToolMode, InPositionOpt, InSpline,
			EITwinSplineUsage::MapCutout)
		, TilesetData(InTilesetData)
	{
		// Cesium cutout polygons only support linear tangent mode.
		TangentMode = EITwinTangentMode::Linear;
	}

	USplineComponent* FCutoutPolygonMaker::GetSplineComponent(UWorld& World,
		ACesiumCartographicPolygon*& OutCartographicPolygon)
	{
		if (TilesetData.IsEmpty())
		{
			return nullptr;
		}

		// Create a Cesium cartographic polygon
		CartographicPolygon = World.SpawnActor<ACesiumCartographicPolygon>();
		CartographicPolygon->GlobeAnchor->SetGeoreference(Georeference);
		if (PositionOpt)
		{
			CartographicPolygon->SetActorLocation(*PositionOpt);
		}
		if (!PositionOpt || ToolMode == EITwinSplineToolMode::InteractiveCreation)
		{
			CartographicPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
			// We will replace the default spline points by those defined by the AdvViz spline.
			CartographicPolygon->Polygon->ClearSplinePoints();
		}
		if (ToolMode == EITwinSplineToolMode::InteractiveCreation)
		{
			// Interactive creation mode starts with just one point.
			CartographicPolygon->Polygon->SetSplinePoints(
				TArray<FVector>{
					FVector(0.0f, 0.0f, 0.0f)},
				ESplineCoordinateSpace::Local);
		}
		// Add the new polygon to all target tilesets sharing the master geo-ref
		for (auto const& Data: TilesetData)
		{
			if (GetGeoRef(Data) == Georeference)
				ITwin::AddCutoutPolygonToOverlay(*Data.RasterOverlay, CartographicPolygon);
		}
		OutCartographicPolygon = CartographicPolygon;
		return CartographicPolygon->Polygon;
	}

	void FCutoutPolygonMaker::OnSplineCreated(AITwinSplineHelper& SplineHelper)
	{
		// Clone the cartographic polygon (as many times as the number of differing geo-references).
		for (auto const& Data : TilesetData)
		{
			auto const Georef = GetGeoRef(Data);
			if (Georef && Georef != Georeference && ensure(Data.TilesetAccess))
			{
				SplineHelper.ActivateCutoutEffect(*Data.TilesetAccess, true, /*bIsCreatingSpline*/true);
			}
		}

		// In interactive creation mode, the polygon is created with only one point => no need to refresh the
		// tileset now, as this will have no effect but showing the tileset off then on...
		if (ToolMode != EITwinSplineToolMode::InteractiveCreation)
		{
			for (auto const& Data: TilesetData)
			{
				Data.TilesetAccess->RefreshTileset();
			}
		}
	}


	/// Generic implementation for all other usages
	class FGenericSplineHelperMaker : public ISplineHelperMaker
	{
	public:
		FGenericSplineHelperMaker(
			TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InLoadedSpline,
			EITwinSplineUsage const InUsage);

	protected:
		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) override {
			OutCartographicPolygon = nullptr;
			return nullptr;
		}
	};

	FGenericSplineHelperMaker::FGenericSplineHelperMaker(
		TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InLoadedSpline,
		EITwinSplineUsage const InUsage)
		: ISplineHelperMaker(InGeoreference, InToolMode, InPositionOpt, InLoadedSpline, InUsage)
	{
		ensureMsgf(SplineUsage != EITwinSplineUsage::MapCutout,
			TEXT("use FCutoutPolygonMaker for cutout polygon"));
	}
}

template <typename TFunc>
void AITwinSplineTool::FImpl::ForEachTargetTileset(TFunc const& Func) const
{
	for (auto const& AccesstPtr : owner.CutoutTargets)
	{
		if (AccesstPtr && AccesstPtr->IsValid())
		{
			Func(*AccesstPtr);
		}
	}
}

AITwinSplineHelper* AITwinSplineTool::FImpl::CreateSpline(EITwinSplineUsage SplineUsage,
	std::optional<FVector> const& PositionOpt, AdvViz::SDK::SharedSpline const& LoadedSpline)
{
	if ((!PositionOpt) == (!LoadedSpline))
	{
		ensureMsgf(false, TEXT("coding mistake: provide either a position or an AdvViz spline"));
		return nullptr;
	}
	if (!splinesManager)
	{
		ensureMsgf(false, TEXT("no spline manager"));
		return nullptr;
	}
	UWorld* World = owner.GetWorld();
	if (!World)
	{
		ensureMsgf(false, TEXT("no world to instantiate spline"));
		return nullptr;
	}
	ensure(SplineUsage != EITwinSplineUsage::Undefined);

	std::unique_ptr<ISplineHelperMaker> SplineMaker;

	if (SplineUsage == EITwinSplineUsage::MapCutout)
	{
		TArray<FCutoutPolygonMaker::FTilesetData> LinkedTilesets;
		ForEachTargetTileset([&LinkedTilesets](FITwinTilesetAccess const& TilesetAccess)
		{
			if (TilesetAccess.HasTileset())
			{
				ACesium3DTileset& Tileset = *TilesetAccess.GetMutableTileset();
				ITwin::InitCutoutOverlay(Tileset);
				auto* RasterOverlay = ITwin::GetCutoutOverlay(Tileset);
				if (ensure(RasterOverlay))
				{
					LinkedTilesets.Add({ &TilesetAccess, RasterOverlay });
				}
			}
		});
		if (!LinkedTilesets.IsEmpty())
		{
			SplineMaker = std::make_unique<FCutoutPolygonMaker>(
				LinkedTilesets, owner.GetMode(), PositionOpt, LoadedSpline);
		}
		ensureMsgf(SplineMaker.get() != nullptr, TEXT("no tileset ready for cut-out polygon creation"));
	}
	else
	{
		// Generic spline creation.
		auto&& Geoloc = FITwinGeolocation::Get(*World);
		SplineMaker = std::make_unique<FGenericSplineHelperMaker>(
			Geoloc->GeoReference.Get(), owner.GetMode(), PositionOpt, LoadedSpline, SplineUsage);
	}

	AITwinSplineHelper* const CreatedSpline = SplineMaker
		 ? SplineMaker->MakeSplineHelper(*World, !bIsEnabled, *splinesManager, CutoutTargetIdentifiers)
		 : nullptr;

	if (CreatedSpline)
	{
		// In interactive creation mode, do not broadcast the creation event until the spline is fully
		// created.
		if (ToolMode != EITwinSplineToolMode::InteractiveCreation)
		{
			owner.SplineAddedEvent.Broadcast(CreatedSpline);
			owner.SplineEditionEvent.Broadcast();
		}
	}
	else
	{
		ensureMsgf(false, TEXT("no spline created for usage %d"), static_cast<int>(SplineUsage));
	}
	return CreatedSpline;
}

AITwinSplineHelper* AITwinSplineTool::FImpl::AddSpline(FVector const& Position)
{
	return CreateSpline(ToolUsage, Position, {});
}

bool AITwinSplineTool::FImpl::LoadSpline(const AdvViz::SDK::SharedSpline& Spline)
{
	if (!Spline)
	{
		ensureMsgf(false, TEXT("no spline in input!"));
		return false;
	}
	return CreateSpline(static_cast<EITwinSplineUsage>(Spline->GetUsage()), {}, Spline) != nullptr;
}

void AITwinSplineTool::FImpl::RemoveCartographicPolygon(ACesiumCartographicPolygon* Polygon)
{
	// Here we iterate on all tilesets in scene (in case we are deleting a spline which is not currently
	// edited...)
	ITwin::IterateAllITwinTilesets([Polygon](FITwinTilesetAccess const& TilesetAccess)
	{
		ITwin::RemoveCutoutPolygon(TilesetAccess, Polygon);
	}, owner.GetWorld());
}

bool AITwinSplineTool::FImpl::GetInvertSelectedSplineEffect() const
{
	if (!selectedSplineHelper)
		return false;
	auto const AvizSpline = selectedSplineHelper->GetAVizSpline();
	if (!AvizSpline)
		return false;
	return AvizSpline->GetInvertEffect();
}

void AITwinSplineTool::FImpl::SetInvertSelectedSplineEffect(bool bInvertEffect)
{
	if (!selectedSplineHelper)
		return;
	auto const AVizSpline = selectedSplineHelper->GetAVizSpline();
	if (!AVizSpline || AVizSpline->GetInvertEffect() == bInvertEffect)
		return;
	if (AVizSpline->GetUsage() == AdvViz::SDK::ESplineUsage::MapCutout)
	{
		ForEachTargetTileset([this, bInvertEffect](FITwinTilesetAccess const& TilesetAccess)
		{
			selectedSplineHelper->InvertCutoutEffect(TilesetAccess, bInvertEffect);
		});
	}
	// Notify persistence manager.
	AVizSpline->SetInvertEffect(bInvertEffect);
}

bool AITwinSplineTool::FImpl::GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const
{
	if (HasSelection())
	{
		RefLocation = selectedSplineHelper->GetActorLocation();
		return selectedSplineHelper->IncludeInWorldBox(OutBox);
	}
	else
	{
		int NumValidSplines = 0;
		for (TActorIterator<AITwinSplineHelper> SplineIter(owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if (SplineIter->IncludeInWorldBox(OutBox))
				NumValidSplines++;
		}
		if (NumValidSplines > 0)
		{
			RefLocation = OutBox.GetCenter();
		}
		return (NumValidSplines > 0);
	}
}


bool AITwinSplineTool::FImpl::GetOverviewCameraReferencePosition(FVector& RefLocation, FBox& OutBox) const
{
	if (GetSplineReferencePosition(RefLocation, OutBox))
	{
		return true;
	}

	// For the first created polygon, try to take the existing iModels as reference.
	FBox AllIModelsBox;
	for (TActorIterator<AITwinIModel> IModelIter(owner.GetWorld()); IModelIter; ++IModelIter)
	{
		FBox IModelBBox;
		if (IModelIter->GetBoundingBox(IModelBBox, /*bClampOutlandishValues*/true))
		{
			AllIModelsBox += IModelBBox;
		}
	}
	if (AllIModelsBox.IsValid)
	{
		RefLocation = AllIModelsBox.GetCenter();
		RefLocation.Z = AllIModelsBox.Max.Z;
		OutBox = AllIModelsBox;
		return true;
	}

	// If the scene only contains a Google tileset or reality data, don't try to move the camera.
	return false;
}

void AITwinSplineTool::FImpl::ToggleInteractiveCreationMode(bool bInAutoSelectCutoutTarget /*= false*/)
{
	const EITwinSplineToolMode PreviousMode = ToolMode;

	bool bHasNewSpline = false;
	TWeakObjectPtr<AITwinSplineHelper> NewSpline = nullptr;
	if (PreviousMode == EITwinSplineToolMode::InteractiveCreation
		&& HasSelectedPoint())
	{
		// Discard the last duplicated point.
		// If the newly create spline has not enough points, remove it at once.
		if (CanDeletePoint())
		{
			NewSpline = selectedSplineHelper; // Store value before it is reset in DeleteSelectedPoint
			DeleteSelectedPoint();
			bHasNewSpline = true;
		}
		else
		{
			DeleteSelectedSpline();
		}
	}

	if (bHasNewSpline)
	{
		// End of the creation of a spline in interactive mode => refresh scene and broadcast creation event.
		RefreshScene(NewSpline.Get());

		owner.SplineAddedEvent.Broadcast(NewSpline.Get());
		owner.SplineEditionEvent.Broadcast();
		owner.InteractiveCreationCompletedEvent.Broadcast();
	}

	ActorsExcludedFromPicking.Reset();

	ToolMode = (PreviousMode == EITwinSplineToolMode::InteractiveCreation)
		? EITwinSplineToolMode::Undefined
		: EITwinSplineToolMode::InteractiveCreation;

	if (ToolMode == EITwinSplineToolMode::InteractiveCreation)
	{
		// Deselect any spline before creating a new one.
		SetSelectedSpline(nullptr);

		// Avoid conflict with slightly similar feature...
		EnableDuplicationWhenMovingPoint(false);

		bAutoSelectCutoutTarget = bInAutoSelectCutoutTarget;
	}
}

void AITwinSplineTool::FImpl::AbortInteractiveCreation()
{
	if (ToolMode != EITwinSplineToolMode::InteractiveCreation)
		return;
	DeleteSelectedSpline();
	SetMode(EITwinSplineToolMode::Undefined);
}

void AITwinSplineTool::FImpl::RefreshScene(AITwinSplineHelper const* TargetSpline /*= nullptr*/)
{
	if (GetUsage() == EITwinSplineUsage::MapCutout)
	{
		// Refresh the target tilesets.
		ForEachTargetTileset([](FITwinTilesetAccess const& TilesetAccess)
		{
			TilesetAccess.RefreshTileset();
		});
	}
	else if ((GetUsage() == EITwinSplineUsage::PopulationPath
		   || GetUsage() == EITwinSplineUsage::PopulationZone)
		&& owner.PopulationTool.IsValid())
	{
		// (Re-)populate the active spline.
		AITwinSplineHelper const* SplineToPopulate = TargetSpline
			? TargetSpline
			: (HasSelection() ? selectedSplineHelper : nullptr);
		if (SplineToPopulate)
		{
			owner.PopulationTool->PopulateSpline(*SplineToPopulate);
		}
	}
}

void AITwinSplineTool::FImpl::TriggerDelayedRefresh()
{
	if (ToolMode == EITwinSplineToolMode::InteractiveCreation)
	{
		// Forbid (costly) tileset refreshes while creating a new polygon.
		return;
	}
	AITwinSplineTool* SplineToolActor = &owner;
	// If the user makes no modification in the interval, the refresh will occur in 3 seconds.
	const float RefreshDelay = 3.f;
	// Note that this does replace the callback, so if a pending refresh was already there, it will be
	// discarded, which is exactly what we want here.
	owner.GetWorld()->GetTimerManager().SetTimer(RefreshTimerHandle,
		FTimerDelegate::CreateLambda([this, SplineToolActor, _ = TStrongObjectPtr<AITwinSplineTool>(SplineToolActor)]
	{
		if (IsValid(SplineToolActor))
			RefreshScene();
	}),
		RefreshDelay, false);
}

EITwinTangentMode AITwinSplineTool::FImpl::GetTangentMode() const
{
	if (HasSelection())
	{
		return selectedSplineHelper->GetTangentMode();
	}
	else
	{
		return EITwinTangentMode::Custom;
	}
}

void AITwinSplineTool::FImpl::SetTangentMode(EITwinTangentMode TangentMode)
{
	if (HasSelection())
	{
		selectedSplineHelper->SetTangentMode(TangentMode);
	}
}

void AITwinSplineTool::FImpl::StartCameraMovement(FTransform const& CamTransform, float BlendTime)
{
	if (OverviewCamera)
	{
		OverviewCamera->Destroy();
		OverviewCamera = nullptr;
	}
	UWorld* World = owner.GetWorld();
	APlayerController* PlayerController = ensure(World) ? World->GetFirstPlayerController() : nullptr;
	if (!ensure(PlayerController))
		return;
	OverviewCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CamTransform);
	OverviewCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlayerController->SetViewTargetWithBlend(OverviewCamera, BlendTime, VTBlend_Linear, 0, true);
}

void AITwinSplineTool::FImpl::EndCameraMovement(FTransform const& Transform)
{
	if (!ensure(OverviewCamera))
		return;

	OverviewCamera->Destroy();
	OverviewCamera = nullptr;

	const UWorld* World = owner.GetWorld();
	APlayerController* PlayerController = ensure(World) ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = ensure(PlayerController) ? PlayerController->GetPawnOrSpectator() : nullptr;
	if (!ensure(Pawn))
		return;

	Pawn->SetActorLocation(Transform.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	FRotator Rot = Transform.Rotator();
	PlayerController->SetControlRotation(Rot);
	PlayerController->GetPawnOrSpectator()->SetActorRotation(Rot);
	PlayerController->SetViewTargetWithBlend(Pawn);
}


// -----------------------------------------------------------------------------
//                              AITwinSplineTool

AITwinSplineTool::AITwinSplineTool()
	: AITwinInteractiveTool(), Impl(MakePimpl<FImpl>(*this))
{
	// Set this actor to call Tick() every frame.
	PrimaryActorTick.bCanEverTick = true;
}

void AITwinSplineTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->ActionOnTick(DeltaTime);
}

AITwinSplineHelper* AITwinSplineTool::GetSelectedSpline() const
{
	return Impl->GetSelectedSpline();
}

void AITwinSplineTool::SetSelectedSpline(AITwinSplineHelper* splineHelper)
{
	Impl->SetSelectedSpline(splineHelper);
}

void AITwinSplineTool::SetSelectedPointIndex(int32 pointIndex)
{
	Impl->SetSelectedPointIndex(pointIndex);
}

int32 AITwinSplineTool::GetSelectedPointIndex() const
{
	return Impl->GetSelectedPointIndex();
}

bool AITwinSplineTool::HasSelectionImpl() const
{
	return Impl->HasSelection();
}

bool AITwinSplineTool::HasSelectedPoint() const
{
	return Impl->HasSelectedPoint();
}

bool AITwinSplineTool::HasSplines() const
{
	return Impl->splinesManager && Impl->splinesManager->HasSplines();
}

void AITwinSplineTool::DeleteSelectionImpl()
{
	Impl->DeleteSelection();
}

void AITwinSplineTool::DeleteSelectedSpline()
{
	Impl->DeleteSelectedSpline();
}

void AITwinSplineTool::DeleteSpline(AITwinSplineHelper* SplineHelper)
{
	Impl->DeleteSpline(SplineHelper);
}

bool AITwinSplineTool::CanDeletePoint() const
{
	return Impl->CanDeletePoint();
}

void AITwinSplineTool::DeleteSelectedPoint()
{
	Impl->DeleteSelectedPoint();
}

void AITwinSplineTool::DuplicateSelectedPoint()
{
	Impl->DuplicateSelectedPoint();
}

void AITwinSplineTool::EnableDuplicationWhenMovingPoint(bool value)
{
	Impl->EnableDuplicationWhenMovingPoint(value);
}

FTransform AITwinSplineTool::GetSelectionTransformImpl() const
{
	return Impl->GetSelectionTransform();
}

void AITwinSplineTool::SetSelectionTransformImpl(const FTransform& transform)
{
	Impl->SetSelectionTransform(transform);
}

void AITwinSplineTool::SetEnabledImpl(bool bValue)
{
	Impl->SetEnabled(bValue);
}

bool AITwinSplineTool::IsEnabledImpl() const
{
	return Impl->IsEnabled();
}

void AITwinSplineTool::ResetToDefaultImpl()
{
	Impl->ResetToDefault();
}

bool AITwinSplineTool::DoMouseClickActionImpl()
{
	return Impl->DoMouseClickAction();
}

AITwinSplineHelper* AITwinSplineTool::AddSpline(FVector const& Position)
{
	return Impl->AddSpline(Position);
}

bool AITwinSplineTool::LoadSpline(const std::shared_ptr<AdvViz::SDK::ISpline>& spline,
	TilesetAccessArray&& InCutoutTargets /*= {}*/)
{
	Be::CleanUpGuard restoreGuard([this, bIsLoadingSplineOld = Impl->bIsLoadingSpline]
	{
		Impl->bIsLoadingSpline = bIsLoadingSplineOld;
	});
	Impl->bIsLoadingSpline = true;
	SetCutoutTargets(std::move(InCutoutTargets));
	return Impl->LoadSpline(spline);
}

void AITwinSplineTool::SetSplinesManager(const std::shared_ptr<AdvViz::SDK::ISplinesManager>& splinesManager)
{
	Impl->splinesManager = splinesManager;
}

bool AITwinSplineTool::GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const
{
	return Impl->GetSplineReferencePosition(RefLocation, OutBox);
}

EITwinSplineToolMode AITwinSplineTool::GetMode() const
{
	return Impl->GetMode();
}

void AITwinSplineTool::SetMode(EITwinSplineToolMode NewMode)
{
	Impl->SetMode(NewMode);
}

void AITwinSplineTool::ToggleInteractiveCreationMode(bool bAutoSelectCutoutTarget /*= false*/)
{
	Impl->ToggleInteractiveCreationMode(bAutoSelectCutoutTarget);
}

bool AITwinSplineTool::StartInteractiveCreationImpl()
{
	if (GetMode() == EITwinSplineToolMode::InteractiveCreation)
	{
		// Abort previous creation.
		Impl->AbortInteractiveCreation();
	}
	const bool bAutoSelectCutoutTarget = GetUsage() == EITwinSplineUsage::MapCutout
		&& CutoutTargets.IsEmpty();

	Impl->ToggleInteractiveCreationMode(bAutoSelectCutoutTarget);
	return IsInteractiveCreationModeImpl();
}

bool AITwinSplineTool::IsInteractiveCreationModeImpl() const
{
	return GetMode() == EITwinSplineToolMode::InteractiveCreation;
}

void AITwinSplineTool::AbortInteractiveCreation()
{
	Impl->AbortInteractiveCreation();
}

EITwinSplineUsage AITwinSplineTool::GetUsage() const
{
	return Impl->GetUsage();
}

void AITwinSplineTool::SetUsage(EITwinSplineUsage NewUsage)
{
	Impl->SetUsage(NewUsage);
}

void AITwinSplineTool::SetCutoutTargets(TilesetAccessArray&& CutoutTargetAccesses)
{
	ensure(CutoutTargetAccesses.IsEmpty()
		|| GetUsage() == EITwinSplineUsage::MapCutout
		|| (GetUsage() == EITwinSplineUsage::Undefined && Impl->bIsLoadingSpline));

	this->CutoutTargets.Reset();
	this->CutoutTargets = std::move(CutoutTargetAccesses);
	Impl->CutoutTargetIdentifiers.clear();

	for (auto const& TilesetAccess : CutoutTargets)
	{
		if (ensure(TilesetAccess))
		{
			auto* Tileset = TilesetAccess->GetMutableTileset();
			if (Tileset)
			{
				// Store identifier used for persistence in the decoration service.
				Impl->CutoutTargetIdentifiers.insert(TilesetAccess->GetDecorationKey());

				// Create the cut-out overlay now, to avoid letting the iModel or reality data disappear when
				// we start drawing the first cut-out polygon.
				ITwin::InitCutoutOverlay(*Tileset);
			}
		}
	}
}

AITwinSplineTool::TilesetAccessArray const& AITwinSplineTool::GetCutoutTargets() const
{
	return this->CutoutTargets;
}

AITwinSplineTool::FAutomaticVisibilityDisabler::FAutomaticVisibilityDisabler()
	: bAutomaticVisibility_Old(AutomaticSplineVisibility())
{
	SetAutomaticSplineVisibility(false);
}
AITwinSplineTool::FAutomaticVisibilityDisabler::~FAutomaticVisibilityDisabler()
{
	SetAutomaticSplineVisibility(bAutomaticVisibility_Old);
}

/* static */
bool AITwinSplineTool::AutomaticSplineVisibility()
{
	return FImpl::bAutomaticSplineVisibility;
}
/* static */
void AITwinSplineTool::SetAutomaticSplineVisibility(bool bAutomatic)
{
	FImpl::bAutomaticSplineVisibility = bAutomatic;
}


bool AITwinSplineTool::GetInvertSelectedSplineEffect() const
{
	return Impl->GetInvertSelectedSplineEffect();
}
void AITwinSplineTool::SetInvertSelectedSplineEffect(bool bInvertEffect)
{
	Impl->SetInvertSelectedSplineEffect(bInvertEffect);
}

EITwinTangentMode AITwinSplineTool::GetTangentMode() const
{
	return Impl->GetTangentMode();
}

void AITwinSplineTool::SetTangentMode(EITwinTangentMode TangentMode)
{
	Impl->SetTangentMode(TangentMode);
}

void AITwinSplineTool::RefreshScene()
{
	Impl->RefreshScene();
}

void AITwinSplineTool::OnOverviewCamera(bool bInUndoRedoContext /*= false*/)
{
	// Compute a top view camera transformation, centered on current spline
	FVector RefLocation = FVector::ZeroVector;
	FBox BBox;
	if (!Impl->GetOverviewCameraReferencePosition(RefLocation, BBox))
	{
		return;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!ensure(PlayerController))
		return;

	auto BBoxSize = BBox.GetSize().Length();
	float FOVInDegrees = 60.f;
	FRotator Rot(0.0f, 0.0f, 0.0f);
	FTransform CurrentCameraTsf;

	// Get current camera transformation
	APawn const* Pawn = PlayerController->GetPawnOrSpectator();
	if (ensure(Pawn))
	{
		Rot = Pawn->GetActorRotation();
		CurrentCameraTsf = Pawn->GetActorTransform();

		TInlineComponentArray<UCameraComponent*> cameras;
		Pawn->GetComponents<UCameraComponent>(cameras);
		for (UCameraComponent const* cameraComponent : cameras)
		{
			FOVInDegrees = cameraComponent->FieldOfView;
			BBoxSize *= std::max(1.f, cameraComponent->AspectRatio);
			break;
		}
	}

	// Frame the bounding box
	const float HalfFov = FMath::DegreesToRadians(FOVInDegrees) / 2.f;
	float Dist = 0.5 * BBoxSize / FMath::Tan(HalfFov);

	// Adjust the trace extent used in picking, so that one can always select the spline or its points from
	// the new camera view.
	float TraceExtentInMeters = 2.f * Dist * 0.01f;
	if (this->GetCustomPickingExtentInMeters() < TraceExtentInMeters)
	{
		this->SetCustomPickingExtentInMeters(TraceExtentInMeters);
	}

	FTransform NewCameraTsf;
	NewCameraTsf.SetTranslation(RefLocation + (1.2 * Dist * FVector::UpVector));

	// Make camera look down.
	bool bHasSetRotation = false;
	FVector CurCameraDir = Rot.Vector();

	FVector TargetCameraDir(FVector::DownVector);
	// Avoid using strictly DownVector, as it leads to degenerated cases...
	TargetCameraDir += 0.01 * CurCameraDir;
	TargetCameraDir.Normalize();

	if ((CurCameraDir | TargetCameraDir) < 0.9)
	{
		const FQuat DeltaRotationQuat = FQuat::FindBetween(CurCameraDir, TargetCameraDir);
		if (!DeltaRotationQuat.ContainsNaN())
		{
			NewCameraTsf.SetRotation(DeltaRotationQuat * CurrentCameraTsf.GetRotation());
			NewCameraTsf.NormalizeRotation();
			bHasSetRotation = true;
		}
	}
	if (!bHasSetRotation)
	{
		NewCameraTsf.SetRotation(CurrentCameraTsf.GetRotation());
	}

	// When calling this in the undo/redo system, we should not broadcast the event, or else a new undo
	// entry will be created...
	if (!bInUndoRedoContext)
	{
		OverviewCameraEvent.Broadcast(CurrentCameraTsf);
	}

	StartBlendedCameraMovement(NewCameraTsf);
}

void AITwinSplineTool::StartBlendedCameraMovement(const FTransform& NewCameraTransform)
{
	// (Strongly) inspired from ASavedViewsController
	const float BlendTime = 3.f;
	Impl->StartCameraMovement(NewCameraTransform, BlendTime);
	GetWorld()->GetTimerManager().SetTimer(Impl->TimerHandle,
		FTimerDelegate::CreateLambda([=, this, _ = TStrongObjectPtr<AITwinSplineTool>(this)]
	{
		if (IsValid(this))
			Impl->EndCameraMovement(NewCameraTransform);
	}),
		BlendTime, false);
}

void AITwinSplineTool::SetPopulationTool(AITwinPopulationTool* InPopulationTool)
{
	PopulationTool = InPopulationTool;
}


namespace
{
	class FSplineStateRecord : public AITwinInteractiveTool::IActiveStateRecord
	{
	public:
		FSplineStateRecord(AITwinSplineTool const& InTool)
			: Usage(InTool.GetUsage())
		{
			for (auto const& TilesetAcc : InTool.GetCutoutTargets())
			{
				CutoutTargets.Push(TilesetAcc->Clone());
			}
		}

		EITwinSplineUsage Usage = EITwinSplineUsage::Undefined;
		AITwinSplineTool::TilesetAccessArray CutoutTargets;
	};
}

TUniquePtr<AITwinInteractiveTool::IActiveStateRecord> AITwinSplineTool::MakeStateRecord() const
{
	return MakeUnique<FSplineStateRecord>(*this);
}

bool AITwinSplineTool::RestoreState(IActiveStateRecord const& State)
{
	FSplineStateRecord const* SplineState = static_cast<FSplineStateRecord const*>(&State);

	AITwinSplineTool::TilesetAccessArray Targets;
	for (auto const& TilesetAcc : SplineState->CutoutTargets)
	{
		Targets.Push(TilesetAcc->Clone());
	}
	ITwin::EnableSplineTool(GetWorld(), true, SplineState->Usage, std::move(Targets));
	return IsEnabled();
}

namespace
{
	AITwinSplineHelper* FindSplineByRefID(UWorld* World, AdvViz::SDK::RefID const& RefId)
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(World); SplineIter; ++SplineIter)
		{
			if (SplineIter->GetAVizSplineId() == RefId)
				return *SplineIter;
		}
		return nullptr;
	}

	class FSplineSelectionRecord : public AITwinInteractiveTool::ISelectionRecord
	{
	public:
		FSplineSelectionRecord(AITwinSplineHelper* InSpline, int32 InPointIndex)
			: SelectedPointIndex(InPointIndex)
		{
			if (ensure(InSpline))
				SelectedSplineID = InSpline->GetAVizSplineId();
		}

		AdvViz::SDK::RefID SelectedSplineID = AdvViz::SDK::RefID::Invalid();
		int32 SelectedPointIndex = INDEX_NONE;
	};
}

TUniquePtr<AITwinInteractiveTool::ISelectionRecord> AITwinSplineTool::MakeSelectionRecord() const
{
	if (ensure(Impl->HasSelection()))
	{
		return MakeUnique<FSplineSelectionRecord>(
			Impl->GetSelectedSpline(), Impl->GetSelectedPointIndex());
	}
	return {};
}

bool AITwinSplineTool::HasSameSelection(ISelectionRecord const& Selection) const
{
	FSplineSelectionRecord const* SplineSelection = static_cast<FSplineSelectionRecord const*>(&Selection);
	return Impl->GetSelectedSpline()
		&& Impl->GetSelectedSpline()->GetAVizSplineId() == SplineSelection->SelectedSplineID
		&& Impl->GetSelectedPointIndex() == SplineSelection->SelectedPointIndex;
}

bool AITwinSplineTool::RestoreSelection(ISelectionRecord const& Selection)
{
	FSplineSelectionRecord const* SplineSelection = static_cast<FSplineSelectionRecord const*>(&Selection);
	if (SplineSelection->SelectedSplineID.IsValid())
	{
		AITwinSplineHelper* SplineToSelect = FindSplineByRefID(GetWorld(), SplineSelection->SelectedSplineID);
		if (!ensure(SplineToSelect))
			return false;
		if (Impl->GetSelectedSpline() != SplineToSelect)
			Impl->SetSelectedSpline(SplineToSelect);
		if (Impl->GetSelectedPointIndex() != SplineSelection->SelectedPointIndex)
			Impl->SetSelectedPointIndex(SplineSelection->SelectedPointIndex);
		return true;
	}
	else
	{
		return false;
	}
}

namespace ITwin
{
	extern int32 GetLinkedTilesets(
		AITwinSplineTool::TilesetAccessArray& OutArray,
		std::shared_ptr<AdvViz::SDK::ISpline> const& Spline,
		const UWorld* World);
}

namespace
{
	class FSplineItemBackup : public AITwinInteractiveTool::IItemBackup
	{
	public:
		FSplineItemBackup(AITwinSplineHelper const& InSpline, bool bInIsPointDeletion)
			: bIsPointDeletion(bInIsPointDeletion)
		{
			auto const AVizSpline = InSpline.GetAVizSpline();
			if (AVizSpline)
			{
				AVizSplineBackup = AVizSpline->Clone();
				ensure(AVizSplineBackup->GetId() == AVizSpline->GetId());
				ensure(AVizSplineBackup->GetId().IsValid());
			}
		}

		virtual FString GetGenericName() const override
		{
			FString ItemName;
			if (AVizSplineBackup && AVizSplineBackup->GetUsage() == AdvViz::SDK::ESplineUsage::MapCutout)
				ItemName = TEXT("polygon");
			else
				ItemName = TEXT("spline");
			if (bIsPointDeletion)
				ItemName += TEXT(" point");
			return ItemName;
		}

		const bool bIsPointDeletion;
		std::shared_ptr<AdvViz::SDK::ISpline> AVizSplineBackup;
	};
}

TUniquePtr<AITwinInteractiveTool::IItemBackup> AITwinSplineTool::MakeSelectedItemBackup() const
{
	if (ensure(Impl->HasSelection()))
	{
		return MakeUnique<FSplineItemBackup>(
			*Impl->GetSelectedSpline(), Impl->CanDeletePoint());
	}
	return {};
}

bool AITwinSplineTool::RestoreItem(IItemBackup const& ItemBackup)
{
	FSplineItemBackup const* SplineBackup = static_cast<FSplineItemBackup const*>(&ItemBackup);
	if (SplineBackup->AVizSplineBackup)
	{
		// Depending on whether we deleted a point or a full spline, we may have to instantiate a new spline
		// here.
		AITwinSplineHelper* ExistingSpline = FindSplineByRefID(GetWorld(),
			SplineBackup->AVizSplineBackup->GetId());
		ensure((ExistingSpline != nullptr) == SplineBackup->bIsPointDeletion);

		bool bRestored = false;
		if (ExistingSpline)
		{
			ExistingSpline->SetAVizSpline(SplineBackup->AVizSplineBackup->Clone());
			ensure(ExistingSpline->GetAVizSplineId() == SplineBackup->AVizSplineBackup->GetId());
			bRestored = true;
		}
		else if (!SplineBackup->bIsPointDeletion)
		{
			// Recreate spline helper from the backup.
			TilesetAccessArray LinkedTilesets;
			ITwin::GetLinkedTilesets(LinkedTilesets, SplineBackup->AVizSplineBackup, GetWorld());
			AdvViz::SDK::SharedSpline const RestoredSpline = SplineBackup->AVizSplineBackup->Clone();
			bRestored = LoadSpline(RestoredSpline, std::move(LinkedTilesets));
			// Notify the manager
			if (Impl->splinesManager)
			{
				Impl->splinesManager->RestoreSpline(RestoredSpline);
			}
		}
		if (bRestored)
		{
			Impl->TriggerDelayedRefresh();
		}
		return bRestored;
	}
	return false;
}

namespace ITwin
{
	void EnableSplineTool(UObject* WorldContextObject, bool bEnable, EITwinSplineUsage Usage,
		AITwinSplineTool::TilesetAccessArray&& CutoutTargets /*= {}*/,
		bool bAutomaticCutoutTarget /*= false*/)
	{
		ensureMsgf(!bEnable || Usage != EITwinSplineUsage::MapCutout || !CutoutTargets.IsEmpty() || bAutomaticCutoutTarget,
			TEXT("Cut-out mode requires to provide target tileset(s)"));

		AActor* Tool = UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinSplineTool::StaticClass());
		if (IsValid(Tool))
		{
			AITwinSplineTool* SplineTool = Cast<AITwinSplineTool>(Tool);
			if (bEnable && Usage != EITwinSplineUsage::Undefined)
			{
				// Usage must be filled before enabling the tool, in order to show only the splines matching the
				// active usage.
				SplineTool->SetUsage(Usage);
			}
			SplineTool->SetCutoutTargets(std::move(CutoutTargets));
			// Connect the spline tool to the population tool if applicable.
			SplineTool->SetPopulationTool(Cast<AITwinPopulationTool>(WorldContextObject));

			SplineTool->SetEnabled(bEnable);

			if (bEnable)
			{
				SplineTool->ResetToDefault();
			}
		}
	}
}


#if ENABLE_DRAW_DEBUG

// Console command to flip the selected cutout polygon
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFlipSelectedCutoutPolygon(
	TEXT("cmd.ITwinFlipSelectedCutoutPolygon"),
	TEXT("Flip selected cutout polygon effect."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	auto SplineToolActor = TWorldSingleton<AITwinSplineTool>().Get(World);
	if (ensure(SplineToolActor) && SplineToolActor->GetSelectedSpline())
	{
		SplineToolActor->SetInvertSelectedSplineEffect(!SplineToolActor->GetInvertSelectedSplineEffect());
	}
}));

#endif // ENABLE_DRAW_DEBUG
