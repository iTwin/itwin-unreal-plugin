/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Spline/ITwinSplineHelper.h>
#include <Math/UEMathConversion.h>
#include <CesiumGlobeAnchorComponent.h>
#include <CesiumCartographicPolygon.h>
#include <Components/SceneComponent.h>
#include <Components/SplineComponent.h>
#include <Components/SplineMeshComponent.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <Compil/AfterNonUnrealIncludes.h>

#define SPL_LOCAL ESplineCoordinateSpace::Local
#define SPL_WORLD ESplineCoordinateSpace::World
#define SMOOTH_FACTOR 0.5

#define CHECK_NUMBER_OF_POINTS() \
	ensureMsgf(Owner.SplineComponent->GetNumberOfSplinePoints() == static_cast<int32>(Spline->GetNumberOfPoints()), \
			   TEXT("The UE and AdvViz::SDK splines should have the same number of points."));

#define CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS() \
	ensureMsgf(CheckSplineMeshComponents(), \
			   TEXT("Mismatch closed-loop property vs number of spline mesh components."));

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

	AdvViz::SDK::ESplineTangentMode UEToAViz(const EITwinTangentMode sdkMode)
	{
		switch (sdkMode)
		{
			BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH
		case EITwinTangentMode::Linear:
			return AdvViz::SDK::ESplineTangentMode::Linear;
		case EITwinTangentMode::Smooth:
			return AdvViz::SDK::ESplineTangentMode::Smooth;
		case EITwinTangentMode::Custom:
			return AdvViz::SDK::ESplineTangentMode::Custom;
		}
	}

	EITwinTangentMode AVizToUE(const AdvViz::SDK::ESplineTangentMode ueMode)
	{
		switch (ueMode)
		{
			BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH
		case AdvViz::SDK::ESplineTangentMode::Linear:
			return EITwinTangentMode::Linear;
		case AdvViz::SDK::ESplineTangentMode::Smooth:
			return EITwinTangentMode::Smooth;
		case AdvViz::SDK::ESplineTangentMode::Custom:
			return EITwinTangentMode::Custom;
		}
	}
}

struct AITwinSplineHelper::FImpl
{
	AITwinSplineHelper& Owner;
	EITwinTangentMode TangentMode = EITwinTangentMode::Custom;
	EITwinSplineUsage Usage = EITwinSplineUsage::Undefined;
	std::shared_ptr<AdvViz::SDK::ISpline> Spline;
	double ScaleFactor = 2.0;

	static constexpr double RIBBON_SCALE = 0.60;
	static std::optional<EITwinSplineUsage> UsageForSpawnedActor;

	FImpl(AITwinSplineHelper& InOwner);
	void Initialize(USplineComponent* splineComp, std::shared_ptr<AdvViz::SDK::ISpline> spline);
	void UpdatePointFromUEtoAViz(int32 pointIndex);
	void UpdatePointFromAVizToUE(int32 pointIndex);
	void UpdateSplineFromUEtoAViz();
	void UpdateSplineFromAVizToUE();
	void InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh);
	void AddAllMeshComponents();
	void AddSplineMeshComponentsForPoint(int32 pointIndex);
	bool CheckSplineMeshComponents() const;
	void AddMeshComponentsForPoint(int32 pointIndex);
	void UpdateAllMeshComponents();
	void UpdateMeshComponentsForPoint(int32 pointIndex);
	int32 FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const;
	void SetTangentMode(const EITwinTangentMode mode);
	void SetTransform(const FTransform& NewTransform, bool markSplineForSaving);
	FVector GetLocationAtSplinePoint(int32 pointIndex) const;
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);
	bool IncludeInWorldBox(FBox& Box) const;
	void RemoveSplineMeshComponentForPoint(int32 pointIndex);
	void DeletePoint(int32 pointIndex);
	void DuplicatePoint(int32 pointIndex);
	void DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);
	void ScaleMeshComponentsForCurrentPOV();
	void SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline);
};

/*static*/
std::optional<EITwinSplineUsage> AITwinSplineHelper::FImpl::UsageForSpawnedActor = std::nullopt;

AITwinSplineHelper::FImpl::FImpl(AITwinSplineHelper& InOwner) : Owner(InOwner)
{
}

void AITwinSplineHelper::FImpl::Initialize(
	USplineComponent* splineComp, std::shared_ptr<AdvViz::SDK::ISpline> spline)
{
	Owner.SplineComponent = splineComp;
	Spline = spline;

	ensureMsgf(Spline && static_cast<EITwinSplineUsage>(Spline->GetUsage()) == this->Usage,
		TEXT("spline usage mismatch vs AdvViz::SDK"));

	// Detect the direction of the update
	if (Spline->GetNumberOfPoints() == 0 && splineComp->GetNumberOfSplinePoints() > 0)
	{
		UpdateSplineFromUEtoAViz();
	}
	else if (Spline->GetNumberOfPoints() > 0 && splineComp->GetNumberOfSplinePoints() == 0)
	{
		UpdateSplineFromAVizToUE();
	}

	AddAllMeshComponents();
}

void AITwinSplineHelper::FImpl::UpdatePointFromUEtoAViz(int32 pointIndex)
{
	using namespace AdvViz::SDK;
	typedef FITwinMathConversion MathConv;

	if (!Spline || !Owner.SplineComponent)
		return;

	size_t index = static_cast<size_t>(pointIndex);
	USplineComponent const& SplineComp(*Owner.SplineComponent);

	SharedSplinePoint point = Spline->GetPoint(index);
	point->SetPosition(MathConv::UEtoSDK(SplineComp.GetLocationAtSplinePoint(index, SPL_LOCAL)));
	point->SetUpVector(MathConv::UEtoSDK(SplineComp.GetUpVectorAtSplinePoint(index, SPL_LOCAL)));
	point->SetInTangent(MathConv::UEtoSDK(SplineComp.GetArriveTangentAtSplinePoint(index, SPL_LOCAL)));
	point->SetOutTangent(MathConv::UEtoSDK(SplineComp.GetLeaveTangentAtSplinePoint(index, SPL_LOCAL)));
	point->SetInTangentMode(ITwinSpline::UEToAViz(TangentMode));
	point->SetOutTangentMode(ITwinSpline::UEToAViz(TangentMode));
	point->SetShouldSave(true);
}

void AITwinSplineHelper::FImpl::UpdatePointFromAVizToUE(int32 pointIndex)
{
	using namespace AdvViz::SDK;
	typedef FITwinMathConversion MathConv;

	if (!Spline || !Owner.SplineComponent)
		return;

	SharedSplinePoint point = Spline->GetPoint(static_cast<size_t>(pointIndex));
	USplineComponent& SplineComp(*Owner.SplineComponent);

	SplineComp.SetLocationAtSplinePoint(
		pointIndex, MathConv::SDKtoUE(point->GetPosition()), SPL_LOCAL, false);

	SplineComp.SetTangentsAtSplinePoint(
		pointIndex,
		MathConv::SDKtoUE(point->GetInTangent()),
		MathConv::SDKtoUE(point->GetOutTangent()),
		SPL_LOCAL, false);

	SplineComp.SetUpVectorAtSplinePoint(
		pointIndex, MathConv::SDKtoUE(point->GetUpVector()), SPL_LOCAL, false);
}

void AITwinSplineHelper::FImpl::UpdateSplineFromUEtoAViz()
{
	if (!Spline || !Owner.SplineComponent)
		return;
	USplineComponent const& SplineComp(*Owner.SplineComponent);

	Spline->SetTransform(FITwinMathConversion::UEtoSDK(Owner.GetActorTransform()));

	// Adjust the number of points in the AdvViz::SDK spline
	const int32 NbPoints = SplineComp.GetNumberOfSplinePoints();
	if (NbPoints != static_cast<int32>(Spline->GetNumberOfPoints()))
	{
		Spline->SetNumberOfPoints(static_cast<size_t>(NbPoints));
	}

	for (int32 i = 0; i < NbPoints; ++i)
	{
		UpdatePointFromUEtoAViz(i);
	}

	CHECK_NUMBER_OF_POINTS();

	Spline->SetShouldSave(true);
}

void AITwinSplineHelper::FImpl::UpdateSplineFromAVizToUE()
{
	if (!Spline || !Owner.SplineComponent)
		return;

	Owner.SetTransform(FITwinMathConversion::SDKtoUE(Spline->GetTransform()), false);

	// Adjust the number of points in the USplineComponent
	USplineComponent& SplineComp(*Owner.SplineComponent);
	const int32 NbPoints = static_cast<int32>(Spline->GetNumberOfPoints());
	int32 CurNbPoints = SplineComp.GetNumberOfSplinePoints();
	if (CurNbPoints < NbPoints)
	{
		// Add missing point(s) to UE component
		for ( ; CurNbPoints < NbPoints; ++CurNbPoints)
		{
			SplineComp.AddSplinePoint(FVector(0), SPL_LOCAL, false);
		}
	}
	else if (CurNbPoints > NbPoints)
	{
		// Remove supernumerary point(s) from UE component
		for (; CurNbPoints > NbPoints; CurNbPoints--)
		{
			const bool bIsLast = CurNbPoints == NbPoints + 1;
			SplineComp.RemoveSplinePoint(CurNbPoints - 1, bIsLast /*bUpdateSpline*/);
		}
	}
	CHECK_NUMBER_OF_POINTS();

	SplineComp.SetClosedLoop(Spline->IsClosedLoop(), false /*bUpdateSpline*/);

	// Update points
	for (int32 i = 0; i < NbPoints; ++i)
	{
		UpdatePointFromAVizToUE(i);
	}

	// Update tangent mode
	bool isSameModeForAllPoints = true;
	bool bHasInitializedMode = false;
	AdvViz::SDK::ESplineTangentMode tgtMode = AdvViz::SDK::ESplineTangentMode::Linear;
	for (auto const& spPoint : Spline->GetPoints())
	{
		if (!bHasInitializedMode)
		{
			tgtMode = spPoint->GetInTangentMode();
			bHasInitializedMode = true;
		}
		if ((spPoint->GetInTangentMode() != tgtMode) || (spPoint->GetOutTangentMode() != tgtMode))
		{
			isSameModeForAllPoints = false;
			break;
		}
	}
	TangentMode = isSameModeForAllPoints ? ITwinSpline::AVizToUE(tgtMode) : EITwinTangentMode::Custom;
}

void AITwinSplineHelper::FImpl::InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh)
{
	USceneComponent* rootComp = Owner.GetRootComponent();

	rootComp->SetMobility(EComponentMobility::Static); // avoids a warning
	meshComp->AttachToComponent(rootComp, FAttachmentTransformRules::KeepWorldTransform);
	rootComp->SetMobility(EComponentMobility::Movable);

	meshComp->SetMobility(EComponentMobility::Movable);
	meshComp->SetStaticMesh(mesh);
	meshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	meshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
}

void AITwinSplineHelper::FImpl::AddAllMeshComponents()
{
	if (!Owner.SplineMesh)
	{
		Owner.SplineMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/SplineMesh.SplineMesh"), nullptr, LOAD_None, nullptr);
	}

	if (!Owner.PointMesh)
	{
		Owner.PointMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/PointMesh.PointMesh"), nullptr, LOAD_None, nullptr);
	}

	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		AddMeshComponentsForPoint(i);
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();
}


bool AITwinSplineHelper::FImpl::CheckSplineMeshComponents() const
{
	const bool bIsClosedLoop = Owner.IsClosedLoop();
	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	if (bIsClosedLoop)
	{
		return Owner.SplineMeshComponents.Num() == NbSplinePoints;
	}
	else
	{
		return Owner.SplineMeshComponents.Num() == NbSplinePoints - 1;
	}
}

void AITwinSplineHelper::FImpl::AddSplineMeshComponentsForPoint(int32 pointIndex)
{
	if (!ensure(pointIndex >= 0 && pointIndex <= Owner.SplineMeshComponents.Num()))
		return;

	USplineMeshComponent* splineMeshComp = Cast<USplineMeshComponent>(
		Owner.AddComponentByClass(USplineMeshComponent::StaticClass(), true, Owner.GetTransform(), false));

	Owner.SplineMeshComponents.Insert(splineMeshComp, pointIndex);

	InitMeshComponent(splineMeshComp, Owner.SplineMesh.Get());

	splineMeshComp->SetForwardAxis(ESplineMeshAxis::X, false);
	const FVector2D SplineScale(ScaleFactor * FImpl::RIBBON_SCALE, 1.0);
	splineMeshComp->SetStartScale(SplineScale, false);
	splineMeshComp->SetEndScale(SplineScale, false);
}

void AITwinSplineHelper::FImpl::AddMeshComponentsForPoint(int32 pointIndex)
{
	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	if (!Owner.SplineComponent ||
		pointIndex < 0 || pointIndex >= NbSplinePoints)
		return;

	const bool bIsClosedLoop = Owner.SplineComponent->IsClosedLoop();

	// Add a spline mesh if needed
	if (bIsClosedLoop || pointIndex < NbSplinePoints - 1)
	{
		AddSplineMeshComponentsForPoint(pointIndex);
	}

	// Add a point mesh
	UStaticMeshComponent* pointMeshComp = Cast<UStaticMeshComponent>(
		Owner.AddComponentByClass(UStaticMeshComponent::StaticClass(), true, Owner.GetTransform(), false));

	Owner.PointMeshComponents.Insert(pointMeshComp, pointIndex);

	InitMeshComponent(pointMeshComp, Owner.PointMesh.Get());

	pointMeshComp->SetRelativeLocation(Owner.SplineComponent->GetLocationAtSplinePoint(pointIndex, SPL_LOCAL));
	pointMeshComp->SetRelativeScale3D(FVector(ScaleFactor));

	// Update meshes
	UpdateMeshComponentsForPoint(pointIndex);
}

void AITwinSplineHelper::FImpl::UpdateAllMeshComponents()
{
	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		UpdateMeshComponentsForPoint(i);
	}
}

void AITwinSplineHelper::FImpl::UpdateMeshComponentsForPoint(int32 pointIndex)
{
	if (!Owner.SplineComponent ||
		pointIndex < 0 || pointIndex >= Owner.SplineComponent->GetNumberOfSplinePoints())
		return;

	USplineComponent const& SplineComp(*Owner.SplineComponent);

	int32 startIndex = pointIndex;
	int32 endIndex = ITwinSpline::GetNextIndex(startIndex, SplineComp.GetNumberOfSplinePoints());

	if (pointIndex < Owner.SplineMeshComponents.Num())
	{
		USplineMeshComponent* splineMeshComp = Owner.SplineMeshComponents[pointIndex];
		splineMeshComp->SetStartAndEnd(
			SplineComp.GetLocationAtSplinePoint(startIndex, SPL_LOCAL),
			SplineComp.GetLeaveTangentAtSplinePoint(startIndex, SPL_LOCAL),
			SplineComp.GetLocationAtSplinePoint(endIndex, SPL_LOCAL),
			SplineComp.GetArriveTangentAtSplinePoint(endIndex, SPL_LOCAL));
	}

	UStaticMeshComponent* pointMeshComp = Owner.PointMeshComponents[pointIndex];
	pointMeshComp->SetRelativeLocation(
		SplineComp.GetLocationAtSplinePoint(startIndex, SPL_LOCAL));
}

int32 AITwinSplineHelper::FImpl::FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const
{
	return Owner.PointMeshComponents.Find(meshComp);
}

void AITwinSplineHelper::FImpl::SetTangentMode(const EITwinTangentMode mode)
{
	TangentMode = mode;

	if (!Owner.SplineComponent ||
		TangentMode == EITwinTangentMode::Custom)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);
	const int32 NbSplinePoints = SplineComp.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		int32 prevIndex = ITwinSpline::GetPrevIndex(i, NbSplinePoints);
		int32 currIndex = i;
		int32 nextIndex = ITwinSpline::GetNextIndex(i, NbSplinePoints);

		FVector prevPoint = SplineComp.GetLocationAtSplinePoint(prevIndex, SPL_LOCAL);
		FVector currPoint = SplineComp.GetLocationAtSplinePoint(currIndex, SPL_LOCAL);
		FVector nextPoint = SplineComp.GetLocationAtSplinePoint(nextIndex, SPL_LOCAL);

		if (TangentMode == EITwinTangentMode::Linear)
		{
			SplineComp.SetTangentsAtSplinePoint(
				i, currPoint - prevPoint, nextPoint - currPoint, SPL_LOCAL, false);
		}
		else if (TangentMode == EITwinTangentMode::Smooth)
		{
			SplineComp.SetTangentAtSplinePoint(
				i, (nextPoint - prevPoint) * SMOOTH_FACTOR, SPL_LOCAL, false);
		}
	}

	SplineComp.UpdateSpline();

	UpdateSplineFromUEtoAViz();

	UpdateAllMeshComponents();
}

void AITwinSplineHelper::FImpl::SetTransform(const FTransform& NewTransform, bool markSplineForSaving)
{
	if (Owner.CartographicPolygon)
	{
		Owner.CartographicPolygon->SetActorTransform(NewTransform);
	}

	if (Spline)
	{
		Spline->SetTransform(FITwinMathConversion::UEtoSDK(NewTransform));
		if (markSplineForSaving)
		{
			Spline->SetShouldSave(true);
		}
	}
}

FVector AITwinSplineHelper::FImpl::GetLocationAtSplinePoint(int32 pointIndex) const
{
	if (Owner.SplineComponent)
	{
		return Owner.SplineComponent->GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	}
	return FVector(0);
}

void AITwinSplineHelper::FImpl::SetLocationAtSplinePoint(int32 pointIndex, const FVector& location)
{
	if (!Owner.SplineComponent)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);
	SplineComp.SetLocationAtSplinePoint(pointIndex, location, SPL_WORLD);

	// Use the local position for the next calculations
	FVector pos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);

	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);

	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	// Update tangents
	if (TangentMode == EITwinTangentMode::Linear)
	{
		FVector arriveTangent, leaveTangent;
		arriveTangent = SplineComp.GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL);
		leaveTangent = (pos - prevPos);
		SplineComp.SetTangentsAtSplinePoint(
			prevPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = (nextPos - pos);
		SplineComp.SetTangentsAtSplinePoint(
			pointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = SplineComp.GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL);
		SplineComp.SetTangentsAtSplinePoint(
			nextPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);
	}
	else if (TangentMode == EITwinTangentMode::Smooth)
	{
		FVector prevPrevPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints), SPL_LOCAL);

		SplineComp.SetTangentAtSplinePoint(
			prevPointIndex, (pos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		SplineComp.SetTangentAtSplinePoint(
			pointIndex, (nextPos - prevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		SplineComp.SetTangentAtSplinePoint(
			nextPointIndex, (nextNextPos - pos) * SMOOTH_FACTOR, SPL_LOCAL, false);
	}

	SplineComp.UpdateSpline();

	// Update the AdvViz::SDK spline (for the saving of points)
	if (Spline)
	{
		UpdatePointFromUEtoAViz(prevPointIndex);
		UpdatePointFromUEtoAViz(pointIndex);
		UpdatePointFromUEtoAViz(nextPointIndex);
	}

	// Update meshes
	UpdateMeshComponentsForPoint(pointIndex);
	UpdateMeshComponentsForPoint(prevPointIndex);

	if (TangentMode == EITwinTangentMode::Smooth)
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(pointIndex, numPoints));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints));
	}
}

bool AITwinSplineHelper::FImpl::IncludeInWorldBox(FBox& Box) const
{
	if (!Owner.SplineComponent)
	{
		return false;
	}
	USplineComponent const& SplineComp(*Owner.SplineComponent);
	int32 const NbPoints = SplineComp.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbPoints; ++i)
	{
		Box += SplineComp.GetLocationAtSplinePoint(i, SPL_WORLD);
	}
	return NbPoints > 0;
}

void AITwinSplineHelper::FImpl::RemoveSplineMeshComponentForPoint(int32 pointIndex)
{
	// Remove the meshes representing the point
	if (pointIndex < Owner.SplineMeshComponents.Num()
		&& Owner.SplineMeshComponents[pointIndex])
	{
		Owner.SplineMeshComponents[pointIndex]->UnregisterComponent();
		Owner.SplineMeshComponents[pointIndex]->DestroyComponent();
		Owner.SplineMeshComponents.RemoveAt(pointIndex);
	}
}

void AITwinSplineHelper::FImpl::DeletePoint(int32 pointIndex)
{
	if (!Owner.SplineComponent)
	{
		return;
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	USplineComponent& SplineComp(*Owner.SplineComponent);

	// Set the new tangents before deleting the point
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	const bool bIsLastPoint = (pointIndex == numPoints - 1);
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);
	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	if (TangentMode == EITwinTangentMode::Linear)
	{
		SplineComp.SetTangentsAtSplinePoint(prevPointIndex,
			SplineComp.GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL),
			nextPos - prevPos,
			SPL_LOCAL, false);

		SplineComp.SetTangentsAtSplinePoint(nextPointIndex,
			nextPos - prevPos,
			SplineComp.GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL),
			SPL_LOCAL, false);
	}
	else if (TangentMode == EITwinTangentMode::Smooth)
	{
		FVector prevPrevPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints), SPL_LOCAL);

		SplineComp.SetTangentAtSplinePoint(
			prevPointIndex, (nextPos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		SplineComp.SetTangentAtSplinePoint(
			nextPointIndex, (nextNextPos - prevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
	}

	if (Spline && pointIndex < Spline->GetNumberOfPoints())
	{
		UpdatePointFromUEtoAViz(prevPointIndex);
		UpdatePointFromUEtoAViz(nextPointIndex);
	}

	// Remove the spline point
	if (pointIndex < SplineComp.GetNumberOfSplinePoints())
	{
		SplineComp.RemoveSplinePoint(pointIndex);

		if (Spline && pointIndex < Spline->GetNumberOfPoints())
		{
			Spline->RemovePoint(static_cast<size_t>(pointIndex));
			CHECK_NUMBER_OF_POINTS();
		}
	}

	// Remove the meshes representing the point
	int32 splineMeshIndex = pointIndex;
	if (bIsLastPoint && !Owner.IsClosedLoop())
	{
		splineMeshIndex = pointIndex - 1;
	}
	RemoveSplineMeshComponentForPoint(splineMeshIndex);
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	if (pointIndex < Owner.PointMeshComponents.Num()
		&& Owner.PointMeshComponents[pointIndex])
	{
		Owner.PointMeshComponents[pointIndex]->UnregisterComponent();
		Owner.PointMeshComponents[pointIndex]->DestroyComponent();
		Owner.PointMeshComponents.RemoveAt(pointIndex);
	}

	// Update the meshes of the previous point to fill the gap
	numPoints = SplineComp.GetNumberOfSplinePoints();
	prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	UpdateMeshComponentsForPoint(prevPointIndex);

	if (TangentMode == EITwinTangentMode::Smooth)
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(prevPointIndex, numPoints));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints));
	}
}

void AITwinSplineHelper::FImpl::DuplicatePoint(int32 pointIndex)
{
	using namespace AdvViz::SDK;

	if (!Owner.SplineComponent || pointIndex < 0)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);

	FVector pointPos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector arriveTangent = SplineComp.GetArriveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector leaveTangent = SplineComp.GetLeaveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	SplineComp.AddSplinePointAtIndex(pointPos, pointIndex, SPL_LOCAL, false);
	SplineComp.SetTangentsAtSplinePoint(pointIndex, arriveTangent, FVector(0), SPL_LOCAL, false);
	SplineComp.SetTangentsAtSplinePoint(pointIndex + 1, FVector(0), leaveTangent, SPL_LOCAL, false);
	SplineComp.UpdateSpline();
	AddMeshComponentsForPoint(pointIndex);

	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	// Add the same point in the SDK Core spline.
	if (Spline)
	{
		Spline->InsertPoint(static_cast<size_t>(pointIndex));
		UpdatePointFromUEtoAViz(pointIndex);
		UpdatePointFromUEtoAViz(pointIndex + 1);

		CHECK_NUMBER_OF_POINTS();
	}
}

void AITwinSplineHelper::FImpl::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	if (!Owner.SplineComponent)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints);
	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_WORLD);
	FVector currPos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_WORLD);

	DuplicatePoint(pointIndex);

	double direction = (nextPos - prevPos).Dot(newWorldPosition - currPos);
	if (direction > 0)
	{
		pointIndex++;
	}
}

void AITwinSplineHelper::FImpl::ScaleMeshComponentsForCurrentPOV()
{
	// Scale meshes depending on distance to the camera
	APlayerController const* pController = Owner.GetWorld()->GetFirstPlayerController();
	if (!pController || !pController->PlayerCameraManager)
	{
		return;
	}
	bool const bOrtho = pController->PlayerCameraManager->IsOrthographic();
	if (bOrtho)
	{
		// TODO_JDE
		return;
	}
	FVector const CameraPos = pController->PlayerCameraManager->GetCameraLocation();
	double const FOVRad = FMath::DegreesToRadians(pController->PlayerCameraManager->GetFOVAngle());
	double const SinFOV = FMath::Sin(FOVRad);
	double MinScreenPercentage = 1.0;
	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		if (PointMeshComp)
		{
			auto const Dist = (PointMeshComp->Bounds.Origin - CameraPos).Length();
			double const EvalScreenPercentage = PointMeshComp->Bounds.SphereRadius / (Dist * SinFOV);
			MinScreenPercentage = std::min(MinScreenPercentage, EvalScreenPercentage);
		}
	}
	double const DesiredPercentage = 0.015;
	double const dMult = DesiredPercentage / MinScreenPercentage;
	if (std::fabs(1.0 - dMult) < 0.05)
		return;

	ScaleFactor *= dMult;

	const FVector NewScale3D = FVector(ScaleFactor);
	const FVector2D NewSplineScale = FVector2D(ScaleFactor * FImpl::RIBBON_SCALE, 1.);

	// Scale all components to reach the desired size (approximatively).
	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		if (PointMeshComp)
		{
			PointMeshComp->SetRelativeScale3D(NewScale3D);
		}
	}
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		if (SplineMeshComp)
		{
			SplineMeshComp->SetStartScale(NewSplineScale, true);
			SplineMeshComp->SetEndScale(NewSplineScale, true);
		}
	}
}

void AITwinSplineHelper::FImpl::SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline)
{
	if (!Owner.SplineComponent)
		return;

	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	const bool bPropertyChanged = (bInClosedLoop != Owner.SplineComponent->IsClosedLoop());
	Owner.SplineComponent->SetClosedLoop(bInClosedLoop, bUpdateSpline);

	if (bPropertyChanged)
	{
		// Add or remove the last segment, depending on the new closed loop state
		const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
		if (bInClosedLoop && ensure(Owner.SplineMeshComponents.Num() == NbSplinePoints - 1))
		{
			AddSplineMeshComponentsForPoint(NbSplinePoints - 1);
			UpdateMeshComponentsForPoint(NbSplinePoints - 1);
		}
		else if (!bInClosedLoop && ensure(Owner.SplineMeshComponents.Num() == NbSplinePoints))
		{
			RemoveSplineMeshComponentForPoint(NbSplinePoints - 1);
		}
		if (Spline)
		{
			Spline->SetClosedLoop(bInClosedLoop);
		}
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();
}


AITwinSplineHelper::FSpawnContext::FSpawnContext(EITwinSplineUsage SplineUsage)
{
	ensureMsgf(!FImpl::UsageForSpawnedActor, TEXT("do not nest AITwinSplineHelper construction"));
	FImpl::UsageForSpawnedActor = SplineUsage;
}

AITwinSplineHelper::FSpawnContext::~FSpawnContext()
{
	FImpl::UsageForSpawnedActor.reset();
}


AITwinSplineHelper::AITwinSplineHelper()
	: AActor(), Impl(MakePimpl<FImpl>(*this))
{
	// For cutout polygon, we'll use the spline component of the cartographic polygon ; in all other cases, we
	// use a standalone spline component:
	EITwinSplineUsage SplineUsage = FImpl::UsageForSpawnedActor.value_or(EITwinSplineUsage::Undefined);
	Impl->Usage = SplineUsage;
	if (SplineUsage != EITwinSplineUsage::MapCutout)
	{
		this->SplineComponent = CreateDefaultSubobject<USplineComponent>(
			FName(*UEnum::GetDisplayValueAsText(SplineUsage).ToString()));
		this->SplineComponent->SetClosedLoop(SplineUsage == EITwinSplineUsage::PopulationZone);
		this->SplineComponent->SetMobility(EComponentMobility::Movable);
		// Create just one point located at reference position
		this->SplineComponent->SetSplinePoints(
			TArray<FVector>{
			FVector(0.0f, 0.0f, 0.0f)},
			ESplineCoordinateSpace::Local);
		SetRootComponent(this->SplineComponent);
	}
	else
	{
		SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	}
	GetRootComponent()->SetMobility(EComponentMobility::Movable); // needed for the anchor

	GlobeAnchor = CreateDefaultSubobject<UCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));

	PrimaryActorTick.bCanEverTick = true;
}

std::shared_ptr<AdvViz::SDK::ISpline> AITwinSplineHelper::GetAVizSpline() const
{
	return Impl->Spline;
}

namespace ITwin {
	ModelLink GetSplineModelLink(AdvViz::SDK::SharedSpline const& Spline);
}

std::optional<ITwin::ModelLink> AITwinSplineHelper::GetLinkedModel() const
{
	if (Impl->Spline && !Impl->Spline->GetLinkedModelId().empty())
	{
		return ITwin::GetSplineModelLink(Impl->Spline);
	}
	return std::nullopt;
}

int32 AITwinSplineHelper::GetNumberOfSplinePoints() const
{
	return SplineComponent ? SplineComponent->GetNumberOfSplinePoints() : 0;
}

bool AITwinSplineHelper::IsClosedLoop() const
{
	return SplineComponent && SplineComponent->IsClosedLoop();
}

void AITwinSplineHelper::SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline /*= true*/)
{
	Impl->SetClosedLoop(bInClosedLoop, bUpdateSpline);
}

void AITwinSplineHelper::Initialize(USplineComponent* splineComp, std::shared_ptr<AdvViz::SDK::ISpline> spline)
{
	Impl->Initialize(splineComp, spline);
}

EITwinSplineUsage AITwinSplineHelper::GetUsage() const
{
	ensureMsgf(!Impl->Spline || static_cast<EITwinSplineUsage>(Impl->Spline->GetUsage()) == Impl->Usage,
		TEXT("unsynchronized spline usage Unreal vs AdvViz::SDK"));
	return Impl->Usage;
}

EITwinTangentMode AITwinSplineHelper::GetTangentMode() const
{
	return Impl->TangentMode;
}

void AITwinSplineHelper::SetTangentMode(const EITwinTangentMode mode)
{
	Impl->SetTangentMode(mode);
}

int32 AITwinSplineHelper::FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const
{
	return Impl->FindPointIndexFromMeshComponent(meshComp);
}

ACesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygon() const
{
	return CartographicPolygon.Get();
}

void AITwinSplineHelper::SetCartographicPolygon(ACesiumCartographicPolygon* InPolygon)
{
	CartographicPolygon = InPolygon;
}

void AITwinSplineHelper::SetTransform(const FTransform& NewTransform, bool markSplineForSaving)
{
	SetActorTransform(NewTransform);

	Impl->SetTransform(NewTransform, markSplineForSaving);
}

FVector AITwinSplineHelper::GetLocationAtSplinePoint(int32 pointIndex) const
{
	return Impl->GetLocationAtSplinePoint(pointIndex);
}

void AITwinSplineHelper::SetLocationAtSplinePoint(int32 pointIndex, const FVector& location)
{
	Impl->SetLocationAtSplinePoint(pointIndex, location);
}

bool AITwinSplineHelper::IncludeInWorldBox(FBox& Box) const
{
	return Impl->IncludeInWorldBox(Box);
}

int32 AITwinSplineHelper::MinNumberOfPointsForValidSpline() const
{
	return IsClosedLoop() ? 3 : 2;
}

bool AITwinSplineHelper::CanDeletePoint() const
{
	return GetNumberOfSplinePoints() > MinNumberOfPointsForValidSpline();
}

void AITwinSplineHelper::DeletePoint(int32 pointIndex)
{
	Impl->DeletePoint(pointIndex);
}

void AITwinSplineHelper::DuplicatePoint(int32 pointIndex)
{
	Impl->DuplicatePoint(pointIndex);
}

// This function can be used to duplicate a point when moving it. The passed index should
// be the currently selected point. It is modified if necessary depending on the movement.
void AITwinSplineHelper::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	Impl->DuplicatePoint(pointIndex, newWorldPosition);
}

void AITwinSplineHelper::Tick(float /*DeltaTime*/)
{
	if (IsHidden())
	{
		return;
	}

	Impl->ScaleMeshComponentsForCurrentPOV();
}
