/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineSnapshotCurveTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Spline/ITwinUESplineCurve.h>

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Components/SplineComponent.h>
#include <Misc/AutomationTest.h>
#include <UObject/Package.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSplineSnapshotCurveTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SplineSnapshotCurve", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FVector ToFVector(glm::dvec3 const& v) { return FVector(v.x, v.y, v.z); }

	struct FSplineCase
	{
		FString Name;
		TArray<FVector> Points;
		bool bClosedLoop = false;
		float Duration = 1.f;
		FTransform Transform = FTransform::Identity;
	};

	USplineComponent* MakeSpline(FSplineCase const& Case)
	{
		USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Spline->ClearSplinePoints(false);
		for (FVector const& P : Case.Points)
			Spline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
		Spline->SetClosedLoop(Case.bClosedLoop, false);
		Spline->Duration = Case.Duration;
		// Not registered in a world and unparented: relative == world transform, and SetRelativeTransform
		// updates ComponentToWorld directly (no MoveComponent/physics path involved).
		Spline->SetRelativeTransform(Case.Transform);
		Spline->UpdateSpline();
		return Spline;
	}
}

bool FITwinSplineSnapshotCurveTest::RunTest(FString const& /*Parameters*/)
{
	// Tolerance in centimeters / unitless for tangents (live evaluation goes through floats in places).
	constexpr double PosTolerance = 1e-2;
	constexpr double TgtTolerance = 1e-2;

	const FTransform ArbitraryTsf(FRotator(12., -73., 41.), FVector(12345., -6789., 321.), FVector(2., 2., 2.));

	TArray<FSplineCase> Cases;
	Cases.Add({ TEXT("open, identity, duration 1"),
		{ FVector(0, 0, 0), FVector(1000, 0, 0), FVector(1000, 1000, 200), FVector(0, 1000, -100) },
		false, 1.f, FTransform::Identity });
	Cases.Add({ TEXT("closed, identity, duration 1"),
		{ FVector(0, 0, 0), FVector(1000, 0, 0), FVector(1000, 1000, 200), FVector(0, 1000, -100) },
		true, 1.f, FTransform::Identity });
	Cases.Add({ TEXT("open, transformed, duration 7.5"),
		{ FVector(-500, 20, 0), FVector(300, 400, 50), FVector(900, -200, 10), FVector(1500, 100, 0), FVector(2000, 800, 30) },
		false, 7.5f, ArbitraryTsf });
	Cases.Add({ TEXT("closed, transformed, duration 0.25"),
		{ FVector(-500, 20, 0), FVector(300, 400, 50), FVector(900, -200, 10), FVector(1500, 100, 0), FVector(2000, 800, 30) },
		true, 0.25f, ArbitraryTsf });
	Cases.Add({ TEXT("two points, open"),
		{ FVector(0, 0, 0), FVector(500, 500, 500) },
		false, 3.f, ArbitraryTsf });

	// Include the exact endpoints, interior values, and slightly out-of-range values (which the snapshot
	// clamps; the live spline clamps as well through its own time/key handling).
	const double Samples[] = { 0.0, 1e-6, 0.1, 0.25, 1. / 3., 0.5, 0.6180339887, 0.75, 0.9, 0.999999, 1.0 };

	for (FSplineCase const& Case : Cases)
	{
		USplineComponent* Spline = MakeSpline(Case);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] spline created"), *Case.Name), Spline))
			continue;

		FITwinUESplineCurve const Live(*Spline);
		FITwinSplineSnapshotCurve const Snapshot(*Spline);

		TestEqual(*FString::Printf(TEXT("[%s] IsCyclic"), *Case.Name), Snapshot.IsCyclic(), Live.IsCyclic());
		TestEqual(*FString::Printf(TEXT("[%s] PointCount"), *Case.Name), Snapshot.PointCount(false), Live.PointCount(false));

		for (size_t i = 0; i < Live.PointCount(false); ++i)
		{
			TestTrue(*FString::Printf(TEXT("[%s] control point %d"), *Case.Name, (int)i),
				ToFVector(Snapshot.GetPositionAtIndex(i)).Equals(ToFVector(Live.GetPositionAtIndex(i)), PosTolerance));
		}

		for (double u : Samples)
		{
			const FVector LivePos = ToFVector(Live.GetPositionAtCoord(u));
			const FVector SnapPos = ToFVector(Snapshot.GetPositionAtCoord(u));
			TestTrue(*FString::Printf(TEXT("[%s] position at u=%f: live=%s snapshot=%s"),
					*Case.Name, u, *LivePos.ToString(), *SnapPos.ToString()),
				SnapPos.Equals(LivePos, PosTolerance));

			const FVector LiveTgt = ToFVector(Live.GetTangentAtCoord(u));
			const FVector SnapTgt = ToFVector(Snapshot.GetTangentAtCoord(u));
			// Compare directions (normalized) as well as raw vectors, to give a clearer diagnostic.
			TestTrue(*FString::Printf(TEXT("[%s] tangent at u=%f: live=%s snapshot=%s"),
					*Case.Name, u, *LiveTgt.ToString(), *SnapTgt.ToString()),
				SnapTgt.Equals(LiveTgt, TgtTolerance * FMath::Max(1., LiveTgt.Size())));
		}

		// Endpoints must match the first control point (and last one for an open spline).
		TestTrue(*FString::Printf(TEXT("[%s] u=0 is the first control point"), *Case.Name),
			ToFVector(Snapshot.GetPositionAtCoord(0.)).Equals(ToFVector(Snapshot.GetPositionAtIndex(0)), PosTolerance));
		const size_t LastIdx = Case.bClosedLoop ? 0 : Snapshot.PointCount(false) - 1;
		TestTrue(*FString::Printf(TEXT("[%s] u=1 is the last control point"), *Case.Name),
			ToFVector(Snapshot.GetPositionAtCoord(1.)).Equals(ToFVector(Snapshot.GetPositionAtIndex(LastIdx)), PosTolerance));

		// Out-of-range coordinates are clamped to the domain.
		TestTrue(*FString::Printf(TEXT("[%s] u<0 clamps to u=0"), *Case.Name),
			ToFVector(Snapshot.GetPositionAtCoord(-0.5)).Equals(ToFVector(Snapshot.GetPositionAtCoord(0.)), PosTolerance));
		TestTrue(*FString::Printf(TEXT("[%s] u>1 clamps to u=1"), *Case.Name),
			ToFVector(Snapshot.GetPositionAtCoord(1.5)).Equals(ToFVector(Snapshot.GetPositionAtCoord(1.)), PosTolerance));

		// The snapshot must be independent from later modifications of the live spline.
		const FVector Before = ToFVector(Snapshot.GetPositionAtCoord(0.5));
		Spline->SetLocationAtSplinePoint(0, FVector(99999., 99999., 99999.), ESplineCoordinateSpace::Local, true);
		Spline->SetRelativeTransform(FTransform(FVector(-5000., 0., 0.)));
		TestTrue(*FString::Printf(TEXT("[%s] snapshot is immune to later spline edits"), *Case.Name),
			ToFVector(Snapshot.GetPositionAtCoord(0.5)).Equals(Before, 1e-9));

		Spline->MarkAsGarbage();
	}

	return true;
}

#endif // WITH_TESTS
