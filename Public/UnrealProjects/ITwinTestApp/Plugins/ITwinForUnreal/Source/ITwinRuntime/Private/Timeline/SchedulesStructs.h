/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <ITwinElementID.h>
#include <Timeline/Schedule/TimeInSeconds.h>
#include <Math/Vector.h>
#include <Compil/StdHash.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace std {

template <>
struct hash<std::pair<FString, ITwinElementID>>
{
public:
	size_t operator()(std::pair<FString, ITwinElementID> const& key) const
	{
		size_t res = std::hash<uint64_t>()(key.second.value());
		boost::hash_combine(res, GetTypeHash(key.first));
		return res;
	}
};

}

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
	float Alpha;
	bool bUseOriginalColor : 1 = true;
	bool bUseOriginalAlpha : 1 = true;
};

/// Default init yields a nilpotent profile (keeps original color and alpha, no cutting plane)
class FActiveAppearance
{
public: // Note: ordered for best packing, not semantics (keep order or change list inits!)
	FSimpleAppearance Base; ///< Task color, color/alpha flags, and alpha (see also FinishAlpha)
	FVector GrowthDirectionCustom; ///< Growth direction for the case EGrowthSimulationMode::Custom
	float FinishAlpha; ///< Alpha at the end of the task
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

/// Default init yields nilpotent profiles (keeps original colors and alphas, not cut planes)
class FAppearanceProfile
{
public:
	EProfileAction ProfileType = EProfileAction::Neutral;
	FSimpleAppearance StartAppearance;
	FActiveAppearance ActiveAppearance;
	FSimpleAppearance FinishAppearance;
};

namespace ITwin
{
	constexpr size_t INVALID_IDX = static_cast<size_t>(-1);
}

class FAnimationBinding
{
public:
	FString TaskId, TaskName, AppearanceProfileId;
	ITwinElementID AnimatedEntityId = ITwin::NOT_ELEMENT;
	size_t AppearanceProfileInVec = ITwin::INVALID_IDX;
	/// Task's start and finish times using dates in UTC time, expressed in seconds since Midnight
	/// 00:00:00, January 1, 0001
	FTimeRangeInSeconds TimeRange;
	FString ResourceId; ///< Only set in PROD, temporarily - not even useful except maybe for debug

	// do we really want to know the whole tasks hierarchy?
	//int AncestryLevel; ///< 0 for root tasks, 1 for their children, 2 for grand-children, etc.
	//std::optional<size_t> Parent, Children; ///< Index in FITwinSchedule::AnimationBindings
};

enum class EITwinSchedulesGeneration : uint8
{
	Legacy,
	NextGen,
	Unknown
};

/**
 * Schedule identifier obtained from /api/v1/schedules, filtered by targeted iModel.
 * The minimum amount of data necessary to support any meaningful display of the schedule are:
 *		- "Animated Entity User Field Id" of the schedule: not sure what this is for, some kind of
 *		  indirection to access the actual animation data
 *		- Each schedule's name and id.
 * Subsequent data is fetched on-demand (with some amount of pre-fetch, obviously). In particular,
 * there is no total duration for a schedule: the tasks hierarchy has to be explored for that. So
 * in an assumed graphical timeline, one could merely list folded schedules, then start materializing
 * tasks when unfolding a schedule "line", extending the schedule's span as new tasks and subtasks appear.
 *
 * Data to pre-fetch could include the first few levels of tasks: top-level tasks would already give us
 * the total time range of each schedule and thus of the whole project. More detail could be pre-fetched
 * until some limit, and to populate the timeline when zooming in. Not sure whether a really large project
 * could indeed overwhelm us with data, as those are only made of small amounts of metadata??
 *
 * Requests will be made asynchronously to complete missing data, hence the need for mutexes - and later
 * also notifications of some sort to update UI, scheduled objects materials, etc.
 */
class FITwinSchedule
{
public:
	FString Id, Name; // <== keep first and ordered, for list init
	EITwinSchedulesGeneration Generation = EITwinSchedulesGeneration::Unknown;

	// Not good here, prevents the class from going into a vector (could use a shared pointer? for the moment
	// the sync will remain in FITwinSchedulesImport::FImpl
	//std::mutex Mutex;

	/// "user field id" needed for animationBinding/query (EITwinSchedulesGeneration::NextGen only)
	FString AnimatedEntityUserFieldId;
	/// All animated tasks, unordered, without any parent/child relationships. Could also be used as
	/// task pool if we want the whole tasks hierarchy and set 'Parent' and 'Children' in 
	/// FAnimationBinding
	std::vector<FAnimationBinding> AnimationBindings;
	std::vector<FAppearanceProfile> AppearanceProfiles;
	std::unordered_map<FString/*AppearanceProfileId*/, size_t/*index...*/> KnownAppearanceProfiles;

	// Note: nothing yet to avoid redundant requests with time range filtering: the whole point of time
	// filtering is actually questionable since we need the StartAppearance profiles of the very first task
	// to get the initial display state (same for the last task's EndAppearance for the final state) so...
	/// Stores 'false' when querying for all tasks of an Element in the schedule (not just for a
	/// specific time range), and replaces with 'true' once the request has been processed.
	std::unordered_map<ITwinElementID, bool> AnimBindingsFullyKnownForElem;

	/// Known animation bindings (= Task + animated Element), to avoid useless requests to task details and
	/// appearance profiles, and also useless calls to OnAnimationBinding: the current timeline
	/// implementation should ensure no duplicate keyframes are added, but it still seems better not to call
	/// it in the first place.
	std::unordered_map<std::pair<FString/*Task.Id*/, ITwinElementID>,
					   size_t/*index in AnimationBindings*/> KnownAnimationBindings;
	/// Task's name and time range is shared among all AnimationBindings... Copy whenever possible from any
	/// already known binding of the same task, instead of querying known task details over again
	std::unordered_map<FString/*Task.Id*/, size_t/*index in AnimationBindings*/> KnownTaskDetails;

	// do we really want to know the whole tasks hierarchy?
	//std::optional<std::vector<size_t/*index in AnimationBindings*/>> RootTasks;

}; // class FITwinSchedule

using FOnAnimationBindingAdded =
	std::function<void(FAnimationBinding const&, FAppearanceProfile const&)>;
