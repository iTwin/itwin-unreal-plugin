/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineToolTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineEnums.h>
#include <Spline/ITwinSplineGeometry.h>
#include <Math/UEMathConversion.h>

#include <Editor.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>
#include <TimerManager.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/SplinesManager.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <memory>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSplineToolTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SplineTool", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	//! Minimal transient world hosting the spline tool and its helpers.
	//! The splines manager is a pure in-memory one (no decoration service involved).
	//! A single instance is used for the whole test: Reset() brings the tool back to a pristine
	//! state between sections, which avoids repeatedly creating/destroying a UWorld (a source of
	//! "Old World ... not cleaned up by GC" warnings).
	class [[nodiscard]] FSplineToolFixture
	{
	public:
		FSplineToolFixture()
		{
			World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
			if (!World)
				return;

			SplinesManager.reset(AdvViz::SDK::ISplinesManager::New());

			Tool = World->SpawnActor<AITwinSplineTool>();
			if (Tool)
				Tool->SetSplinesManager(SplinesManager);
		}

		~FSplineToolFixture()
		{
			if (!World)
				return;

			Reset(EITwinSplineUsage::Undefined);   // deletes all spline helpers

			if (::IsValid(Tool))
			{
				World->GetTimerManager().ClearAllTimersForObject(Tool);
				Tool->Destroy();                   // leaked strong refs no longer matter:
			}                                      // the world is not ours to destroy
			Tool = nullptr;
			SplinesManager.reset();
			World = nullptr;
		}

		FSplineToolFixture(FSplineToolFixture const&) = delete;
		FSplineToolFixture& operator=(FSplineToolFixture const&) = delete;

		bool IsValidFixture() const { return World != nullptr && ::IsValid(Tool); }

		AITwinSplineTool* GetTool() const { return Tool; }

		//! Removes all splines and restores the tool's default state for the given usage.
		void Reset(EITwinSplineUsage Usage)
		{
			if (!IsValidFixture())
				return;

			Tool->SetSelectedSpline(nullptr);

			TArray<AITwinSplineHelper*> Splines;
			for (TActorIterator<AITwinSplineHelper> It(World); It; ++It)
			{
				Splines.Add(*It);
			}
			for (AITwinSplineHelper* Spline : Splines)
			{
				Tool->DeleteSpline(Spline, /*bTriggeredFromITS*/false);
			}

			Tool->SetMode(EITwinSplineToolMode::Undefined);
			Tool->SetEnabled(false);
			Tool->SetUsage(Usage);
		}

		//! Axis-aligned square (4 points) around the given centre.
		AITwinSplineHelper* AddSquareSpline(FVector const& Centre) const
		{
			const TArray<FVector> Points{
				Centre + FVector(-100., -100., 0.), Centre + FVector(100., -100., 0.),
				Centre + FVector( 100.,  100., 0.), Centre + FVector(-100.,  100., 0.) };
			return Tool->AddSpline(Centre, Points);
		}

		//! Creates a regular polygon spline with NumPoints points around the given centre.
		AITwinSplineHelper* AddPolygonSpline(FVector const& Centre, int32 NumPoints) const
		{
			TArray<FVector> Points;
			Points.Reserve(NumPoints);
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const double Angle = 2. * UE_DOUBLE_PI * (double)i / (double)NumPoints;
				Points.Add(Centre + FVector(100. * FMath::Cos(Angle), 100. * FMath::Sin(Angle), 0.));
			}
			return Tool->AddSpline(Centre, Points);
		}

	private:
		UWorld* World = nullptr;
		AITwinSplineTool* Tool = nullptr;
		std::shared_ptr<AdvViz::SDK::ISplinesManager> SplinesManager;
	};
}

bool FITwinSplineToolTest::RunTest(const FString& /*Parameters*/)
{
	// NB: the tool's FSplineXxxEvent delegates are dynamic multicast delegates, which can only be bound
	// from a UObject. They are therefore not asserted here: add a dedicated UCLASS listener (in a header
	// with a matching *.generated.h) if the broadcast order ever needs to be covered.

	FSplineToolFixture Fixture;
	if (!TestTrue(TEXT("Fixture is valid"), Fixture.IsValidFixture()))
		return false;
	AITwinSplineTool* Tool = Fixture.GetTool();

	SECTION("Selection - no spline selected by default")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		TestNull(TEXT("No selected spline"), Tool->GetSelectedSpline());
		TestEqual(TEXT("No selected point"), Tool->GetSelectedPointIndex(), (int32)INDEX_NONE);
		TestFalse(TEXT("HasSelectedPoint"), Tool->HasSelectedPoint());
		TestFalse(TEXT("CanDeletePoint"), Tool->CanDeletePoint());
		TestEqual(TEXT("Selection transform is identity"),
			Tool->GetSelectionTransform().GetLocation(), FVector::ZeroVector);
	}

	SECTION("SetSelectedSpline - only one spline is selected at a time")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* SplineA = Fixture.AddSquareSpline(FVector(0., 0., 0.));
		AITwinSplineHelper* SplineB = Fixture.AddSquareSpline(FVector(1000., 0., 0.));
		if (!TestNotNull(TEXT("Spline A created"), SplineA) ||
			!TestNotNull(TEXT("Spline B created"), SplineB))
			return false;

		Tool->SetSelectedSpline(SplineA);
		TestEqual(TEXT("A is selected"), Tool->GetSelectedSpline(), SplineA);
		TestTrue(TEXT("A knows it is selected"), SplineA->IsSelected());

		// Selecting another one must deselect the previous one.
		Tool->SetSelectedSpline(SplineB);
		TestEqual(TEXT("B is selected"), Tool->GetSelectedSpline(), SplineB);
		TestFalse(TEXT("A is deselected"), SplineA->IsSelected());
		TestTrue(TEXT("B knows it is selected"), SplineB->IsSelected());

		// Selecting a spline resets the point selection.
		TestEqual(TEXT("Point selection reset on selection change"),
			Tool->GetSelectedPointIndex(), -1);

		Tool->SetSelectedSpline(nullptr);
		TestNull(TEXT("Selection cleared"), Tool->GetSelectedSpline());
		TestFalse(TEXT("B is deselected"), SplineB->IsSelected());
	}

	SECTION("Selection visibility - other splines are hidden while one is selected")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* SplineA = Fixture.AddSquareSpline(FVector(0., 0., 0.));
		AITwinSplineHelper* SplineB = Fixture.AddSquareSpline(FVector(1000., 0., 0.));
		if (!TestNotNull(TEXT("Spline A created"), SplineA) ||
			!TestNotNull(TEXT("Spline B created"), SplineB))
			return false;

		// AzDev#1967146: when a polygon is selected, the other ones are all hidden.
		Tool->SetSelectedSpline(SplineA);
		TestFalse(TEXT("Selected spline stays visible"), SplineA->IsHidden());
		TestTrue(TEXT("Other spline is hidden"), SplineB->IsHidden());
	}

	SECTION("Point selection")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;

		// Setting a point index without any selected spline must be a no-op.
		Tool->SetSelectedPointIndex(1);
		TestEqual(TEXT("Ignored without a selected spline"),
			Tool->GetSelectedPointIndex(), (int32)INDEX_NONE);

		Tool->SetSelectedSpline(Spline);
		Tool->SetSelectedPointIndex(1);
		TestEqual(TEXT("Point 1 selected"), Tool->GetSelectedPointIndex(), 1);
		TestTrue(TEXT("HasSelectedPoint"), Tool->HasSelectedPoint());

		// The gizmo must sit on the selected point.
		TestEqual(TEXT("Selection transform follows the point"),
			Tool->GetSelectionTransform().GetLocation(),
			Spline->GetLocationAtSplinePoint(1));

		Tool->SetSelectedPointIndex(-1);
		TestFalse(TEXT("HasSelectedPoint after reset"), Tool->HasSelectedPoint());
	}

	SECTION("CanDeletePoint - degenerate splines are protected")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;
		Tool->SetSelectedSpline(Spline);

		// No point selected => nothing to delete.
		TestFalse(TEXT("No selected point"), Tool->CanDeletePoint());

		Tool->SetSelectedPointIndex(0);
		const int32 MinPoints = Spline->MinNumberOfPointsForValidSpline();
		TestTrue(TEXT("Above the minimum"), Spline->GetNumberOfSplinePoints() > MinPoints);
		TestTrue(TEXT("Can delete above the minimum"), Tool->CanDeletePoint());

		// Delete points down to the minimum: the last one must be refused.
		while (Spline->GetNumberOfSplinePoints() > MinPoints)
		{
			Tool->DeleteSelectedPoint();
		}
		TestEqual(TEXT("Stopped at the minimum"), Spline->GetNumberOfSplinePoints(), MinPoints);
		TestFalse(TEXT("Cannot delete at the minimum"), Tool->CanDeletePoint());
	}

	SECTION("DeleteSelectedPoint - the selected index loops on the last point")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		// 6 points, so that both deletions below stay above the minimum
		// (3 for a closed spline, 2 for an open one).
		AITwinSplineHelper* Spline = Fixture.AddPolygonSpline(FVector::ZeroVector, 6);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;
		const int32 NumPoints = Spline->GetNumberOfSplinePoints();
		if (!TestTrue(TEXT("Enough points for two deletions"),
			NumPoints >= Spline->MinNumberOfPointsForValidSpline() + 2))
			return false;
		Tool->SetSelectedSpline(Spline);

		// Deleting an interior point keeps the same index (which now designates the next point).
		Tool->SetSelectedPointIndex(1);
		TestTrue(TEXT("Point deleted"), Tool->DeleteSelectedPoint());
		TestEqual(TEXT("One point removed"), Spline->GetNumberOfSplinePoints(), NumPoints - 1);
		TestEqual(TEXT("Index unchanged"), Tool->GetSelectedPointIndex(), 1);

		// Deleting the last point must loop back to the first one.
		Tool->SetSelectedPointIndex(Spline->GetNumberOfSplinePoints() - 1);
		TestTrue(TEXT("Point deleted"), Tool->DeleteSelectedPoint());
		TestEqual(TEXT("Index looped to 0"), Tool->GetSelectedPointIndex(), 0);
	}

	SECTION("DeleteSpline - clears the selection and forgets the spline")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* SplineA = Fixture.AddSquareSpline(FVector(0., 0., 0.));
		AITwinSplineHelper* SplineB = Fixture.AddSquareSpline(FVector(1000., 0., 0.));
		if (!TestNotNull(TEXT("Spline A created"), SplineA) ||
			!TestNotNull(TEXT("Spline B created"), SplineB))
			return false;
		TestTrue(TEXT("Tool has splines"), Tool->HasSplines());

		// Deleting a non-selected spline must preserve the current selection.
		Tool->SetSelectedSpline(SplineA);
		Tool->DeleteSpline(SplineB, /*bTriggeredFromITS*/false);
		TestEqual(TEXT("A is still selected"), Tool->GetSelectedSpline(), SplineA);

		Tool->DeleteSelectedSpline();
		TestNull(TEXT("Selection cleared after deletion"), Tool->GetSelectedSpline());
		TestFalse(TEXT("No spline left"), Tool->HasSplines());
	}

	SECTION("InsertPointAt")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;
		const int32 NumPoints = Spline->GetNumberOfSplinePoints();

		// Inserting into a spline that is not selected must select it first.
		Tool->SetSelectedSpline(nullptr);
		const FVector NewPos(0., -200., 0.);
		TestTrue(TEXT("Insertion succeeded"), Tool->InsertPointAt(Spline, 1, NewPos));
		TestEqual(TEXT("Spline got selected"), Tool->GetSelectedSpline(), Spline);
		TestEqual(TEXT("One point added"), Spline->GetNumberOfSplinePoints(), NumPoints + 1);
		TestEqual(TEXT("New point is selected"), Tool->GetSelectedPointIndex(), 1);
		TestEqual(TEXT("New point is at the requested position"),
			Spline->GetLocationAtSplinePoint(1), NewPos);

		// Appending after the last point is the other valid slot.
		const int32 AppendIndex = Spline->GetNumberOfSplinePoints();
		TestTrue(TEXT("Append at the end"),
			Tool->InsertPointAt(Spline, AppendIndex, FVector(0., 400., 0.)));
		TestEqual(TEXT("Point appended"), Spline->GetNumberOfSplinePoints(), AppendIndex + 1);

		// NB: out-of-range indices (and a null spline) hit an ensure() by design, so they are not
		// exercised here. The index validation itself is covered by IsValidInsertionIndex in
		// ITwinSplineGeometryTest.cpp.
	}

	SECTION("Mode and usage round-trip")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);
	
		TestEqual(TEXT("Usage set by the fixture"), Tool->GetUsage(), EITwinSplineUsage::PopulationZone);

		Tool->SetMode(EITwinSplineToolMode::InteractiveCreation);
		TestEqual(TEXT("Mode round-trip"), Tool->GetMode(), EITwinSplineToolMode::InteractiveCreation);
		Tool->SetMode(EITwinSplineToolMode::Undefined);
		TestEqual(TEXT("Mode reset"), Tool->GetMode(), EITwinSplineToolMode::Undefined);

		Tool->SetUsage(EITwinSplineUsage::AnimPathObject);
		TestEqual(TEXT("Usage round-trip"), Tool->GetUsage(), EITwinSplineUsage::AnimPathObject);
	}

	SECTION("SetTangentMode applies to the selected spline")
	{
		Fixture.Reset(EITwinSplineUsage::AnimPathObject);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;
		Tool->SetSelectedSpline(Spline);

		Tool->SetTangentMode(EITwinTangentMode::Smooth);
		TestEqual(TEXT("Tool reports the spline mode"), Tool->GetTangentMode(), EITwinTangentMode::Smooth);
		TestEqual(TEXT("Spline was updated"), Spline->GetTangentMode(), EITwinTangentMode::Smooth);

		Tool->SetTangentMode(EITwinTangentMode::Linear);
		TestEqual(TEXT("Linear mode"), Spline->GetTangentMode(), EITwinTangentMode::Linear);
	}

	SECTION("SetEnabled - disabling clears the selection")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;

		Tool->SetEnabled(true);
		TestTrue(TEXT("Tool is enabled"), Tool->IsEnabled());
		Tool->SetSelectedSpline(Spline);

		Tool->SetEnabled(false);
		TestFalse(TEXT("Tool is disabled"), Tool->IsEnabled());
		TestNull(TEXT("Selection cleared on disable"), Tool->GetSelectedSpline());
	}

	SECTION("GetSplineReferencePosition - union of all splines when nothing is selected")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		FVector RefLocation = FVector::ZeroVector;
		FBox Box;
		// No spline at all => no reference position.
		TestFalse(TEXT("No spline in the scene"), Tool->GetSplineReferencePosition(RefLocation, Box));

		AITwinSplineHelper* SplineA = Fixture.AddSquareSpline(FVector(0., 0., 0.));
		AITwinSplineHelper* SplineB = Fixture.AddSquareSpline(FVector(1000., 0., 0.));
		if (!TestNotNull(TEXT("Spline A created"), SplineA) ||
			!TestNotNull(TEXT("Spline B created"), SplineB))
			return false;

		FBox BoxA, BoxB;
		TestTrue(TEXT("A has a world box"), SplineA->IncludeInWorldBox(BoxA));
		TestTrue(TEXT("B has a world box"), SplineB->IncludeInWorldBox(BoxB));

		TestTrue(TEXT("Union of all splines"), Tool->GetSplineReferencePosition(RefLocation, Box));
		TestTrue(TEXT("Box contains both splines"),
			Box.IsInsideOrOn(BoxA.Min) && Box.IsInsideOrOn(BoxA.Max) &&
			Box.IsInsideOrOn(BoxB.Min) && Box.IsInsideOrOn(BoxB.Max));

		// With a selection, only the selected spline is framed.
		Tool->SetSelectedSpline(SplineA);
		FBox SelectedBox;
		TestTrue(TEXT("Selected spline only"), Tool->GetSplineReferencePosition(RefLocation, SelectedBox));
		TestFalse(TEXT("B is excluded"), SelectedBox.IsInsideOrOn(BoxB.GetCenter()));
	}

	SECTION("FAutomaticVisibilityDisabler restores the previous setting")
	{
		const bool bInitial = AITwinSplineTool::AutomaticSplineVisibility();
		{
			const AITwinSplineTool::FAutomaticVisibilityDisabler Disabler;
			TestFalse(TEXT("Automatic visibility is off in the scope"),
				AITwinSplineTool::AutomaticSplineVisibility());
		}
		TestEqual(TEXT("Setting restored"), AITwinSplineTool::AutomaticSplineVisibility(), bInitial);
	}

	SECTION("DuplicateSelectedPoint")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationZone);

		AITwinSplineHelper* Spline = Fixture.AddSquareSpline(FVector::ZeroVector);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;
		const int32 NumPoints = Spline->GetNumberOfSplinePoints();

		// No spline selected at all.
		TestFalse(TEXT("No selection"), Tool->DuplicateSelectedPoint());

		// Spline selected, but no point selected.
		Tool->SetSelectedSpline(Spline);
		TestFalse(TEXT("No selected point"), Tool->DuplicateSelectedPoint());
		TestEqual(TEXT("Point count unchanged"), Spline->GetNumberOfSplinePoints(), NumPoints);

		// Interior point: the duplicate lands next to the original, at the same position.
		Tool->SetSelectedPointIndex(1);
		const FVector OriginalPos = Spline->GetLocationAtSplinePoint(1);
		TestTrue(TEXT("Duplication succeeded"), Tool->DuplicateSelectedPoint());
		TestEqual(TEXT("One point added"), Spline->GetNumberOfSplinePoints(), NumPoints + 1);
		TestEqual(TEXT("Duplicate is at the original position"),
			Spline->GetLocationAtSplinePoint(1), OriginalPos);
		TestEqual(TEXT("Neighbour is the original point"),
			Spline->GetLocationAtSplinePoint(2), OriginalPos);

		// Last point: duplication must not read out of range.
		Tool->SetSelectedPointIndex(Spline->GetNumberOfSplinePoints() - 1);
		const int32 CountBeforeLast = Spline->GetNumberOfSplinePoints();
		TestTrue(TEXT("Duplicating the last point"), Tool->DuplicateSelectedPoint());
		TestEqual(TEXT("One more point"), Spline->GetNumberOfSplinePoints(), CountBeforeLast + 1);
	}

	SECTION("Tightness - per point on a population path, persisted in the AdvViz spline")
	{
		Fixture.Reset(EITwinSplineUsage::PopulationPath);

		AITwinSplineHelper* Spline = Fixture.AddPolygonSpline(FVector::ZeroVector, 5);
		if (!TestNotNull(TEXT("Spline created"), Spline))
			return false;

		const float OtherPointBefore = Spline->GetTightness(3);

		Spline->SetTightness(1, 0.2f);
		Spline->SetTightness(2, 0.8f);
		TestNearlyEqual(TEXT("Point 1 tightness"), Spline->GetTightness(1), 0.2f, 1e-3f);
		TestNearlyEqual(TEXT("Point 2 tightness"), Spline->GetTightness(2), 0.8f, 1e-3f);
		TestNearlyEqual(TEXT("Other points are untouched"), Spline->GetTightness(3), OtherPointBefore, 1e-3f);

		// Changing a point again must not affect the previously edited one.
		Spline->SetTightness(1, 0.6f);
		TestNearlyEqual(TEXT("Point 1 re-edited"), Spline->GetTightness(1), 0.6f, 1e-3f);
		TestNearlyEqual(TEXT("Point 2 kept"), Spline->GetTightness(2), 0.8f, 1e-3f);

		// Invalid indices are ignored.
		Spline->SetTightness(-1, 0.5f);
		Spline->SetTightness(Spline->GetNumberOfSplinePoints(), 0.5f);
		TestEqual(TEXT("Out of range tightness"), Spline->GetTightness(-1), 0.f);

		// The tangents written in the AdvViz spline (used for saving) must match the edited ones.
		auto const AVizSpline = Spline->GetAVizSpline();
		if (TestTrue(TEXT("AdvViz spline"), (bool)AVizSpline))
		{
			AdvViz::SDK::ISplinePointPtr PointPtr;
			{
				auto SplineLock = AVizSpline->GetRAutoLock();
				TestTrue(TEXT("Spline should be saved"), SplineLock->ShouldSave());
				PointPtr = SplineLock->GetPoint(1);
			}
			if (TestTrue(TEXT("AdvViz point"), (bool)PointPtr))
			{
				const FVector SavedTangent = FITwinMathConversion::SDKtoUE(PointPtr->GetRAutoLock()->GetInTangent());
				TestNearlyEqual(TEXT("Persisted tightness"),
					ITwinSpline::TightnessFromTangentLength(SavedTangent.Length()), 0.6f, 1e-3f);
			}
		}
	}

	return true;
}

#endif // WITH_TESTS && WITH_EDITOR
