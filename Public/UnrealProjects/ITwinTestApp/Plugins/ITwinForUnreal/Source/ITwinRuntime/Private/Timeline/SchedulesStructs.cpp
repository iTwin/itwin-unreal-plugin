/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesStructs.h"

#include <algorithm>
#include <cmath>

namespace
{
	FCalendarDayType const* FindCalendarDayType(FCalendar const& Calendar, FString const& DayTypeId)
	{
		if (DayTypeId.IsEmpty())
			return nullptr;
		auto const It = std::find_if(Calendar.DayTypes.begin(), Calendar.DayTypes.end(),
			[&DayTypeId](FCalendarDayType const& DayType) { return DayType.Id == DayTypeId; });
		return (It != Calendar.DayTypes.end()) ? &(*It) : nullptr;
	}

	bool IsCalendarWorkingDay(FCalendarDayType const* DayType)
	{
		return DayType
			&& (ECalendarDayType::Normal == DayType->Type || ECalendarDayType::Partial == DayType->Type);
	}
}

std::vector<FWorkingTimeKeyframe> FITwinSchedule::GetWorkingTimeKeyframes(FTimeRangeInSeconds const& TimeRange,
	FString const& CalendarId) const
{
	constexpr double SecondsPerDay = 86400.;
	std::vector<FWorkingTimeKeyframe> Keyframes;
	if (TimeRange.second < TimeRange.first) // includes ITwin::Time::Undefined()
		return Keyframes;
	auto const LinearProgression = [&TimeRange]()
		{
			return std::vector<FWorkingTimeKeyframe>{ { TimeRange.first, 0. }, { TimeRange.second, 1. } };
		};
	auto const Found = KnownCalendars.find(CalendarId);
	if (Found == KnownCalendars.end() || Found->second >= Calendars.size() || Calendars[Found->second].bDeleted)
		return LinearProgression();
	FCalendar const& Calendar = Calendars[Found->second];

	// Exceptions override the usual working week (unknown day types are ignored)
	std::unordered_map<int64, FCalendarDayType const*> ExceptionDays;
	for (auto&& Exception : Calendar.Exceptions)
	{
		if (FCalendarDayType const* DayType = FindCalendarDayType(Calendar, Exception.DayTypeId))
			ExceptionDays[static_cast<int64>(std::floor(Exception.Date / SecondsPerDay))] = DayType;
	}
	std::array<FCalendarDayType const*, 7> WeekDayTypes;
	for (size_t Day = 0; Day < WeekDayTypes.size(); ++Day)
		WeekDayTypes[Day] = FindCalendarDayType(Calendar, Calendar.WeekDayTypeIds[Day]);

	// Collect the blocks of working hours intersecting TimeRange. Start one day early because a time block can
	// end on the following day.
	std::vector<FTimeRangeInSeconds> WorkBlocks;
	int64 const FirstDay = std::max<int64>(0, static_cast<int64>(std::floor(TimeRange.first / SecondsPerDay)) - 1);
	int64 const LastDay = static_cast<int64>(std::floor(TimeRange.second / SecondsPerDay));
	for (int64 Day = FirstDay; Day <= LastDay; ++Day)
	{
		auto const Exception = ExceptionDays.find(Day);
		FCalendarDayType const* DayType = (Exception != ExceptionDays.end())
			? Exception->second
			: WeekDayTypes[(size_t)FDateTime(Day * ETimespan::TicksPerDay).GetDayOfWeek()];
		if (!IsCalendarWorkingDay(DayType))
			continue;
		double const DayStart = static_cast<double>(Day) * SecondsPerDay;
		for (auto&& Block : DayType->TimeBlocks)
		{
			double const BlockStart = DayStart + Block.TimeBlock.first;
			double BlockEnd = DayStart + Block.TimeBlock.second;
			if (Block.TimeBlock.second <= Block.TimeBlock.first)
				BlockEnd += SecondsPerDay;
			auto const Clipped = ITwin::Time::Intersection({ BlockStart, BlockEnd }, TimeRange);
			if (Clipped.first < Clipped.second)
				WorkBlocks.push_back(Clipped);
		}
	}

	// Sort and merge overlapping or contiguous blocks, so that no two keyframes share the same time
	std::sort(WorkBlocks.begin(), WorkBlocks.end());
	std::vector<FTimeRangeInSeconds> Merged;
	Merged.reserve(WorkBlocks.size());
	double TotalWorkingTime = 0.;
	for (auto&& Block : WorkBlocks)
	{
		if (!Merged.empty() && Block.first <= Merged.back().second)
		{
			if (Block.second > Merged.back().second)
			{
				TotalWorkingTime += Block.second - Merged.back().second;
				Merged.back().second = Block.second;
			}
		}
		else
		{
			Merged.push_back(Block);
			TotalWorkingTime += Block.second - Block.first;
		}
	}
	if (Merged.empty() || TotalWorkingTime <= 0.)
		return LinearProgression();

	Keyframes.reserve(2 * Merged.size());
	double WorkedTime = 0.;
	for (auto&& Block : Merged)
	{
		Keyframes.push_back({ Block.first, WorkedTime / TotalWorkingTime });
		WorkedTime += Block.second - Block.first;
		Keyframes.push_back({ Block.second, WorkedTime / TotalWorkingTime });
	}
	Keyframes.back().Progress = 1.; // avoid rounding errors
	return Keyframes;
}

size_t FITwinSchedule::NumGroups() const
{
	// No, when S+ schedule gets a binding update, ElemIDGroups only lacks the newly created group
	//ensure(ElemIDGroups.size() == FedGUIDGroups.size() || ElemIDGroups.empty() || FedGUIDGroups.empty());
	// Should test Generation, but keep this flexibility for test data
	return std::max(ElemIDGroups.size(), FedGUIDGroups.size());
	//switch (Generation)
	//{
	//case EITwinSchedulesGeneration::Legacy:
	//	return ElemIDGroups.size();
	//case EITwinSchedulesGeneration::NextGen:
	//	return FedGUIDGroups.size();
	//case EITwinSchedulesGeneration::Unknown:
	//	break;
	//}
	//return 0;
}

void FITwinSchedule::ResetElemIDGroups()
{
	ElemIDGroups.clear();
}

size_t FITwinSchedule::GetNextGroupID() const { return NumGroups(); }

void FITwinSchedule::CreateNextGroup(bool const bIsElemIDGroup)
{
	// Should test Generation, but keep this flexibility for test data
	if (bIsElemIDGroup)
		ElemIDGroups.emplace_back();
	else
		FedGUIDGroups.emplace_back();
}

void FITwinSchedule::CreateNextGroup(FElementsGroup&& Group)
{
	ElemIDGroups.emplace_back(std::move(Group));
}

bool FITwinSchedule::AddToGroup(size_t InVec, ITwinElementID const ElemID)
{
	// Skip the Generation test to allow pseudo-NextGen test data generated from a Legacy schedule's
	// requests cache to work
	if (ensure(/*EITwinSchedulesGeneration::Legacy == Generation &&*/ InVec < ElemIDGroups.size()))
	{
		return ElemIDGroups[InVec].insert(ElemID).second; // was inserted
	}
	return false;
}

bool FITwinSchedule::AddToGroup(size_t InVec, FGuid const FedGUID)
{
	if (ensure(EITwinSchedulesGeneration::NextGen == Generation && InVec < FedGUIDGroups.size()))
	{
		return FedGUIDGroups[InVec].insert(FedGUID).second; // was inserted
	}
	return false;
}
