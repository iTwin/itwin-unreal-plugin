/*--------------------------------------------------------------------------------------+
|
|     $Source: UnrealGCSTransformTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>

#include <Decoration/UnrealDecorationGCSTransform.h>
#include <Math/UEMathConversion.h>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/SceneAPICameraUtils.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCSTransformRoundTripTest,
	"ITwinRuntime.Decoration.GCSTransform.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGCSTransformRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	FUnrealDecorationGCSTransform Gcs;
	Gcs.Init({ 47.1234, 2.5678, 150.0 }, /*bUseFixedMatrixComposition*/ true); // lat, lon, height (deg/m)

	// A few non-trivial, non-axis-aligned placements, including a non-uniform scale.
	const TArray<FTransform> Cases = {
		FTransform(FRotator(0., 0., 0.), FVector(1000., 2000., 300.)),
		FTransform(FRotator(30.5, 45., 12.), FVector(-12345., 678., 9010.)),
		FTransform(FRotator(-70., 170., -95.), FVector(0., 0., 5000.), FVector(2., 2., 2.)),
	};

	for (const FTransform& T : Cases)
	{
		const FMatrix Src = T.ToMatrixWithScale();
		const FMatrix Out = toUnreal(Gcs.MatrixToClient(Gcs.MatrixFromClient(toAdvizSdk(Src))));
		TestTrue(TEXT("MatrixToClient(MatrixFromClient(M)) == M"), Out.Equals(Src, 1e-6));

		// Positions must agree with the matrix path (this is what the fix restored).
		const AdvViz::SDK::double3 PosViaMatrix{ Out.M[3][0], Out.M[3][1], Out.M[3][2] };
		const FMatrix Ecef = toUnreal(Gcs.MatrixFromClient(toAdvizSdk(Src)));
		const AdvViz::SDK::double3 PosDirect =
			Gcs.PositionToClient({ Ecef.M[3][0], Ecef.M[3][1], Ecef.M[3][2] });
		TestTrue(TEXT("matrix and position paths agree"),
			FMath::IsNearlyEqual(PosViaMatrix[0], PosDirect[0], 1e-6)
			&& FMath::IsNearlyEqual(PosViaMatrix[1], PosDirect[1], 1e-6)
			&& FMath::IsNearlyEqual(PosViaMatrix[2], PosDirect[2], 1e-6));
	}

	// The client -> ECEF map is a reflection: UE +X -> East, +Y -> -North, +Z -> Up.
	// Checked on the basis rows of the transformed identity.
	const FMatrix EcefId = toUnreal(Gcs.MatrixFromClient(toAdvizSdk(FMatrix::Identity)));
	TestTrue(TEXT("client->ECEF basis is left-handed (det < 0)"),
		EcefId.RemoveTranslation().Determinant() < 0.);

	// ENU2ECEF and ECEF2ENU must be mutual inverses, otherwise the above only
	// round-trips by construction and the absolute orientation is still off.
	TestTrue(TEXT("ENU2ECEF * ECEF2ENU == Identity"),
		(Gcs.ENU2ECEF * Gcs.ECEF2ENU).Equals(FMatrix::Identity, 1e-9));

	// Legacy path must keep its own (different) convention, and still round-trip.
	FUnrealDecorationGCSTransform Legacy;
	Legacy.Init({ 47.1234, 2.5678, 150.0 }, /*bUseFixedMatrixComposition*/ false);
	const FMatrix Src = Cases[1].ToMatrixWithScale();
	TestTrue(TEXT("legacy round-trip preserved"),
		toUnreal(Legacy.MatrixToClient(Legacy.MatrixFromClient(toAdvizSdk(Src)))).Equals(Src, 1e-6));
	TestFalse(TEXT("legacy and fixed conventions differ"),
		toUnreal(Legacy.MatrixFromClient(toAdvizSdk(Src)))
			.Equals(toUnreal(Gcs.MatrixFromClient(toAdvizSdk(Src))), 1e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGCSCameraVectorsTest,
	"ITwinRuntime.Decoration.GCSTransform.CameraVectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGCSCameraVectorsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace AdvViz::SDK;
	using namespace AdvViz::SDK::SceneAPIDetails;

	auto GcsPtr = std::make_shared<FUnrealDecorationGCSTransform>();
	GcsPtr->Init({ 47.1234, 2.5678, 150.0 }, /*bUseFixedMatrixComposition*/ true);

	auto const PreviousTransform = FITwinMathConversion::transform_;
	FITwinMathConversion::transform_ = GcsPtr;
	ON_SCOPE_EXIT{ FITwinMathConversion::transform_ = PreviousTransform; };

	const double Handedness = GcsPtr->GetHandedness();
	TestEqual(TEXT("the fixed composition is a reflection"), Handedness, -1.);

	// Several camera poses, as captured from the viewport.
	const TArray<FTransform> Views = {
		FTransform(FRotator(0., 0., 0.),      FVector(0., 0., 17000.)),
		FTransform(FRotator(30.52, 45., 0.),  FVector(-12345., 678., 9010.)),
		FTransform(FRotator(-12., -170., 0.), FVector(5000., -5000., 25000.)),
	};

	for (const FTransform& View : Views)
	{
		// Unreal -> SDK, applying the GCS: this is what SetHomeCamera / AddOrUpdateKeyFrame do.
		const dmat3x4 Placement = FITwinMathConversion::UEtoSDK(View, /*applyGCS*/ true);

		// SDK -> View3d vectors -> SDK, as GenerateBody / HandleLoadLinksResponse do.
		SJsonCamera Cam;
		Cam.ecefTransform = std::array<double, 16>{};
		FMemory::Memcpy(Cam.ecefTransform->data(), Placement.data(), 12 * sizeof(double));
		FillCameraVectorsFromTransform(Cam, Handedness);

		dmat3x4 Rebuilt{};
		TestTrue(TEXT("vectors rebuild a valid placement"),
			TransformFromCameraVectors(Cam.direction, Cam.up, Cam.position, Handedness, Rebuilt));
		for (size_t i = 0; i < 12; ++i)
		{
			TestTrue(TEXT("placement survives the vectors round-trip"),
				FMath::IsNearlyEqual(Rebuilt[i], Placement[i], 1e-9));
		}

		// And back to Unreal: the restored view must match the captured one.
		const FTransform Restored = FITwinMathConversion::SDKtoUE(Rebuilt, /*applyGCS*/ true);
		TestTrue(TEXT("restored view matches the captured one"),
			Restored.Equals(View, 1e-4));

		// Geodetic sanity: 'up' must be free of roll, i.e. dot(up, N) == cos(pitch),
		// where N is the geodetic normal. This is the diagnostic that found the bug.
		double3 N{ Cam.position.x, Cam.position.y, Cam.position.z };
		TestTrue(TEXT("position is a plausible ECEF point"),
			FMath::IsNearlyEqual(std::sqrt(Dot3(N, N)), 6.37e6, 5e4));
		Normalize3(N);
		const double3 Dir{ Cam.direction.x, Cam.direction.y, Cam.direction.z };
		const double3 Up{ Cam.up.x, Cam.up.y, Cam.up.z };
		const double SinPitch = -Dot3(Dir, N);
		const double CosPitch = std::sqrt(FMath::Max(0., 1. - SinPitch * SinPitch));
		TestTrue(TEXT("camera has no roll (dot(up, N) == cos(pitch))"),
			FMath::IsNearlyEqual(Dot3(Up, N), CosPitch, 2e-3)); // geocentric/geodetic approx.
	}

	// The legacy composition is orientation-preserving, and the helpers must follow it.
	FUnrealDecorationGCSTransform Legacy;
	Legacy.Init({ 47.1234, 2.5678, 150.0 }, /*bUseFixedMatrixComposition*/ false);
	TestEqual(TEXT("the legacy composition preserves orientation"), Legacy.GetHandedness(), 1.);

	return true;
}

#endif // WITH_TESTS
