/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBase.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TimelineBase.h"
#include "Interpolators.h"
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

template<class _PropertyValues>
void PropertyTimeline<_PropertyValues>::Prune()
{
	// We are going to remove the last entries if they are useless, starting from the last one.
	// An entry #N is useless if it contains the same value as entry #N-1.
	// However, we still need to keep the last entry in the list, so that the time range of the
	// entire animation remains correct.
	// Otherwise, the time range displayed in the timeline may be shortened, which is confusing
	// for the user, because he expects to see the same dates as in Synchro.
	std::optional<PropertyEntry<_PropertyValues>> lastEntry;
	while (Values.size() >= 2 &&
		static_cast<_PropertyValues const&>(*Values.rbegin())
			== static_cast<_PropertyValues const&>(*(++Values.rbegin())))
	{
		// Remember last entry.
		if (!lastEntry)
			lastEntry = std::move(*Values.rbegin());
		Values.erase(--Values.end());
	}
	// If last entry has been removed, restore it.
	if (lastEntry)
		Values.insert(std::move(*lastEntry));
}

template<class _PropertyValues>
bool PropertyTimeline<_PropertyValues>::HasNoEffect() const
{
	for (auto&& Val : Values)
		if (!NoEffect(static_cast<_PropertyValues const&>(Val))) return false;
	return true;
}

template<class _PropertyValues>
std::optional<_PropertyValues> PropertyTimeline<_PropertyValues>::GetStateAtTime(double time,
	StateAtEntryTimeBehavior entryTimeBehavior, void* userData) const
{
	if (Values.empty())
		return {};
	// To be consistent with iModel.js behavior, we do this special case for when current time is equal to
	// or greater than the last entry time. In this case we return the last entry, ignoring entryTimeBehavior.
	// Without this special case, when all the following consitions are met:
	// - current time is exactly equal to the last entry time,
	// - entryTimeBehavior == StateAtEntryTimeBehavior::UseLeftInterval
	// - second to last entry has "step" interpolation
	// then we would return the second to last entry, instead of the last entry.
	if (time >= Values.rbegin()->Time)
		return *Values.rbegin();
	// "auto entryIt" resolves to a std::forward_iterator (at least in VS2022 17.8.5) and fails to compile,
	// because we need a std::bidirectional_iterator, which std::set::[const_]iterator actually is...!
	//const auto entryIt = ...
	typename FTimeOrderedProperties::const_iterator entryIt =
		(entryTimeBehavior == StateAtEntryTimeBehavior::UseLeftInterval)
		? std::lower_bound(Values.cbegin(), Values.cend(), time,
						   [](const auto& x, double y) { return x.Time < y; })
		: std::upper_bound(Values.cbegin(), Values.cend(), time,
						   [](double x, auto const& y) { return x < y.Time; });
	if (entryIt == Values.cbegin())
		return *entryIt;
	if (entryIt == Values.cend())
		return *Values.rbegin();
	const auto& entry1 = *(entryIt);
	const auto& entry0 = *(--entryIt);
	switch (entry0.Interpolation)
	{
		case EInterpolation::Step:
			return entry0;
		case EInterpolation::Next:
			return entry1;
		case EInterpolation::Linear:
		{
			_PropertyValues result;
			const float u = float((time - entry0.Time) / (entry1.Time - entry0.Time));
			decltype(_iTwinTimelineGetInterpolators(_PropertyValues{})) interpolators;
			using Zipped = boost::fusion::vector<_PropertyValues&, const _PropertyValues&,
				const _PropertyValues&, const decltype(interpolators)&>;
			Interpolators::FContinue bContinue(true);
			boost::fusion::for_each(
				boost::fusion::zip_view<Zipped>(Zipped(result, entry0, entry1, interpolators)),
				[userData, &bContinue, u](const auto& tuple)
				{
					if (!bContinue) return;
					auto&& x0 = boost::fusion::at_c<1>(tuple);
					auto&& x1 = boost::fusion::at_c<2>(tuple);
					auto&& interpolator = boost::fusion::at_c<3>(tuple);
					bContinue = interpolator(boost::fusion::at_c<0>(tuple), x0, x1, u, userData);
				});
			return result;
		}
		BE_NO_UNCOVERED_ENUM_ASSERT_AND_BREAK
	}
	return {};
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
			if (propertyTimeline.Values.empty())
				return;
			timeRange.first = std::min(timeRange.first, propertyTimeline.Values.begin()->Time);
			timeRange.second = std::max(timeRange.second, propertyTimeline.Values.rbegin()->Time);
		});
	return timeRange;
}

template<class _Metadata>
FDateRange ObjectTimeline<_Metadata>::GetDateRange() const
{
	auto const TimeRange = GetTimeRange();
	return (TimeRange.first < TimeRange.second) ? ITwin::Time::ToDateRange(TimeRange) : FDateRange();
}

template<class _Metadata>
void ObjectTimeline<_Metadata>::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	FInternationalization& I18N = FInternationalization::Get();
	FDateRange const TimeRange = GetDateRange();
	if (TimeRange.HasLowerBound())
		JsonObj->SetStringField(TEXT("startTime"),
			ITwin::Time::UTCDateTimeToString(TimeRange.GetLowerBound().GetValue()));
	else
		JsonObj->SetStringField(TEXT("startTime"), TEXT("<wrong startTime?!>"));
	if (TimeRange.HasUpperBound())
		JsonObj->SetStringField(TEXT("endTime"),
			ITwin::Time::UTCDateTimeToString(TimeRange.GetUpperBound().GetValue()));
	else
		JsonObj->SetStringField(TEXT("endTime"), TEXT("<wrong endTime?!>"));
	static const std::vector<FString> HardcodedNames(
		{ TEXT("Visiblity"), TEXT("Color"), TEXT("Transform"), TEXT("CuttingPlane") });
	int HardcodedIndex = -1;
	boost::fusion::for_each(*this, [&HardcodedIndex, &JsonObj](const auto& propertyTimeline)
		{
			++HardcodedIndex;
			if (propertyTimeline.HasNoEffect())
				return;
			TArray<TSharedPtr<FJsonValue>> Keys, Values;
			for (auto&& Keyframe : propertyTimeline.Values)
			{
				Keys.Add(MakeShared<FJsonValueString>(
					ITwin::Time::UTCDateTimeToString(ITwin::Time::ToDateTime(Keyframe.Time))));
			}
			Values.Reserve(Keys.Num());
			for (auto&& Keyframe : propertyTimeline.Values)
			{
				auto&& Val = ToJsonValue(Keyframe);
				Values.Add(std::move(Val));
			}
			//using TimelineType =
			//	std::remove_reference_t<std::remove_const_t<decltype(propertyTimeline)>>;
			JsonObj->SetArrayField(
				// Can't make that work because of unhelpful compile errors... maybe with std::decay_t above?
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

template<class _ObjectTimeline>
const FTimeRangeInSeconds& MainTimelineBase<_ObjectTimeline>::GetTimeRange() const
{
	return TimeRange;
}

template<class _ObjectTimeline>
FDateRange MainTimelineBase<_ObjectTimeline>::GetDateRange() const
{
	return (TimeRange.first < TimeRange.second) ? ITwin::Time::ToDateRange(TimeRange) : FDateRange();
}

template<class _ObjectTimeline>
void MainTimelineBase<_ObjectTimeline>::IncludeTimeRange(_ObjectTimeline const& ObjectTimeline)
{
	const auto ObjectTimeRange = ObjectTimeline.GetTimeRange();
	TimeRange.first = std::min(TimeRange.first, ObjectTimeRange.first);
	TimeRange.second = std::max(TimeRange.second, ObjectTimeRange.second);
}

template<class _ObjectTimeline>
void MainTimelineBase<_ObjectTimeline>::IncludeTimeRange(FTimeRangeInSeconds const& CustomRange)
{
	TimeRange.first = std::min(TimeRange.first, CustomRange.first);
	TimeRange.second = std::max(TimeRange.second, CustomRange.second);
}

template<class _ObjectTimeline>
std::shared_ptr<_ObjectTimeline> const& MainTimelineBase<_ObjectTimeline>::AddTimeline(
	const std::shared_ptr<_ObjectTimeline>& Timeline)
{
	Container.push_back(Timeline);
	// Note: "AddTimeline" is now called from ElementTimelineFor, when creating an empty timeline, in that
	// case this call is pointless, but let's keep it to preserve also the other use case (adding an already
	// filled timeline, for example in unit testing - see other call to IncludeTimeRange done only in
	// FITwinIModelInternals::OnElementsTimelineModified)
	// Note 2: IncludeTimeRange is also now included directly from SchedulesImport.cpp in case of pre-fetching
	// of all Tasks, in which case this is again redundant.
	IncludeTimeRange(*Timeline);
	return Container.back();
}

} // namespace ITwin::Timeline
