/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSynchro4DSchedulesTimelineBuilder.h"
#include "ITwinIModel.h"
#include "ITwinIModelInternals.h"
#include "ITwinSceneMapping.h"
#include "ITwinSynchro4DSchedules.h"
#include "ITwinSynchro4DSchedulesInternals.h"
#include "ITwinUtilityLibrary.h"
#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
#include <Compil/AfterNonUnrealIncludes.h>
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex
#include <Timeline/Timeline.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesStructs.h>

#include <JsonObjectConverter.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>

#include <algorithm>

void AddColorToTimeline(FITwinElementTimeline& ElementTimeline,
						FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time)
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
	using namespace ITwin::Timeline;
	auto const ColorBefore = Profile.StartAppearance.bUseOriginalColor
		? std::nullopt : std::optional<FVector>(Profile.StartAppearance.Color);
	auto const StartColor = Profile.ActiveAppearance.Base.bUseOriginalColor
		? std::nullopt : std::optional<FVector>(Profile.ActiveAppearance.Base.Color);
	if (ColorBefore != StartColor)
	{
		ElementTimeline.SetColorAt(Time.first - KEYFRAME_TIME_EPSILON, ColorBefore, EInterpolation::Step);
	}
	// Since we don't need the epsilon for the following calls to SetColorAt, don't test it here, in case we
	// get extra short tasks but user still expects to see the StartColor when time is exactly Time.first
	bool const bZeroTimeTask = ((Time.second /*- KEYFRAME_TIME_EPSILON*/) <= Time.first);
	auto const ColorAfter = Profile.FinishAppearance.bUseOriginalColor
		? std::nullopt : std::optional<FVector>(Profile.FinishAppearance.Color);
	if (bZeroTimeTask)
	{
		ElementTimeline.SetColorAt(Time.first, ColorAfter, EInterpolation::Step);
	}
	else
	{
		// The difference with Visibilities is that there is no FinishColor (no color interp)
		ElementTimeline.SetColorAt(Time.first, StartColor, EInterpolation::Step);
		ElementTimeline.SetColorAt(Time.second, ColorAfter, EInterpolation::Step);
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
	ITwin::Timeline::PTransform const* const TransformKeyframe = nullptr)
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
	using namespace ITwin::Timeline;
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
		ElementTimeline.SetCuttingPlaneAt(Time.first, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
	}
	ElementTimeline.SetCuttingPlaneAt(Time.first + KEYFRAME_TIME_EPSILON, PlaneOrientation,
		bInvertGrowth ? EGrowthStatus::DeferredFullyGrown : EGrowthStatus::DeferredFullyRemoved,
		EInterpolation::Linear, TransformKeyframe);
	ElementTimeline.SetCuttingPlaneAt(Time.second - KEYFRAME_TIME_EPSILON, PlaneOrientation,
		bInvertGrowth ? EGrowthStatus::DeferredFullyRemoved : EGrowthStatus::DeferredFullyGrown,
		EInterpolation::Step, TransformKeyframe);
	if (bVisibleOutsideTask && bInvertGrowth)
	{
		ElementTimeline.SetCuttingPlaneAt(Time.second, {}, EGrowthStatus::FullyGrown, EInterpolation::Step);
	}
}
	
void AddVisibilityToTimeline(FITwinElementTimeline& ElementTimeline,
							 FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time)
{
	using namespace ITwin::Timeline;
	if (Profile.ProfileType == EProfileAction::Neutral)
	{
		// "Neutral" means "neutralize", ie. the Element is hidden the whole time!
		ElementTimeline.SetVisibilityAt(Time.first, 0.f, EInterpolation::Step);
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
		bool const bZeroTimeTask = ((Time.second - KEYFRAME_TIME_EPSILON) <= Time.first);
		float const AlphaBefore =
			(EProfileAction::Install == Profile.ProfileType
				|| EProfileAction::Temporary == Profile.ProfileType) ? 0.f
			: (Profile.StartAppearance.bUseOriginalAlpha ? 1.f : Profile.StartAppearance.Alpha);
		float const AlphaAfter =
			(EProfileAction::Remove == Profile.ProfileType
				|| EProfileAction::Temporary == Profile.ProfileType) ? 0.f
			: (Profile.FinishAppearance.bUseOriginalAlpha ? 1.f : Profile.FinishAppearance.Alpha);
		if (bZeroTimeTask)
		{
			if (AlphaBefore != AlphaAfter)
			{
				ElementTimeline.SetVisibilityAt(Time.first - KEYFRAME_TIME_EPSILON,
												AlphaBefore, EInterpolation::Step);
			}
			ElementTimeline.SetVisibilityAt(Time.second, AlphaAfter, EInterpolation::Step);
			return;
		}
		float const StartAlpha =
			Profile.ActiveAppearance.Base.bUseOriginalAlpha ? 1.f : Profile.ActiveAppearance.Base.Alpha;
		float const FinishAlpha =
			Profile.ActiveAppearance.Base.bUseOriginalAlpha ? 1.f : Profile.ActiveAppearance.FinishAlpha;
		if (AlphaBefore != StartAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.first - KEYFRAME_TIME_EPSILON,
											AlphaBefore, EInterpolation::Step);
		}
		if (StartAlpha == FinishAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.first, StartAlpha, EInterpolation::Step);
		}
		else
		{
			ElementTimeline.SetVisibilityAt(Time.first, StartAlpha, EInterpolation::Linear);
			ElementTimeline.SetVisibilityAt(Time.second - KEYFRAME_TIME_EPSILON,
											FinishAlpha, EInterpolation::Step);
		}
		if (AlphaAfter != FinishAlpha)
		{
			ElementTimeline.SetVisibilityAt(Time.second, AlphaAfter, EInterpolation::Step);
		}
	}
}

ITwin::Timeline::PTransform const& AddStaticTransformToTimeline(FITwinElementTimeline& ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, FTransform const& Transform, FITwinCoordConversions const&)
{
	using namespace ITwin::Timeline;
	ElementTimeline.SetTransformationDisabledAt(TaskTimes.second, EInterpolation::Step);
	// Let's keep the possible anterior transformation set:
	//ElementTimeline.SetTransformationDisabledAt(TaskTimes.first, EInterpolation::Step);
	return ElementTimeline.SetTransformationAt(TaskTimes.first /*+ KEYFRAME_TIME_EPSILON*/,
		// KEEPING iModel-space transform here! I couldn't make static transfos work without hacking directly
		// in FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe... :/
		Transform.GetTranslation(), Transform.GetRotation(),
		FDeferredAnchor{ EAnchorPoint::Static, false, FVector::ZeroVector },
		EInterpolation::Step);
}

void Add3DPathTransformToTimeline(FITwinElementTimeline& ElementTimeline, FTimeRangeInSeconds const& TaskTimes,
	std::variant<ITwin::Timeline::EAnchorPoint, FVector> const& TransformAnchor,
	std::vector<FTransformKey> const& Keyframes, FITwinCoordConversions const& CoordConv,
	bool const b3DPathReverseDirection)
{
	if (Keyframes.empty())
		return;
	using namespace ITwin::Timeline;
	// Let's keep the possible anterior transformation set:
	//ElementTimeline.SetTransformationDisabledAt(TaskTimes.first, EInterpolation::Step);
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
	bool const bRelativeKFPositions = (ITwin::Timeline::EAnchorPoint::Original == BaseAnchor.AnchorPoint);
	FVector FirstPos = FVector::Zero();
	double LastKFRelativeTime = 0.;
	if (bRelativeKFPositions)
	{
		// No: the Element's position in the iModel is apparently the position at the start of the path,
		// (as witnessed in azdev#1625066's project when the animation is Stop'd) even if it is to be
		// applied in reverse, so offsets must always be computed from this position.
		//if (b3DPathReverseDirection)
		//	FirstPos = std::max_element(Keyframes.begin(), Keyframes.end())->Transform.GetLocation();
		//else
		auto const FirstKF = std::min_element(Keyframes.begin(), Keyframes.end());
		FirstPos = FirstKF->Transform.GetLocation();
		if (b3DPathReverseDirection)
			LastKFRelativeTime = FirstKF->RelativeTime;
		else
			LastKFRelativeTime = std::max_element(Keyframes.begin(), Keyframes.end())->RelativeTime;
	}
	else
	{
		LastKFRelativeTime = b3DPathReverseDirection
			? std::min_element(Keyframes.begin(), Keyframes.end())->RelativeTime
			: std::max_element(Keyframes.begin(), Keyframes.end())->RelativeTime;
	}
	// TODO_GCO: there are other cases of nilpotent paths, but harder to detect since alignments other than
	// 'Original' need to compare the Translation component of the Transform to the right bounding box value
	// (center for Center alignment, etc.)
	if (ITwin::Timeline::EAnchorPoint::Original == BaseAnchor.AnchorPoint
		&& Keyframes.end() == std::find_if(Keyframes.begin(), Keyframes.end(),
			[&First=Keyframes.begin()->Transform](FTransformKey const& K) {
				return !FITwinMathExts::StrictlyEqualTransforms(K.Transform, First); }))
	{
		// Corner case of a nilpotent 3D path (azdev#1608204)
		return;
	}
	for (auto&& Key : Keyframes)
	{
		FVector RotAxis; double Angle;
		FQuat KeyRot = Key.Transform.GetRotation();
		KeyRot.Normalize();
		KeyRot.ToAxisAndAngle(RotAxis, Angle);
		RotAxis = CoordConv.IModelToUnreal.TransformVector(RotAxis);
		RotAxis.Normalize();
		bool const bIsLastKF = (Key.RelativeTime == LastKFRelativeTime);
		// Note: SetTransformationAt will order keyframes by their time point, whatever their ordering in
		// the input vector
		double const ActualRelativeTime =
			(b3DPathReverseDirection ? (1. - Key.RelativeTime) : Key.RelativeTime)
			// Prevent one KF overwriting another one when a resource is assigned to successive 3D paths
			// on exactly adjacent tasks (time-wise)
			// (noticed while investigating https://github.com/iTwin/itwin-unreal-plugin/issues/89)
			- (bIsLastKF ? KEYFRAME_TIME_EPSILON : 0.);
		ElementTimeline.SetTransformationAt(TaskTimes.first + ActualRelativeTime * TaskDuration,
			bRelativeKFPositions
				? CoordConv.IModelToUnreal.TransformVector(Key.Transform.GetTranslation() - FirstPos)
				: CoordConv.IModelToUnreal.TransformPosition(Key.Transform.GetTranslation()),
			FQuat(RotAxis, Angle), BaseAnchor,
			// "Step": ie. do not interpolate linearly between successive paths!
			// (ADO#1979908 = https://github.com/iTwin/itwin-unreal-plugin/issues/89)
			bIsLastKF ? EInterpolation::Step : EInterpolation::Linear);
	}
}

void CreateTestingTimeline(FITwinElementTimeline& Timeline, FITwinCoordConversions const& CoordConv)
{
	constexpr double delta = 1000. * KEYFRAME_TIME_EPSILON;

	// Initial conditions, to not depend on the first keyframe of each feature, which can be much farther
	// along the timeline
	using namespace ITwin::Timeline;
	Timeline.SetColorAt(0., std::nullopt/*ie. bUseOriginalColor*/, EInterpolation::Step);
	Timeline.SetVisibilityAt(0., 1.f, EInterpolation::Step);
	Timeline.SetCuttingPlaneAt(0., {}, EGrowthStatus::FullyGrown, EInterpolation::Step);

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

namespace Detail {

template<typename ElemDesignationContainer>
void InsertAnimatedMeshSubElemsRecursively(FIModelElementsKey const& AnimationKey,
	FITwinSceneMapping& Scene, ElemDesignationContainer const& Elements,
	FITwinScheduleTimeline& MainTimeline, FElementsGroup& OutSet,
	bool const bPrefetchAllElementAnimationBindings,
	std::vector<ITwinElementID>* OutElemsDiff = nullptr)
{
	for (auto const ElementDesignation : Elements)
	{
		FITwinElement* pElem;
		// NOT safe to use ElementForSLOW here: we may be in a worker thread!
		if constexpr (std::is_same_v<ITwinElementID, typename ElemDesignationContainer::value_type>)
		{
			pElem = Scene.GetElementForSLOW(ElementDesignation);
			if (!pElem)
				continue; // Element not known after querying metadata! (azdev#1704016)
		}
		else
			pElem = &Scene.ElementFor(ElementDesignation);
		FITwinElement& Elem = *pElem;
		if (Elem.AnimationKeys.end()
			== std::find(Elem.AnimationKeys.begin(), Elem.AnimationKeys.end(), AnimationKey))
		{
			Elem.AnimationKeys.push_back(AnimationKey);
		}
		// When pre-fetching bindings, no Element has been received yet... It's annoying to have to add the
		// Elements to their timeline(s) only once some geometry has been received for them :/
		// On the other hand adding intermediate non-leaves Elements has some cost later on when iterating
		// on a timeline's AnimatedMeshElements: let's assume only leave nodes have meshes? TODO_GCO
		if ((bPrefetchAllElementAnimationBindings && Elem.SubElemsInVec.empty()) || Elem.bHasMesh)
		{
			if (!OutSet.insert(Elem.ElementID).second)
				continue; // already in set: no need for RemoveNonAnimatedDuplicate nor recursion
			else
			{
				MainTimeline.RemoveNonAnimatedDuplicate(Elem.ElementID);
				if (OutElemsDiff)
					OutElemsDiff->push_back(Elem.ElementID);
			}
		}
		Detail::InsertAnimatedMeshSubElemsRecursively(AnimationKey, Scene, Elem.SubElemsInVec, MainTimeline,
			OutSet, bPrefetchAllElementAnimationBindings, OutElemsDiff);
	}
}

template<typename ContainerToHandle>
void HideNonAnimatedDuplicates(FITwinSceneMapping const& Scene, ContainerToHandle const& ElemIDs,
							   FITwinScheduleTimeline& MainTimeline)
{
	for (ITwinElementID ElemID : ElemIDs)
	{
		auto const& Duplicates = Scene.GetDuplicateElements(ElemID);
		bool bOneIsAnimated = false;
		for (auto Dupl : Duplicates)
		{
			auto const& Elem = Scene.GetElement(Dupl);
			if (!Elem.AnimationKeys.empty())
			{
				bOneIsAnimated = true;
				break;
			}
		}
		if (!bOneIsAnimated)
			continue;
		for (auto Dupl : Duplicates)
		{
			auto const& Elem = Scene.GetElement(Dupl);
			if (Elem.AnimationKeys.empty())
			{
				MainTimeline.AddNonAnimatedDuplicate(Elem.ElementID);
			}
		}
	}
}

} // ns Detail

bool FITwinScheduleTimelineBuilder::IsUnitTesting() const
{
	return nullptr == Owner;
}

void FITwinScheduleTimelineBuilder::UpdateAnimationGroupInTimeline(size_t const GroupIndex,
	FElementsGroup const& GroupElements, FSchedLock&)
{
	FITwinElementTimeline* Timeline = MainTimeline.GetElementTimelineFor(FIModelElementsKey(GroupIndex));
	if (Timeline) // group may be used by bindings not yet notified, so the case !Timeline is perfectly fine
	{
		// until we load iModel metadata for it, Unit Testing can only support "flat" iModels
		// and no SourceID-duplicates!
		if (IsUnitTesting())
		{
			Timeline->IModelElementsRef() = GroupElements; // the whole updated group is passed
			Timeline->OnIModelElementsAdded(); // just invalidates group's BBox
		}
		else
		{
			AITwinIModel* IModel = Cast<AITwinIModel>(Owner->GetOwner());
			if (!ensure(IModel))
				return;
			FITwinSceneMapping& Scene = GetInternals(*IModel).SceneMapping;
			std::vector<ITwinElementID> ElementsSetDiff;
			Detail::InsertAnimatedMeshSubElemsRecursively(FIModelElementsKey(GroupIndex), Scene, GroupElements,
				MainTimeline, Timeline->IModelElementsRef(),
				GetInternals(*Owner).PrefetchWholeSchedule(),
				&ElementsSetDiff);
			::Detail::HideNonAnimatedDuplicates(Scene, ElementsSetDiff, MainTimeline);
			Timeline->OnIModelElementsAdded(); // just invalidates group's BBox
			if (OnElementsTimelineModified)
				OnElementsTimelineModified(*Timeline, &ElementsSetDiff);
		}
	}
}

void FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline(FITwinSchedule const& Schedule,
	size_t const AnimationBindingIndex, FSchedLock&)
{
	auto&& Binding = Schedule.AnimationBindings[AnimationBindingIndex];
	if (!ensure(Binding.NotifiedVersion == VersionToken::None))
		return;
	AITwinIModel* IModel = Owner ? Cast<AITwinIModel>(Owner->GetOwner()) : nullptr;
	if (!ensure(IModel || IsUnitTesting()))
		return;
	FITwinSceneMapping* pScene = nullptr;
	if (!IsUnitTesting()) // TODO_GCO: Source ID mapping not loaded yet for unit testing :/
	{
		pScene = &GetInternals(*IModel).SceneMapping;
	}
	bool bSingleElement = true;
	ITwinElementID SingleElementID;
	FElementsGroup BoundElements;
	std::optional<FIModelElementsKey> AnimationKey;
	std::visit([&](auto&& Ident)
		{
			using T = std::decay_t<decltype(Ident)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
			{
				SingleElementID = Ident;
				BoundElements.insert(SingleElementID);
				AnimationKey.emplace(SingleElementID);
			}
			else if constexpr (std::is_same_v<T, FGuid>)
			{
				if (pScene && pScene->FindElementIDForGUID(Ident, SingleElementID))
				{
					BoundElements.insert(SingleElementID);
					AnimationKey.emplace(SingleElementID);
				}
				else
					SingleElementID = ITwin::NOT_ELEMENT;
			}
			else if constexpr (std::is_same_v<T, FString>)
			{
				SingleElementID = ITwin::NOT_ELEMENT;
				BoundElements = Schedule.Groups[Binding.GroupInVec];
				AnimationKey.emplace(Binding.GroupInVec);
			}
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		Binding.AnimatedEntities);
	FElementsGroup AnimatedMeshElements;
	// until we load iModel metadata for it, Unit Testing can only support "flat" iModels
	// and no SourceID-duplicates!
	if (IsUnitTesting())
	{
		AnimatedMeshElements = BoundElements;
	}
	else
	{
		if (!AnimationKey || !ensure(!BoundElements.empty()))
			return;
		Detail::InsertAnimatedMeshSubElemsRecursively(*AnimationKey, *pScene, BoundElements, MainTimeline,
			AnimatedMeshElements, GetInternals(*Owner).PrefetchWholeSchedule());
		if (AnimatedMeshElements.empty()) // no 'ensure', it seems to happen in rare cases
			return;
		::Detail::HideNonAnimatedDuplicates(*pScene, AnimatedMeshElements, MainTimeline);
	}
	FITwinElementTimeline& ElementTimeline = MainTimeline.ElementTimelineFor(*AnimationKey,
																			 AnimatedMeshElements);
	if (Owner && Owner->bDebugWithDummyTimelines)
	{
		CreateTestingTimeline(ElementTimeline, *CoordConversions);
	}
	else
	{
		auto&& AppearanceProfile = Schedule.AppearanceProfiles[Binding.AppearanceProfileInVec];
		auto&& Task = Schedule.Tasks[Binding.TaskInVec];
		AddColorToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange);
		AddVisibilityToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange);
		ITwin::Timeline::PTransform const* TransformKeyframe = nullptr;
	#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
		if (ITwin::INVALID_IDX != Binding.TransfoAssignmentInVec) // optional
		{
			// Animation binding can have both static transfo and 3D path (with same Id, see azdev#1689132),
			// In that case, we store both assignments separately (see KnownTransfoAssignments's bool subkey),
			// to avoid having to worry about concurrent writes to the TransfoAssignment variant.
			// But the static transform is ignored like in Synchro Pro (TransfoAssignment.bStaticTransform is
			// set to false in FITwinSchedulesImport::FImpl::RequestAnimationBindings).
			auto&& TransfoAssignment = Schedule.TransfoAssignments[Binding.TransfoAssignmentInVec];
			if (Binding.bStaticTransform
				&& std::holds_alternative<FTransform>(TransfoAssignment.Transformation))
			{
				TransformKeyframe = &AddStaticTransformToTimeline(ElementTimeline, Task.TimeRange,
					std::get<0>(TransfoAssignment.Transformation), *CoordConversions);
			}
			else if (!Binding.bStaticTransform
				&& std::holds_alternative<FPathAssignment>(TransfoAssignment.Transformation))
			{
				auto&& PathAssignment = std::get<1>(TransfoAssignment.Transformation);
				if (ensure(ITwin::INVALID_IDX != PathAssignment.Animation3DPathInVec))
				{
					auto&& Path3D = Schedule.Animation3DPaths[PathAssignment.Animation3DPathInVec].Keyframes;
					Add3DPathTransformToTimeline(ElementTimeline, Task.TimeRange,
						PathAssignment.TransformAnchor, Path3D, *CoordConversions,
						PathAssignment.b3DPathReverseDirection);
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Inconsistent transformation assignment interpretation"));
			}
		}
	#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS
		AddCuttingPlaneToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange,
								  *CoordConversions, TransformKeyframe);
	}
	if (OnElementsTimelineModified) OnElementsTimelineModified(ElementTimeline, nullptr);
}

/*static*/
FITwinScheduleTimelineBuilder FITwinScheduleTimelineBuilder::CreateForUnitTesting(
	FITwinCoordConversions const& InCoordConv)
{
	FITwinScheduleTimelineBuilder Builder;
	Builder.Owner = nullptr;
	Builder.CoordConversions = &InCoordConv;
	return Builder;
}

// private, for CreateForUnitTesting
FITwinScheduleTimelineBuilder::FITwinScheduleTimelineBuilder()
{
}

FITwinScheduleTimelineBuilder::FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules const& InOwner,
															 FITwinCoordConversions const& InCoordConv)
	: Owner(&InOwner)
	, CoordConversions(&InCoordConv)
{
}

void FITwinScheduleTimelineBuilder::Initialize(FOnElementsTimelineModified&& InOnElementsTimelineModified)
{
	ensure(EInit::Pending == InitState);
	InitState = EInit::Ready;
	OnElementsTimelineModified = std::move(InOnElementsTimelineModified);
}

FITwinScheduleTimelineBuilder& FITwinScheduleTimelineBuilder::operator=(FITwinScheduleTimelineBuilder&& Other)
{
	Owner = Other.Owner;
	CoordConversions = Other.CoordConversions;
	MainTimeline = Other.MainTimeline;
	OnElementsTimelineModified = Other.OnElementsTimelineModified;
	ensure(EInit::Pending == Other.InitState);
	InitState = Other.InitState;
	Other.InitState = EInit::Disposable;
	return *this;
}

FITwinScheduleTimelineBuilder::~FITwinScheduleTimelineBuilder()
{
	ensure(EInit::Ready != InitState); // Pending or Disposable are both OK
}

void FITwinScheduleTimelineBuilder::Uninitialize()
{
	if (!ensure(EInit::Disposable != InitState))
		return;
	if (EInit::Pending != InitState)
	{
		for (auto const& ElementTimelinePtr : MainTimeline.GetContainer())
			if (ElementTimelinePtr->ExtraData)
				delete (static_cast<FTimelineToScene*>(ElementTimelinePtr->ExtraData));

		AITwinIModel* IModel = Cast<AITwinIModel>(Owner->GetOwner());
		if (ensure(IModel))
		{
			GetInternals(*IModel).SceneMapping.ForEachKnownTile([](FITwinSceneTile& SceneTile)
				{
					SceneTile.TimelinesIndices.clear();
				});
		}
	}
	InitState = EInit::Disposable;
}

void FITwinScheduleTimelineBuilder::DebugDumpFullTimelinesAsJson(FString const& RelPath) const
{
	FString TimelineAsJson = GetTimeline().ToPrettyJsonString();
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	Path.Append(RelPath);
	FString CoordConvPath;
	if (RelPath.EndsWith(".json"))
	{
		CoordConvPath = Path.LeftChop(4);
	}
	else
	{
		CoordConvPath = Path;
		Path.Append(".json");
	}
	CoordConvPath += TEXT(".CoordConv.json");
	if (FileManager.FileExists(*Path))
		FileManager.DeleteFile(*Path);
	FFileHelper::SaveStringToFile(TimelineAsJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	if (!IsUnitTesting() && CoordConversions)
	{
		if (FileManager.FileExists(*CoordConvPath))
			FileManager.DeleteFile(*CoordConvPath);
		FString CCString;
		FJsonObjectConverter::UStructToJsonObjectString(*CoordConversions, CCString, 0, 0);
		FFileHelper::SaveStringToFile(CCString, *CoordConvPath, FFileHelper::EEncodingOptions::ForceUTF8);
	}
}
