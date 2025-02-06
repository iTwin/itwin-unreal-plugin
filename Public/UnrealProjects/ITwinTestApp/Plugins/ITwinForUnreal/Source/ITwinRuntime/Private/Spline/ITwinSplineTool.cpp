/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineTool.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinCesiumCartographicPolygon.h>
#include <ITwinCesiumPolygonRasterOverlay.h>
#include <Components/SplineComponent.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h>

class AITwinSplineTool::FImpl
{
public:
	AITwinSplineTool& owner;

	bool enabled = false; // boolean used to switch on or off the spline tool

	AITwinSplineHelper* selectedSplineHelper = nullptr;
	int32 selectedPointIndex = -1;
	bool duplicateWhenMovingPoint = false;

	FImpl(AITwinSplineTool& inOwner);

	// Implementation of AITwinSplineTool functions
	AITwinSplineHelper* GetSelectedSpline() const;
	void SetSelectedSpline(AITwinSplineHelper* splineHelper);
	void SetSelectedPointIndex(int32 pointIndex);
	bool HasSelection() const;
	void DeleteSelection();
	void DeleteSelectedPoint();
	void DuplicateSelectedPoint();
	void EnableDuplicationWhenMovingPoint(bool value);
	FTransform GetSelectionTransform() const;
	void SetSelectionTransform(const FTransform& transform);
	void SetEnabled(bool value);
	bool IsEnabled() const;
	void ResetToDefault();
	void DoMouseClickAction();
	void AddCartographicPolygon(const FVector& position);

	// Additional internal functions
	FHitResult LineTraceFromMousePos();
	void RemoveCartographicPolygon(AITwinCesiumCartographicPolygon* polygon);
};

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
}

void AITwinSplineTool::FImpl::SetSelectedPointIndex(int32 pointIndex)
{
	selectedPointIndex = pointIndex;
}

bool AITwinSplineTool::FImpl::HasSelection() const
{
	return IsValid(selectedSplineHelper);
}

void AITwinSplineTool::FImpl::DeleteSelection()
{
	if (IsValid(selectedSplineHelper))
	{
		if (selectedPointIndex >= 0 && selectedSplineHelper->GetNumberOfSplinePoints() > 1)
		{
			selectedSplineHelper->DeletePoint(selectedPointIndex);
		}
		else
		{
			AITwinCesiumCartographicPolygon* polygon = selectedSplineHelper->GetCartographicPolygon();
			if (polygon)
			{
				RemoveCartographicPolygon(polygon);
				selectedSplineHelper->SetCartographicPolygon(nullptr);
				selectedSplineHelper->Destroy();
				polygon->Destroy();
			}
		}
		selectedSplineHelper = nullptr;
		selectedPointIndex = -1;
	}
}

void AITwinSplineTool::FImpl::DeleteSelectedPoint()
{
	if (IsValid(selectedSplineHelper) && selectedPointIndex >= 0)
	{
		selectedSplineHelper->DeletePoint(selectedPointIndex);
		selectedSplineHelper = nullptr;
		selectedPointIndex = -1;
	}
}

void AITwinSplineTool::FImpl::DuplicateSelectedPoint()
{
	if (IsValid(selectedSplineHelper) && selectedPointIndex >= 0)
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
			return selectedSplineHelper->GetActorTransform();
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
			selectedSplineHelper->SetTransform(transform);
		}
	}
}

void AITwinSplineTool::FImpl::SetEnabled(bool value)
{
	if (value != enabled)
	{
		enabled = value;

		// Show/hide splines helpers
		TArray<AActor*> splineHelpers;
		UGameplayStatics::GetAllActorsOfClass(
			owner.GetWorld(), AITwinSplineHelper::StaticClass(), splineHelpers);
		for (auto splineHelper : splineHelpers)
		{
			splineHelper->SetActorHiddenInGame(!enabled);
		}

		if (!enabled)
		{
			SetSelectedSpline(nullptr);
		}
	}
}

bool AITwinSplineTool::FImpl::IsEnabled() const
{
	return enabled;
}

void AITwinSplineTool::FImpl::ResetToDefault()
{
}

void AITwinSplineTool::FImpl::DoMouseClickAction()
{
	FHitResult hitResult = LineTraceFromMousePos();

	AActor* hitActor = hitResult.GetActor();

	SetSelectedSpline(nullptr);

	if (hitActor)
	{
		if (hitActor->IsA(AITwinSplineHelper::StaticClass()))
		{
			AITwinSplineHelper* splineHelper = Cast<AITwinSplineHelper>(hitActor);
			SetSelectedSpline(splineHelper);

			if (hitResult.GetComponent() &&
				hitResult.GetComponent()->IsA(UStaticMeshComponent::StaticClass()))
			{
				int32 pointIndex = splineHelper->FindPointIndexFromMeshComponent(
					Cast<UStaticMeshComponent>(hitResult.GetComponent()));

				if (pointIndex != INDEX_NONE)
				{
					SetSelectedPointIndex(pointIndex);
				}
			}
		}
	}
}

void AITwinSplineTool::FImpl::AddCartographicPolygon(const FVector& position)
{
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(owner.GetWorld()); Google3DIter; ++Google3DIter)
	{
		AITwinGoogle3DTileset* ggTileset = (*Google3DIter);
		ggTileset->InitPolygonRasterOverlay();
		auto* RasterOverlay = ggTileset->GetPolygonRasterOverlay();
		if (ensure(RasterOverlay))
		{
			auto cartographicPolygon = owner.GetWorld()->SpawnActor<AITwinCesiumCartographicPolygon>();
			cartographicPolygon->GlobeAnchor->SetGeoreference(ggTileset->GetGeoreference());
			cartographicPolygon->SetActorLocation(position);
			RasterOverlay->Polygons.Push(cartographicPolygon);

			// Reduce the area of the default polygon
			USplineComponent* splineComp = cartographicPolygon->Polygon;
			for (int32 i = 0; i < splineComp->GetNumberOfSplinePoints(); ++i)
			{
				FVector pos = splineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
				splineComp->SetLocationAtSplinePoint(i, pos * 0.25, ESplineCoordinateSpace::Local);
			}

			auto splineHelper = owner.GetWorld()->SpawnActor<AITwinSplineHelper>();
			splineHelper->GlobeAnchor->SetGeoreference(ggTileset->GetGeoreference());
			splineHelper->SetActorLocation(position);
			splineHelper->SetCartographicPolygon(cartographicPolygon);
			splineHelper->Initialize(cartographicPolygon->Polygon, ETangentMode::Linear);

			ggTileset->RefreshTileset();

			break;// TODO_NS: share cartographic polygons
		}
	}
}

void AITwinSplineTool::FImpl::RemoveCartographicPolygon(AITwinCesiumCartographicPolygon* polygon)
{
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(owner.GetWorld()); Google3DIter; ++Google3DIter)
	{
		AITwinGoogle3DTileset* ggTileset = (*Google3DIter);
		auto* RasterOverlay = ggTileset->GetPolygonRasterOverlay();
		if (ensure(RasterOverlay))
		{
			int32 index = RasterOverlay->Polygons.Find(polygon);
			if (index != INDEX_NONE)
			{
				 RasterOverlay->Polygons.RemoveAt(index);
				 ggTileset->RefreshTileset();
			}
		}
	}
}

FHitResult AITwinSplineTool::FImpl::LineTraceFromMousePos()
{
	FHitResult hitResult;

	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return hitResult;

	FVector traceStart, traceEnd, traceDir;
	if (!playerController->DeprojectMousePositionToWorld(traceStart, traceDir))
		return hitResult;

	traceEnd = traceStart + traceDir * 1e8f;

	TArray<AActor*> actorsToIgnore;

	UKismetSystemLibrary::LineTraceSingle(
		&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, true,
		actorsToIgnore, EDrawDebugTrace::None, hitResult, true);

	return hitResult;
}

// -----------------------------------------------------------------------------
//                              AITwinSplineTool

AITwinSplineTool::AITwinSplineTool()
	:Impl(MakePimpl<FImpl>(*this))
{
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

bool AITwinSplineTool::HasSelection() const
{
	return Impl->HasSelection();
}

void AITwinSplineTool::DeleteSelection()
{
	Impl->DeleteSelection();
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

FTransform AITwinSplineTool::GetSelectionTransform() const
{
	return Impl->GetSelectionTransform();
}

void AITwinSplineTool::SetSelectionTransform(const FTransform& transform)
{
	Impl->SetSelectionTransform(transform);
}

void AITwinSplineTool::SetEnabled(bool value)
{
	Impl->SetEnabled(value);
}

bool AITwinSplineTool::IsEnabled() const
{
	return Impl->IsEnabled();
}

void AITwinSplineTool::ResetToDefault()
{
	Impl->ResetToDefault();
}

void AITwinSplineTool::DoMouseClickAction()
{
	Impl->DoMouseClickAction();
}

void AITwinSplineTool::AddCartographicPolygon(const FVector& position)
{
	Impl->AddCartographicPolygon(position);
}