/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBase.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Timeline/TimeInSeconds.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/Attributes.h>
	#include <BeHeaders/Util/Enumerations.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <limits>
#include <memory>
#include <set>
#include <unordered_set>

class FJsonObject;

//! IMPORTANT NOTE: All types/functions in this header must remain GENERIC.
//! They are supposed to be used with any user-defined properties and metadata.
//! Do not add specific data like "color", "visibility" or whatever here.

namespace ITwin::Timeline
{

//! Defines how values are computed (interpolated) between 2 entries.
enum class EInterpolation : int32_t
{
	Step, //!< Use value of the "previous" entry - TODO_GCO: rename 'Previous'? (see 'Next')
	Linear, //!< Linear interpolation between previous and next entries.
	//! Use value of the "next" entry: useful in theory (and in CreateTestingTimeline), because the Step
	//! interpolation means only the "previous" keyframe is considered. The way the schedule timeline
	//! is built around tasks, there is an ambiguity as to what happens in case of successive tasks. At the
	//! moment, StartAppearance instructions would be arbitrarily overridden by a previous task's
	//! FinishAppearance...Using 'Next' instead of 'Step' at the end of tasks would allow to set a default
	//! appearance after the task *unless* there's another task in the future.
	//! Note that it is not redundant with StateAtEntryTimeBehavior::UseRightInterval, which is a parameter
	//! passed to GetStateAtTime and applies to the whole timeline, not selectively for this or that keyframe!
	Next,
};

//! Base class for entries (ie keyframes).
//! Contains base data that must be available in all types of entries.
class PropertyEntryBase
{
public:
	double Time = {};
	EInterpolation Interpolation = {};
};

bool operator <(const PropertyEntryBase& x, const PropertyEntryBase& y);

//! Generic entry with custom values.
template<class _Values>
class PropertyEntry
	:public PropertyEntryBase
	,public _Values
{
};

template<class _Values>
bool NoEffect(const _Values& Prop);

//! This enum controls the behavior of function GetStateAtTime() when the given time
//! matches exactly the time of an entry (say, entry N).
//! This has an effect only if entry N-1 uses "step" interpolation.
//! With UseLeftInterval behavior, GetStateAtTime() will return value N-1,
//! which is consistent with iModel.js behavior.
//! With UseRightInterval will return value N, which can be useful in some cases
//! (and seems more sensible).
enum class StateAtEntryTimeBehavior
{
	UseLeftInterval, //!< Consider entry N belongs in interval [N-1, N] (same behavior as iModel.js)
	UseRightInterval, //!< Consider entry N belongs in interval [N, N+1]
};

//! A PropertyTimeline is basically a list of entries, with the ability to retrieve the state
//! at any given time, by interpolating the property values.
template<class _PropertyValues>
class PropertyTimeline
{
public:
	using PropertyValues = _PropertyValues;
	//! Property keyframes, ordered by PropertyEntryBase::Time. Using an std::set to have them naturally
	//! ordered (since there is no guarantee they are added in chronological order), but for efficiency
	//! it might be a good idea(?) to use a mere std::list and a fast_pool_allocator (TODO_GCO) since
	//! timelines have usually very few keyframes (2-3!).
	using FTimeOrderedProperties = std::set<PropertyEntry<_PropertyValues>>;
	FTimeOrderedProperties Values;
	//! Removes duplicate, useless entries that may exist at the end of the list.
	void Prune();
	//! Tells whether the timeline will have no effect at all (useful to trim print-outs)
	bool HasNoEffect() const;
	//! Returns the interpolated property values at the given time.
	[[nodiscard]] std::optional<_PropertyValues> GetStateAtTime(double time,
		StateAtEntryTimeBehavior entryTimeBehavior, void* userData) const;
};

template<class _Base, class _ObjectState>
struct ObjectTimelineMetadata
{
	using Base = _Base;
	using ObjectState = _ObjectState;
};

//! An ObjectTimeline is a set of PropertyTimelines, with the ability to retrieve the state of the
//! object (ObjectState) at any given time.
//! The ObjectState is the set of the corresponding Property values at the given time.
template<class _Metadata>
class ObjectTimeline : public _Metadata::Base
{
public:
	virtual ~ObjectTimeline();

	using Super = _Metadata::Base;
	using This = ObjectTimeline<_Metadata>;
	using PropertyOptionals = typename _Metadata::ObjectState;
	[[nodiscard]] PropertyOptionals GetStateAtTime(double time, StateAtEntryTimeBehavior entryTimeBehavior,
												   void* userData) const;
	/// \return The union of the time ranges of all PropertyTimelines for this object.
	[[nodiscard]] FTimeRangeInSeconds GetTimeRange() const;
	/// \return A valid range if the timeline is not empty, or FDateRange() otherwise
	[[nodiscard]] FDateRange GetDateRange() const;

	virtual void ToJson(TSharedRef<FJsonObject>& JsonObj) const;
};

//! A MainTimelineBase is a group of ObjectTimeline's.
template<class _ObjectTimeline>
class MainTimelineBase
{
public:
	using ObjectTimeline = _ObjectTimeline;
	using ObjectTimelinePtr = std::shared_ptr<_ObjectTimeline>;
	using TimelineObjectContainer = std::vector<ObjectTimelinePtr>;
	virtual ~MainTimelineBase() {}
	[[nodiscard]] const TimelineObjectContainer& GetContainer() const { return Container; }
	[[nodiscard]] TimelineObjectContainer& GetContainer() { return Container; }
	[[nodiscard]] const FTimeRangeInSeconds& GetTimeRange() const;
	/// \return A valid range if any of the contained timeline is not empty, or FDateRange() otherwise
	[[nodiscard]] FDateRange GetDateRange() const;
	void IncludeTimeRange(const _ObjectTimeline& Object);
	void IncludeTimeRange(const FTimeRangeInSeconds& CustomRange);
	ObjectTimelinePtr const& AddTimeline(const ObjectTimelinePtr& object);

protected:
	TimelineObjectContainer Container;

private:
	FTimeRangeInSeconds TimeRange = ITwin::Time::InitForMinMax();
};

} // namespace ITwin::Timeline

#include "TimelineBase.inl"
