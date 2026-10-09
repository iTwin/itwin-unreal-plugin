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

#endif // WITH_TESTS
