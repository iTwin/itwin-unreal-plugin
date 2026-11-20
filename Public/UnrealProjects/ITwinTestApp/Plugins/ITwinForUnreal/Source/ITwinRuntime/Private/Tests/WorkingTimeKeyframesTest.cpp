/*--------------------------------------------------------------------------------------+
|
|     $Source: WorkingTimeKeyframesTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <CoreMinimal.h>
#include <Dom/JsonObject.h>
#include <Misc/AutomationTest.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinUtilityLibrary.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesKeyframes.h>
#include <Timeline/SchedulesParse.h>
#include <Timeline/SchedulesStructs.h>
#include <Timeline/TimeInSeconds.h>

#include <optional>
#include <vector>

#if WITH_TESTS

namespace
{
	double WorkingTimeTestDate(int32 Year, int32 Month, int32 Day, int32 Hour = 0, int32 Minute = 0)
	{
		return ITwin::Time::FromDateTime(FDateTime(Year, Month, Day, Hour, Minute));
	}

	constexpr double WorkingTimeTestHours(double H) { return H * 3600.; }
}

BEGIN_DEFINE_SPEC(WorkingTimeKeyframesSpec, "Bentley.ITwinForUnreal.ITwinRuntime.Schedules.WorkingTimeKeyframes",
				  EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
	std::optional<FITwinSchedule> Schedule;
	FITwinCoordConversions CoordConv;
	/// Fri 10:00 to Mon 15:00, ie. 11h of work (see "skips non-working hours and non-working days")
	FTimeRangeInSeconds const TaskRange{ WorkingTimeTestDate(2026, 10, 16, 10), WorkingTimeTestDate(2026, 10, 19, 15) };
	void CheckKeyframes(std::vector<FWorkingTimeKeyframe> const& Actual,
						std::vector<FWorkingTimeKeyframe> const& Expected);
END_DEFINE_SPEC(WorkingTimeKeyframesSpec)

void WorkingTimeKeyframesSpec::CheckKeyframes(std::vector<FWorkingTimeKeyframe> const& Actual,
											  std::vector<FWorkingTimeKeyframe> const& Expected)
{
	if (!TestEqual(TEXT("Keyframes count"), (int32)Actual.size(), (int32)Expected.size()))
		return;
	for (size_t i = 0; i < Actual.size(); ++i)
	{
		TestEqual(*FString::Printf(TEXT("Keyframe %d time"), (int32)i), Actual[i].Time, Expected[i].Time, 1e-3);
		TestEqual(*FString::Printf(TEXT("Keyframe %d progress"), (int32)i), Actual[i].Progress,
				  Expected[i].Progress, 1e-9);
	}
}

void WorkingTimeKeyframesSpec::Define()
{
	BeforeEach([this]()
		{
			Schedule.emplace(TEXT("SchedId"), TEXT("SchedName"));
			FCalendar Calendar;
			Calendar.Id = TEXT("Cal");
			Calendar.bDeleted = EDeletedProp(false);
			FCalendarDayType& Work = Calendar.DayTypes.emplace_back();
			Work.Id = TEXT("Work");
			Work.Type = ECalendarDayType::Normal;
			Work.TimeBlocks = { { { WorkingTimeTestHours(9), WorkingTimeTestHours(12) } },
								{ { WorkingTimeTestHours(13), WorkingTimeTestHours(17) } } };
			FCalendarDayType& Off = Calendar.DayTypes.emplace_back();
			Off.Id = TEXT("Off");
			Off.Type = ECalendarDayType::NonWorking;
			FCalendarDayType& FullDay = Calendar.DayTypes.emplace_back();
			FullDay.Id = TEXT("FullDay");
			FullDay.Type = ECalendarDayType::Partial;
			FullDay.TimeBlocks = { { { 0., 0. } } };
			Calendar.WeekDayTypeIds = { TEXT("Work"), TEXT("Work"), TEXT("Work"), TEXT("Work"), TEXT("Work"),
										TEXT("Off"), TEXT("Off") };
			Schedule->Calendars.push_back(std::move(Calendar));
			Schedule->KnownCalendars[TEXT("Cal")] = 0;
		});

	It("checks the test dates' days of week", [this]()
		{
			TestTrue(TEXT("Friday"), FDateTime(2026, 10, 16).GetDayOfWeek() == EDayOfWeek::Friday);
			TestTrue(TEXT("Monday"), FDateTime(2026, 10, 19).GetDayOfWeek() == EDayOfWeek::Monday);
		});

	It("skips non-working hours and non-working days", [this]()
		{
			// Fri 10:00 to Mon 15:00: 2h + 4h on Friday, 3h + 2h on Monday => 11h of work
			auto const Keyframes = Schedule->GetWorkingTimeKeyframes(
				{ WorkingTimeTestDate(2026, 10, 16, 10), WorkingTimeTestDate(2026, 10, 19, 15) }, TEXT("Cal"));
			CheckKeyframes(Keyframes, {
				{ WorkingTimeTestDate(2026, 10, 16, 10), 0. },
				{ WorkingTimeTestDate(2026, 10, 16, 12), 2. / 11. },
				{ WorkingTimeTestDate(2026, 10, 16, 13), 2. / 11. },
				{ WorkingTimeTestDate(2026, 10, 16, 17), 6. / 11. },
				{ WorkingTimeTestDate(2026, 10, 19, 9), 6. / 11. },
				{ WorkingTimeTestDate(2026, 10, 19, 12), 9. / 11. },
				{ WorkingTimeTestDate(2026, 10, 19, 13), 9. / 11. },
				{ WorkingTimeTestDate(2026, 10, 19, 15), 1. } });
		});

	It("applies calendar exceptions", [this]()
		{
			Schedule->Calendars[0].Exceptions.push_back({ WorkingTimeTestDate(2026, 10, 19), TEXT("Off") });
			// Monday is now off: same as above but with Tuesday instead of Monday
			auto const Keyframes = Schedule->GetWorkingTimeKeyframes(
				{ WorkingTimeTestDate(2026, 10, 16, 10), WorkingTimeTestDate(2026, 10, 20, 15) }, TEXT("Cal"));
			CheckKeyframes(Keyframes, {
				{ WorkingTimeTestDate(2026, 10, 16, 10), 0. },
				{ WorkingTimeTestDate(2026, 10, 16, 12), 2. / 11. },
				{ WorkingTimeTestDate(2026, 10, 16, 13), 2. / 11. },
				{ WorkingTimeTestDate(2026, 10, 16, 17), 6. / 11. },
				{ WorkingTimeTestDate(2026, 10, 20, 9), 6. / 11. },
				{ WorkingTimeTestDate(2026, 10, 20, 12), 9. / 11. },
				{ WorkingTimeTestDate(2026, 10, 20, 13), 9. / 11. },
				{ WorkingTimeTestDate(2026, 10, 20, 15), 1. } });
		});

	It("merges contiguous blocks spanning midnight", [this]()
		{
			Schedule->Calendars[0].WeekDayTypeIds.fill(TEXT("FullDay"));
			FTimeRangeInSeconds const Range{ WorkingTimeTestDate(2026, 10, 16, 8), WorkingTimeTestDate(2026, 10, 18, 8) };
			CheckKeyframes(Schedule->GetWorkingTimeKeyframes(Range, TEXT("Cal")),
						   { { Range.first, 0. }, { Range.second, 1. } });
		});

	It("falls back to linear progression", [this]()
		{
			FTimeRangeInSeconds const Weekend{ WorkingTimeTestDate(2026, 10, 17, 8), WorkingTimeTestDate(2026, 10, 18, 20) };
			CheckKeyframes(Schedule->GetWorkingTimeKeyframes(Weekend, TEXT("Cal")),
						   { { Weekend.first, 0. }, { Weekend.second, 1. } });
			CheckKeyframes(Schedule->GetWorkingTimeKeyframes(Weekend, TEXT("UnknownCal")),
						   { { Weekend.first, 0. }, { Weekend.second, 1. } });
			TestTrue(TEXT("Undefined range"),
					 Schedule->GetWorkingTimeKeyframes(ITwin::Time::Undefined(), TEXT("Cal")).empty());
		});

	It("parses time blocks given as hours of the day", [this]()
		{
			FString const Json = TEXT(R"({
				"id": "Cal", "name": "Standard", "workingDaysPerWeek": 5, "workingSecondsPerDay": 28800,
				"dayTypes": [{ "id": "Work", "type": "Normal",
					"timeBlocks": [{ "startTime": "09:00", "endTime": "17:30" }, { "startTime": "22:00", "endTime": "24:00" }] }],
				"exceptions": [],
				"week": { "mondayDayTypeId": "Work" } })");
			TSharedPtr<FJsonObject> JsonObj;
			auto const Reader = TJsonReaderFactory<>::Create(Json);
			if (!TestTrue(TEXT("Deserialize"), FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid()))
				return;
			FCalendar Calendar;
			Calendar.Id = TEXT("Cal");
			FSchedulesParse::ParseCalendar(JsonObj, Calendar);
			if (!TestEqual(TEXT("Day types"), (int32)Calendar.DayTypes.size(), 1)
				|| !TestEqual(TEXT("Time blocks"), (int32)Calendar.DayTypes[0].TimeBlocks.size(), 2))
			{
				return;
			}
			auto const& Blocks = Calendar.DayTypes[0].TimeBlocks;
			TestEqual(TEXT("Block 0 start"), Blocks[0].TimeBlock.first, WorkingTimeTestHours(9));
			TestEqual(TEXT("Block 0 end"), Blocks[0].TimeBlock.second, WorkingTimeTestHours(17.5));
			TestEqual(TEXT("Block 1 start"), Blocks[1].TimeBlock.first, WorkingTimeTestHours(22));
			TestEqual(TEXT("Block 1 end"), Blocks[1].TimeBlock.second, WorkingTimeTestHours(24));
			TestEqual(TEXT("Monday"), Calendar.WeekDayTypeIds[0], FString(TEXT("Work")));
		});

	It("pauses a 3D path during non-working time", [this]()
		{
			using namespace ITwin::Timeline;
			auto const WorkingTimeKeyframes = Schedule->GetWorkingTimeKeyframes(TaskRange, TEXT("Cal"));
			FPathTransformAssignment PathAssignment;
			PathAssignment.TransformAnchor = EAnchorPoint::Original;
			PathAssignment.bPauseDuringNonWorkingTime = true;
			std::vector<FTransformKey> PathKeys(2);
			PathKeys[0].Transform = FTransform(FVector::ZeroVector);
			PathKeys[0].RelativeTime = 0.;
			PathKeys[1].Transform = FTransform(FVector(11., 0., 0.));
			PathKeys[1].RelativeTime = 1.;
			FITwinElementTimeline Timeline(FIModelElementsKey(ITwinElementID(1)), FElementsGroup{ ITwinElementID(1) });
			Add3DPathTransformToTimeline(&Timeline, TaskRange, nullptr, PathAssignment, PathKeys, CoordConv,
										 FTaskDependenciesData{}, nullptr, &WorkingTimeKeyframes);

			// One keyframe per pause/resume point, the path's X coordinate following the task progression
			std::vector<PropertyEntry<PTransform>> const Keys(Timeline.Transform.Values.begin(),
															  Timeline.Transform.Values.end());
			if (!TestEqual(TEXT("Transform keyframes count"), (int32)Keys.size(), (int32)WorkingTimeKeyframes.size()))
				return;
			for (size_t i = 0; i < Keys.size(); ++i)
			{
				bool const bLast = (i + 1 == Keys.size());
				double const ExpectedTime = WorkingTimeKeyframes[i].Time - (bLast ? 3 * KEYFRAME_TIME_EPSILON : 0.);
				TestEqual(*FString::Printf(TEXT("Keyframe %d time"), (int32)i), Keys[i].Time, ExpectedTime, 1e-2);
				TestEqual(*FString::Printf(TEXT("Keyframe %d position"), (int32)i), Keys[i].Position,
						  FVector(11. * WorkingTimeKeyframes[i].Progress, 0., 0.), 1e-6f);
				TestTrue(*FString::Printf(TEXT("Keyframe %d interpolation"), (int32)i), Keys[i].Interpolation
					== (bLast ? EInterpolation::Step : EInterpolation::Linear));
			}
			// Over the weekend, the path does not move
			TestEqual(TEXT("Frozen over the weekend"), Keys[3].Position, Keys[4].Position, 1e-6f);
		});

	auto const CheckGrowthKeyframes = [this](EProfileAction const Action, bool const bInvert)
		{
			using namespace ITwin::Timeline;
			auto const WorkingTimeKeyframes = Schedule->GetWorkingTimeKeyframes(TaskRange, TEXT("Cal"));
			FAppearanceProfile Profile;
			Profile.ProfileType = Action;
			Profile.ActiveAppearance.GrowthSimulationMode = EGrowthSimulationMode::Bottom2Top;
			Profile.ActiveAppearance.bGrowthSimulationPauseDuringNonWorkingTime = true;
			FITwinElementTimeline Timeline(FIModelElementsKey(ITwinElementID(1)), FElementsGroup{ ITwinElementID(1) });
			AddCuttingPlaneToTimeline(Timeline, Profile, TaskRange, CoordConv, nullptr, &WorkingTimeKeyframes);

			std::vector<PropertyEntry<PClippingPlane>> const Keys(Timeline.ClippingPlane.Values.begin(),
																  Timeline.ClippingPlane.Values.end());
			if (!TestEqual(TEXT("Cutting plane keyframes count"), (int32)Keys.size(),
						   (int32)WorkingTimeKeyframes.size()))
			{
				return;
			}
			for (size_t i = 0; i < Keys.size(); ++i)
			{
				bool const bLast = (i + 1 == Keys.size());
				double const Progress = WorkingTimeKeyframes[i].Progress;
				double const Grown = bInvert ? (1. - Progress) : Progress;
				double const ExpectedTime = WorkingTimeKeyframes[i].Time - (bLast ? 3 * KEYFRAME_TIME_EPSILON : 0.);
				auto const& Plane = Keys[i].DefrdPlaneEq;
				TestEqual(*FString::Printf(TEXT("Keyframe %d time"), (int32)i), Keys[i].Time, ExpectedTime, 1e-2);
				TestTrue(*FString::Printf(TEXT("Keyframe %d orientation"), (int32)i),
						 Plane.PlaneOrientation.Equals(FVector3f::ZAxisVector, 1e-6f));
				if (Grown <= 0.)
					TestTrue(*FString::Printf(TEXT("Keyframe %d removed"), (int32)i),
							 EGrowthStatus::DeferredFullyRemoved == Plane.GrowthStatus);
				else if (Grown >= 1.)
					TestTrue(*FString::Printf(TEXT("Keyframe %d grown"), (int32)i),
							 EGrowthStatus::DeferredFullyGrown == Plane.GrowthStatus);
				else
				{
					TestTrue(*FString::Printf(TEXT("Keyframe %d partial"), (int32)i),
							 EGrowthStatus::DeferredPartial == Plane.GrowthStatus);
					TestEqual(*FString::Printf(TEXT("Keyframe %d growth ratio"), (int32)i), (double)Plane.PlaneW,
							  Grown, 1e-6);
				}
				// Step from each pause start (and at the end), so that growth is frozen during non-working time
				bool const bProgressing = !bLast && WorkingTimeKeyframes[i + 1].Progress > Progress;
				TestTrue(*FString::Printf(TEXT("Keyframe %d interpolation"), (int32)i), Keys[i].Interpolation
					== (bProgressing ? EInterpolation::Linear : EInterpolation::Step));
			}
		};

	It("pauses an Install growth simulation during non-working time", [CheckGrowthKeyframes]()
		{
			CheckGrowthKeyframes(EProfileAction::Install, false);
		});

	It("pauses a Remove growth simulation during non-working time", [CheckGrowthKeyframes]()
		{
			CheckGrowthKeyframes(EProfileAction::Remove, true);
		});

	It("finalizes a partial growth cutting plane", [this]()
		{
			using namespace ITwin::Timeline;
			FDeferredPlaneEquation const Deferred{
				.PlaneOrientation = FVector3f::ZAxisVector,
				.TransformKeyframe = nullptr,
				.PlaneW = 0.25f,
				.GrowthStatus = EGrowthStatus::DeferredPartial
			};
			FITwinSynchro4DSchedulesInternals::FinalizeCuttingPlaneEquation(CoordConv, Deferred,
				FBox(FVector::ZeroVector, FVector(100.)));
			TestTrue(TEXT("Partial"), EGrowthStatus::Partial == Deferred.GrowthStatus);
			// Box is expanded by 1% of its size: Z goes from -1 (removed) to 101 (grown)
			TestEqual(TEXT("Plane W"), (double)Deferred.PlaneW, -1. + 0.25 * 102., 1e-3);
		});
}

#endif // WITH_TESTS
