/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBase.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TimelineBase.h"
#include "TimeInSeconds.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/fusion/include/for_each.hpp>
	#include <boost/fusion/include/zip.hpp>
	#include <BeHeaders/Compil/EnumSwitchCoverage.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <type_traits>

namespace ITwin::Timeline
{

template<class _Values>
std::size_t hash_value(const PropertyEntry<_Values>& v) noexcept
{
	std::size_t seed = 0;
	boost::hash_combine(seed, (const PropertyEntryBase&)v);
	boost::hash_combine(seed, (const _Values&)v);
	return seed;
}

template<class _Values>
bool operator ==(const PropertyEntry<_Values>& x, const PropertyEntry<_Values>& y)
{
	return (const PropertyEntryBase&)x == (const PropertyEntryBase&)y &&
		(const _Values&)x == (const _Values&)y;
}

template<class _PropertyValues>
void PropertyTimeline<_PropertyValues>::Prune()
{
	// We are going to remove the last entries if they are useless, starting from the last one.
	// An entry #N is useless if it contains the same value as entry #N-1.
	// However, we still need to keep the last entry in the list, so that the time range of the
	// entire animation remains correct.
	// Otherwise, the time range displayed in the timeline may be shortened, which is confusing
	// for the user, because he expects to see the same dates as in Synchro.
	boost::optional<PropertyEntry<_PropertyValues>> lastEntry;
	while (list_.size() >= 2 &&
		static_cast<_PropertyValues&>(list_[list_.size()-1]) == static_cast<_PropertyValues&>(list_[list_.size()-2]))
	{
		// Remember last entry.
		if (!lastEntry)
			lastEntry = std::move(*list_.rbegin());
		list_.pop_back();
	}
	// If last entry has been removed, restore it.
	if (lastEntry)
		list_.push_back(std::move(*lastEntry));
}

template<class _PropertyValues>
boost::optional<_PropertyValues> PropertyTimeline<_PropertyValues>::GetStateAtTime(double time,
	StateAtEntryTimeBehavior entryTimeBehavior, void* userData) const
{
	if (list_.empty())
		return {};
	// To be consistent with iModel.js behavior, we do this special case for when current time is equal to
	// or greater than the last entry time. In this case we return the last entry, ignoring entryTimeBehavior.
	// Without this special case, when all the following consitions are met:
	// - current time is exactly equal to the last entry time,
	// - entryTimeBehavior == StateAtEntryTimeBehavior::UseLeftInterval
	// - second to last entry has "step" interpolation
	// then we would return the second to last entry, instead of the last entry.
	if (time >= list_.rbegin()->time_)
		return *list_.rbegin();
	// "auto entryIt" resolves to a std::forward_iterator (at least in VS2022 17.8.5) and fails to compile,
	// because we need a std::bidirectional_iterator, which std::set::[const_]iterator actually is...!
	//const auto entryIt = ...
	typename FTimeOrderedProperties::const_iterator entryIt =
		(entryTimeBehavior == StateAtEntryTimeBehavior::UseLeftInterval)
		? std::lower_bound(list_.cbegin(), list_.cend(), time,
						   [](const auto& x, double y) { return x.time_ < y; })
		: std::upper_bound(list_.cbegin(), list_.cend(), time,
						   [](double x, auto const& y) { return x < y.time_; });
	if (entryIt == list_.cbegin())
		return *entryIt;
	if (entryIt == list_.cend())
		return *list_.rbegin();
	const auto& entry1 = *(entryIt);
	const auto& entry0 = *(--entryIt);
	switch (entry0.interpolation_)
	{
		case Interpolation::Step:
			return entry0;
		case Interpolation::Next:
			return entry1;
		case Interpolation::Linear:
		{
			_PropertyValues result;
			const float u = float((time-entry0.time_)/(entry1.time_-entry0.time_));
			decltype(_iTwinTimelineGetInterpolators(_PropertyValues{})) interpolators;
			using Zipped = boost::fusion::vector<_PropertyValues&, const _PropertyValues&,
				const _PropertyValues&, const decltype(interpolators)&>;
			boost::fusion::for_each(
				boost::fusion::zip_view<Zipped>(Zipped(result, entry0, entry1, interpolators)),
				[userData, u](const auto& tuple)
				{
					auto&& x0 = boost::fusion::at_c<1>(tuple);
					auto&& x1 = boost::fusion::at_c<2>(tuple);
					auto&& interpolator = boost::fusion::at_c<3>(tuple);
					interpolator.WillInterpolateBetween(x0, x1, userData);
					boost::fusion::at_c<0>(tuple) = interpolator(x0, x1, u);
				});
			return result;
		}
		BE_NO_UNCOVERED_ENUM_ASSERT_AND_BREAK
	}
	return {};
}

template<class _PropertyValues>
std::size_t hash_value(const PropertyTimeline<_PropertyValues>& v) noexcept
{
	return boost::hash_value(v.list_);
}

template<class _PropertyValues>
bool operator ==(const PropertyTimeline<_PropertyValues>& x, const PropertyTimeline<_PropertyValues>& y)
{
	return x.list_ == y.list_;
}

template<class _Metadata> ObjectTimeline<_Metadata>::~ObjectTimeline() {}

template<class _Metadata>
typename _Metadata::ObjectState ObjectTimeline<_Metadata>::GetStateAtTime(double time,
	StateAtEntryTimeBehavior entryTimeBehavior, void* userData) const
{
	typename _Metadata::ObjectState state;
	using Zipped = boost::fusion::vector<const This&, typename _Metadata::ObjectState&>;
	boost::fusion::for_each(boost::fusion::zip_view<Zipped>(Zipped(*this, state)), [&](const auto& tuple)
		{
			boost::fusion::at_c<1>(tuple) =
				boost::fusion::at_c<0>(tuple).GetStateAtTime(time, entryTimeBehavior, userData);
		});
	return state;
}

template<class _Metadata>
FTimeRangeInSeconds ObjectTimeline<_Metadata>::GetTimeRange() const
{
	FTimeRangeInSeconds timeRange = ITwin::Time::InitForMinMax();
	boost::fusion::for_each(*this, [&](const auto& propertyTimeline)
		{
			if (propertyTimeline.list_.empty())
				return;
			timeRange.first = std::min(timeRange.first, propertyTimeline.list_.begin()->time_);
			timeRange.second = std::max(timeRange.second, propertyTimeline.list_.rbegin()->time_);
		});
	return timeRange;
}

template<class _Metadata>
FDateRange ObjectTimeline<_Metadata>::GetDateRange() const
{
	return ITwin::Time::ToDateRange(GetTimeRange());
}

template<class _Metadata>
void ObjectTimeline<_Metadata>::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	FInternationalization& I18N = FInternationalization::Get();
	FCultureRef Culture = I18N.GetInvariantCulture();
	FDateRange const TimeRange = GetDateRange();
	if (TimeRange.HasLowerBound())
		JsonObj->SetStringField("startTime",
			ITwin::Time::UTCDateTimeToString(TimeRange.GetLowerBound().GetValue()));
	else
		JsonObj->SetStringField("startTime", TEXT("<wrong startTime?!>"));
	if (TimeRange.HasUpperBound())
		JsonObj->SetStringField("endTime",
			ITwin::Time::UTCDateTimeToString(TimeRange.GetUpperBound().GetValue()));
	else
		JsonObj->SetStringField("endTime", TEXT("<wrong endTime?!>"));
	static const std::vector<FString> HardcodedNames(
		{ TEXT("Visiblity"), TEXT("Color"), TEXT("Transform"), TEXT("CuttingPlane") });
	int HardcodedIndex = -1;
	boost::fusion::for_each(*this, [&HardcodedIndex, &JsonObj, &Culture](const auto& propertyTimeline)
		{
			++HardcodedIndex;
			if (propertyTimeline.list_.empty())
				return;
			TArray<TSharedPtr<FJsonValue>> Keys, Values;
			for (auto&& Keyframe : propertyTimeline.list_)
			{
				Keys.Add(MakeShared<FJsonValueString>(
					ITwin::Time::UTCDateTimeToString(ITwin::Time::ToDateTime(Keyframe.time_))));
			}
			Values.Reserve(Keys.Num());
			for (auto&& Keyframe : propertyTimeline.list_)
			{
				auto&& Val = ToJsonValue(Keyframe);
				Values.Add(std::move(Val));
			}
			//using TimelineType =
			//	std::remove_reference_t<std::remove_const_t<decltype(propertyTimeline)>>;
			JsonObj->SetArrayField(
				// Can't make that work because of unhelpful compile errors...:-(
				//ITwin::Timeline::_iTwinTimelineGetPropertyName<TimelineType::PropertyValues>()
				HardcodedNames[HardcodedIndex]
					+ TEXT("Times"),
				Keys);
			JsonObj->SetArrayField(
				//ITwin::Timeline::_iTwinTimelineGetPropertyName<TimelineType::PropertyValues>()
				HardcodedNames[HardcodedIndex]
					+ TEXT("Values"),
				Values);
		});
}

template<class _Metadata>
std::size_t hash_value(const ObjectTimeline<_Metadata>& timeline) noexcept
{
	size_t seed = 0;
	boost::fusion::for_each(timeline, [&](const auto& propertyTimeline)
		{
			boost::hash_combine(seed, propertyTimeline);
		});
	return seed;
}

template<class _ObjectTimeline>
const FTimeRangeInSeconds& MainTimelineBase<_ObjectTimeline>::GetTimeRange() const
{
	return timeRange_;
}

template<class _ObjectTimeline>
FDateRange MainTimelineBase<_ObjectTimeline>::GetDateRange() const
{
	return ITwin::Time::ToDateRange(timeRange_);
}

template<class _ObjectTimeline>
void MainTimelineBase<_ObjectTimeline>::IncludeTimeRange(const _ObjectTimeline& object)
{
	const auto objectTimeRange = object.GetTimeRange();
	timeRange_.first = std::min(timeRange_.first, objectTimeRange.first);
	timeRange_.second = std::max(timeRange_.second, objectTimeRange.second);
}

template<class _ObjectTimeline>
std::shared_ptr<_ObjectTimeline> MainTimelineBase<_ObjectTimeline>::Add(const ObjectTimelinePtr& object)
{
	auto result = container_.push_back(object);
	// Note: "Add" is now called from ElementTimelineFor, when creating an empty timeline...
	IncludeTimeRange(*object);
	return result.second ? object : *result.first;
}

} // namespace ITwin::Timeline
