/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineGeometry.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Containers/ArrayView.h>
#include <Engine/Polys.h>

#include <Spline/ITwinSplineEnums.h>

//! Engine-agnostic (component-free) geometry and index helpers backing AITwinSplineHelper.
//! Everything here is testable under NullRHI: it depends only on FVector/FPoly/FMath and
//! never on USplineComponent, UStaticMeshComponent or the 2D widget.
namespace ITwinSpline
{
	//! Maximum tangent length (in cm) corresponding to a tightness of 0.
	inline constexpr double MaxTangentLength = 10000.;

	inline int32 GetPrevIndex(const int32 Index, const int32 NumPoints, bool bLoop)
	{
		return (Index > 0) ? (Index - 1) : (bLoop ? (NumPoints - 1) : Index);
	}

	inline int32 GetNextIndex(const int32 Index, const int32 NumPoints, bool bLoop)
	{
		return (Index < NumPoints - 1) ? (Index + 1) : (bLoop ? 0 : Index);
	}

	inline bool IsPathAnim(const EITwinSplineUsage Usage)
	{
		return Usage == EITwinSplineUsage::AnimPath
			|| Usage == EITwinSplineUsage::AnimPathTraffic
			|| Usage == EITwinSplineUsage::AnimPathCrowd
			|| Usage == EITwinSplineUsage::AnimPathObject;
	}

	inline bool IsPopulation(const EITwinSplineUsage Usage)
	{
		return Usage == EITwinSplineUsage::PopulationPath
			|| Usage == EITwinSplineUsage::PopulationZone
			|| Usage == EITwinSplineUsage::SplinePopulation;
	}

	//! Minimum number of points to build a non-degenerated spline.
	inline int32 MinNumberOfPointsForValidSpline(bool bClosedLoop)
	{
		return bClosedLoop ? 3 : 2;
	}

	inline bool CanDeletePoint(int32 NumPoints, bool bClosedLoop)
	{
		return NumPoints > MinNumberOfPointsForValidSpline(bClosedLoop);
	}

	//! Converts a tightness in [0;1] to the target tangent length.
	//! 0 => flat/smooth (max tangent length), 1 => sharp turn (~zero tangent length).
	ITWINRUNTIME_API double TangentLengthFromTightness(float InTightness);

	//! Inverse of TangentLengthFromTightness.
	ITWINRUNTIME_API float TightnessFromTangentLength(double TangentLength);

	//! Half-width used to turn a 2-point spline into a thin pickable rectangle.
	inline constexpr float TwoPointPolygonHalfWidth = 50.f; // considering spline to be 1m wide

	//! Builds the polygon used for line-tracing from the spline point locations, and computes
	//! the barycenter used by the selection gizmo.
	//! Handles the degenerated cases: 0/1 point (empty polygon, zero barycenter) and 2 points
	//! (thin rectangle).
	ITWINRUNTIME_API void BuildSplinePolygon(TArrayView<const FVector> Points,
		FPoly& OutPolygon, FVector& OutBarycenter);

	//! Tests whether the [Start;End] segment intersects the given polygon.
	//! Re-implementation of FPoly::DoesLineIntersect, which is not exported.
	ITWINRUNTIME_API bool DoesLineIntersectPolygon(FPoly const& Polygon,
		const FVector& Start, const FVector& End);

	//! When a point is duplicated in order to be dragged, the two resulting points are
	//! co-located and we must decide which one the user is actually moving.
	//! Returns true if the *new* (second) point should follow the cursor, meaning the caller's
	//! point index must be incremented; false if the original point keeps the index.
	//! Decided by projecting the intended movement onto the local spline direction.
	inline bool ShouldAdvanceIndexAfterDuplication(
		const FVector& PrevPos, const FVector& CurrPos, const FVector& NextPos,
		const FVector& NewWorldPosition)
	{
		// Strictly positive: a movement perpendicular to the spline keeps the original index.
		return (NextPos - PrevPos).Dot(NewWorldPosition - CurrPos) > 0.;
	}

	//! Whether PointIndex denotes a valid insertion slot for a spline of NumPoints points.
	//! Insertion is allowed *after* the last point, hence the inclusive upper bound.
	inline bool IsValidInsertionIndex(int32 PointIndex, int32 NumPoints)
	{
		return PointIndex >= 0 && NumPoints > 0 && PointIndex <= NumPoints;
	}

	//! Insertion is implemented by duplicating an existing point and then moving it.
	//! Returns the index of the point to duplicate, clamped so that inserting after the
	//! last point duplicates the last point rather than running out of range.
	inline int32 ResolveDuplicationSourceIndex(int32 PointIndex, int32 NumPoints)
	{
		return FMath::Min(PointIndex, NumPoints - 1);
	}
}
