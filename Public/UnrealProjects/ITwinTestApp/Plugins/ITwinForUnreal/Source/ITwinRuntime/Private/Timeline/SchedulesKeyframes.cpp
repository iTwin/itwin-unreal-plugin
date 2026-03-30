/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesKeyframes.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesKeyframes.h"
#include "SchedulesConstants.h"
#include "SchedulesStructs.h"

#include <ITwinUtilityLibrary.h>
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex

namespace ITwin::Timeline {

// To avoid random overwrites in the case of exactly adjacent tasks (timewise), use different
// epsilons at start and end of tasks to set the "before" and the "after" states. An alternative
// would be to prioritize the keyframe setting calls (SetColorAt, etc.) depending on whether
// the setting is for before the task, at task start, end or after the end.
#define KF_START_EPSILON KEYFRAME_TIME_EPSILON
#define KF_END_2_EPSILON (2 * KEYFRAME_TIME_EPSILON)
#define KF_END_3_EPSILON (3 * KEYFRAME_TIME_EPSILON)

std::optional<FVector> SelectAppearanceColor(FSimpleAppearance const& DefaultAppearance,
											 FSimpleAppearance const* MaybeForcedAppearance = nullptr)
{
	auto&& Appearance = MaybeForcedAppearance ? (*MaybeForcedAppearance) : DefaultAppearance;
	return Appearance.bUseOriginalColor ? std::nullopt : std::optional<FVector>(Appearance.Color);
}

void AddColorToTimeline(FITwinElementTimeline& ElementTimeline,
	FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time, FTaskDependenciesData const& TaskDeps)
{
	if (Profile.ProfileType == EProfileAction::Neutral) // handled in AddVisibilityToTimeline
	{
		return;
	}
	// Wrong in the case of successive tasks: we could get the same problem with colors as with growth
	// reported there: https://dev.azure.com/bentleycs/e-onsoftware/_workitems/edit/1551970
	//if (Profile.StartAppearance.bUseOriginalColor
	//	&& Profile.ActiveAppearance.Base.bUseOriginalColor
	//	&& Profile.FinishAppearance.bUseOriginalColor)
	//{
	//	return;
	//}
	// Note: ProfileType is already handled in ParseAppearanceProfileDetails so that bUseOriginalColor
	// are correctly set, so no need to test ProfileType here
	auto const ColorBefore = SelectAppearanceColor(Profile.StartAppearance,
												   TaskDeps.ProfileForcedAppearanceBefore);
	auto const StartColor = SelectAppearanceColor(Profile.ActiveAppearance.Base);
	if (ColorBefore != StartColor)
	{
		ElementTimeline.SetColorAt(Time.first - KF_START_EPSILON, ColorBefore, EInterpolation::Step);
	}
	// Since we don't need the epsilon for the following calls to SetColorAt, don't test it here, in case we
	// get extra short tasks but user still expects to see the StartColor when time is exactly Time.first
	bool const bZeroTimeTask = ((Time.second /*- KEYFRAME_TIME_EPSILON*/) <= Time.first);
	auto const ColorAfter = SelectAppearanceColor(Profile.FinishAppearance,
												  TaskDeps.ProfileForcedAppearanceAfter);
	if (bZeroTimeTask)
	{
		ElementTimeline.SetColorAt(Time.first, ColorAfter, EInterpolation::Step);
	}
	else
	{
		// The difference with Visibilities is that there is no no color interpolation (ie no
		// FActiveAppearance::FinishColor, as opposed to FActiveAppearance::FinishAlpha)
		ElementTimeline.SetColorAt(Time.first, StartColor, EInterpolation::Step);
		// In case of exactly adjacent tasks, the larger epsilon avoids random overwrite of the second
		// task's "before" color by the first task's "after" color
		ElementTimeline.SetColorAt(Time.second - KF_END_2_EPSILON, ColorAfter, EInterpolation::Step);
	}
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
		ensure(false);
		break;
	}
	// No, bInvertGrowth only changes the BBox boundary from which we start, not the direction
	return /*Appearance.bInvertGrowth ? (-Orientation) :*/ Orientation;
}

void AddCuttingPlaneToTimeline(FITwinElementTimeline& ElementTimeline, FAppearanceProfile const& Profile,
	FTimeRangeInSeconds const& Time, FITwinCoordConversions const& CoordConv,
	PTransform const* const TransformKeyframe /*= nullptr*/)
{
	auto const& GrowthAppearance = Profile.ActiveAppearance;// all others are ignored...
	if (Profile.ProfileType == EProfileAction::Neutral) // handled in AddVisibilityToTimeline
	{
		return;
	}
	bool const bZeroTimeTask = ((Time.second - KEYFRAME_TIME_EPSILON) <= Time.first);
	if (bZeroTimeTask)
	{
		return; // nothing to do, FullyGrown/FullyRemoved states would be handled by Visibilities already
	}
	if (GrowthAppearance.GrowthSimulationMode == EGrowthSimulationMode::None
		|| GrowthAppearance.GrowthSimulationMode == EGrowthSimulationMode::Unknown)
	{
		// We need this keyframe for (at least) these cases of successive tasks:
		//	* Growth-simulated "Remove" or "Temporary" task A followed by non-growth-simulated task B
		//	  (of any on-Neutral kind): without these, task A's "FullyRemoved" keyframe would also apply
		//	  during task B!
		//	* Non-growth-simulated "Install" or "Maintenance" task A followed by growth-simulated task B of
		//	  kind "Install" or "Temporary": without these, task B's "FullyRemoved" keyframe would also apply
		//	  during task A!
		// From https://dev.azure.com/bentleycs/e-onsoftware/_workitems/edit/1551970
		if (EProfileAction::Remove == Profile.ProfileType
			|| EProfileAction::Maintenance == Profile.ProfileType)
		{
			ElementTimeline.SetCuttingPlaneAt(Time.first, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
		}
		if (EProfileAction::Install == Profile.ProfileType
			|| EProfileAction::Maintenance == Profile.ProfileType)
		{
			ElementTimeline.SetCuttingPlaneAt(Time.second, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
		}
		return;
	}
	FVector PlaneOrientation = CoordConv.IModelToUntransformedIModelInUE
		.TransformVector(GetCuttingPlaneOrientation(GrowthAppearance));
	if (!PlaneOrientation.Normalize())
		return;
	bool const bVisibleOutsideTask = (Profile.ProfileType == EProfileAction::Maintenance);
	// 'bInvertGrowth' is "Simulate as Remove" in SynchroPro, but it is only applicable to Maintenance
	// and Temporary tasks, for which the default growth behaves like Install, and thus needs can a custom
	// flag to be inverted and behave like a Remove action.
	bool const bInvertGrowth = (Profile.ProfileType == EProfileAction::Remove)
		|| (GrowthAppearance.bInvertGrowth && (Profile.ProfileType == EProfileAction::Maintenance
			|| Profile.ProfileType == EProfileAction::Temporary));
	// Regular growth means "building (new or temp) stuff", while inverted growth means "removing"
	// (dismantling) existing/temp stuff. Before new/temp stuff is built, or after existing/temp stuff is
	// removed, visibility is 0 anyway so the cutting plane setting does not matter, thus we only need a
	// 'Step' keyframe when FullyRemoved before (resp. after) the task but the growth status starts
	// (resp. ends) at FullyGrown.
	if (bVisibleOutsideTask && !bInvertGrowth)
	{
		ElementTimeline.SetCuttingPlaneAt(Time.first - KF_START_EPSILON, {}, EGrowthStatus::FullyGrown,
			EInterpolation::Step);
	}
	ElementTimeline.SetCuttingPlaneAt(Time.first, PlaneOrientation,
		bInvertGrowth ? EGrowthStatus::DeferredFullyGrown : EGrowthStatus::DeferredFullyRemoved,
		EInterpolation::Linear, TransformKeyframe);
	// In case of exactly adjacent tasks, the larger epsilon avoids random overwrite of the second task's
	// "FullyGrown" KF by one of the first task's ending keyframes below:
	ElementTimeline.SetCuttingPlaneAt(Time.second - KF_END_3_EPSILON, PlaneOrientation,
		bInvertGrowth ? EGrowthStatus::DeferredFullyRemoved : EGrowthStatus::DeferredFullyGrown,
		EInterpolation::Step, TransformKeyframe);
	if (bVisibleOutsideTask && bInvertGrowth)
	{
		ElementTimeline.SetCuttingPlaneAt(Time.second - KF_END_2_EPSILON, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
	}
}

float SelectAppearanceVisibility(FSimpleAppearance const& DefaultAppearance,
	FSimpleAppearance const* MaybeForcedAppearance, std::optional<bool> const& ProfileForcedVisibility, bool bHidden)
{
	auto&& Appearance = MaybeForcedAppearance ? (*MaybeForcedAppearance) : DefaultAppearance;
	if (ProfileForcedVisibility)
		bHidden = !(*ProfileForcedVisibility);
	return bHidden ? 0.f : (Appearance.bUseOriginalAlpha ? 1.f : Appearance.Alpha);
}

void AddVisibilityToTimeline(FITwinElementTimeline& ElementTimeline,
	FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time, FTaskDependenciesData const& TaskDeps)
{
	if (Profile.ProfileType == EProfileAction::Neutral && TaskDeps.bHasOnlyNeutralTasks)
	{
		/* From SynchroPro documentation: "The Neutral Profile has no effect on the appearance of the assigned
		resources. If the resource is assigned only to task(s) using Neutral profiles, it will never be visible.
		If it is assigned to task(s) after an Install or Maintain task, it will retain the existing colour before,
		during, and after the Neutral task (i.e. the Neutral task has no effect on the object's appearance).
		Therefore, the Neutral Appearance Profile is useful to assign a Resource to a Task for informational
		purposes (e.g. Planned Units) when it is not desired to include it in the 4D simulation." */
		ElementTimeline.SetVisibilityAt(Time.first, 0.f, EInterpolation::Step);
	}
	else
	{
		// DO NOT optimize, KF are needed in the case of successive tasks! (eg. Maintain followed by Install)
		// Was: Every case but 'Maintenance' tasks need a keyframe at some point: start, end or both.
		//if (Profile.ProfileType == EProfileAction::Maintenance
		//	// About the "== 1" tests: is the animation alpha multiplied, or somehow replacing the
		//	// material's "base" opacity? In the latter case, these tests are wrong, and on top of that,
		//	// Features initially rendered with the Translucent material could switch to the Opaque...
		//	&& (Profile.StartAppearance.bUseOriginalAlpha || Profile.StartAppearance.Alpha == 1)
		//	&& (Profile.ActiveAppearance.Base.bUseOriginalAlpha ||
		//		(Profile.ActiveAppearance.Base.Alpha == 1 && Profile.ActiveAppearance.FinishAlpha == 1))
		//	&& (Profile.FinishAppearance.bUseOriginalAlpha || Profile.FinishAppearance.Alpha == 1))
		//{
		//	return;
		//}
		bool const bZeroTimeTask = ((Time.second - KEYFRAME_TIME_EPSILON) <= Time.first);
		float const AlphaBefore = SelectAppearanceVisibility(Profile.StartAppearance,
			TaskDeps.ProfileForcedAppearanceBefore, TaskDeps.ProfileForcedVisibilityBefore,
			EProfileAction::Install == Profile.ProfileType || EProfileAction::Temporary == Profile.ProfileType);
		float const AlphaAfter = SelectAppearanceVisibility(Profile.FinishAppearance,
			TaskDeps.ProfileForcedAppearanceAfter, TaskDeps.ProfileForcedVisibilityAfter,
			EProfileAction::Remove == Profile.ProfileType || EProfileAction::Temporary == Profile.ProfileType);
		if (bZeroTimeTask)
		{
			if (AlphaBefore != AlphaAfter)
			{
				ElementTimeline.SetVisibilityAt(Time.first - KF_END_3_EPSILON, AlphaBefore, EInterpolation::Step);
			}
			ElementTimeline.SetVisibilityAt(Time.second - KF_END_2_EPSILON, AlphaAfter, EInterpolation::Step);
			return;
		}
		float const StartAlpha =
			Profile.ActiveAppearance.Base.bUseOriginalAlpha ? 1.f : Profile.ActiveAppearance.Base.Alpha;
		float const FinishAlpha =
			Profile.ActiveAppearance.Base.bUseOriginalAlpha ? 1.f : Profile.ActiveAppearance.FinishAlpha;
		if (AlphaBefore != StartAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.first - KF_START_EPSILON, AlphaBefore, EInterpolation::Step);
		}
		if (StartAlpha == FinishAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.first, StartAlpha, EInterpolation::Step);
		}
		else
		{
			ElementTimeline.SetVisibilityAt(Time.first, StartAlpha, EInterpolation::Linear);
			ElementTimeline.SetVisibilityAt(Time.second - KF_END_3_EPSILON, FinishAlpha, EInterpolation::Step);
		}
		if (AlphaAfter != FinishAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.second - KF_END_2_EPSILON, AlphaAfter, EInterpolation::Step);
		}
	}
}

void HandleFallbackTransfoOutsideTaskIfNeeded(FITwinElementTimeline& ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps)
{
	double StartTimeToAdd = TaskTimes.first - KF_START_EPSILON;
	double FinishTimeToAdd = TaskTimes.second - KF_END_2_EPSILON;
	if (TaskDeps.bForceDisablingTransformationOutside)
	{
		ElementTimeline.SetTransformationDisabledAt(StartTimeToAdd, EInterpolation::Step);
		ElementTimeline.SetTransformationDisabledAt(FinishTimeToAdd, EInterpolation::Step);
	}
	else if (TaskDeps.ProfileForcedTransfoAssignOutside)
	{
		if (!TaskDeps.ProfileForced3DPathOutside) // must be static then
		{
			auto&& Transform = std::get<0>(TaskDeps.ProfileForcedTransfoAssignOutside->Transformation);
			ElementTimeline.SetTransformationAt(StartTimeToAdd,
				Transform.GetTranslation(), Transform.GetRotation(),
				FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
				EInterpolation::Step);
			ElementTimeline.SetTransformationAt(FinishTimeToAdd,
				Transform.GetTranslation(), Transform.GetRotation(),
				FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
				EInterpolation::Step);
		}
		else
		{
			auto&& PathAssignment = std::get<1>(TaskDeps.ProfileForcedTransfoAssignOutside->Transformation);
			if (ensure(ITwin::INVALID_IDX != PathAssignment.Animation3DPathInVec))
			{
				F3DPathKFData ForcedKF;
				ForcedKF.bFirstOrLastKeyframe = TaskDeps.bProfileForced3DPathOutsideIsAtPathStart;
				if (GetLast3DPathTransformKeyframeToApply(TaskTimes, PathAssignment.TransformAnchor,
														  TaskDeps.ProfileForced3DPathOutside->Keyframes, CoordConv,
														  PathAssignment.b3DPathReverseDirection, ForcedKF))
				{
					ElementTimeline.SetTransformationAt(StartTimeToAdd,
						ForcedKF.ConvertedPosition, ForcedKF.NormalizedRotation,
						ForcedKF.BaseAnchor, EInterpolation::Step);
					ElementTimeline.SetTransformationAt(FinishTimeToAdd,
						ForcedKF.ConvertedPosition, ForcedKF.NormalizedRotation,
						ForcedKF.BaseAnchor, EInterpolation::Step);
				}
			}
		}
	}
}

PTransform const& AddStaticTransformToTimeline(FITwinElementTimeline & ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, FTransform const& Transform, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps)
{
	HandleFallbackTransfoOutsideTaskIfNeeded(ElementTimeline, TaskTimes, CoordConv, TaskDeps);
	// Previously we had this reset for static transfos but not 3D paths, but transfos are not reset at the end of
	// a task unless superceded by a more prioritary transformation (which can be "original transformation")
	//ElementTimeline.SetTransformationDisabledAt(TaskTimes.second, EInterpolation::Step);
	return ElementTimeline.SetTransformationAt(TaskTimes.first,
		// KEEPING iModel-space transform here! I couldn't make static transfos work without hacking directly
		// in FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe... :/
		Transform.GetTranslation(), Transform.GetRotation(),
		FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
		EInterpolation::Step);
}

bool GetLast3DPathTransformKeyframeToApply(FTimeRangeInSeconds const& TaskTimes,
	std::variant<EAnchorPoint, FVector> const& TransformAnchor, std::vector<FTransformKey> const& Keyframes,
	FITwinCoordConversions const& CoordConv, bool const b3DPathReverseDirection,
	F3DPathKFData& KeyframeToApply)
{
	return Add3DPathTransformToTimeline(nullptr, TaskTimes, TransformAnchor, Keyframes,
										CoordConv, b3DPathReverseDirection, {}, &KeyframeToApply);
}

bool Add3DPathTransformToTimeline(FITwinElementTimeline* ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, std::variant<EAnchorPoint, FVector> const& TransformAnchor,
	std::vector<FTransformKey> const& Keyframes, FITwinCoordConversions const& CoordConv,
	bool const b3DPathReverseDirection, FTaskDependenciesData const& TaskDeps,
	F3DPathKFData* pOnlyGetSingleKeyframe/*= nullptr*/)
{
	if (Keyframes.empty())
		return false;
	// Let's keep the possible anterior transformation set:
	//ElementTimeline->SetTransformationDisabledAt(TaskTimes.first, EInterpolation::Step);
	double const TaskDuration = TaskTimes.second - TaskTimes.first;
	FDeferredAnchor BaseAnchor;
	std::visit([&BaseAnchor, &CoordConv](auto&& Var)
		{
			using T = std::decay_t<decltype(Var)>;
			if constexpr (std::is_same_v<T, EAnchorPoint>)
			{
				if (EAnchorPoint::Original != Var)
				{
					BaseAnchor.bDeferred = true;
					BaseAnchor.AnchorPoint = Var;
				}
			}
			else if constexpr (std::is_same_v<T, FVector>)
			{
				BaseAnchor.bDeferred = false;
				BaseAnchor.AnchorPoint = EAnchorPoint::Custom;
				BaseAnchor.Offset = CoordConv.IModelToUnreal.TransformVector(Var);
			}
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		TransformAnchor);
	// TODO_GCO: there are other cases of nilpotent paths, but harder to detect since alignments other than
	// 'Original' need to compare the Translation component of the Transform to the right bounding box value
	// (center for Center alignment, etc.)
	if (EAnchorPoint::Original == BaseAnchor.AnchorPoint
		&& Keyframes.end() == std::find_if(Keyframes.begin(), Keyframes.end(),
			[&First = Keyframes.begin()->Transform](FTransformKey const& K) {
				return !FITwinMathExts::StrictlyEqualTransforms(K.Transform, First); }))
	{
		// Corner case of a nilpotent 3D path (azdev#1608204)
		return false;
	}
	bool const bRelativeKFPositions = (EAnchorPoint::Original == BaseAnchor.AnchorPoint);
	FVector FirstPos = FVector::Zero();
	double LastKFRelativeTime = 0.;
	auto&& prepKF = [&CoordConv, &FirstPos, &BaseAnchor, bRelativeKFPositions]
		(FTransform const& Transform, bool bFirstOrLastKeyframe)
		{
			FVector RotAxis; double Angle;
			FQuat KeyRot = Transform.GetRotation();
			KeyRot.Normalize();
			KeyRot.ToAxisAndAngle(RotAxis, Angle);
			RotAxis = CoordConv.IModelToUnreal.TransformVector(RotAxis);
			RotAxis.Normalize();
			return F3DPathKFData{
				bRelativeKFPositions
					? CoordConv.IModelToUnreal.TransformVector(Transform.GetTranslation() - FirstPos)
					: CoordConv.IModelToUnreal.TransformPosition(Transform.GetTranslation())
				, FQuat(RotAxis, Angle)
				, BaseAnchor
				, bFirstOrLastKeyframe
			};
		};
	auto FirstKF = std::min_element(Keyframes.begin(), Keyframes.end());
	auto LastKF  = std::max_element(Keyframes.begin(), Keyframes.end());
	if (bRelativeKFPositions)
	{
		// Do not consider b3DPathReverseDirection here: the Element's position in the iModel is apparently the
		// position at the start of the path, (as witnessed in azdev#1625066's project when the animation is Stop'd)
		// even if it is to be applied in reverse, so offsets must always be computed from this position.
		FirstPos = FirstKF->Transform.GetLocation();
	}
	if (b3DPathReverseDirection)
	{
		std::swap(FirstKF, LastKF);
	}
	if (pOnlyGetSingleKeyframe)
	{
		*pOnlyGetSingleKeyframe = prepKF(
			pOnlyGetSingleKeyframe->bFirstOrLastKeyframe ? FirstKF->Transform : LastKF->Transform,
			pOnlyGetSingleKeyframe->bFirstOrLastKeyframe);
		return true;
	}
	LastKFRelativeTime = LastKF->RelativeTime;
	// Shouldn't reach this when called from GetLast3DPathTransformKeyframeToApply
	ensure(ElementTimeline && !pOnlyGetSingleKeyframe);
	for (auto&& Key : Keyframes)
	{
		auto&& KFData = prepKF(Key.Transform, false/*ignored*/);
		bool const bIsLastKF = (Key.RelativeTime == LastKFRelativeTime);
		// Note: SetTransformationAt will order keyframes by their time point, whatever their ordering in
		// the input vector
		double const ActualRelativeTime =
			(b3DPathReverseDirection ? (1. - Key.RelativeTime) : Key.RelativeTime)
			// Prevent one KF overwriting another one when a resource is assigned to successive 3D paths
			// on exactly adjacent tasks (time-wise)
			// (noticed while investigating https://github.com/iTwin/itwin-unreal-plugin/issues/89)
			-(bIsLastKF ? KF_END_3_EPSILON : 0.);
		ElementTimeline->SetTransformationAt(TaskTimes.first + ActualRelativeTime * TaskDuration,
			KFData.ConvertedPosition, KFData.NormalizedRotation, BaseAnchor,
			// "Step": ie. do not interpolate linearly between successive paths!
			// (ADO#1979908 = https://github.com/iTwin/itwin-unreal-plugin/issues/89)
			bIsLastKF ? EInterpolation::Step : EInterpolation::Linear);
	}
	HandleFallbackTransfoOutsideTaskIfNeeded(*ElementTimeline, TaskTimes, CoordConv, TaskDeps);
	return false;
}

void CreateTestingTimeline(FITwinElementTimeline& Timeline, FITwinCoordConversions const& CoordConv)
{
	constexpr double delta = 1000. * KEYFRAME_TIME_EPSILON;

	// Initial conditions, to not depend on the first keyframe of each feature, which can be much farther
	// along the timeline
	Timeline.SetColorAt(0., std::nullopt/*ie. bUseOriginalColor*/, EInterpolation::Step);
	Timeline.SetVisibilityAt(0., 1.f, EInterpolation::Step);
	Timeline.SetCuttingPlaneAt(0., {}, EGrowthStatus::FullyGrown, EInterpolation::Step);

	// tests occur every 4 deltas: one before task, one for task duration, one after task, one for blink
	constexpr int cycle = 4;
	FTimeRangeInSeconds TimeRange = { -(cycle - 1) * delta, 0. };
	size_t Idx = 0;
	auto const incrTimes = [&TimeRange, &cycle, &delta]() -> FTimeRangeInSeconds const&
		{
			TimeRange.first += cycle * delta;
			TimeRange.second = TimeRange.first + delta;
			return TimeRange;
		};
	FAppearanceProfile Profile;
	Profile.ProfileType = EProfileAction::Maintenance;

	auto const blinkAndResetBetweenTests = [&Timeline, &TimeRange, delta]()
		{
			double const BlinkStart = TimeRange.second + delta;
			// "Blink" the Element
			Timeline.SetVisibilityAt(BlinkStart - KEYFRAME_TIME_EPSILON, 1, EInterpolation::Step);
			Timeline.SetVisibilityAt(BlinkStart, 0.f, EInterpolation::Step);
			// End blink and instruct to use the next keyframes' values, if any, otherwise reset values
			Timeline.SetVisibilityAt(BlinkStart + delta, 1.f, EInterpolation::Next);
			Timeline.SetColorAt(BlinkStart + delta, std::nullopt, EInterpolation::Next);
			Timeline.SetCuttingPlaneAt(BlinkStart + delta, {}, EGrowthStatus::FullyGrown, EInterpolation::Next);
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
			AddColorToTimeline(Timeline, Profile, incrTimes(), FTaskDependenciesData{});
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

			AddVisibilityToTimeline(Timeline, Profile, incrTimes(), FTaskDependenciesData{});
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
		AddCuttingPlaneToTimeline(Timeline, Profile, incrTimes(), CoordConv);
		blinkAndResetBetweenTests();
	}
	Profile.ActiveAppearance.bInvertGrowth = true;
	for (uint8_t i = 0; i <= (int)EGrowthSimulationMode::Custom; ++i)
	{
		Profile.ActiveAppearance.GrowthSimulationMode = (EGrowthSimulationMode)i;
		AddCuttingPlaneToTimeline(Timeline, Profile, incrTimes(), CoordConv);
		blinkAndResetBetweenTests();
	}
}

} // namespace ITwin::Timeline
