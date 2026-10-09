/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Math/Vector.h>

#include <Hashing/UnrealGuid.h>
#include <Hashing/UnrealString.h>
#include <ITwinElementID.h>
#include <Timeline/AnchorPoint.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesGeneration.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/TimelineTypes.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <BeHeaders/StrongTypes/TaggedValue.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

class FITwinSchedule;
class FJsonObject;
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
	FVector Color = FVector::ZeroVector;
	float Alpha = 1.f;
	bool bUseOriginalColor : 1 = true;
	bool bUseOriginalAlpha : 1 = true;
	FSimpleAppearance()
	{}
	FSimpleAppearance(const FVector& color, float alpha, bool useOriginalColor, bool useOriginalAlpha)
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
	/// transformed base (see FPathTransformAssignment, FStaticTransformAssignment).
	FVector GrowthDirectionCustom = FVector::ZeroVector;
	float FinishAlpha = 1.f; ///< Alpha at the end of the task
	/// Growth direction, either along a common axis, or custom, expressed in the iTwin base/convention.
	/// Note that it should be interpreted in the transformed base (see FPathTransformAssignment, FStaticTransformAssignment).
	EGrowthSimulationMode GrowthSimulationMode = EGrowthSimulationMode::None;
	/// Not yet impl. in AppearanceProfilesApi.ts so we will ignore it
	/// (see also To/FromJsonObject for FActiveAppearance)
	bool bGrowthSimulationBasedOnPercentComplete : 1 = false;
	/// Not yet impl. in AppearanceProfilesApi.ts so we will ignore it
	/// (see also To/FromJsonObject for FActiveAppearance)
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

DEFINE_STRONG_BOOL(EDeletedProp);

class FAnimProperty
{
public:
	FString Id;
	/// Defaulting to true allows to detect creation of new properties without having to rely on default-init of the
	/// other property details, which may not have an "invalid" default value (see for example EProfileAcion below)
	EDeletedProp bDeleted = EDeletedProp(true);
};

/// Default init yields nilpotent profiles (keeps original colors and alphas, not cut planes)
class FAppearanceProfile : public FAnimProperty
{
public:
	EProfileAction ProfileType = EProfileAction::Neutral;
	FSimpleAppearance StartAppearance;
	FActiveAppearance ActiveAppearance;
	FSimpleAppearance FinishAppearance;
};

/// Keyframe of an animation path (note: the base's "Id" property is irrelevant in this particular case)
class FTransformKey : public FAnimProperty
{
public:
	/// Contains the translation (relative to the anchor point), rotation and scaling, in the iTwin reference
	/// system. Scaling is apparently not used for 3D paths in the current Synchro tools, but supported here
	/// nonetheless. Note that FTransform's default init is the identity transform.
	FTransform Transform;
	/// Time of passing at this path point, as a proportion in [0;1] of the task duration. In case of a
	/// static transform and not a 3D path, it is simply ignored.
	double RelativeTime = 0.;
};

/// List of control points of a 3D path. We don't care for the 3D path name and color and thus skip the path
/// endpoint to query directly the keyframes
class FAnimation3DPath : public FAnimProperty
{
public:
	FString Name;
	FVector Color = FVector::ZeroVector;
	std::vector<FTransformKey> Keyframes;
};

/// Defines an animation path (FAnimation3DPath, through a FPathTransformAssignment) that a (group of) Element(s) can follow
/// during the task. Animation is cumulated with the appearance profile, which uses the transformed base (for growth
/// simulation). Trajectory and other properties are linearly interpolated.
class FPathTransformAssignment : public FAnimProperty
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
	/** From SynchroPro documentation : "Use the Motion Start and Motion End fields to specify where along the
	3D path the object should start and stop. This can be used if the object should travel only a portion of the total
	path during this task. (...) Regardless of the Start and End location, the object will still use the entire
	duration of the task to travel the specified portion of the path."
		These members use normalized values instead of the original percentages.
	*/
	double MotionStart = 0., MotionEnd = 1.;
};

/// Defines a static transformation as a single FTransform expressed in the iTwin reference system,
/// applying during the whole task
class FStaticTransformAssignment : public FAnimProperty
{
public:
	FTransform Transform;
};

class FScheduleTask : public FAnimProperty
{
public:
	FString Name;
	/// Task's start and finish times using dates in UTC time, expressed in seconds since Midnight
	/// 00:00:00, January 1, 0001
	FTimeRangeInSeconds TimeRange = ITwin::Time::Undefined();
};

/// Description of the animation of Elements during a Task: the properties that strictly identify a
/// binding as unique are the following:<ul>
/// <li>AnimatedEntities (ie. a single ElementID or Fed.GUID, or the string Id of an elements group)</li>
/// <li>TaskId, to get the animation's time range from a task</li>
/// <li>AppearanceProfileId, to get the initial, active and final appearance of the elements</li>
/// <li>TransfoAssignmentId, to get the optional transformation(s) of the elements (static or following
///		a path)</li>
/// </ul>
class FAnimationBinding : public FAnimProperty
{
public:
	FString TaskId;
	size_t TaskInVec = ITwin::INVALID_IDX;
	/// Single Element bound, or Id of the elements group listing all Elements bound by this animation
	std::variant<ITwinElementID, FGuid/*Federated Element GUID*/, FString/*group Id*/>
		AnimatedEntities{ ITwin::NOT_ELEMENT };
	/// Index of the item matching AnimatedEntities, in case it is a group, in FITwinSchedule::Groups
	size_t GroupInVec = ITwin::INVALID_IDX;
	/// Id of the FAppearanceProfile
	FString AppearanceProfileId;
	/// Index of the item matching AppearanceProfileId in FITwinSchedule::AppearanceProfiles
	size_t AppearanceProfileInVec = ITwin::INVALID_IDX;
	/// \see FStaticTransformAssignment
	FString StaticTransfoAssignmentId;
	size_t StaticTransfoAssignmentInVec = ITwin::INVALID_IDX;
	/// \see FPathTransformAssignment
	FString PathTransfoAssignmentId;
	size_t PathTransfoAssignmentInVec = ITwin::INVALID_IDX;

	FString ToString(const TCHAR* SpecificElementID = nullptr) const;
	bool FullyDefined(FITwinSchedule const& Schedule, FSchedLock& Lock) const;
};

/**
 * Statistics obtained from https://api.bentley.com/schedules/{scheduleId}/animation-statistics
 */
class FITwinScheduleStats
{
public:
	size_t Animation3dPathAssignmentCount = 0;
	size_t Animation3dPathCount = 0;
	size_t Animation3dPathKeyframeCount = 0;
	size_t Animation3dTransformCount = 0;
	size_t AnimationBindingCount = 0;
	size_t AppearanceProfileCount = 0;
	size_t TaskCount = 0;
};

namespace ITwin::Timeline
{
	struct FTaskDependenciesData;
}

#include "SchedulesStructsOps.inl" // defines hash funcs as well, hence included before FITwinSchedule

/**
 * Schedules obtained from https://api.bentley.com/schedules, filtered by targeted iModel
 */
class FITwinSchedule
{
public:
	FString Id, Name;
	/// Increment when older chedule json's will have to be converted to the current version, or simply wiped out
	static const int CurrentJsonCacheVersion = 1;
	int JsonCacheVersion = CurrentJsonCacheVersion;
	/// "Unknown" also means "Not needed", when used with APIM, which hides this detail from us.
	EITwinSchedulesGeneration Generation = EITwinSchedulesGeneration::Unknown;

	FString BindingsDeltaToken, AppearanceProfilesDeltaToken, TasksDeltaToken, StaticTransfosDeltaToken,
			Anim3DPathsAssignmentsDeltaToken, Anim3DPathsDeltaToken, Anim3DPathKeyframesDeltaToken;

	FITwinSchedule(FString const& ScheduleId, FString const& ScheduleName,
				   EITwinSchedulesGeneration ScheduleGen = EITwinSchedulesGeneration::Unknown)
		: Id(ScheduleId), Name(ScheduleName), Generation(ScheduleGen) {}

public:
	/// Schedule statistics, eg. for download progress feedback purposes: expected totals queried from server
	std::optional<FITwinScheduleStats> StatisticsTotal;
	/// Schedule statistics, eg. for download progress feedback purposes: current items received from 4D api
	FITwinScheduleStats StatisticsCurrent;

	// Not good here, prevents the class from going into a vector (which we no longer do btw, so we could move the
	// mutex here back from FITwinSchedulesImport::FImpl)
	//std::[recursive_]mutex Mutex;

	std::vector<FAnimationBinding> AnimationBindings;
	std::vector<FScheduleTask> Tasks;
	std::vector<FAppearanceProfile> AppearanceProfiles;
	std::vector<FStaticTransformAssignment> StaticTransfoAssignments;
	std::vector<FPathTransformAssignment> PathTransfoAssignments;
	std::vector<FAnimation3DPath> Animation3DPaths;

	size_t NumGroups() const;
	size_t GetNextGroupID() const;
	void CreateNextGroup(bool const bIsElemIDGroup);
	void CreateNextGroup(FElementsGroup&& Group);
	bool AddToGroup(size_t InVec, ITwinElementID const ElemID);
	bool AddToGroup(size_t InVec, FGuid const FedGUID);
	void ResetElemIDGroups();

	template<typename TFedGUID2ElemID>
	FElementsGroup const& GetGroupAsElementIDs(size_t GroupInVec, TFedGUID2ElemID const& FedGUID2ElemID)
	{
		if (!ensure(ITwin::INVALID_IDX != GroupInVec
			&& (GroupInVec < ElemIDGroups.size() || GroupInVec < FedGUIDGroups.size())))
		{
			static FElementsGroup Dummy;
			return Dummy;
		}
		if (EITwinSchedulesGeneration::NextGen == Generation
			// Adding this flexibility for test data (see comment in FITwinSchedule::AddToGroup), which has
			// ElemIDGroups filled and an empty FedGUIDGroups even though it's (simulated) NextGen
			&& ElemIDGroups.size() <= FedGUIDGroups.size())
		{
			// Schedule increments bringing bindings changes and thus possible group updates should reset ElemIDGroups
			// entirely, to make sure the groups are rebuilt (eg. the same group could have an element added and
			// another removed, so comparing the size is of course not sufficient)
			ensure(ElemIDGroups.empty() || ElemIDGroups.size() == FedGUIDGroups.size());
			if (ElemIDGroups.empty())
				ElemIDGroups.resize(FedGUIDGroups.size());
			auto& Group = ElemIDGroups[GroupInVec];
			auto&& FedGroup = FedGUIDGroups[GroupInVec];
			if (Group.empty()) // not  "!= FedGroup.size()" in case FedGUID2ElemID has returned false earlier
			{
				Group.reserve(FedGroup.size());
				ITwinElementID Found;
				for (auto&& FedGUID : FedGroup)
					if (FedGUID2ElemID(FedGUID, Found))
						Group.insert(Found);
			}
			return Group;
		}
		else // GroupInVec validity is ensured above
		{
			return ElemIDGroups[GroupInVec];
		}
	}

private:
	/// Legacy schedule animation bindings identify Elements by their Element ID, hence FElementsGroup.
	/// Only one array is used, either this one for Legacy schedules, or FedGUIDGroups for Next-gen.
	std::vector<FElementsGroup> ElemIDGroups;
	/// Next-gen schedule animation bindings identify Elements by their Federation GUIDs, which can no longer be
	/// resolved into Element IDs until FinalizeTimeline as we used to before iModel metadata and 4D queries were
	/// made to run concurrently to reduce loading times.
	/// Only one array is used, either this one for Next-gen schedules, or ElemIDGroups for Legacy.
	std::vector<std::unordered_set<FGuid>> FedGUIDGroups;

	void RebuildKnownProperties();

public:
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
	std::unordered_map<FString, size_t/*index in ...*/> KnownStaticTransfoAssignments;
	std::unordered_map<FString, size_t/*index in ...*/> KnownPathTransfoAssignments;
	std::unordered_map<FString, size_t/*index in ...*/> KnownAnimation3DPaths;

	void Reserve(size_t Count);

	bool FullyDefined(FSchedLock& Lock) const;

	[[nodiscard]] TSharedPtr<FJsonObject> ToJson() const;
	[[nodiscard]] FString ToCondensedJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	bool FromJson(TSharedPtr<FJsonObject> const& Root);
	bool FromJsonString(FString const& JsonString);
	bool SaveToJson(FString const& Path, bool bPretty, FSchedLock&) const;
	bool ReadFromJson(FString const& Path, FSchedLock&);
	/// Return a string description with some statistics
	FString ToString() const;

	template<typename BindingIndexIterator>
	bool HasOnlyNeutralBindings(BindingIndexIterator First, BindingIndexIterator Last) const;

	template<typename BindingIndexIterator>
	void FindAnyPriorityAppearances(BindingIndexIterator First, BindingIndexIterator Last,
		FAnimationBinding const& ThisBinding, EProfileAction const ThisAction,
		ITwin::Timeline::FTaskDependenciesData& TaskDeps) const;

	// In ScheduleComparison.inl
	friend bool operator==(const FITwinSchedule& A, const FITwinSchedule& B);

}; // class FITwinSchedule

using FOnReceivedScheduleStats = std::function<void(FITwinScheduleStats const&)>;
