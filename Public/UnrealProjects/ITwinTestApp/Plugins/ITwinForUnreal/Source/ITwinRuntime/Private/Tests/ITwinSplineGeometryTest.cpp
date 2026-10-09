/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineGeometryTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Spline/ITwinSplineGeometry.h>
#include <Spline/ITwinSplineEnums.h>

#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSplineGeometryTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SplineGeometry", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	//! Axis-aligned square in the Z=0 plane, side 100, first corner at the origin.
	TArray<FVector> MakeSquare()
	{
		return { FVector(0., 0., 0.), FVector(100., 0., 0.),
				 FVector(100., 100., 0.), FVector(0., 100., 0.) };
	}
}

bool FITwinSplineGeometryTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ITwinSpline;

	SECTION("GetPrevIndex / GetNextIndex - open spline clamps at the ends")
	{
		TestEqual(TEXT("Prev in the middle"), GetPrevIndex(2, 5, false), 2 - 1);
		TestEqual(TEXT("Next in the middle"), GetNextIndex(2, 5, false), 2 + 1);
		// On an open spline the first/last indices are their own neighbours.
		TestEqual(TEXT("Prev of first"), GetPrevIndex(0, 5, false), 0);
		TestEqual(TEXT("Next of last"), GetNextIndex(4, 5, false), 4);
	}

	SECTION("GetPrevIndex / GetNextIndex - closed loop wraps around")
	{
		TestEqual(TEXT("Prev of first wraps to last"), GetPrevIndex(0, 5, true), 4);
		TestEqual(TEXT("Next of last wraps to first"), GetNextIndex(4, 5, true), 0);
		// Single-point loop degenerates onto itself rather than going out of range.
		TestEqual(TEXT("Prev on 1-point loop"), GetPrevIndex(0, 1, true), 0);
		TestEqual(TEXT("Next on 1-point loop"), GetNextIndex(0, 1, true), 0);
	}

	SECTION("MinNumberOfPointsForValidSpline / CanDeletePoint")
	{
		TestEqual(TEXT("Open spline needs 2 points"), MinNumberOfPointsForValidSpline(false), 2);
		TestEqual(TEXT("Closed spline needs 3 points"), MinNumberOfPointsForValidSpline(true), 3);

		// At the minimum, deleting would degenerate the spline.
		TestFalse(TEXT("Open spline at minimum"), CanDeletePoint(2, false));
		TestTrue(TEXT("Open spline above minimum"), CanDeletePoint(3, false));
		TestFalse(TEXT("Closed spline at minimum"), CanDeletePoint(3, true));
		TestTrue(TEXT("Closed spline above minimum"), CanDeletePoint(4, true));
		// Guard against under-populated splines during interactive creation.
		TestFalse(TEXT("Empty spline"), CanDeletePoint(0, false));
	}

	SECTION("Tightness <-> tangent length are inverses")
	{
		// 0 => flat/smooth (maximum tangent influence), 1 => sharp turn (no tangent).
		TestEqual(TEXT("Tightness 0 gives max length"),
			TangentLengthFromTightness(0.f), MaxTangentLength, UE_DOUBLE_KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Tightness 0.5 gives half length"),
			TangentLengthFromTightness(0.5f), MaxTangentLength * 0.5, UE_DOUBLE_KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Tightness 1 gives a strictly positive length"),
			TangentLengthFromTightness(1.f) > 0.);

		// Round-trip through both directions.
		for (const float Tightness : { 0.f, 0.25f, 0.5f, 0.75f })
		{
			TestEqual(TEXT("Round-trip tightness"),
				TightnessFromTangentLength(TangentLengthFromTightness(Tightness)),
				Tightness, UE_KINDA_SMALL_NUMBER);
		}
	}

	SECTION("Tightness clamps out-of-range inputs")
	{
		TestEqual(TEXT("Negative tightness clamps to 0"),
			TangentLengthFromTightness(-1.f), MaxTangentLength, UE_DOUBLE_KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Tightness above 1 clamps"),
			TangentLengthFromTightness(2.f), TangentLengthFromTightness(1.f), UE_DOUBLE_KINDA_SMALL_NUMBER);

		TestEqual(TEXT("Over-long tangent clamps to tightness 0"),
			TightnessFromTangentLength(MaxTangentLength * 2.), 0.f, UE_KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Zero-length tangent gives tightness 1"),
			TightnessFromTangentLength(0.), 1.f, UE_KINDA_SMALL_NUMBER);
		TestEqual(TEXT("Negative length is treated as zero"),
			TightnessFromTangentLength(-100.), 1.f, UE_KINDA_SMALL_NUMBER);
	}

	SECTION("BuildSplinePolygon - degenerate splines produce no polygon")
	{
		FPoly Polygon;
		FVector Barycenter(1., 2., 3.); // non-zero, to check it really is reset

		BuildSplinePolygon(TArrayView<const FVector>(), Polygon, Barycenter);
		TestEqual(TEXT("No vertices for an empty spline"), Polygon.Vertices.Num(), 0);
		TestEqual(TEXT("Barycenter reset for an empty spline"), Barycenter, FVector::ZeroVector);

		const TArray<FVector> SinglePoint{ FVector(10., 20., 30.) };
		BuildSplinePolygon(SinglePoint, Polygon, Barycenter);
		TestEqual(TEXT("No vertices for a 1-point spline"), Polygon.Vertices.Num(), 0);
		TestEqual(TEXT("Barycenter reset for a 1-point spline"), Barycenter, FVector::ZeroVector);
	}

	SECTION("BuildSplinePolygon - 2 points produce a thin pickable rectangle")
	{
		const TArray<FVector> TwoPoints{ FVector(0., 0., 0.), FVector(100., 0., 0.) };
		FPoly Polygon;
		FVector Barycenter;
		BuildSplinePolygon(TwoPoints, Polygon, Barycenter);

		// A 2-point spline has no area, so it is widened to stay selectable.
		TestEqual(TEXT("Rectangle has 4 vertices"), Polygon.Vertices.Num(), 4);
		TestEqual(TEXT("Barycenter is the segment mid-point"),
			Barycenter, FVector(50., 0., 0.));
	}

	SECTION("BuildSplinePolygon - 3+ points use the points directly")
	{
		const TArray<FVector> Square = MakeSquare();
		FPoly Polygon;
		FVector Barycenter;
		BuildSplinePolygon(Square, Polygon, Barycenter);

		TestEqual(TEXT("Square has 4 vertices"), Polygon.Vertices.Num(), 4);
		TestEqual(TEXT("Barycenter is the centre of the square"),
			Barycenter, FVector(50., 50., 0.));
		// CalcNormal must have produced a usable normal for the plane tests below.
		TestTrue(TEXT("Normal is normalized"),
			FMath::IsNearlyEqual(Polygon.Normal.Size(), 1.f, UE_KINDA_SMALL_NUMBER));
	}

	SECTION("DoesLineIntersectPolygon - segment crossing the interior")
	{
		FPoly Polygon;
		FVector Barycenter;
		BuildSplinePolygon(MakeSquare(), Polygon, Barycenter);

		TestTrue(TEXT("Vertical segment through the centre"),
			DoesLineIntersectPolygon(Polygon, FVector(50., 50., 100.), FVector(50., 50., -100.)));
	}

	SECTION("DoesLineIntersectPolygon - misses")
	{
		FPoly Polygon;
		FVector Barycenter;
		BuildSplinePolygon(MakeSquare(), Polygon, Barycenter);

		// Crosses the polygon's plane, but far outside its bounds.
		TestFalse(TEXT("Crosses the plane outside the polygon"),
			DoesLineIntersectPolygon(Polygon, FVector(500., 500., 100.), FVector(500., 500., -100.)));

		// Entirely on one side of the plane: must bail out early.
		TestFalse(TEXT("Segment entirely above the plane"),
			DoesLineIntersectPolygon(Polygon, FVector(50., 50., 100.), FVector(50., 50., 50.)));
		TestFalse(TEXT("Segment entirely below the plane"),
			DoesLineIntersectPolygon(Polygon, FVector(50., 50., -50.), FVector(50., 50., -100.)));
	}

	SECTION("DoesLineIntersectPolygon - degenerate polygon never intersects")
	{
		FPoly Empty;
		Empty.Init();
		TestFalse(TEXT("Polygon with no vertices"),
			DoesLineIntersectPolygon(Empty, FVector(0., 0., 100.), FVector(0., 0., -100.)));
	}

	SECTION("ShouldAdvanceIndexAfterDuplication - direction of travel picks the point")
	{
		// Spline running along +X, the dragged point sits at the origin.
		const FVector Prev(-100., 0., 0.), Curr(0., 0., 0.), Next(100., 0., 0.);

		TestTrue(TEXT("Dragging forward moves the new point"),
			ShouldAdvanceIndexAfterDuplication(Prev, Curr, Next, FVector(50., 0., 0.)));
		TestFalse(TEXT("Dragging backward keeps the original point"),
			ShouldAdvanceIndexAfterDuplication(Prev, Curr, Next, FVector(-50., 0., 0.)));

		// Perpendicular / no movement are ties: the original index must win.
		TestFalse(TEXT("Perpendicular drag keeps the original point"),
			ShouldAdvanceIndexAfterDuplication(Prev, Curr, Next, FVector(0., 50., 0.)));
		TestFalse(TEXT("No movement keeps the original point"),
			ShouldAdvanceIndexAfterDuplication(Prev, Curr, Next, Curr));

		// A reversed spline must reverse the decision.
		TestFalse(TEXT("Reversed spline, dragging +X"),
			ShouldAdvanceIndexAfterDuplication(Next, Curr, Prev, FVector(50., 0., 0.)));
	}

	SECTION("IsValidInsertionIndex")
	{
		TestTrue(TEXT("First slot"),            IsValidInsertionIndex(0, 4));
		TestTrue(TEXT("Middle slot"),           IsValidInsertionIndex(2, 4));
		TestTrue(TEXT("Append after the last"), IsValidInsertionIndex(4, 4));

		TestFalse(TEXT("Past the append slot"), IsValidInsertionIndex(5, 4));
		TestFalse(TEXT("Negative index"),       IsValidInsertionIndex(-1, 4));
		TestFalse(TEXT("Empty spline"),         IsValidInsertionIndex(0, 0));
	}

	SECTION("ResolveDuplicationSourceIndex clamps to the last point")
	{
		TestEqual(TEXT("Interior index is unchanged"), ResolveDuplicationSourceIndex(2, 4), 2);
		TestEqual(TEXT("Last index is unchanged"),     ResolveDuplicationSourceIndex(3, 4), 3);
		// Appending duplicates the last point rather than reading out of range.
		TestEqual(TEXT("Append clamps to last"),       ResolveDuplicationSourceIndex(4, 4), 3);
	}
	return true;
}

#endif // WITH_TESTS
