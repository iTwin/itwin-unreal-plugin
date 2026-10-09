/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUESplineCurve.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinUESplineCurve.h>

#include <Components/SplineComponent.h>

// ----------------------------- FITwinSplineSnapshotCurve -----------------------------

class FITwinSplineSnapshotCurve::FImpl
{
public:
	FSplineCurves SplineCurves;
	FTransform ComponentToWorld;
	bool bClosedLoop = false;

	explicit FImpl(USplineComponent const& InSpline)
		: SplineCurves(InSpline.GetSplineCurves())
		, ComponentToWorld(InSpline.GetComponentTransform())
		, bClosedLoop(InSpline.IsClosedLoop())
	{
	}

	int32 NumPoints() const { return SplineCurves.Position.Points.Num(); }

	float TimeToInputKey(value_type const& u) const
	{
		// Same mapping as USplineComponent::GetLocationAtTime with bUseConstantVelocity = false:
		// InputKey = (u * Duration) * (NumSegments / Duration), which simplifies to u * NumSegments.
		// u is clamped to the curve domain [0,1] to avoid extrapolating past the spline range when callers
		// pass values slightly outside of it (common with numeric sampling).
		const int32 NumSegments = bClosedLoop ? NumPoints() : FMath::Max(NumPoints() - 1, 0);
		if (NumSegments == 0)
			return 0.f;
		const value_type ClampedU = FMath::Clamp(u, value_type(0), value_type(1));
		return static_cast<float>(ClampedU * NumSegments);
	}
};

namespace
{
	// The snapshot reads a UObject: it must be taken on the game thread (evaluation can then occur anywhere).
	// Returns its argument so it can be used in the member initializer list, i.e. *before* FImpl touches
	// the spline.
	USplineComponent const& CheckGameThreadThenReturn(USplineComponent const& InSpline)
	{
		ensureMsgf(IsInGameThread(), TEXT("FITwinSplineSnapshotCurve must be built on the game thread"));
		return InSpline;
	}
}

FITwinSplineSnapshotCurve::FITwinSplineSnapshotCurve(USplineComponent const& InSpline)
	: Impl(MakePimpl<FImpl>(CheckGameThreadThenReturn(InSpline)))
{
}

FITwinSplineSnapshotCurve::~FITwinSplineSnapshotCurve() = default;

glm::dvec3 FITwinSplineSnapshotCurve::GetPositionAtCoord(value_type const& u) const
{
	const FVector Pos_Local = Impl->SplineCurves.Position.Eval(Impl->TimeToInputKey(u), FVector::ZeroVector);
	const FVector Pos_World = Impl->ComponentToWorld.TransformPosition(Pos_Local);
	return { Pos_World.X, Pos_World.Y, Pos_World.Z };
}

glm::dvec3 FITwinSplineSnapshotCurve::GetTangentAtCoord(value_type const& u) const
{
	const FVector Tgte_Local = Impl->SplineCurves.Position.EvalDerivative(Impl->TimeToInputKey(u), FVector::ZeroVector);
	const FVector Tgte_World = Impl->ComponentToWorld.TransformVector(Tgte_Local);
	return { Tgte_World.X, Tgte_World.Y, Tgte_World.Z };
}

size_t FITwinSplineSnapshotCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return static_cast<size_t>(Impl->NumPoints());
}

glm::dvec3 FITwinSplineSnapshotCurve::GetPositionAtIndex(size_t idx) const
{
	const int32 NumPoints = Impl->NumPoints();
	if (NumPoints == 0)
		return glm::dvec3(0.0);
	const int32 ClampedIdx = FMath::Clamp(static_cast<int32>(idx), 0, NumPoints - 1);
	const FVector Pos_World = Impl->ComponentToWorld.TransformPosition(
		Impl->SplineCurves.Position.Points[ClampedIdx].OutVal);
	return { Pos_World.X, Pos_World.Y, Pos_World.Z };
}

bool FITwinSplineSnapshotCurve::IsCyclic() const
{
	return Impl->bClosedLoop;
}

// -------------------------------- FITwinUESplineCurve --------------------------------

FITwinUESplineCurve::FITwinUESplineCurve(USplineComponent const& InSpline)
	: UESpline(InSpline)
{
}

glm::dvec3 FITwinUESplineCurve::GetPositionAtCoord(value_type const& u) const
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

glm::dvec3 FITwinUESplineCurve::GetTangentAtCoord(value_type const& u) const
{
	const float SplineTime = u * UESpline.Duration;
	auto const Tgte_World = UESpline.GetTangentAtTime(SplineTime, ESplineCoordinateSpace::World);
	return {
		Tgte_World.X,
		Tgte_World.Y,
		Tgte_World.Z
	};
}

size_t FITwinUESplineCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return static_cast<size_t>(UESpline.GetNumberOfSplinePoints());
}

glm::dvec3 FITwinUESplineCurve::GetPositionAtIndex(size_t idx) const
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

bool FITwinUESplineCurve::IsCyclic() const
{
	return UESpline.IsClosedLoop();
}


// -------------------------------- FITwinUESplineCurve --------------------------------

FITwinUESplineChunkCurve::FITwinUESplineChunkCurve(USplineComponent const& InSpline, int32 InChunkIndex)
	: UESpline(InSpline)
	, StartInputKey(static_cast<float>(InChunkIndex))
{

}

glm::dvec3 FITwinUESplineChunkCurve::GetPositionAtCoord(value_type const& u) const
{
	auto const Pos_World = UESpline.GetLocationAtSplineInputKey(StartInputKey + u, ESplineCoordinateSpace::World);
	return {
		Pos_World.X,
		Pos_World.Y,
		Pos_World.Z
	};
}

glm::dvec3 FITwinUESplineChunkCurve::GetTangentAtCoord(value_type const& u) const
{
	auto const Tgte_World = UESpline.GetTangentAtSplineInputKey(StartInputKey + u, ESplineCoordinateSpace::World);
	return {
		Tgte_World.X,
		Tgte_World.Y,
		Tgte_World.Z
	};
}

size_t FITwinUESplineChunkCurve::PointCount(const bool /*accountForCyclicity*/) const
{
	return 2; // A chunk is defined by 2 control points
}

glm::dvec3 FITwinUESplineChunkCurve::GetPositionAtIndex(size_t idx) const
{
	return GetPositionAtCoord(static_cast<value_type>(idx));
}

bool FITwinUESplineChunkCurve::IsCyclic() const
{
	return UESpline.IsClosedLoop();
}
