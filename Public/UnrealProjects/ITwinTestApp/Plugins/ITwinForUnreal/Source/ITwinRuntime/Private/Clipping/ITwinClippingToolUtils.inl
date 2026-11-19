/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolUtils.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Population/ITwinPopulation.h>

namespace ITwinClippingToolUtils
{
	// Get the plane equation (normal and W) from the transform of a given instance in the ClippingPlanePopulation.
	template <typename T>
	void GetPlaneEquationFromTransform(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW,
		const FTransform& Transform)
	{
		auto const PositionUE = Transform.GetLocation();
		auto const PlaneOrientationUE = Transform.GetUnitAxis(EAxis::Z); // GetUpVector
		OutPlaneOrientation = UE::Math::TVector<T>(PlaneOrientationUE);
		OutPlaneW = static_cast<T>(PositionUE.Dot(PlaneOrientationUE));
	}

	// Get the plane equation (normal and W) from the transform of a given instance in the ClippingPlanePopulation.
	template <typename T>
	bool GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW,
		TWeakObjectPtr<AITwinPopulation> const& PlanePopulation,
		int32 InInstanceIndex)
	{
		if (!ensure(PlanePopulation.IsValid()))
		{
			return false;
		}
		if (!ensure(InInstanceIndex >= 0
				 && InInstanceIndex < PlanePopulation->GetNumberOfInstances()))
		{
			return false;
		}
		const FTransform InstanceTransform = PlanePopulation->GetInstanceTransform(InInstanceIndex);
		GetPlaneEquationFromTransform<T>(OutPlaneOrientation, OutPlaneW, InstanceTransform);
		return true;
	}

	//-----------------------------------------------------------------------------------
	// Gizmo placement heuristics (pure functions, extracted for unit-testing).
	//-----------------------------------------------------------------------------------

	/// Whether a position projected at the given screen ratio is comfortably usable for the gizmo,
	/// ie. far enough from the viewport borders.
	/// \param ScreenRatio Projected position, expressed as a ratio of the viewport size.
	/// \param Margin Relative margin kept on each border.
	inline bool IsScreenRatioComfortable(FVector2d const& ScreenRatio, double Margin = 0.2)
	{
		return ScreenRatio.GetMin() >= Margin
			&& ScreenRatio.GetMax() <= (1. - Margin);
	}

	/// Distance of a projected position to the center of the screen, in ratio units.
	inline double GetDistanceToScreenCenter(FVector2d const& ScreenRatio)
	{
		return FVector2D(ScreenRatio - FVector2d(0.5, 0.5)).Size();
	}

	/// Whether the gizmo should be moved from its current position to a candidate one.
	/// We only accept the move if the candidate is *significantly* more centered, to avoid a gizmo
	/// jittering between two almost equivalent positions.
	/// \param ImprovementFactor The candidate must be at least this much closer to the center.
	inline bool ShouldReplaceGizmoPosition(FVector2d const& CurrentScreenRatio,
		FVector2d const& CandidateScreenRatio,
		double ImprovementFactor = 0.7)
	{
		return GetDistanceToScreenCenter(CandidateScreenRatio)
			< GetDistanceToScreenCenter(CurrentScreenRatio) * ImprovementFactor;
	}

	//-----------------------------------------------------------------------------------
	// Grid snapping (pure functions, extracted for unit-testing).
	//-----------------------------------------------------------------------------------

	/// Size of the grid used to snap a clipping plane center, depending on its influence bounds.
	inline double GetSnapGridSize(FBox const& InfluenceBoundingBox)
	{
		if (InfluenceBoundingBox.IsValid)
		{
			return FMath::Max(InfluenceBoundingBox.GetSize().GetMax() * 0.001, 1000.0);
		}
		// Infinite influence (Google tileset): use a fixed size of 20 meters.
		return 20 * 100.0;
	}

	inline FVector SnapVectorToGrid(FVector const& Vec, double SnapGridSize)
	{
		if (!ensure(SnapGridSize > 0))
		{
			return Vec;
		}
		return FVector(
			FMath::RoundToFloat(Vec.X / SnapGridSize) * SnapGridSize,
			FMath::RoundToFloat(Vec.Y / SnapGridSize) * SnapGridSize,
			FMath::RoundToFloat(Vec.Z / SnapGridSize) * SnapGridSize);
	}

	/// Snap a world position on a grid aligned on the minimum corner of the reference box (so that
	/// the snapped position never drifts outside of the box because of the grid origin).
	inline FVector SnapLocationToGrid(FVector const& WorldPosition,
		FBox const& RefBoundingBox,
		double SnapGridSize)
	{
		if (!ensure(SnapGridSize > 0))
		{
			return WorldPosition;
		}
		if (!RefBoundingBox.IsValid)
		{
			return SnapVectorToGrid(WorldPosition, SnapGridSize);
		}
		return RefBoundingBox.Min + SnapVectorToGrid(WorldPosition - RefBoundingBox.Min, SnapGridSize);
	}

	//-----------------------------------------------------------------------------------
	// Clipping plane proxy scaling (pure function, extracted for unit-testing).
	//-----------------------------------------------------------------------------------

	/// Compute the scale to apply to the clipping plane proxy mesh so that it covers the whole
	/// influence bounding box, whatever the orientation of the plane.
	/// \param BoxSize Size of the influence bounding box.
	/// \param PlaneOrientation Normal of the clipping plane (expected normalized).
	/// \param EulerAnglesDeg Euler angles of the plane instance rotation, in degrees.
	/// \param PlaneToUnreal Scale factor of the imported proxy geometry (1 meter).
	inline FVector ComputePlaneProxyScale(FVector const& BoxSize,
		FVector const& PlaneOrientation,
		FVector const& EulerAnglesDeg,
		double PlaneToUnreal)
	{
		double const AngleX = FMath::DegreesToRadians(EulerAnglesDeg.X);
		double const AngleY = FMath::DegreesToRadians(EulerAnglesDeg.Y);
		double const AngleZ = FMath::DegreesToRadians(EulerAnglesDeg.Z);

		FVector const AbsOrientation = PlaneOrientation.GetAbs();

		FVector NewScale(1., 1., 0.1);
		if (AbsOrientation.Z >= AbsOrientation.X && AbsOrientation.Z >= AbsOrientation.Y)
		{
			NewScale.X = (BoxSize.X * FMath::Abs(FMath::Cos(AngleZ)))
				+ (BoxSize.Y * FMath::Abs(FMath::Sin(AngleZ)));
			NewScale.Y = (BoxSize.Y * FMath::Abs(FMath::Cos(AngleZ)))
				+ (BoxSize.X * FMath::Abs(FMath::Sin(AngleZ)));
		}
		else if (AbsOrientation.Y >= AbsOrientation.X && AbsOrientation.Y >= AbsOrientation.Z)
		{
			NewScale.X = (BoxSize.X * FMath::Abs(FMath::Cos(AngleY)))
				+ (BoxSize.Z * FMath::Abs(FMath::Sin(AngleY)));
			NewScale.Y = (BoxSize.Z * FMath::Abs(FMath::Cos(AngleY)))
				+ (BoxSize.X * FMath::Abs(FMath::Sin(AngleY)));
		}
		else
		{
			NewScale.X = (BoxSize.Z * FMath::Abs(FMath::Cos(AngleX)))
				+ (BoxSize.Y * FMath::Abs(FMath::Sin(AngleX)));
			NewScale.Y = (BoxSize.Y * FMath::Abs(FMath::Cos(AngleX)))
				+ (BoxSize.Z * FMath::Abs(FMath::Sin(AngleX)));
		}
		NewScale.X *= PlaneToUnreal;
		NewScale.Y *= PlaneToUnreal;
		return NewScale;
	}
}
