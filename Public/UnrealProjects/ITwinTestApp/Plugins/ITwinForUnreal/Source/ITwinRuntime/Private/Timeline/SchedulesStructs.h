/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Math/Vector.h>

#include <Compil/StdHash.h>
#include <ITwinElementID.h>
#include <Timeline/AnchorPoint.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/TimeInSeconds.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

class FITwinSchedule;
using FSchedLock = std::lock_guard<std::recursive_mutex>;

enum class EGrowthSimulationMode : uint8_t
{
	Bottom2Top, Top2Bottom, Left2Right, Right2Left, Front2Back, Back2Front, Custom,
	// Note: keep these no-op values at the end (tested in ITwinSynchro4DSchedules.cpp's
	// FITwinScheduleTimelineBuilder::AddCuttingPlaneToTimeline)
	None, Unknown
};

class FSimpleAppearance
{
public: // Note: ordered for best packing, not semantics (keep order or change list inits!)
	FVector Color;
	float Alpha = 1.f;
	bool bUseOriginalColor : 1 = true;
	bool bUseOriginalAlpha : 1 = true;
	FSimpleAppearance()
	{}
	FSimpleAppearance(const FVector & color, float alpha, bool useOriginalColor, bool useOriginalAlpha)
		: Color(color)
		, Alpha(alpha)
		, bUseOriginalColor(useOriginalColor)
		, bUseOriginalAlpha(useOriginalAlpha)
	{}

};

/// Default init yields a nilpotent profile (keeps original color and alpha, no cutting plane)
class FActiveAppearance
{
public: // Note: ordered for best packing, not semantics (keep order or change list inits!)
	FSimpleAppearance Base; ///< color, color/alpha flags, and alpha (see also FinishAlpha)
	/// Growth direction for the case EGrowthSimulationMode::Custom. Note that it is expressed in the
	/// transformed base (see FTransformAssignment).
	FVector GrowthDirectionCustom;
	float FinishAlpha = 1.f; ///< Alpha at the end of the task
	/// Growth direction, either along a common axis, or custom, expressed in the iTwin base/convention.
	/// Note that it should be interpreted in the transformed base (see FTransformAssignment).
	EGrowthSimulationMode GrowthSimulationMode = EGrowthSimulationMode::None;
	/// Not yet impl. in AppearanceProfilesApi.ts
	bool bGrowthSimulationBasedOnPercentComplete : 1 = false;
	/// Not yet impl. in AppearanceProfilesApi.ts
	bool bGrowthSimulationPauseDuringNonWorkingTime : 1 = false;
	/// Means the Element disappears during the task, instead of appearing. It also means the opposite
	/// cutting plane /orientation/ will be used, but that's NOT equivalent to using the opposite value of
	/// EGrowthSimulationMode!
	bool bInvertGrowth : 1 = false;
};

enum class EProfileAction : uint8_t
{
	Neutral, Install, Remove, Temporary, Maintenance
};

/**
 From Julius Senkus (https://dev.azure.com/bentleycs/Synchro/_git/SynchroScheduleContract/pullRequest/430148#1717072325): "It is the date when the [item] was last modified, but in some cases might be a combination of multiple things, that is why it is defined as string. When you receive the last page, you also receive the last modified item date (delta token), then next time when you do a request, you can provide the delta token and receive only the items that were modified or removed (to update a local cache)."

 For the moment, we don't support this system here, but this flag is actually an embryo of the future feature.
*/
using FVersionToken = bool;
namespace VersionToken
{
	constexpr FVersionToken None = false;
	constexpr FVersionToken InitialVersion = true;
}

class FAnimationBindingProperty
{
public:
	FVersionToken Version = VersionToken::None;
	/// Until the property has been fully queried (including nested properties, if any), but no longer,
	/// this member will contain a list of indices in FITwinSchedule::AnimationBindings for bindings sharing
	/// this property (either directly, or indirectly through a nested property in the case of
	/// a FTransfoAssignment pointing at a FAnimation3DPath, to name it).
	/// Upon completion of all queries needed to fully define the property, the list will be used to notify
	/// the animation bindings that they, in turn, might also now be fully defined. Then the list is emptied.
	/// At creation, it is immediately non-empty because it contains the first AnimIdx that asked for this
	/// property, so an empty collection here really means that the property is fully defined (it has been
	/// queried and the query has completed).
	std::vector<size_t> Bindings;
};

/// Default init yields nilpotent profiles (keeps original colors and alphas, not cut planes)
class FAppearanceProfile : public FAnimationBindingProperty
{
public:
	EProfileAction ProfileType = EProfileAction::Neutral;
	FSimpleAppearance StartAppearance;
	FActiveAppearance ActiveAppearance;
	FSimpleAppearance FinishAppearance;
};

/// Collection of Elements assigned an animation binding together. Using set instead of unordered_set so that
/// FITwinSynchro4DSchedulesInternals::HandleReceivedElements can use set_intersection, but this would need
/// to be benched
using FElementsGroup = std::set<ITwinElementID>;

/// Keyframe of an animation path
class FTransformKey
{
public:
	/// Contains the translation (relative to the anchor point), rotation and scaling, in the iTwin reference
	/// system. Scaling is apparently not used for 3D paths in the current Synchro tools, but supported here nonetheless.
	FTransform Transform;
	/// Time of passing at this path point, as a proportion in [0;1] of the task duration. In case of a
	/// static transform and not a 3D path, it is simply ignored.
	double RelativeTime = 0.;
};

/// List of control points of a 3D path. We don't care for the 3D path name and color and thus skip the path
/// endpoint to query directly the keyframes
class FAnimation3DPath : public FAnimationBindingProperty
{
public:
	std::vector<FTransformKey> Keyframes;
};

class FPathAssignment
{
public:
	/// Id of an animation path that a (group of) Element(s) can follow during the task.
	FString Animation3DPathId;
	size_t Animation3DPathInVec = ITwin::INVALID_IDX;
	/// Offset from the animated element or group's bbox, expressed in the iTwin base/convention. Should be
	/// zero for a static transform, only because it does not seem to be supported in Synchro Modeler.
	std::variant<ITwin::Timeline::EAnchorPoint, FVector> TransformAnchor;
	/// Direction of the trajectory along the path, in case of a non-static transform
	bool b3DPathReverseDirection = false;
};

/// Defines either a static transformation (a single FTransform expressed in the iTwin reference system,
/// applying during the whole task), or an animation path (FAnimation3DPath, through a FPathAssignment)
/// that a (group of) Element(s) can follow during the task.
/// Animation is cumulated with the appearance profile, which uses the transformed base (for growth
/// simulation). In case of a path, trajectory and other properties are linearly interpolated.
class FTransformAssignment : public FAnimationBindingProperty
{
public:
	std::variant<FTransform, FPathAssignment> Transformation;
};

class FScheduleTask : public FAnimationBindingProperty
{
public:
	FString Id;
	FString Name;
	/// Task's start and finish times using dates in UTC time, expressed in seconds since Midnight
	/// 00:00:00, January 1, 0001
	FTimeRangeInSeconds TimeRange = ITwin::Time::Undefined();
};

/// Description of the animation of Elements during a Task: the properties that strictly identify a
/// binding as unique are the following:<ul>
/// <li>AnimatedEntities (ie. a single ElementID or the string Id of an FElementsGroup)</li>
/// <li>TaskId, to get the animation's time range from a task</li>
/// <li>AppearanceProfileId, to get the initial, active and final appearance of the elements</li>
/// <li>TransfoAssignmentId, to get the optional transformation(s) of the elements (static or following
///		a path)</li>
/// </ul>
class FAnimationBinding
{
public:
	FString TaskId;
	size_t TaskInVec = ITwin::INVALID_IDX;
	/// Single Element bound, or Id of the FElementsGroup listing all Elements bound by this animation
	std::variant<ITwinElementID, FString/*group Id*/> AnimatedEntities{ ITwin::NOT_ELEMENT };
	/// Index of the item matching AnimatedEntities, in case it is a group, in FITwinSchedule::Groups
	size_t GroupInVec = ITwin::INVALID_IDX;
	/// Id of the FAppearanceProfile
	FString AppearanceProfileId;
	/// Index of the item matching AppearanceProfileId in FITwinSchedule::AppearanceProfiles
	size_t AppearanceProfileInVec = ITwin::INVALID_IDX;
	/// \see FTransformAssignment
	FString TransfoAssignmentId;
	size_t TransfoAssignmentInVec = ITwin::INVALID_IDX;
	bool bStaticTransform = true;

	/// For book-keeping: with 'None', notifications to the timeline will create the associated keyframes,
	/// whereas when true, only an update of the list of affected Elements can be enacted. This flag is
	/// necessary because of the delay between registration of a new binding in [Known]AnimationBindings
	/// and the actual call to OnAnimationBinding, since task details, appearance profiles and/or
	/// transformations (static or along a path) usually need to be queried in the meantime.
	/// Note that many queries can be skipped when bindings are registered or elements are already known to
	/// their assigned groups, even when this flag is false, because it means there is necessarily a pending
	/// query which callback will end up creating the timeline entries for the whole binding.
	FVersionToken NotifiedVersion = VersionToken::None;

	FString ToString(const TCHAR* SpecificElementID = nullptr) const;
	bool FullyDefined(FITwinSchedule const& Schedule, bool const bAllowPendingQueries,
					  FSchedLock& Lock) const;
};

template <>
struct std::hash<FAnimationBinding>
{
public:
	size_t operator()(FAnimationBinding const& Key) const
	{
		size_t Res = GetTypeHash(Key.TaskId);
		std::visit([&Res](auto&& ElemOrGroupId)
			{
				using T = std::decay_t<decltype(ElemOrGroupId)>;
				if constexpr (std::is_same_v<T, ITwinElementID>)
					boost::hash_combine(Res, std::hash<uint64_t>()(ElemOrGroupId.value()));
				else if constexpr (std::is_same_v<T, FString>)
					boost::hash_combine(Res, GetTypeHash(ElemOrGroupId));
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Key.AnimatedEntities);
		boost::hash_combine(Res, GetTypeHash(Key.AppearanceProfileId));
		boost::hash_combine(Res, GetTypeHash(Key.TransfoAssignmentId));
		return Res;
	}
};

inline bool operator ==(FAnimationBinding const& A, FAnimationBinding const& B)
{
	return A.TaskId == B.TaskId
		&& A.AnimatedEntities.index() == B.AnimatedEntities.index()
		&& (0 == A.AnimatedEntities.index()
			? std::get<0>(A.AnimatedEntities) == std::get<0>(B.AnimatedEntities)
			: std::get<1>(A.AnimatedEntities) == std::get<1>(B.AnimatedEntities))
		&& A.AppearanceProfileId == B.AppearanceProfileId
		&& A.TransfoAssignmentId == B.TransfoAssignmentId;
}

/// Should be irrelevant ultimately
enum class EITwinSchedulesGeneration : uint8
{
	Legacy,
	NextGen,
	Unknown
};

/**
 * Schedules obtained from /api/v1/schedules, filtered by targeted iModel
 */
class FITwinSchedule
{
public:
	FString Id, Name; // <== keep first and ordered, for list init
	EITwinSchedulesGeneration Generation = EITwinSchedulesGeneration::Unknown;

	// Not good here, prevents the class from going into a vector (could use a shared pointer? for the moment
	// the sync will remain in FITwinSchedulesImport::FImpl
	//std::[recursve_]mutex Mutex;

	/// "user field id" needed for animationBinding/query (EITwinSchedulesGeneration::NextGen only)
	FString AnimatedEntityUserFieldId;

	std::vector<FAnimationBinding> AnimationBindings;
	std::vector<FScheduleTask> Tasks;
	std::vector<FElementsGroup> Groups;
	std::vector<FAppearanceProfile> AppearanceProfiles;
	std::vector<FTransformAssignment> TransfoAssignments;
	std::vector<FAnimation3DPath> Animation3DPaths;

	/// Known animation bindings: NOT to avoid useless requests to task details, appearance profiles, etc.
	/// as those have their own maps to cache data after (or pending) retrieval.
	/// NOT really to avoid useless calls to OnAnimationBinding either, as the current timeline
	/// implementation should ensure no duplicate keyframes are added. Although it's still a good thing to
	/// actually avoid those redundant calls I guess...
	/// BUT mostly so that the many per-Element binding, received as independent items but that are part of 
	/// the same FAnimationBinding, can find their common entry in the AnimationBindings member!
	/// The string Id properties are used for the key hashing, but the matching *InVec are not as they are not
	/// supposed to be known yet (see doc on FAnimationBinding for the list of properties).
	std::unordered_map<FAnimationBinding, size_t/*index in AnimationBindings*/> KnownAnimationBindings;

	std::unordered_map<FString, size_t/*index in ...*/> KnownTasks;
	std::unordered_map<FString, size_t/*index in ...*/> KnownGroups;
	std::unordered_map<FString, size_t/*index in ...*/> KnownAppearanceProfiles;
	std::unordered_map<FString, size_t/*index in ...*/> KnownTransfoAssignments;
	std::unordered_map<FString, size_t/*index in ...*/> KnownAnimation3DPaths;

	// Note: nothing yet to avoid redundant requests with time range filtering: the whole point of time
	// filtering is actually questionable since we need the StartAppearance profiles of the very first task
	// to get the initial display state (same for the last task's EndAppearance for the final state) so...
	/// Stores VersionToken::None when querying for all tasks of an Element in the schedule (not just for a
	/// specific time range), and replaces with VersionToken::InitialVersion once the request has been
	/// processed.
	std::unordered_map<ITwinElementID, FVersionToken> AnimBindingsFullyKnownForElem;

	void Reserve(size_t Count);

	/// Return a string description with some statistics
	FString ToString() const;

}; // class FITwinSchedule

using FOnAnimationBindingAdded =
	std::function<void(FITwinSchedule const&, size_t const/*AnimationBindingIndex*/, FSchedLock&)>;
using FOnAnimationGroupModified = std::function<
	void(size_t const/*GroupIndex*/, std::set<ITwinElementID> const&/*GroupElements*/, FSchedLock&)>;