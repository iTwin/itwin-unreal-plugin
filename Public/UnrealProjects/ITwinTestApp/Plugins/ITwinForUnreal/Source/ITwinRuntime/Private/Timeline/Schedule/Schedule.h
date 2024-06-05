/*--------------------------------------------------------------------------------------+
|
|     $Source: Schedule.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Timeline/Schedule/TimeInSeconds.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/multi_index_container.hpp>
	#include <boost/multi_index/identity.hpp>
	#include <boost/multi_index/random_access_index.hpp>
	#include <boost/multi_index/hashed_index.hpp>
	#include <boost/operators.hpp>
	#include <boost/optional.hpp>
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

namespace ITwin::Schedule
{

//! Defines how values are computed (interpolated) between 2 entries.
enum class InterpolationMode: int32_t
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
class PropertyEntryBase: boost::equality_comparable<PropertyEntryBase>
{
public:
	double time_ = {};
	InterpolationMode interpolation_ = {};
};

std::size_t hash_value(const PropertyEntryBase& v) noexcept;
bool operator ==(const PropertyEntryBase& x, const PropertyEntryBase& y);
bool operator <(const PropertyEntryBase& x, const PropertyEntryBase& y);

//! Generic entry with custom values.
template<class _Values>
class PropertyEntry
	:public PropertyEntryBase
	,public _Values
	,boost::equality_comparable<PropertyEntry<_Values>>
{
};

template<class _Values>
std::size_t hash_value(const PropertyEntry<_Values>& v) noexcept;
template<class _Values>
bool operator ==(const PropertyEntry<_Values>& x, const PropertyEntry<_Values>& y);

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
class PropertyTimeline: boost::equality_comparable<PropertyTimeline<_PropertyValues>>
{
public:
	using PropertyValues = _PropertyValues;
	//! Property keyframes, ordered by PropertyEntryBase::time_
	using FTimeOrderedProperties = std::set<PropertyEntry<_PropertyValues>>;
	FTimeOrderedProperties list_;
	//! Removes duplicate, useless entries that may exist at the end of the list.
	void Prune();
	//! Returns the interpolated property values at the given time.
	[[nodiscard]] boost::optional<_PropertyValues> GetStateAtTime(double time,
		StateAtEntryTimeBehavior entryTimeBehavior, void* userData) const;
};

template<class _PropertyValues>
std::size_t hash_value(const PropertyTimeline<_PropertyValues>& v) noexcept;
template<class _PropertyValues>
bool operator ==(const PropertyTimeline<_PropertyValues>& x, const PropertyTimeline<_PropertyValues>& y);

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
class ObjectTimeline: public _Metadata::Base
{
public:
	virtual ~ObjectTimeline();

	using This = ObjectTimeline<_Metadata>;
	using PropertyOptionals = typename _Metadata::ObjectState;
	[[nodiscard]] PropertyOptionals GetStateAtTime(double time, StateAtEntryTimeBehavior entryTimeBehavior,
												   void* userData) const;
	//! Returns the union of the time ranges of all PropertyTimelines for this object.
	[[nodiscard]] FTimeRangeInSeconds GetTimeRange() const;
	[[nodiscard]] FDateRange GetDateRange() const;

	virtual void ToJson(TSharedRef<FJsonObject>& JsonObj) const;
};

template<class _Metadata>
std::size_t hash_value(const ObjectTimeline<_Metadata>& timeline) noexcept;

//! A MainTimeline is a group of ObjectTimelines.
template<class _ObjectTimeline>
class MainTimeline
{
public:
	using ObjectTimeline = _ObjectTimeline;
	using ObjectTimelinePtr = std::shared_ptr<_ObjectTimeline>;
	using TimelineObjectContainer = boost::multi_index_container<
		ObjectTimelinePtr,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<>,
			boost::multi_index::hashed_unique<boost::multi_index::identity<ObjectTimelinePtr>>,
			boost::multi_index::hashed_unique<boost::multi_index::identity<_ObjectTimeline>>
		>
	>;
	CONSTRUCT_ENUMERATION(TimelineObjectContainerTags, (Index, Ptr, Value));
	virtual ~MainTimeline() {}
	[[nodiscard]] const TimelineObjectContainer& GetContainer() const { return container_; }
	[[nodiscard]] const FTimeRangeInSeconds& GetTimeRange() const;
	[[nodiscard]] FDateRange GetDateRange() const;
	virtual ObjectTimelinePtr Add(const ObjectTimelinePtr& object);
	void IncludeTimeRange(const _ObjectTimeline& object);
protected:
	[[nodiscard]] TimelineObjectContainer& Container() { return container_; }
private:
	TimelineObjectContainer container_;
	FTimeRangeInSeconds timeRange_ = ITwin::Time::InitForMinMax();
};

} // namespace ITwin::Schedule

#include "Schedule.inl"
