/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineGeometry.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineGeometry.h>

namespace ITwinSpline
{
	double TangentLengthFromTightness(float InTightness)
	{
		return FMath::Max((1.f - FMath::Clamp(InTightness, 0.0f, 1.0f)) * MaxTangentLength,
			double(SMALL_NUMBER));
	}

	float TightnessFromTangentLength(double TangentLength)
	{
		double Length = FMath::Max(TangentLength, double(SMALL_NUMBER));
		Length /= MaxTangentLength;
		return 1.f - FMath::Clamp(float(Length), 0.f, 1.f);
	}

	void BuildSplinePolygon(TArrayView<const FVector> Points, FPoly& OutPolygon,
		FVector& OutBarycenter)
	{
		OutPolygon.Init();
		const int32 NumPoints = Points.Num();
		if (NumPoints > 2)
		{
			OutPolygon.Vertices.SetNum(NumPoints);
			for (int32 i(0); i < NumPoints; ++i)
			{
				OutPolygon.Vertices[i] = FVector3f(Points[i]);
			}
		}
		else if (NumPoints == 2)
		{
			// Create a thin rectangle to allow intersection tests on 2-point splines
			const FVector3f Point0(Points[0]);
			const FVector3f Point1(Points[1]);
			const FVector3f Direction = (Point1 - Point0).GetSafeNormal();
			const FVector3f Normal = FVector3f::CrossProduct(Direction, FVector3f::UpVector);
			OutPolygon.Vertices.SetNum(4);
			OutPolygon.Vertices[0] = Point0 + Normal * TwoPointPolygonHalfWidth;
			OutPolygon.Vertices[1] = Point0 - Normal * TwoPointPolygonHalfWidth;
			OutPolygon.Vertices[2] = Point1 - Normal * TwoPointPolygonHalfWidth;
			OutPolygon.Vertices[3] = Point1 + Normal * TwoPointPolygonHalfWidth;
		}

		OutBarycenter = (NumPoints >= 2) ? OutPolygon.GetMidPoint() : FVector::ZeroVector;

		OutPolygon.Fix();
		OutPolygon.CalcNormal(true);
	}

	//! Returns true if the line segment from Start to End intersects the polygon.
	//! This reproduces the logic of FPoly::DoesLineIntersect, which is not exported.
	bool DoesLineIntersectPolygon(FPoly const& Polygon, const FVector& Start, const FVector& End)
	{
		auto const& Vertices(Polygon.Vertices);
		auto const& Normal(Polygon.Normal);

		// Filter degenerated cases...
		if (Vertices.Num() < 3)
		{
			return false;
		}

		// If the ray doesn't cross the plane, don't bother going any further.
		const float DistStart = FVector::PointPlaneDist(Start, (FVector)Vertices[0], (FVector)Normal);
		const float DistEnd = FVector::PointPlaneDist(End, (FVector)Vertices[0], (FVector)Normal);

		if ((DistStart < 0 && DistEnd < 0) || (DistStart > 0 && DistEnd > 0))
		{
			return false;
		}

		// Get the intersection of the line and the plane.
		const FVector Intersection = FMath::LinePlaneIntersection(
			Start, End, (FVector)Vertices[0], (FVector)Normal);
		if (Intersection == Start || Intersection == End)
		{
			return false;
		}

		// Check if the intersection point is actually on the poly.
		for (int32 x = 0; x < Vertices.Num(); x++)
		{
			// Create plane perpendicular to both this side and the polygon's normal.
			const FVector3f Side =
				Vertices[x] - Vertices[(x - 1 < 0) ? Vertices.Num() - 1 : x - 1];
			FVector SidePlaneNormal = FVector(Side ^ Normal);
			SidePlaneNormal.Normalize();

			// If point is not behind all the planes created by this poly's edges,
			// it's outside the poly.
			if (FVector::PointPlaneDist(Intersection, (FVector)Vertices[x], SidePlaneNormal)
				> UE_THRESH_POINT_ON_PLANE)
			{
				return false;
			}
		}

		return true;
	}
}
