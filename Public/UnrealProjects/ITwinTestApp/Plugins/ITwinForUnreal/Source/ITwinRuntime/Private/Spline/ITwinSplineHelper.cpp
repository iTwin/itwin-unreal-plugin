/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Spline/ITwinSplineHelper.h>
#include <ITwinCesiumGlobeAnchorComponent.h>
#include <ITwinCesiumCartographicPolygon.h>
#include <Components/SceneComponent.h>
#include <Components/SplineComponent.h>
#include <Components/SplineMeshComponent.h>

#define SPL_LOCAL ESplineCoordinateSpace::Local
#define SPL_WORLD ESplineCoordinateSpace::World
#define SMOOTH_FACTOR 0.5

AITwinSplineHelper::AITwinSplineHelper() : AActor()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	GetRootComponent()->SetMobility(EComponentMobility::Movable); // needed for the anchor

	GlobeAnchor = CreateDefaultSubobject<UITwinCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));
}

void AITwinSplineHelper::SetSplineComponent(USplineComponent* splineComp)
{
	splineComponent = splineComp;
}

int32 AITwinSplineHelper::GetNumberOfSplinePoints() const
{
	return IsValid(splineComponent) ? splineComponent->GetNumberOfSplinePoints() : 0;
}

void AITwinSplineHelper::Initialize(USplineComponent* splineComp, const ETangentMode& mode)
{
	SetSplineComponent(splineComp);
	SetTangentMode(mode);
	AddAllMeshComponents();
}

namespace ITwinSpline
{
	inline int32 GetPrevIndex(const int32& index, const int32& arraySize)
	{
		return (index > 0) ? (index - 1) : (arraySize - 1);
	}

	inline int32 GetNextIndex(const int32& index, const int32& arraySize)
	{
		return (index < arraySize - 1) ? (index + 1) : 0;
	}
}

void AITwinSplineHelper::SetTangentMode(const ETangentMode& mode)
{
	tangentMode = mode;

	if (!IsValid(splineComponent) ||
		tangentMode == ETangentMode::Custom)
	{
		return;
	}


	for (int32 i = 0; i < splineComponent->GetNumberOfSplinePoints(); ++i)
	{
		int32 prevIndex = ITwinSpline::GetPrevIndex(i, splineComponent->GetNumberOfSplinePoints());
		int32 currIndex = i;
		int32 nextIndex = ITwinSpline::GetNextIndex(i, splineComponent->GetNumberOfSplinePoints());

		FVector prevPoint = splineComponent->GetLocationAtSplinePoint(prevIndex, SPL_LOCAL);
		FVector currPoint = splineComponent->GetLocationAtSplinePoint(currIndex, SPL_LOCAL);
		FVector nextPoint = splineComponent->GetLocationAtSplinePoint(nextIndex, SPL_LOCAL);

		if (tangentMode == ETangentMode::Linear)
		{
			splineComponent->SetTangentsAtSplinePoint(
				i, currPoint - prevPoint, nextPoint - currPoint, SPL_LOCAL, false);
		}
		else if (tangentMode == ETangentMode::Smooth)
		{
			splineComponent->SetTangentAtSplinePoint(
				i, (nextPoint - prevPoint)*SMOOTH_FACTOR, SPL_LOCAL, false);
		}
	}

	splineComponent->UpdateSpline();
}

void AITwinSplineHelper::InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh)
{
	USceneComponent* rootComp = GetRootComponent();

	rootComp->SetMobility(EComponentMobility::Static); // avoids a warning
	meshComp->AttachToComponent(rootComp, FAttachmentTransformRules::KeepWorldTransform);
	rootComp->SetMobility(EComponentMobility::Movable);

	meshComp->SetMobility(EComponentMobility::Movable);
	meshComp->SetStaticMesh(mesh);
	meshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	meshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
}

void AITwinSplineHelper::AddAllMeshComponents()
{
	if (!splineMesh)
	{
		splineMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/SplineMesh.SplineMesh"), nullptr, LOAD_None, nullptr);
	}

	if (!pointMesh)
	{
		pointMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/PointMesh.PointMesh"), nullptr, LOAD_None, nullptr);
	}

	for (int32 i = 0; i < splineComponent->GetNumberOfSplinePoints(); ++i)
	{
		AddMeshComponentsForPoint(i);
	}
}

void AITwinSplineHelper::AddMeshComponentsForPoint(int32 pointIndex)
{
	if (!IsValid(splineComponent) ||
		pointIndex < 0 || pointIndex >= splineComponent->GetNumberOfSplinePoints() ||
		pointIndex > splineMeshComponents.size())
		return;

	// Add a spline mesh
	USplineMeshComponent* splineMeshComp = Cast<USplineMeshComponent>(
		AddComponentByClass(USplineMeshComponent::StaticClass(), true, GetTransform(), false));

	splineMeshComponents.insert(splineMeshComponents.begin() + pointIndex, splineMeshComp);

	InitMeshComponent(splineMeshComp, splineMesh);

	splineMeshComp->SetForwardAxis(ESplineMeshAxis::X, false);
	double width = 2;
	splineMeshComp->SetStartScale(FVector2D(width, 1), false);
	splineMeshComp->SetEndScale(FVector2D(width, 1), false);

	// Add a point mesh
	UStaticMeshComponent* pointMeshComp = Cast<UStaticMeshComponent>(
		AddComponentByClass(UStaticMeshComponent::StaticClass(), true, GetTransform(), false));

	pointMeshComponents.insert(pointMeshComponents.begin() + pointIndex, pointMeshComp);

	InitMeshComponent(pointMeshComp, pointMesh);

	pointMeshComp->SetRelativeLocation(splineComponent->GetLocationAtSplinePoint(pointIndex, SPL_LOCAL));
	pointMeshComp->SetRelativeScale3D(FVector(1.5));

	// Update meshes
	UpdateMeshComponentsForPoint(pointIndex);
}

void AITwinSplineHelper::UpdateMeshComponentsForPoint(int32 pointIndex)
{
	if (!IsValid(splineComponent) ||
		pointIndex < 0 || pointIndex >= splineComponent->GetNumberOfSplinePoints() ||
		pointIndex > splineMeshComponents.size())
		return;

	USplineMeshComponent* splineMeshComp = splineMeshComponents[pointIndex];

	int32 startIndex = pointIndex;
	int32 endIndex = ITwinSpline::GetNextIndex(startIndex, splineComponent->GetNumberOfSplinePoints());

	splineMeshComp->SetStartAndEnd(
		splineComponent->GetLocationAtSplinePoint(startIndex, SPL_LOCAL),
		splineComponent->GetLeaveTangentAtSplinePoint(startIndex, SPL_LOCAL),
		splineComponent->GetLocationAtSplinePoint(endIndex, SPL_LOCAL),
		splineComponent->GetArriveTangentAtSplinePoint(endIndex, SPL_LOCAL));

	UStaticMeshComponent* pointMeshComp = pointMeshComponents[pointIndex];
	pointMeshComp->SetRelativeLocation(
		splineComponent->GetLocationAtSplinePoint(startIndex, SPL_LOCAL));
}

int32 AITwinSplineHelper::FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const
{
	auto it = std::find(pointMeshComponents.cbegin(), pointMeshComponents.cend(), meshComp);
	if (it != pointMeshComponents.cend())
	{
		return static_cast<int32>(it - pointMeshComponents.cbegin());
	}
	return INDEX_NONE;
}

AITwinCesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygon() const
{
	return cartographicPolygon;
}

void AITwinSplineHelper::SetCartographicPolygon(AITwinCesiumCartographicPolygon* polygon)
{
	cartographicPolygon = polygon;
}

void AITwinSplineHelper::SetTransform(const FTransform& NewTransform)
{
	SetActorTransform(NewTransform);

	if (IsValid(cartographicPolygon))
	{
		cartographicPolygon->SetActorLocation(NewTransform.GetLocation());
	}
}

FVector AITwinSplineHelper::GetLocationAtSplinePoint(int32 pointIndex) const
{
	if (IsValid(splineComponent))
	{
		return splineComponent->GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	}
	return FVector(0);
}

void AITwinSplineHelper::SetLocationAtSplinePoint(int32 pointIndex, const FVector& location)
{
	if (!IsValid(splineComponent))
	{
		return;
	}

	splineComponent->SetLocationAtSplinePoint(pointIndex, location, SPL_WORLD);

	// Use the local position for the next calculations
	FVector pos = splineComponent->GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);

	int32 numPoints = splineComponent->GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);
		
	FVector prevPos = splineComponent->GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = splineComponent->GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	// Update tangents
	if (tangentMode == ETangentMode::Linear)
	{
		FVector arriveTangent, leaveTangent;
		arriveTangent = splineComponent->GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL);
		leaveTangent = (pos - prevPos);
		splineComponent->SetTangentsAtSplinePoint(
			prevPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = (nextPos - pos);
		splineComponent->SetTangentsAtSplinePoint(
			pointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = splineComponent->GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL);
		splineComponent->SetTangentsAtSplinePoint(
			nextPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);
	}
	else if (tangentMode == ETangentMode::Smooth)
	{
		FVector prevPrevPos = splineComponent->GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints), SPL_LOCAL);
		FVector nextNextPos = splineComponent->GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints), SPL_LOCAL);

		splineComponent->SetTangentAtSplinePoint(
			prevPointIndex, (pos - prevPrevPos)*SMOOTH_FACTOR, SPL_LOCAL, false);
		splineComponent->SetTangentAtSplinePoint(
			pointIndex, (nextPos - prevPos)*SMOOTH_FACTOR, SPL_LOCAL, false);
		splineComponent->SetTangentAtSplinePoint(
			nextPointIndex, (nextNextPos - pos)*SMOOTH_FACTOR, SPL_LOCAL, false);
	}

	splineComponent->UpdateSpline();

	// Update meshes
	UpdateMeshComponentsForPoint(pointIndex);
	UpdateMeshComponentsForPoint(prevPointIndex);

	if (tangentMode == ETangentMode::Smooth)
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(pointIndex, numPoints));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints));
	}
}

void AITwinSplineHelper::DeletePoint(int32 pointIndex)
{
	if (!IsValid(splineComponent))
	{
		return;
	}

	// Set the new tangents before deleting the point
	int32 numPoints = splineComponent->GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);
	FVector prevPos = splineComponent->GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = splineComponent->GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	if (tangentMode == ETangentMode::Linear)
	{
		splineComponent->SetTangentsAtSplinePoint(prevPointIndex,
			splineComponent->GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL),
			nextPos - prevPos, SPL_LOCAL, false);

		splineComponent->SetTangentsAtSplinePoint(nextPointIndex, nextPos - prevPos,
			splineComponent->GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL),
			SPL_LOCAL, false);
	}
	else if (tangentMode == ETangentMode::Smooth)
	{
		FVector prevPrevPos = splineComponent->GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints), SPL_LOCAL);
		FVector nextNextPos = splineComponent->GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints), SPL_LOCAL);

		splineComponent->SetTangentAtSplinePoint(
			prevPointIndex, (nextPos - prevPrevPos)*SMOOTH_FACTOR, SPL_LOCAL, false);
		splineComponent->SetTangentAtSplinePoint(
			nextPointIndex, (nextNextPos - prevPos)*SMOOTH_FACTOR, SPL_LOCAL, false);
	}

	// Remove the spline point
	if (IsValid(splineComponent) && pointIndex < splineComponent->GetNumberOfSplinePoints())
	{
		splineComponent->RemoveSplinePoint(pointIndex);
	}

	// Remove the meshes representing the point
	if (pointIndex < splineMeshComponents.size() && IsValid(splineMeshComponents[pointIndex]))
	{
		splineMeshComponents[pointIndex]->UnregisterComponent();
		splineMeshComponents[pointIndex]->DestroyComponent();
		splineMeshComponents.erase(splineMeshComponents.begin() + pointIndex);
	}
	if (pointIndex < pointMeshComponents.size() && IsValid(pointMeshComponents[pointIndex]))
	{
		pointMeshComponents[pointIndex]->UnregisterComponent();
		pointMeshComponents[pointIndex]->DestroyComponent();
		pointMeshComponents.erase(pointMeshComponents.begin() + pointIndex);
	}

	// Update the meshes of the previous point to fill the gap
	numPoints = splineComponent->GetNumberOfSplinePoints();
	prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	UpdateMeshComponentsForPoint(prevPointIndex);

	if (tangentMode == ETangentMode::Smooth)
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(prevPointIndex, numPoints));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints));
	}
}

void AITwinSplineHelper::DuplicatePoint(int32 pointIndex)
{
	if (!IsValid(splineComponent))
	{
		return;
	}

	FVector pointPos = splineComponent->GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector arriveTangent = splineComponent->GetArriveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector leaveTangent = splineComponent->GetLeaveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	splineComponent->AddSplinePointAtIndex(pointPos, pointIndex, SPL_LOCAL, false);
	splineComponent->SetTangentsAtSplinePoint(pointIndex, arriveTangent, FVector(0), SPL_LOCAL, false);
	splineComponent->SetTangentsAtSplinePoint(pointIndex + 1, FVector(0), leaveTangent, SPL_LOCAL, false);
	splineComponent->UpdateSpline();
	AddMeshComponentsForPoint(pointIndex);
}

// This function can be used to duplicate a point when moving it. The passed index should
// be the currently selected point. It is modified if necessary depending on the movement.
void AITwinSplineHelper::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	if (!IsValid(splineComponent))
	{
		return;
	}

	int32 numPoints = splineComponent->GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);
	FVector prevPos = splineComponent->GetLocationAtSplinePoint(prevPointIndex, SPL_WORLD);
	FVector currPos = splineComponent->GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	FVector nextPos = splineComponent->GetLocationAtSplinePoint(nextPointIndex, SPL_WORLD);

	DuplicatePoint(pointIndex);

	double direction = (nextPos - prevPos).Dot(newWorldPosition - currPos);
	if (direction > 0)
	{
		pointIndex++;
	}
}
