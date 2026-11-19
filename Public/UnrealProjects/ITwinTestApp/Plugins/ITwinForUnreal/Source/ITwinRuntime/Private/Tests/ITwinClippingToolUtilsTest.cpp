/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolUtilsTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include "../Clipping/ITwinClippingToolUtils.inl"

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingPlaneEquationTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingPlaneEquation", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingPlaneEquationTest::RunTest(FString const& /*Parameters*/)
{
	using namespace ITwinClippingToolUtils;

	constexpr double Tolerance = 1e-6;

	// --- Identity transform: the plane is Z = 0, normal pointing up ---
	{
		FVector Normal;
		double W = 0.;
		GetPlaneEquationFromTransform<double>(Normal, W, FTransform::Identity);

		TestTrue(TEXT("Identity gives the up vector as normal"), Normal.Equals(FVector::UpVector, Tolerance));
		TestEqual(TEXT("Identity gives W = 0"), W, 0., Tolerance);
	}

	// --- Translation along the normal shifts W by that amount ---
	{
		FVector Normal;
		double W = 0.;
		FTransform const Transform(FQuat::Identity, FVector(10., -20., 250.));
		GetPlaneEquationFromTransform<double>(Normal, W, Transform);

		TestTrue(TEXT("Translation does not change the normal"), Normal.Equals(FVector::UpVector, Tolerance));
		TestEqual(TEXT("W is the elevation of the plane"), W, 250., Tolerance);
	}

	// --- Translation *within* the plane must leave W unchanged ---
	{
		FVector Normal;
		double W = 0.;
		FTransform const Transform(FQuat::Identity, FVector(1234., -5678., 0.));
		GetPlaneEquationFromTransform<double>(Normal, W, Transform);

		TestEqual(TEXT("Sliding the origin inside the plane keeps W = 0"), W, 0., Tolerance);
	}

	// --- Rotation: a 90 deg pitch turns the up vector into -X (UE left-handed, Z up) ---
	{
		FVector Normal;
		double W = 0.;
		FTransform const Transform(FRotator(90., 0., 0.), FVector(100., 0., 0.));
		GetPlaneEquationFromTransform<double>(Normal, W, Transform);

		TestTrue(TEXT("Rotated normal stays unit length"),
			FMath::IsNearlyEqual(Normal.Size(), 1., Tolerance));
		// Whatever the sign convention, the plane must contain its own origin:
		// dot(Origin, Normal) == W.
		TestEqual(TEXT("The plane passes through the transform origin"),
			Transform.GetLocation().Dot(Normal), W, Tolerance);
	}

	// --- Arbitrary rotation + translation: the defining invariant must always hold ---
	{
		FVector Normal;
		double W = 0.;
		FTransform const Transform(FRotator(37., -128., 64.), FVector(-4321., 987., 55.));
		GetPlaneEquationFromTransform<double>(Normal, W, Transform);

		TestTrue(TEXT("Normal is unit length"), FMath::IsNearlyEqual(Normal.Size(), 1., Tolerance));
		TestEqual(TEXT("dot(Origin, Normal) == W"),
			Transform.GetLocation().Dot(Normal), W, Tolerance);

		// A point obtained by moving along the plane's own X axis must satisfy the equation.
		FVector const InPlanePoint = Transform.GetLocation() + Transform.GetUnitAxis(EAxis::X) * 500.;
		TestEqual(TEXT("A point of the plane satisfies dot(P, N) == W"),
			InPlanePoint.Dot(Normal), W, 1e-4);

		// A point offset along the normal must be at the expected signed distance.
		FVector const OffsetPoint = Transform.GetLocation() + Normal * 42.;
		TestEqual(TEXT("Signed distance along the normal is preserved"),
			OffsetPoint.Dot(Normal) - W, 42., 1e-4);
	}

	// --- Non-uniform scale must NOT denormalize the normal (GetUnitAxis) ---
	{
		FVector Normal;
		double W = 0.;
		FTransform Transform(FQuat::Identity, FVector(0., 0., 30.), FVector(5., 5., 3.));
		GetPlaneEquationFromTransform<double>(Normal, W, Transform);

		TestTrue(TEXT("Scale does not denormalize the normal"),
			FMath::IsNearlyEqual(Normal.Size(), 1., Tolerance));
		TestEqual(TEXT("Scale does not change W"), W, 30., Tolerance);
	}

	// --- The float instantiation must compile and agree with the double one ---
	{
		FVector3f NormalF;
		float WF = 0.f;
		FTransform const Transform(FRotator(15., 45., -30.), FVector(100., 200., 300.));
		GetPlaneEquationFromTransform<float>(NormalF, WF, Transform);

		FVector NormalD;
		double WD = 0.;
		GetPlaneEquationFromTransform<double>(NormalD, WD, Transform);

		TestTrue(TEXT("float and double normals agree"),
			FVector(NormalF).Equals(NormalD, 1e-3));
		TestEqual(TEXT("float and double W agree"), double(WF), WD, 1e-2);
	}

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingGizmoHeuristicsTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingGizmoHeuristics", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingGizmoHeuristicsTest::RunTest(FString const& /*Parameters*/)
{
	using namespace ITwinClippingToolUtils;

	// --- IsScreenRatioComfortable: the [0.2, 0.8] band, borders included ---
	{
		TestTrue(TEXT("The exact center is comfortable"),
			IsScreenRatioComfortable(FVector2d(0.5, 0.5)));
		TestTrue(TEXT("The lower bound is inclusive"),
			IsScreenRatioComfortable(FVector2d(0.2, 0.2)));
		TestTrue(TEXT("The upper bound is inclusive"),
			IsScreenRatioComfortable(FVector2d(0.8, 0.8)));
		TestFalse(TEXT("Just outside the lower bound is rejected"),
			IsScreenRatioComfortable(FVector2d(0.199, 0.5)));
		TestFalse(TEXT("Just outside the upper bound is rejected"),
			IsScreenRatioComfortable(FVector2d(0.5, 0.801)));

		// A single bad axis is enough to reject the position.
		TestFalse(TEXT("X too far left rejects the position"),
			IsScreenRatioComfortable(FVector2d(0.05, 0.5)));
		TestFalse(TEXT("Y too far down rejects the position"),
			IsScreenRatioComfortable(FVector2d(0.5, 0.95)));

		// Off-screen ratios (the projection can return values outside [0,1]).
		TestFalse(TEXT("A negative ratio is never comfortable"),
			IsScreenRatioComfortable(FVector2d(-0.3, 0.5)));
		TestFalse(TEXT("A ratio above 1 is never comfortable"),
			IsScreenRatioComfortable(FVector2d(0.5, 1.4)));

		// A custom margin of 0 accepts everything inside the viewport.
		TestTrue(TEXT("A null margin accepts a border position"),
			IsScreenRatioComfortable(FVector2d(0., 1.), 0.));
	}

	// --- GetDistanceToScreenCenter ---
	{
		TestEqual(TEXT("The center is at distance 0"),
			GetDistanceToScreenCenter(FVector2d(0.5, 0.5)), 0., 1e-12);
		TestEqual(TEXT("A pure X offset gives that offset"),
			GetDistanceToScreenCenter(FVector2d(0.8, 0.5)), 0.3, 1e-12);
		TestEqual(TEXT("The distance is symmetric"),
			GetDistanceToScreenCenter(FVector2d(0.2, 0.5)),
			GetDistanceToScreenCenter(FVector2d(0.8, 0.5)), 1e-12);
	}

	// --- ShouldReplaceGizmoPosition: only a *significant* improvement moves the gizmo ---
	{
		FVector2d const Current(0.8, 0.5); // distance to center = 0.3

		// 0.3 * 0.7 = 0.21 is the acceptance threshold.
		TestFalse(TEXT("An identical position does not move the gizmo"),
			ShouldReplaceGizmoPosition(Current, Current));
		TestFalse(TEXT("A marginally better position does not move the gizmo"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.75, 0.5))); // 0.25 > 0.21

		// Acceptance threshold = 0.7 * distance of the current position to the center.
		// We do not assert the exact tie (it is not a meaningful contract, and both operands are
		// subject to floating-point rounding), but we check both sides of it.
		double const CurrentDistance = GetDistanceToScreenCenter(Current);
		double const Threshold = CurrentDistance * 0.7;
		constexpr double Epsilon = 1e-6;

		TestFalse(TEXT("Just above the threshold does not move the gizmo"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.5 + Threshold + Epsilon, 0.5)));
		TestTrue(TEXT("Just below the threshold moves the gizmo"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.5 + Threshold - Epsilon, 0.5)));

		TestTrue(TEXT("A clearly more centered position moves the gizmo"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.6, 0.5))); // 0.1 < 0.21
		TestFalse(TEXT("A worse position never moves the gizmo"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.95, 0.5)));

		// Degenerate case: the current position is already perfectly centered, nothing can beat it.
		TestFalse(TEXT("Nothing beats a perfectly centered gizmo"),
			ShouldReplaceGizmoPosition(FVector2d(0.5, 0.5), FVector2d(0.5, 0.5)));

		// The factor is configurable and monotonic: a laxer factor accepts more candidates.
		TestTrue(TEXT("A laxer improvement factor accepts a marginal gain"),
			ShouldReplaceGizmoPosition(Current, FVector2d(0.75, 0.5), 1.0));
	}
	return true;
}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingSnapGridTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingSnapGrid", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingSnapGridTest::RunTest(FString const& /*Parameters*/)
{
	using namespace ITwinClippingToolUtils;

	// --- GetSnapGridSize ---
	{
		TestEqual(TEXT("An invalid (infinite) influence gives a 20m grid"),
			GetSnapGridSize(FBox(ForceInit)), 2000., 1e-9);

		// Small box: the 1000 floor applies (1/1000th of 50000 = 50 < 1000).
		FBox const SmallBox(FVector(0.), FVector(50000., 10000., 10000.));
		TestEqual(TEXT("A small influence box falls back on the 1000 floor"),
			GetSnapGridSize(SmallBox), 1000., 1e-9);

		// Large box: 1/1000th of the largest dimension.
		FBox const LargeBox(FVector(0.), FVector(5.e6, 1.e6, 1.e6));
		TestEqual(TEXT("A large influence box gives 1/1000th of its largest side"),
			GetSnapGridSize(LargeBox), 5000., 1e-9);

		// The grid only depends on the *size*, not on the position of the box.
		FBox const TranslatedBox = LargeBox.ShiftBy(FVector(1.e7, -3.e6, 42.));
		TestEqual(TEXT("The grid size is translation invariant"),
			GetSnapGridSize(TranslatedBox), GetSnapGridSize(LargeBox), 1e-9);
	}

	// --- SnapVectorToGrid ---
	{
		TestTrue(TEXT("A vector already on the grid is unchanged"),
			SnapVectorToGrid(FVector(2000., -4000., 0.), 1000.)
				.Equals(FVector(2000., -4000., 0.), 1e-6));
		TestTrue(TEXT("Rounding is done to the nearest node, on each axis"),
			SnapVectorToGrid(FVector(1400., 1600., -1400.), 1000.)
				.Equals(FVector(1000., 2000., -1000.), 1e-6));
		TestTrue(TEXT("The snapped vector never moves by more than half a grid step"),
			(SnapVectorToGrid(FVector(12345., -6789., 101.), 1000.) - FVector(12345., -6789., 101.))
				.GetAbsMax() <= 500. + 1e-6);
	}

	// --- SnapLocationToGrid ---
	{
		FBox const Box(FVector(123., 456., 789.), FVector(123. + 1.e5, 456. + 1.e5, 789. + 1.e5));
		constexpr double GridSize = 1000.;

		TestTrue(TEXT("The box minimum is a node of the grid"),
			SnapLocationToGrid(Box.Min, Box, GridSize).Equals(Box.Min, 1e-6));

		// The grid is anchored on Box.Min, not on the world origin.
		FVector const Pos = Box.Min + FVector(1400., 1600., 400.);
		TestTrue(TEXT("Snapping is relative to the box minimum"),
			SnapLocationToGrid(Pos, Box, GridSize)
				.Equals(Box.Min + FVector(1000., 2000., 0.), 1e-6));

		TestTrue(TEXT("Snapping is idempotent"),
			SnapLocationToGrid(SnapLocationToGrid(Pos, Box, GridSize), Box, GridSize)
				.Equals(SnapLocationToGrid(Pos, Box, GridSize), 1e-6));

		// Without a valid reference box, the grid is anchored on the world origin.
		FBox const InvalidBox(ForceInit);
		TestTrue(TEXT("Without a reference box, the grid is world-aligned"),
			SnapLocationToGrid(FVector(1400., 1600., -1400.), InvalidBox, GridSize)
				.Equals(SnapVectorToGrid(FVector(1400., 1600., -1400.), GridSize), 1e-6));
	}
	return true;
}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingPlaneProxyScaleTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingPlaneProxyScale", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingPlaneProxyScaleTest::RunTest(FString const& /*Parameters*/)
{
	using namespace ITwinClippingToolUtils;

	constexpr double PlaneToUnreal = 0.01; // the imported proxy geometry is exactly 1 meter
	constexpr double Tolerance = 1e-6;
	FVector const BoxSize(30000., 10000., 5000.);

	// --- Horizontal plane (normal = Z), no rotation: the proxy covers X and Y ---
	{
		FVector const Scale = ComputePlaneProxyScale(
			BoxSize, FVector::ZAxisVector, FVector::ZeroVector, PlaneToUnreal);
		TestEqual(TEXT("Scale X covers the box X size"), Scale.X, BoxSize.X * PlaneToUnreal, Tolerance);
		TestEqual(TEXT("Scale Y covers the box Y size"), Scale.Y, BoxSize.Y * PlaneToUnreal, Tolerance);
		TestEqual(TEXT("Scale Z is the fixed proxy thickness"), Scale.Z, 0.1, Tolerance);
	}

	// --- Same plane rotated by 90 deg around Z: X and Y extents are swapped ---
	{
		FVector const Scale = ComputePlaneProxyScale(
			BoxSize, FVector::ZAxisVector, FVector(0., 0., 90.), PlaneToUnreal);
		TestEqual(TEXT("A 90 deg yaw swaps X"), Scale.X, BoxSize.Y * PlaneToUnreal, Tolerance);
		TestEqual(TEXT("A 90 deg yaw swaps Y"), Scale.Y, BoxSize.X * PlaneToUnreal, Tolerance);
	}

	// --- 45 deg yaw: both axes cover the diagonal extent ---
	{
		FVector const Scale = ComputePlaneProxyScale(
			BoxSize, FVector::ZAxisVector, FVector(0., 0., 45.), PlaneToUnreal);
		double const Expected = (BoxSize.X + BoxSize.Y) * (FMath::Sqrt(2.) / 2.) * PlaneToUnreal;
		TestEqual(TEXT("A 45 deg yaw covers the diagonal on X"), Scale.X, Expected, 1e-4);
		TestEqual(TEXT("A 45 deg yaw covers the diagonal on Y"), Scale.Y, Expected, 1e-4);
		// A 45 deg yaw mixes both extents: the coverage is the same on X and Y, and it is always
		// at least the largest of the two half-extents (so the proxy still covers the box).
		TestTrue(TEXT("The diagonal coverage is larger than the smallest axis coverage"),
			Scale.X > BoxSize.Y * PlaneToUnreal);
		TestTrue(TEXT("The diagonal coverage is at least half the sum of both extents"),
			Scale.X >= 0.5 * (BoxSize.X + BoxSize.Y) * PlaneToUnreal - 1e-6);
	}

	// --- 180 deg yaw is equivalent to no rotation (only absolute values matter) ---
	{
		FVector const Scale0 = ComputePlaneProxyScale(
			BoxSize, FVector::ZAxisVector, FVector::ZeroVector, PlaneToUnreal);
		FVector const Scale180 = ComputePlaneProxyScale(
			BoxSize, FVector::ZAxisVector, FVector(0., 0., 180.), PlaneToUnreal);
		TestTrue(TEXT("A 180 deg yaw gives the same scale"), Scale180.Equals(Scale0, 1e-4));

		// Likewise, flipping the normal must not change the proxy scale.
		FVector const ScaleFlipped = ComputePlaneProxyScale(
			BoxSize, -FVector::ZAxisVector, FVector::ZeroVector, PlaneToUnreal);
		TestTrue(TEXT("Flipping the normal does not change the scale"), ScaleFlipped.Equals(Scale0, 1e-4));
	}

	// --- Vertical plane with a normal mostly along Y: the proxy covers X and Z ---
	{
		FVector const Scale = ComputePlaneProxyScale(
			BoxSize, FVector::YAxisVector, FVector::ZeroVector, PlaneToUnreal);
		TestEqual(TEXT("Y-facing plane covers the box X size"), Scale.X, BoxSize.X * PlaneToUnreal, Tolerance);
		TestEqual(TEXT("Y-facing plane covers the box Z size"), Scale.Y, BoxSize.Z * PlaneToUnreal, Tolerance);
	}

	// --- Vertical plane with a normal mostly along X: the proxy covers Z and Y ---
	{
		FVector const Scale = ComputePlaneProxyScale(
			BoxSize, FVector::XAxisVector, FVector::ZeroVector, PlaneToUnreal);
		TestEqual(TEXT("X-facing plane covers the box Z size"), Scale.X, BoxSize.Z * PlaneToUnreal, Tolerance);
		TestEqual(TEXT("X-facing plane covers the box Y size"), Scale.Y, BoxSize.Y * PlaneToUnreal, Tolerance);
	}

	// --- Branch selection is driven by the dominant component of the normal ---
	{
		FVector Normal(0.5, 0.5, 0.71); // Z slightly dominant
		Normal.Normalize();
		FVector const ScaleZ = ComputePlaneProxyScale(BoxSize, Normal, FVector::ZeroVector, PlaneToUnreal);
		TestEqual(TEXT("A Z-dominant normal uses the Z branch"), ScaleZ.X, BoxSize.X * PlaneToUnreal, 1e-4);

		FVector Normal2(0.5, 0.71, 0.5); // Y slightly dominant
		Normal2.Normalize();
		FVector const ScaleY = ComputePlaneProxyScale(BoxSize, Normal2, FVector::ZeroVector, PlaneToUnreal);
		TestEqual(TEXT("A Y-dominant normal uses the Y branch"), ScaleY.Y, BoxSize.Z * PlaneToUnreal, 1e-4);
	}

	// --- The proxy must always be big enough to cover the box, whatever the orientation ---
	{
		double const MinCoverage = FMath::Min3(BoxSize.X, BoxSize.Y, BoxSize.Z) * PlaneToUnreal;
		for (double Yaw = 0.; Yaw < 360.; Yaw += 15.)
		{
			FVector const Scale = ComputePlaneProxyScale(
				BoxSize, FVector::ZAxisVector, FVector(0., 0., Yaw), PlaneToUnreal);
			TestTrue(TEXT("The proxy is never degenerate"),
				Scale.X >= MinCoverage - 1e-6 && Scale.Y >= MinCoverage - 1e-6);
			TestEqual(TEXT("The proxy thickness is constant"), Scale.Z, 0.1, Tolerance);
		}
	}
	return true;
}

#endif // WITH_TESTS
