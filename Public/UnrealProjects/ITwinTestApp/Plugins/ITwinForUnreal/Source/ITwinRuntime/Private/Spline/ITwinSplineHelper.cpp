/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineHelper.inl>
#include <Math/UEMathConversion.h>
#include <CesiumGlobeAnchorComponent.h>
#include <CesiumCartographicPolygon.h>
#include <Components/SceneComponent.h>
#include <Components/SplineComponent.h>
#include <Components/SplineMeshComponent.h>
#include <Engine/Polys.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <IncludeCesium3DTileset.h>
#include <ITwinTilesetAccess.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <SDK/Core/Tools/Assert.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <Compil/AfterNonUnrealIncludes.h>

#define SPL_LOCAL ESplineCoordinateSpace::Local
#define SPL_WORLD ESplineCoordinateSpace::World
#define SMOOTH_FACTOR 0.5

#define CHECK_NUMBER_OF_POINTS() \
	BE_ASSERT(CheckNumberOfPoints(), "Wrong number of spline points")

#define CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS() \
	BE_ASSERT(CheckSplineMeshComponents(), "Wrong number of spline mesh components")

namespace ITwinSpline
{
	inline int32 GetPrevIndex(const int32& index, const int32& arraySize, bool isLoop)
	{
		return (index > 0) ? (index - 1) : (isLoop ? (arraySize - 1) : index);
	}

	inline int32 GetNextIndex(const int32& index, const int32& arraySize, bool isLoop)
	{
		return (index < arraySize - 1) ? (index + 1) : (isLoop ? 0 : index);
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

	struct FTracingData
	{
		/// Polygon built from the spline points (used for line tracing).
		FPoly SplinePolygon;

		/// Barycenter used for selection gizmo when the spline is globally selected.
		FVector SplineBarycenter = FVector::ZeroVector;

		bool bNeedUpdateTracingData = true;
	};
	mutable FTracingData TracingData;

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
	void RecreateAllMeshComponents();
	void AddSplineMeshComponentsForPoint(int32 pointIndex);
	void RemoveSplineMeshComponentForPoint(int32 pointIndex);
	bool CheckSplineMeshComponents() const;
	void AddMeshComponentsForPoint(int32 pointIndex);
	void UpdateAllMeshComponents();
	void UpdateMeshComponentsForPoint(int32 pointIndex);

	void SetTangentMode(const EITwinTangentMode mode);
	void SetTransform(const FTransform& NewTransform, bool markSplineForSaving);
	FVector GetLocationAtSplinePoint(int32 pointIndex) const;
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);

	bool IncludeInWorldBox(FBox& Box) const;

	void DeletePoint(int32 pointIndex);

	void DuplicatePoint(int32 pointIndex);
	void DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);
	int32 InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition);
	void ScaleMeshComponentsForCurrentPOV();
	void SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline);
	bool LoopIndices() const;

	//! Propagate a modification to all secondary cartographic polygons. PrevPointIndex and NextPointIndex
	//! can be supplied to update the surrounding points as well.
	void CopyPointToSecondaryCartographicPolygons(int32 PointIndex,
		int32 PrevPointIndex = -1, int32 NextPointIndex = -1);

	void InsertPointInSecondaryCartographicPolygons(int32 PointIndex);

	template <typename TFunc>
	void ForEachUESplineComponent(TFunc const& Func) const;


	inline int32 GetCommonNumberOfPointsInAllPolygons() const
	{
		int32 CommonNum = -1;
		for (auto const& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
		{
			ACesiumCartographicPolygon const* Polygon = PolygonPtr.Get();
			if (Polygon && ensure(Polygon->Polygon))
			{
				const int32 NbPts = Polygon->Polygon->GetNumberOfSplinePoints();
				if (CommonNum == -1)
					CommonNum = NbPts;
				else if (NbPts != CommonNum)
					return -1;
			}
		}
		return CommonNum;
	}

	inline bool CheckNumberOfPoints() const
	{
		if (!ensureMsgf(!Spline ||
			Owner.SplineComponent->GetNumberOfSplinePoints() == static_cast<int32>(Spline->GetNumberOfPoints()),
			TEXT("The UE and AdvViz::SDK splines should have the same number of points.")))
		{
			return false;
		}

		if (!ensureMsgf(Owner.PerGeorefPolygonMap.IsEmpty() ||
			Owner.SplineComponent->GetNumberOfSplinePoints() == GetCommonNumberOfPointsInAllPolygons(),
			TEXT("All cartographic polygons associated to this spline should have the same number of points.")))
		{
			return false;
		}
		return true;
	}

	void UpdateTracingData() const;
	void InvalidateTracingData() { TracingData.bNeedUpdateTracingData = true; }
	bool DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const;

	FVector const& GetBarycenter() const {
		if (TracingData.bNeedUpdateTracingData)
			UpdateTracingData();
		return TracingData.SplineBarycenter;
	}
};

/*static*/
std::optional<EITwinSplineUsage> AITwinSplineHelper::FImpl::UsageForSpawnedActor = std::nullopt;

AITwinSplineHelper::FImpl::FImpl(AITwinSplineHelper& InOwner) : Owner(InOwner)
{
}

template <typename TFunc>
void AITwinSplineHelper::FImpl::ForEachUESplineComponent(TFunc const& Func) const
{
	if (Owner.SplineComponent)
	{
		Func(*Owner.SplineComponent);
	}
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			Func(*Polygon->Polygon);
		}
	}
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

	CopyPointToSecondaryCartographicPolygons(pointIndex);
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

	// Adjust the number of points in all USplineComponent(s)
	const int32 NbPoints = static_cast<int32>(Spline->GetNumberOfPoints());

	ForEachUESplineComponent([NbPoints](USplineComponent& SplineComp)
	{
		int32 CurNbPoints = SplineComp.GetNumberOfSplinePoints();
		if (CurNbPoints < NbPoints)
		{
			// Add missing point(s) to UE component
			for (; CurNbPoints < NbPoints; ++CurNbPoints)
			{
				SplineComp.AddSplinePoint(FVector(0), SPL_LOCAL, false);
			}
		}
		else if (CurNbPoints > NbPoints)
		{
			// Remove supernumerary point(s) from UE component
			for (; CurNbPoints > NbPoints; CurNbPoints--)
			{
				SplineComp.RemoveSplinePoint(CurNbPoints - 1, false);
			}
		}
	});
	CHECK_NUMBER_OF_POINTS();

	ForEachUESplineComponent([bIsClosedLoop = Spline->IsClosedLoop()](USplineComponent& SplineComponent)
	{
		SplineComponent.SetClosedLoop(bIsClosedLoop, false /*bUpdateSpline*/);
	});

	// Update points
	for (int32 i = 0; i < NbPoints; ++i)
	{
		UpdatePointFromAVizToUE(i);
	}

	InvalidateTracingData();

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

	ForEachUESplineComponent([NbPoints](USplineComponent& SplineComponent)
	{
		SplineComponent.UpdateSpline();
	});
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

void AITwinSplineHelper::FImpl::RecreateAllMeshComponents()
{
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		SplineMeshComp->UnregisterComponent();
		SplineMeshComp->DestroyComponent();
	}
	Owner.SplineMeshComponents.Reset();

	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		PointMeshComp->UnregisterComponent();
		PointMeshComp->DestroyComponent();
	}
	Owner.PointMeshComponents.Reset();

	AddAllMeshComponents();
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

	bool isLoop = LoopIndices();
	int32 startIndex = pointIndex;
	int32 endIndex = ITwinSpline::GetNextIndex(startIndex, SplineComp.GetNumberOfSplinePoints(), isLoop);

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
		bool isLoop = LoopIndices();
		int32 prevIndex = ITwinSpline::GetPrevIndex(i, NbSplinePoints, isLoop);
		int32 currIndex = i;
		int32 nextIndex = ITwinSpline::GetNextIndex(i, NbSplinePoints, isLoop);

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
	Owner.IterateAllCartographicPolygons([&](ACesiumCartographicPolygon& Polygon)
	{
		Polygon.SetActorTransform(NewTransform);
	});
	InvalidateTracingData();

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

	bool isLoop = LoopIndices();
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);

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
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints, isLoop), SPL_LOCAL);

		if (prevPointIndex != pointIndex)
			SplineComp.SetTangentAtSplinePoint(
				prevPointIndex, (pos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		SplineComp.SetTangentAtSplinePoint(
			pointIndex, (nextPos - prevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		if (nextPointIndex != pointIndex)
			SplineComp.SetTangentAtSplinePoint(
				nextPointIndex, (nextNextPos - pos) * SMOOTH_FACTOR, SPL_LOCAL, false);
	}

	InvalidateTracingData();
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
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop));
	}

	CopyPointToSecondaryCartographicPolygons(pointIndex, prevPointIndex, nextPointIndex);
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
	bool isLoop = LoopIndices();
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	const bool bIsLastPoint = (pointIndex == numPoints - 1);
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);
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
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints, isLoop), SPL_LOCAL);

		if (nextPointIndex == pointIndex) // deleting last point of a non-closed spline -> do not take into account the position of the point being deleted
			SplineComp.SetTangentAtSplinePoint(
				prevPointIndex, (prevPos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		else 
			SplineComp.SetTangentAtSplinePoint(
				prevPointIndex, (nextPos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);

		if (prevPointIndex == pointIndex) // deleting first point of a non-closed spline -> do not take into account the position of the point being deleted
			SplineComp.SetTangentAtSplinePoint(
				nextPointIndex, (nextNextPos - nextPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		else
			SplineComp.SetTangentAtSplinePoint(
				nextPointIndex, (nextNextPos - prevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
	}
	// Update tangents in the secondary polygons
	CopyPointToSecondaryCartographicPolygons(pointIndex, prevPointIndex, nextPointIndex);

	if (Spline && pointIndex < Spline->GetNumberOfPoints())
	{
		UpdatePointFromUEtoAViz(prevPointIndex);
		UpdatePointFromUEtoAViz(nextPointIndex);
	}

	// Remove the spline point
	if (pointIndex < SplineComp.GetNumberOfSplinePoints())
	{
		ForEachUESplineComponent([pointIndex](USplineComponent& SplineComponent)
		{
			SplineComponent.RemoveSplinePoint(pointIndex);
		});
		if (Spline && pointIndex < Spline->GetNumberOfPoints())
		{
			Spline->RemovePoint(static_cast<size_t>(pointIndex));
		}
		CHECK_NUMBER_OF_POINTS();
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
	prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	UpdateMeshComponentsForPoint(prevPointIndex);

	if (TangentMode == EITwinTangentMode::Smooth)
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(prevPointIndex, numPoints, isLoop));
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop));
	}
	InvalidateTracingData();
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

	InsertPointInSecondaryCartographicPolygons(pointIndex);
	CopyPointToSecondaryCartographicPolygons(pointIndex, -1, pointIndex + 1);

	// Add the same point in the SDK Core spline.
	if (Spline)
	{
		Spline->InsertPoint(static_cast<size_t>(pointIndex));
		UpdatePointFromUEtoAViz(pointIndex);
		UpdatePointFromUEtoAViz(pointIndex + 1);
	}
	CHECK_NUMBER_OF_POINTS();
}

void AITwinSplineHelper::FImpl::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	if (!Owner.SplineComponent)
	{
		return;
	}

	bool isLoop = LoopIndices();
	USplineComponent& SplineComp(*Owner.SplineComponent);
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);
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

int32 AITwinSplineHelper::FImpl::InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition)
{
	if (!Owner.SplineComponent)
	{
		return INDEX_NONE;
	}
	USplineComponent& SplineComp(*Owner.SplineComponent);
	int32 NumPoints = SplineComp.GetNumberOfSplinePoints();
	if (!ensure(PointIndex >= 0 && NumPoints > 0 && PointIndex <= NumPoints))
	{
		return INDEX_NONE;
	}
	// To avoid code duplication, let's duplicate a point and move it at once.
	DuplicatePoint(std::min(PointIndex, NumPoints - 1));
	if (ensure(PointIndex < SplineComp.GetNumberOfSplinePoints()))
	{
		SetLocationAtSplinePoint(PointIndex, NewWorldPosition);
		return PointIndex;
	}
	else
	{
		return INDEX_NONE;
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
	double const DesiredPercentage = 0.01;
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

bool AITwinSplineHelper::FImpl::LoopIndices() const
{
	return Owner.SplineComponent->IsClosedLoop(); // Usage == EITwinSplineUsage::AnimPath
}


void AITwinSplineHelper::FImpl::CopyPointToSecondaryCartographicPolygons(int32 PointIndex, int32 PrevPointIndex /*= -1*/, int32 NextPointIndex /*= -1*/)
{
	if (!Owner.SplineComponent)
		return;
	USplineComponent const& SrcSplineComponent(*Owner.SplineComponent);
	if (!ensure(PointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		return;
	if (PrevPointIndex >= 0 && !ensure(PrevPointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		PrevPointIndex = -1;
	if (NextPointIndex >= 0 && !ensure(NextPointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		NextPointIndex = -1;
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			USplineComponent& DstSplineComponent(*Polygon->Polygon);
			if (ensure(PointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(PointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(PointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
			if (PrevPointIndex >= 0 &&
				ensure(PrevPointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(PrevPointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(PrevPointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
			if (NextPointIndex >= 0 &&
				ensure(NextPointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(NextPointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(NextPointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::InsertPointInSecondaryCartographicPolygons(int32 PointIndex)
{
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			USplineComponent& DstSplineComponent(*Polygon->Polygon);
			if (ensure(PointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.AddSplinePointAtIndex(FVector::ZeroVector, PointIndex,
					SPL_LOCAL, false);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::UpdateTracingData() const
{
	FPoly& Polygon = TracingData.SplinePolygon;
	Polygon.Init();
	const int32 NumPoints = Owner.GetNumberOfSplinePoints();
	Polygon.Vertices.SetNum(NumPoints);
	for (int32 i(0); i < NumPoints; ++i)
	{
		Polygon.Vertices[i] = FVector3f(GetLocationAtSplinePoint(i));
	}
	if (NumPoints > 0)
	{
		TracingData.SplineBarycenter = Polygon.GetMidPoint();
	}
	else
	{
		TracingData.SplineBarycenter = FVector::ZeroVector;
	}
	Polygon.Fix();
	Polygon.CalcNormal(true);

	TracingData.bNeedUpdateTracingData = false;
}

bool AITwinSplineHelper::FImpl::DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const
{
	if (TracingData.bNeedUpdateTracingData)
	{
		UpdateTracingData();
	}

	// For some reason, FPoly::DoesLineIntersect is not exported...
	// return TracingData.SplinePolygon.DoesLineIntersect(Start, End);
	auto const& Vertices(TracingData.SplinePolygon.Vertices);
	auto const& Normal(TracingData.SplinePolygon.Normal);

	// If the ray doesn't cross the plane, don't bother going any further.
	const float DistStart = FVector::PointPlaneDist(Start, (FVector)Vertices[0], (FVector)Normal);
	const float DistEnd = FVector::PointPlaneDist(End, (FVector)Vertices[0], (FVector)Normal);

	if ((DistStart < 0 && DistEnd < 0) || (DistStart > 0 && DistEnd > 0))
	{
		return false;
	}

	// Get the intersection of the line and the plane.
	FVector Intersection = FMath::LinePlaneIntersection(Start, End, (FVector)Vertices[0], (FVector)Normal);
	//if (Intersect)	*Intersect = Intersection;
	if (Intersection == Start || Intersection == End)
	{
		return false;
	}

	// Check if the intersection point is actually on the poly.
	FVector SidePlaneNormal;
	FVector3f Side;

	for (int32 x = 0; x < Vertices.Num(); x++)
	{
		// Create plane perpendicular to both this side and the polygon's normal.
		Side = Vertices[x] - Vertices[(x - 1 < 0) ? Vertices.Num() - 1 : x - 1];
		SidePlaneNormal = FVector(Side ^ Normal);
		SidePlaneNormal.Normalize();

		// If point is not behind all the planes created by this polys edges, it's outside the poly.
		if (FVector::PointPlaneDist(Intersection, (FVector)Vertices[x], SidePlaneNormal) > UE_THRESH_POINT_ON_PLANE)
		{
			return false;
		}
	}

	return true;
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

void AITwinSplineHelper::SetAVizSpline(std::shared_ptr<AdvViz::SDK::ISpline> const& Spline)
{
	if (Impl->Spline != Spline)
	{
		Impl->Spline = Spline;
		Impl->UpdateSplineFromAVizToUE();
		Impl->RecreateAllMeshComponents();
	}
}

AdvViz::SDK::RefID AITwinSplineHelper::GetAVizSplineId() const
{
	if (Impl->Spline)
		return Impl->Spline->GetId();
	else
		return AdvViz::SDK::RefID::Invalid();
}

namespace ITwin {
	std::set<ModelLink> GetSplineModelLinks(AdvViz::SDK::SharedSpline const& Spline);
}

std::set<ITwin::ModelLink> AITwinSplineHelper::GetLinkedModels() const
{
	if (Impl->Spline && !Impl->Spline->GetLinkedModels().empty())
	{
		return ITwin::GetSplineModelLinks(Impl->Spline);
	}
	return {};
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

int32 AITwinSplineHelper::FindPointIndexFromMeshComponent(UStaticMeshComponent* MeshComp) const
{
	return PointMeshComponents.Find(MeshComp);
}

UStaticMeshComponent* AITwinSplineHelper::GetPointMeshComponent(int32 PointIndex) const
{
	if (ensure(PointIndex < PointMeshComponents.Num()))
	{
		return PointMeshComponents[PointIndex];
	}
	else
	{
		return nullptr;
	}
}

int32 AITwinSplineHelper::FindSegmentIndexFromSplineComponent(USplineMeshComponent* SplineMeshComp) const
{
	return SplineMeshComponents.Find(SplineMeshComp);
}


ACesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygonForTileset(FITwinTilesetAccess const& TilesetAccess) const
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		return GetCartographicPolygonForGeoref(Tileset->GetGeoreference());
	else
		return nullptr;
}

ACesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef) const
{
	auto const* Polygon = PerGeorefPolygonMap.Find(Georef);
	if (Polygon)
	{
		return (*Polygon).Get();
	}
	else
	{
		return nullptr;
	}
}

bool AITwinSplineHelper::HasCartographicPolygon() const
{
	for (auto const& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		if (PolygonPtr.Get())
			return true;
	}
	return false;
}

void AITwinSplineHelper::SetCartographicPolygonForTileset(ACesiumCartographicPolygon* Polygon, FITwinTilesetAccess const& TilesetAccess)
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		SetCartographicPolygonForGeoref(Polygon, Tileset->GetGeoreference());
}

void AITwinSplineHelper::SetCartographicPolygonForGeoref(ACesiumCartographicPolygon* Polygon, TSoftObjectPtr<ACesiumGeoreference> const& Georef)
{
	PerGeorefPolygonMap.FindOrAdd(Georef) = Polygon;
}

ACesiumCartographicPolygon* AITwinSplineHelper::ClonePolygonForTileset(FITwinTilesetAccess const& TilesetAccess)
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		return ClonePolygonForGeoref(Tileset->GetGeoreference());
	else
		return nullptr;
}

namespace ITwin
{
	ACesiumCartographicPolygon* DuplicatePolygonForGeoref(ACesiumCartographicPolygon const& SrcPolygon,
		const TSoftObjectPtr<ACesiumGeoreference>& Georef,
		UWorld& World)
	{
		if (!Georef)
			return nullptr;
		if (!SrcPolygon.Polygon || SrcPolygon.Polygon->GetNumberOfSplinePoints() == 0)
			return nullptr;
		// Create a Cesium cartographic polygon
		ACesiumCartographicPolygon* DstPolygon = World.SpawnActor<ACesiumCartographicPolygon>();
		DstPolygon->GlobeAnchor->SetGeoreference(Georef);
		DstPolygon->SetActorLocation(SrcPolygon.GetActorLocation());

		USplineComponent* DstSplineComponent = DstPolygon->Polygon;
		if (!ensure(DstSplineComponent != nullptr))
		{
			return nullptr;
		}

		DstPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
		// Replace the default spline points by those defined by the AdvViz spline.
		DstSplineComponent->ClearSplinePoints();

		USplineComponent const& SrcSplineComponent(*SrcPolygon.Polygon);
		const int32 NumPoints = SrcSplineComponent.GetNumberOfSplinePoints();
		TArray<FVector> DstPoints;
		DstPoints.SetNum(NumPoints);
		for (int32 i(0); i < NumPoints; ++i)
		{
			DstPoints[i] = SrcSplineComponent.GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		}
		DstSplineComponent->SetSplinePoints(DstPoints, ESplineCoordinateSpace::World);

		DstPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
		return DstPolygon;
	}
}

ACesiumCartographicPolygon* AITwinSplineHelper::ClonePolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef)
{
	ensureMsgf(GetCartographicPolygonForGeoref(Georef) == nullptr, TEXT("Polygon already exists for this geo-ref"));

	// Try to find a valid polygon in map
	ACesiumCartographicPolygon const* MasterPolygon = nullptr;
	for (auto const& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		MasterPolygon = PolygonPtr.Get();
		if (MasterPolygon)
			break;
	}
	if (!ensure(MasterPolygon != nullptr))
		return nullptr;
	UWorld* World = GetWorld();
	if (!ensure(World))
		return nullptr;
	ACesiumCartographicPolygon* NewPolygon = ITwin::DuplicatePolygonForGeoref(*MasterPolygon, Georef, *World);
	if (NewPolygon)
	{
		SetCartographicPolygonForGeoref(NewPolygon, Georef);
	}
	return NewPolygon;
}

void AITwinSplineHelper::DeleteCartographicPolygons(TFunction<void(ACesiumCartographicPolygon*)> const& BeforeDeleteCallback)
{
	for (auto& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon)
		{
			if (BeforeDeleteCallback)
			{
				BeforeDeleteCallback(Polygon);
			}
			Polygon->Destroy();
			PolygonPtr = {};
		}
	}
	PerGeorefPolygonMap.Reset();
}

void AITwinSplineHelper::SetTransform(const FTransform& NewTransform, bool bMarkSplineForSaving)
{
	SetActorTransform(NewTransform);

	Impl->SetTransform(NewTransform, bMarkSplineForSaving);
}

FTransform AITwinSplineHelper::GetTransformForUserInteraction() const
{
	FTransform Transform = GetActorTransform();
	Transform.SetTranslation(Impl->GetBarycenter());
	return Transform;
}

void AITwinSplineHelper::SetTransformFromUserInteraction(const FTransform& NewTransform)
{
	// Take the offset with the barycenter into account.
	FTransform FinalTransform = NewTransform;
	FVector FinalPos = NewTransform.GetTranslation();
	FinalPos += GetActorLocation() - Impl->GetBarycenter();
	FinalTransform.SetTranslation(FinalPos);
	SetTransform(FinalTransform, true);
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

bool AITwinSplineHelper::DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const
{
	return Impl->DoesLineIntersectSplinePolygon(Start, End);
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

int32 AITwinSplineHelper::InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition)
{
	return Impl->InsertPointAt(PointIndex, NewWorldPosition);
}

void AITwinSplineHelper::Tick(float /*DeltaTime*/)
{
	if (IsHidden())
	{
		return;
	}

	Impl->ScaleMeshComponentsForCurrentPOV();
}

namespace ITwin
{
	bool AddCutoutPolygon(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon);
	bool RemoveCutoutPolygon(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon);
	void InvertCutoutPolygonEffect(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon, bool bInvertEffect);
}

void AITwinSplineHelper::ActivateCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bActivate,
	bool bIsCreatingSpline /*= false*/)
{
	if (!ensure(GetUsage() == EITwinSplineUsage::MapCutout))
		return;
	const ITwin::ModelLink Link = TilesetAccess.GetDecorationKey();
	const bool bActivated_Cur = GetLinkedModels().contains(Link);
	if (bActivated_Cur == bActivate && !bIsCreatingSpline)
		return; // Nothing to do

	bool bModified = false;
	ACesiumCartographicPolygon* Polygon = GetCartographicPolygonForTileset(TilesetAccess);
	if (bActivate)
	{
		// Here me may need to instantiate a new polygon, if none exists yet for this tileset
		// geo-reference:
		if (!Polygon)
		{
			Polygon = ClonePolygonForTileset(TilesetAccess);
		}
		bModified = Polygon && ITwin::AddCutoutPolygon(TilesetAccess, Polygon);
	}
	else
	{
		bModified = Polygon && ITwin::RemoveCutoutPolygon(TilesetAccess, Polygon);
	}

	// Handle persistence:
	if (Impl->Spline)
	{
		// Rebuild the list of linked models in SDKCore
		std::vector<AdvViz::SDK::SplineLinkedModel> LinkedModels = Impl->Spline->GetLinkedModels();
		const AdvViz::SDK::SplineLinkedModel EditedModel = {
			.modelType = ITwin::ModelTypeToString(Link.first),
			.modelId = TCHAR_TO_UTF8(*Link.second)
		};
		if (bActivate)
		{
			if (std::find(LinkedModels.begin(), LinkedModels.end(), EditedModel) == LinkedModels.end())
			{
				LinkedModels.push_back(EditedModel);
			}
		}
		else
		{
			std::erase_if(LinkedModels, [&EditedModel](const auto& LinkedModel)
			{
				return LinkedModel.modelType == EditedModel.modelType
					&& LinkedModel.modelId == EditedModel.modelId;
			});
		}
		Impl->Spline->SetLinkedModels(LinkedModels);
		ensure(GetLinkedModels().contains(Link) == bActivate);
	}
}

bool AITwinSplineHelper::IsEnabledEffect() const
{
	return Impl->Spline && Impl->Spline->IsEnabledEffect();
}

void AITwinSplineHelper::EnableEffect(bool bEnable)
{
	// Handle persistence
	if (Impl->Spline)
	{
		Impl->Spline->EnableEffect(bEnable);
	}
}

bool AITwinSplineHelper::IsInvertedCutoutEffect() const
{
	return Impl->Spline && Impl->Spline->GetInvertEffect();
}

void AITwinSplineHelper::InvertCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bInvert)
{
	if (!ensure(GetUsage() == EITwinSplineUsage::MapCutout))
		return;

	ACesiumCartographicPolygon* Polygon = GetCartographicPolygonForTileset(TilesetAccess);
	if (!Polygon)
		return;

	// NB: the inversion of a cutout polygon is managed in UCesiumPolygonRasterOverlay level, and not in
	// ACesiumCartographicPolygon => we cannot invert the effect of an individual polygon, it will apply
	// to all polygons of the tileset.
	ITwin::InvertCutoutPolygonEffect(TilesetAccess, Polygon, bInvert);

	// Handle persistence
	if (Impl->Spline)
	{
		Impl->Spline->SetInvertEffect(bInvert);
	}
}
