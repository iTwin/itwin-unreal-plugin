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
#include "GrowthStatus.h"

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
	if (bZeroTimeTask
		|| GrowthAppearance.GrowthSimulationMode == EGrowthSimulationMode::None
		|| GrowthAppearance.GrowthSimulationMode == EGrowthSimulationMode::Unknown)
	{
		// We need this keyframe in case the task is preceded or followed by a growth-simulated task which
		// "FullyRemove" state would also apply and thus needs to be contradicted here. We could probably optimize out
		// some cases especially since task dependencies already handle many of them, but it's hardly possible to test
		// them all (blame here and you'll see this is not the first version of this fix...) so I'd rather keep it for
		// all non-growth tasks as a safety...
		// From https://dev.azure.com/bentleycs/e-onsoftware/_workitems/edit/1551970
		ElementTimeline.SetCuttingPlaneAt(Time.first, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
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

std::optional<float> SelectAppearanceVisibility(FSimpleAppearance const& DefaultAppearance,
	FSimpleAppearance const* MaybeForcedAppearance, std::optional<bool> const& ProfileForcedVisibility, bool bHidden)
{
	auto&& Appearance = MaybeForcedAppearance ? (*MaybeForcedAppearance) : DefaultAppearance;
	if (ProfileForcedVisibility)
		bHidden = !(*ProfileForcedVisibility);
	return bHidden ? std::optional<float>(0.f)
				   : (Appearance.bUseOriginalAlpha ? std::optional<float>() : std::optional<float>(Appearance.Alpha));
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
		auto const AlphaBefore = SelectAppearanceVisibility(Profile.StartAppearance,
			TaskDeps.ProfileForcedAppearanceBefore, TaskDeps.ProfileForcedVisibilityBefore,
			EProfileAction::Install == Profile.ProfileType || EProfileAction::Temporary == Profile.ProfileType);
		auto const AlphaAfter = SelectAppearanceVisibility(Profile.FinishAppearance,
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
		auto const StartAlpha = Profile.ActiveAppearance.Base.bUseOriginalAlpha
			? std::optional<float>() : std::optional<float>(Profile.ActiveAppearance.Base.Alpha);
		auto const FinishAlpha = Profile.ActiveAppearance.Base.bUseOriginalAlpha
			? std::optional<float>() : std::optional<float>(Profile.ActiveAppearance.FinishAlpha);
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
	else
	{
		if (TaskDeps.ProfileForcedStaticTransfoAssignOutside
			&& !TaskDeps.ProfileForcedPathTransfoAssignOutside)
		{
			auto&& Transform = TaskDeps.ProfileForcedStaticTransfoAssignOutside->Transform;
			ElementTimeline.SetTransformationAt(StartTimeToAdd,
				Transform.GetTranslation(), Transform.GetRotation(),
				FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
				EInterpolation::Step);
			ElementTimeline.SetTransformationAt(FinishTimeToAdd,
				Transform.GetTranslation(), Transform.GetRotation(),
				FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
				EInterpolation::Step);
		}
		else if (TaskDeps.ProfileForcedPathTransfoAssignOutside && ensure(TaskDeps.ProfileForced3DPathOutside))
		{
			F3DPathKFData ForcedKF;
			ForcedKF.bFirstOrLastKeyframe = TaskDeps.bProfileForced3DPathOutsideIsAtPathStart;
			if (GetLast3DPathTransformKeyframeToApply(TaskTimes,
				// Actually ignored, see comment in header
				/*TaskDeps.ProfileForcedStaticTransfoAssignOutside
					? (&TaskDeps.ProfileForcedStaticTransfoAssignOutside->Transform) : */nullptr,
				*TaskDeps.ProfileForcedPathTransfoAssignOutside,
					TaskDeps.ProfileForced3DPathOutside->Keyframes, CoordConv, ForcedKF))
			{
				ElementTimeline.SetTransformationAt(StartTimeToAdd,
					ForcedKF.ConvertedPosition, ForcedKF.NormalizedRotation,
					ForcedKF.BaseAnchor, EInterpolation::Step);
				ElementTimeline.SetTransformationAt(FinishTimeToAdd,
					ForcedKF.ConvertedPosition, ForcedKF.NormalizedRotation,
					ForcedKF.BaseAnchor, EInterpolation::Step);
			}
		}
		// else: no transfo of any kind
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
	FTransform const* /*StaticTransform*/, FPathTransformAssignment const& PathAssignment,
	std::vector<FTransformKey> const& Keyframes, FITwinCoordConversions const& CoordConv,
	F3DPathKFData& KeyframeToApply)
{
	return Add3DPathTransformToTimeline(nullptr, TaskTimes, nullptr/*StaticTransform, see comment in header*/,
										PathAssignment, Keyframes, CoordConv, {}, &KeyframeToApply);
}

bool Add3DPathTransformToTimeline(FITwinElementTimeline* ElementTimeline, FTimeRangeInSeconds const& TaskTimes,
	FTransform const* /*StaticTransform*/, FPathTransformAssignment const& PathAssignment,
	std::vector<FTransformKey> const& OriginalKeyframes, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps, F3DPathKFData* pOnlyGetSingleKeyframe/*= nullptr*/)
{
	if (OriginalKeyframes.empty())
		return false;
	std::optional<std::vector<FTransformKey>> StretchedKeyFrames;
	auto const* pKeyframes = &OriginalKeyframes;
	if (PathAssignment.MotionStart > 0. || PathAssignment.MotionEnd < 1.
		 && ensure(PathAssignment.MotionStart <= PathAssignment.MotionEnd))
	{
		StretchedKeyFrames.emplace(OriginalKeyframes);
		std::sort(StretchedKeyFrames->begin(), StretchedKeyFrames->end());
		double const LowBoundary = std::clamp(
			PathAssignment.b3DPathReverseDirection ? (1. - PathAssignment.MotionEnd) : PathAssignment.MotionStart,
			0., 1.);
		double const HighBoundary = std::clamp(
			PathAssignment.b3DPathReverseDirection ? (1. - PathAssignment.MotionStart) : PathAssignment.MotionEnd,
			0., 1.);
		// Crop the start of the 3D path, interpolating as needed
		auto LowIt = std::lower_bound(StretchedKeyFrames->begin(), StretchedKeyFrames->end(),
									  FTransformKey{ .RelativeTime = LowBoundary });
		// If the 3D path actually stops _before_ LowBoundary, just keep the last keyframe
		if (LowIt == StretchedKeyFrames->end())
			LowIt = std::prev(StretchedKeyFrames->end());
		// Lower bound is the 1st element >= LowBoundary, so we may need the previous one to interpolate
		if (LowIt->RelativeTime != LowBoundary
			&& LowIt != StretchedKeyFrames->begin() // First keyframe might(?) be > LowBoundary
			&& LowIt != StretchedKeyFrames->end())
		{
			auto PrevIt = std::prev(LowIt);
			// Overwrite PrevIt by interpolating PrevIt and LowIt linearly at a RelativeTime equal to
			// LowBoundary, then erase all keyframes before PrevIt
			PrevIt->Transform.BlendWith(LowIt->Transform,
				// Divider not zero because lower bound is the _first_ >= LowBoundary
				(LowBoundary - PrevIt->RelativeTime) / (LowIt->RelativeTime - PrevIt->RelativeTime));
			PrevIt->RelativeTime = LowBoundary;
			LowIt = PrevIt; // PrevIt is our new boundary, exclude it from erasal
		}
		if (LowIt != StretchedKeyFrames->begin())
			StretchedKeyFrames->erase(StretchedKeyFrames->begin(), LowIt);

		// Crop the end of the 3D path, interpolating as needed
		auto HighIt = std::upper_bound(StretchedKeyFrames->begin(), StretchedKeyFrames->end(),
									   FTransformKey{ .RelativeTime = HighBoundary });
		if (HighIt != StretchedKeyFrames->end()) // if 3D path actually stops _before_ HighBoundary, nothing to do
		{
			if (HighIt == StretchedKeyFrames->begin()) // if 3D path starts _after_, keep only the first KF
			{
				++HighIt;
			}
			else
			{
				// Upper bound is the 1st element > HighBoundary, so we need to interpolate with the previous one
				// (or use the previous one itself if exactly at HighBoundary)
				auto PrevIt = std::prev(HighIt);
				if (PrevIt->RelativeTime != HighBoundary)
				{
					// Overwrite PrevIt by interpolating PrevIt and HighIt linearly at a RelativeTime equal to
					// HighBoundary, then erase all keyframes after and including HighIt
					HighIt->Transform.Blend(PrevIt->Transform, HighIt->Transform,
						// Divider not zero because upper bound is the _first_ > HighBoundary
						(HighBoundary - PrevIt->RelativeTime) / (HighIt->RelativeTime - PrevIt->RelativeTime));
					HighIt->RelativeTime = HighBoundary;
					++HighIt; // HighIt is our new boundary, exclude it from erasal
				}
				// else: PrevIt is our new boundary, just erase from HighIt included until the end
			}
			if (HighIt != StretchedKeyFrames->end())
				StretchedKeyFrames->erase(HighIt, StretchedKeyFrames->end());
		}
		double const TimeRangeToStretch = HighBoundary - LowBoundary;
		double const Ratio = (TimeRangeToStretch != 0.) ? (1. / TimeRangeToStretch) : 0.;
		for (auto& StretchedKF : (*StretchedKeyFrames))
			StretchedKF.RelativeTime = Ratio * (StretchedKF.RelativeTime - LowBoundary);
		pKeyframes = &(*StretchedKeyFrames);
	}
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
		PathAssignment.TransformAnchor);
	// TODO_GCO: there are other cases of nilpotent paths, but harder to detect since alignments other than
	// 'Original' need to compare the Translation component of the Transform to the right bounding box value
	// (center for Center alignment, etc.)
	if (EAnchorPoint::Original == BaseAnchor.AnchorPoint
		&& pKeyframes->end() == std::find_if(pKeyframes->begin(), pKeyframes->end(),
			[&First = pKeyframes->begin()->Transform](FTransformKey const& K) {
				return !FITwinMathExts::StrictlyEqualTransforms(K.Transform, First); }))
	{
		// Corner case of a nilpotent 3D path (azdev#1608204)
		return false;
	}
	bool const bRelativeKFPositions = (EAnchorPoint::Original == BaseAnchor.AnchorPoint);
	FVector PathRefPos = FVector::Zero();
	auto&& prepKF = [&CoordConv, &PathRefPos, &BaseAnchor, bRelativeKFPositions]
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
					? CoordConv.IModelToUnreal.TransformVector(Transform.GetTranslation() - PathRefPos)
					: CoordConv.IModelToUnreal.TransformPosition(Transform.GetTranslation())
				, FQuat(RotAxis, Angle)
				, BaseAnchor
				, bFirstOrLastKeyframe
			};
		};
	if (bRelativeKFPositions)
	{
		// Do not consider b3DPathReverseDirection here: the Element's position is apparently the position
		// at the start of the (full) path, (as witnessed in azdev#1625066's project when the animation is Stop'd)
		// even if it is to be applied in reverse, so offsets must always be computed from this position.
		// This reference position is also _not_ affected by MotionStart/MotionEnd.
		PathRefPos = std::min_element(OriginalKeyframes.begin(), OriginalKeyframes.end())->Transform.GetLocation();
	}
	if (pOnlyGetSingleKeyframe)
	{
		bool const bNeedMinTimeKF =
			pOnlyGetSingleKeyframe->bFirstOrLastKeyframe ^ PathAssignment.b3DPathReverseDirection;
		*pOnlyGetSingleKeyframe = prepKF(
			bNeedMinTimeKF
				? std::min_element(pKeyframes->begin(), pKeyframes->end())->Transform
				: std::max_element(pKeyframes->begin(), pKeyframes->end())->Transform,
			pOnlyGetSingleKeyframe->bFirstOrLastKeyframe);
		return true;
	}
	double LastKFRelativeTime = PathAssignment.b3DPathReverseDirection
		? std::min_element(pKeyframes->begin(), pKeyframes->end())->RelativeTime
		: std::max_element(pKeyframes->begin(), pKeyframes->end())->RelativeTime;
	// Shouldn't reach this when called from GetLast3DPathTransformKeyframeToApply
	ensure(ElementTimeline && !pOnlyGetSingleKeyframe);
	for (auto&& Key : (*pKeyframes))
	{
		auto&& KFData = prepKF(Key.Transform, false/*ignored*/);
		bool const bIsLastKF = (Key.RelativeTime == LastKFRelativeTime);
		// Note: SetTransformationAt will order keyframes by date, whatever their ordering in the input vector
		double const ActualRelativeTime =
			(PathAssignment.b3DPathReverseDirection ? (1. - Key.RelativeTime) : Key.RelativeTime);
		ElementTimeline->SetTransformationAt(
			TaskTimes.first + ActualRelativeTime * TaskDuration
				// Prevent one KF overwriting another one when a resource is assigned to successive 3D paths
				// on exactly adjacent tasks (time-wise)
				// (noticed while investigating https://github.com/iTwin/itwin-unreal-plugin/issues/89)
				- (bIsLastKF ? KF_END_3_EPSILON : 0.),
			KFData.ConvertedPosition, KFData.NormalizedRotation, BaseAnchor,
			// "Step": ie. do not interpolate linearly between successive paths!
			// (ADO#1979908 = https://github.com/iTwin/itwin-unreal-plugin/issues/89)
			bIsLastKF ? EInterpolation::Step : EInterpolation::Linear);
	}
	HandleFallbackTransfoOutsideTaskIfNeeded(*ElementTimeline, TaskTimes, CoordConv, TaskDeps);
	return false;
}

// REMOVED blame here:
// void CreateTestingTimeline(FITwinElementTimeline& Timeline, FITwinCoordConversions const& CoordConv)

} // namespace ITwin::Timeline
