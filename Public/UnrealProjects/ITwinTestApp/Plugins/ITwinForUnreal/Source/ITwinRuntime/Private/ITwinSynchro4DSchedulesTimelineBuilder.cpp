/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSynchro4DSchedulesTimelineBuilder.h"
#include "ITwinSynchro4DSchedules.h"
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex
#include <Timeline/Timeline.h>
#include <Timeline/SchedulesStructs.h>
#include <Timeline/SchedulesConstants.h>

void AddColorToTimeline(FITwinElementTimeline& ElementTimeline,
						FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time)
{
	if (Profile.StartAppearance.bUseOriginalColor
		&& Profile.ActiveAppearance.Base.bUseOriginalColor
		&& Profile.FinishAppearance.bUseOriginalColor)
	{
		return;
	}
	using namespace ITwin::Timeline;
	ElementTimeline.SetColorAt(Time.first,
		Profile.StartAppearance.bUseOriginalColor ? std::nullopt
			: std::optional<FVector>(Profile.StartAppearance.Color),
		Interpolation::Step);

	ElementTimeline.SetColorAt(Time.first + KEYFRAME_TIME_EPSILON,
		Profile.ActiveAppearance.Base.bUseOriginalColor ? std::nullopt
			: std::optional<FVector>(Profile.ActiveAppearance.Base.Color),
		Interpolation::Step);

	ElementTimeline.SetColorAt(Time.second,
		Profile.FinishAppearance.bUseOriginalColor ? std::nullopt
			: std::optional<FVector>(Profile.FinishAppearance.Color),
		// See comment on Interpolation::Next for an alternative
		Interpolation::Step);
}

/// IMPORTANT: the orientation here is such that it points into the half space that is *cut out*,
/// NOT the part that remains visible.
FVector GetCuttingPlaneOrientation(FActiveAppearance const& Appearance)
{
	FVector Orientation;
	// Note: not using FVector::[Up|Left|etc.]Vector because 'Right' is +Y in UE but +X in iTwin
	switch (Appearance.GrowthSimulationMode)
	{
	case EGrowthSimulationMode::Bottom2Top:
		Orientation = FVector::ZAxisVector;
		break;
	case EGrowthSimulationMode::Top2Bottom:
		Orientation = -FVector::ZAxisVector;
		break;
	case EGrowthSimulationMode::Left2Right:
		Orientation = FVector::XAxisVector;
		break;
	case EGrowthSimulationMode::Right2Left:
		Orientation = -FVector::XAxisVector;
		break;
	case EGrowthSimulationMode::Front2Back:
		Orientation = FVector::YAxisVector;
		break;
	case EGrowthSimulationMode::Back2Front:
		Orientation = -FVector::YAxisVector;
		break;
	case EGrowthSimulationMode::Custom:
		Orientation = { Appearance.GrowthDirectionCustom.X,
						Appearance.GrowthDirectionCustom.Y,
						Appearance.GrowthDirectionCustom.Z };
		Orientation.Normalize();
		break;
	case EGrowthSimulationMode::None:
	case EGrowthSimulationMode::Unknown:
	default:
		Orientation = FVector::ZeroVector;
		check(false);
		break;
	}
	return Appearance.bInvertGrowth ? (-Orientation) : Orientation;
}
	
void AddCuttingPlaneToTimeline(FITwinElementTimeline& ElementTimeline,
	FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time)
{
	auto const& GrowthAppearance = Profile.ActiveAppearance;// all others are ignored...
	if (GrowthAppearance.GrowthSimulationMode > EGrowthSimulationMode::Custom)
	{
		return;
	}
	const FVector4 PlaneEqDeferredW(GetCuttingPlaneOrientation(GrowthAppearance),
									FITwinElementTimeline::DeferredPlaneEquationW);
	if (PlaneEqDeferredW.X == 0 && PlaneEqDeferredW.Y == 0 && PlaneEqDeferredW.Z == 0)
	{
		return;
	}
	using namespace ITwin::Timeline;
	using namespace ITwin::Timeline;
	bool const bVisibleOutsideTask = (Profile.ProfileType == EProfileAction::Neutral)
									|| (Profile.ProfileType == EProfileAction::Maintenance);
	// Regular growth means "building (new or temp) stuff", while inverted growth means "removing"
	// (dismantling) existing/temp stuff. Before new/temp stuff is built, or after existing/temp stuff is
	// removed, visibility is 0 anyway so the cutting plane setting does not matter.
	ElementTimeline.SetCuttingPlaneAt(Time.first,
		GrowthAppearance.bInvertGrowth ? true/*fullyVisible_*/ : bVisibleOutsideTask/*fullyHidden_?*/,
		Interpolation::Step);
	ElementTimeline.SetCuttingPlaneAt(Time.first + KEYFRAME_TIME_EPSILON,
		FDeferredPlaneEquation{ PlaneEqDeferredW,
								GrowthAppearance.bInvertGrowth
									? EGrowthBoundary::FullyGrown : EGrowthBoundary::FullyRemoved },
		Interpolation::Linear);
	ElementTimeline.SetCuttingPlaneAt(Time.second - KEYFRAME_TIME_EPSILON,
		FDeferredPlaneEquation{ PlaneEqDeferredW,
								GrowthAppearance.bInvertGrowth
									? EGrowthBoundary::FullyRemoved : EGrowthBoundary::FullyGrown },
		Interpolation::Step);
	ElementTimeline.SetCuttingPlaneAt(Time.second,
		GrowthAppearance.bInvertGrowth ? bVisibleOutsideTask/*fullyHidden_?*/ : true/*fullyVisible_*/,
		// See comment on Interpolation::Next for an alternative
		Interpolation::Step);
}
	
void AddVisibilityToTimeline(FITwinElementTimeline& ElementTimeline,
									FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time)
{
	using namespace ITwin::Timeline;
	if (Profile.ProfileType == EProfileAction::Neutral)
	{
		// Element just disappears at start of task?!
		ElementTimeline.SetVisibilityAt(Time.first, 0, Interpolation::Step);
	}
	else
	{
		// Every case but 'Maintenance' tasks need a keyframe at some point: start, end or both
		if (Profile.ProfileType == EProfileAction::Maintenance
			// About the "== 1" tests: is the animation alpha multiplied, or somehow replacing the
			// material's "base" opacity? In the latter case, these tests are wrong, and on top of that,
			// Features initially rendered with the Translucent material could switch to the Opaque...
			&& (Profile.StartAppearance.bUseOriginalAlpha || Profile.StartAppearance.Alpha == 1)
			&& (Profile.ActiveAppearance.Base.bUseOriginalAlpha ||
				(Profile.ActiveAppearance.Base.Alpha == 1 && Profile.ActiveAppearance.FinishAlpha == 1))
			&& (Profile.FinishAppearance.bUseOriginalAlpha || Profile.FinishAppearance.Alpha == 1))
		{
			return;
		}

		ElementTimeline.SetVisibilityAt(Time.first,
			(EProfileAction::Install == Profile.ProfileType
				|| EProfileAction::Temporary == Profile.ProfileType)
			? 0.f : (Profile.StartAppearance.bUseOriginalAlpha ? std::nullopt
					: std::optional<float>(Profile.StartAppearance.Alpha)),
			Interpolation::Step);

		if (Profile.ActiveAppearance.Base.Alpha == Profile.ActiveAppearance.FinishAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.first + KEYFRAME_TIME_EPSILON,
				Profile.ActiveAppearance.Base.bUseOriginalAlpha
					? std::nullopt : std::optional<float>(Profile.ActiveAppearance.Base.Alpha),
				Interpolation::Step);
		}
		else
		{
			ElementTimeline.SetVisibilityAt(Time.first + KEYFRAME_TIME_EPSILON,
				Profile.ActiveAppearance.Base.Alpha, Interpolation::Linear);
			ElementTimeline.SetVisibilityAt(Time.second - KEYFRAME_TIME_EPSILON,
				Profile.ActiveAppearance.FinishAlpha, Interpolation::Step);
		}

		ElementTimeline.SetVisibilityAt(Time.second,
			(EProfileAction::Remove == Profile.ProfileType
				|| EProfileAction::Temporary == Profile.ProfileType)
			? 0.f : (Profile.FinishAppearance.bUseOriginalAlpha ? std::nullopt
						: std::optional<float>(Profile.FinishAppearance.Alpha)),
			// See comment on Interpolation::Next for an alternative
			Interpolation::Step);
	}
}

void CreateTestingTimeline(FITwinElementTimeline& Timeline)
{
	constexpr double delta = 1000. * KEYFRAME_TIME_EPSILON;

	// Initial conditions, to not depend on the first keyframe of each feature, which can be much farther
	// along the timeline
	using namespace ITwin::Timeline;
	Timeline.SetColorAt(0., std::nullopt/*ie. bUseOriginalColor*/, Interpolation::Step);
	Timeline.SetVisibilityAt(0., 1.f, Interpolation::Step);
	Timeline.SetCuttingPlaneAt(0., true/*ie. fullyVisible_*/, Interpolation::Step);

	// tests occur every 4 deltas: one before task, one for task duration, one after task, one for blink
	constexpr int cycle = 4;
	FTimeRangeInSeconds TimeRange = { - (cycle - 1) * delta, 0. };
	size_t Idx = 0;
	auto const incrTimes = [&TimeRange, &cycle, &delta] () -> FTimeRangeInSeconds const&
		{
			TimeRange.first += cycle * delta;
			TimeRange.second = TimeRange.first + delta;
			return TimeRange;
		};
	FAppearanceProfile Profile;
	Profile.ProfileType = EProfileAction::Maintenance;
	
	auto const blinkAndResetBetweenTests = [&Timeline, &TimeRange, delta] ()
		{
			double const BlinkStart = TimeRange.second + delta;
			// "Blink" the Element
			Timeline.SetVisibilityAt(BlinkStart - KEYFRAME_TIME_EPSILON, 1, Interpolation::Step);
			Timeline.SetVisibilityAt(BlinkStart, 0.f, Interpolation::Step);
			// End blink and instruct to use the next keyframes' values, if any, otherwise reset values
			Timeline.SetVisibilityAt(BlinkStart + delta, 1.f, Interpolation::Next);
			Timeline.SetColorAt(BlinkStart + delta, std::nullopt, Interpolation::Next);
			Timeline.SetCuttingPlaneAt(BlinkStart + delta, true, Interpolation::Next);
		};
	auto const testColor = [&Timeline, &Profile, &Idx, &incrTimes, &blinkAndResetBetweenTests]
		(bool start, bool active, bool finish)
		{
			Profile.StartAppearance.bUseOriginalColor = !start;
			Profile.ActiveAppearance.Base.bUseOriginalColor = !active;
			Profile.FinishAppearance.bUseOriginalColor = !finish;
			if (start) Profile.StartAppearance.Color =
				FITwinMathExts::RandomFloatColorFromIndex(Idx++, nullptr);
			if (active) Profile.ActiveAppearance.Base.Color =
				FITwinMathExts::RandomFloatColorFromIndex(Idx++, nullptr);
			if (finish) Profile.FinishAppearance.Color =
				FITwinMathExts::RandomFloatColorFromIndex(Idx++, nullptr);
			AddColorToTimeline(Timeline, Profile, incrTimes());
			blinkAndResetBetweenTests();
		};
	// Reset to defaults
	Profile.StartAppearance.bUseOriginalAlpha = true;
	Profile.ActiveAppearance.Base.bUseOriginalAlpha = true;
	Profile.FinishAppearance.bUseOriginalAlpha = true;
	Profile.ActiveAppearance.GrowthSimulationMode = EGrowthSimulationMode::None;
	testColor(false, true, false);
	testColor(false, true, true);
	testColor(true, true, false);
	testColor(true, true, true);

	auto const testAlpha = [&Timeline, &Profile, &Idx, &incrTimes, &blinkAndResetBetweenTests]
		(bool start, bool active, bool activeVaries, bool finish)
		{
			Profile.StartAppearance.bUseOriginalAlpha = !start;
			Profile.ActiveAppearance.Base.bUseOriginalAlpha = !active;
			Profile.FinishAppearance.bUseOriginalAlpha = !finish;
			if (start)
				Profile.StartAppearance.Alpha = .25f;
			if (active)
			{
				if (activeVaries)
				{
					Profile.ActiveAppearance.Base.Alpha = .05f;
					Profile.ActiveAppearance.FinishAlpha = 1.f;
					if ((Idx++) % 2 == 0) std::swap(
						Profile.ActiveAppearance.Base.Alpha, Profile.ActiveAppearance.FinishAlpha);
				}
				else
					Profile.ActiveAppearance.FinishAlpha = Profile.ActiveAppearance.Base.Alpha = .1f;
			}
			if (finish)
				Profile.FinishAppearance.Alpha = .5f;

			AddVisibilityToTimeline(Timeline, Profile, incrTimes());
			blinkAndResetBetweenTests();
		};

	// Reset to defaults
	Profile.ActiveAppearance.GrowthSimulationMode = EGrowthSimulationMode::None;
	Profile.StartAppearance.bUseOriginalColor = true;
	Profile.ActiveAppearance.Base.bUseOriginalColor = true;
	Profile.FinishAppearance.bUseOriginalColor = true;
	testAlpha(false, true, false, false);
	testAlpha(false, true, true, false);
	//testAlpha(false, true, false, true);
	testAlpha(false, true, true, true);
	//testAlpha(true, true, false, false);
	testAlpha(true, true, true, true);

	// Reset to defaults
	Profile.StartAppearance.bUseOriginalColor = true;
	Profile.ActiveAppearance.Base.bUseOriginalColor = true;
	Profile.FinishAppearance.bUseOriginalColor = true;
	Profile.StartAppearance.bUseOriginalAlpha = true;
	Profile.ActiveAppearance.Base.bUseOriginalAlpha = true;
	Profile.FinishAppearance.bUseOriginalAlpha = true;
	Profile.ActiveAppearance.GrowthDirectionCustom = FVector(1., 1., 1.);
	Profile.ActiveAppearance.bInvertGrowth = false;
	for (uint8_t i = 0; i <= (int)EGrowthSimulationMode::Custom; ++i)
	{
		Profile.ActiveAppearance.GrowthSimulationMode = (EGrowthSimulationMode)i;
		AddCuttingPlaneToTimeline(Timeline, Profile, incrTimes());
		blinkAndResetBetweenTests();
	}
	Profile.ActiveAppearance.bInvertGrowth = true;
	for (uint8_t i = 0; i <= (int)EGrowthSimulationMode::Custom; ++i)
	{
		Profile.ActiveAppearance.GrowthSimulationMode = (EGrowthSimulationMode)i;
		AddCuttingPlaneToTimeline(Timeline, Profile, incrTimes());
		blinkAndResetBetweenTests();
	}
}

void FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline(
	FAnimationBinding const& AnimationBinding, FAppearanceProfile const& Profile)
{
	FITwinElementTimeline& ElementTimeline =
		MainTimeline.ElementTimelineFor(AnimationBinding.AnimatedEntityId);
	if (Owner->bDebugWithDummyTimelines)
	{
		CreateTestingTimeline(ElementTimeline);
	}
	else
	{
		AddColorToTimeline(ElementTimeline, Profile, AnimationBinding.TimeRange);
		AddCuttingPlaneToTimeline(ElementTimeline, Profile, AnimationBinding.TimeRange);
		AddVisibilityToTimeline(ElementTimeline, Profile, AnimationBinding.TimeRange);
	}
	if (OnElementTimelineModified) OnElementTimelineModified(ElementTimeline);
}
