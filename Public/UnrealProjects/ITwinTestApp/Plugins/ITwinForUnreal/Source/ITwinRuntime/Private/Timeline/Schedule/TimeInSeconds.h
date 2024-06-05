/*--------------------------------------------------------------------------------------+
|
|     $Source: TimeInSeconds.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Math/Range.h>

using FTimeRangeInSeconds = std::pair<double, double>;///< Same origin as FDateTime

namespace ITwin::Time {

inline constexpr FTimeRangeInSeconds InitForMinMax()
{
	return std::make_pair(std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest());
}

inline FTimeRangeInSeconds Undefined()
{
	return InitForMinMax();
}

inline FTimespan ToTimespan(double const TimeInSeconds)
{
	return FTimespan(static_cast<int64>(TimeInSeconds * ETimespan::TicksPerSecond));
}

inline double FromTimespan(FTimespan const& Timespan)
{
	return Timespan.GetTicks() / (double)ETimespan::TicksPerSecond;
}

inline FDateTime ToDateTime(double const TimeInSeconds)
{
	return FDateTime(static_cast<int64>(TimeInSeconds * ETimespan::TicksPerSecond));
}

inline double FromDateTime(FDateTime const& DateTime)
{
	return DateTime.GetTicks() / (double)ETimespan::TicksPerSecond;
}

inline FDateRange ToDateRange(FTimeRangeInSeconds TimeRange)
{
	if (TimeRange == Undefined())
	{
		return FDateRange();
	}
	if (TimeRange.second < TimeRange.first)
	{
		check(false);
		std::swap(TimeRange.second, TimeRange.first);
	}
	return FDateRange(ToDateTime(TimeRange.first), ToDateTime(TimeRange.second));
}

/// Note: does not support partially open ranges like [some date;+inf[, it will just return Undefined()
inline FTimeRangeInSeconds FromDateRange(FDateRange const& DateRange)
{
	if (DateRange.HasLowerBound() && DateRange.HasUpperBound())
	{
		return std::make_pair(FromDateTime(DateRange.GetLowerBoundValue()),
							  FromDateTime(DateRange.GetLowerBoundValue()));
	}
	else
	{
		return Undefined();
	}
}

/// Copied from UInternationalizationSettingsModel::GetTimezoneValue() in order to output strings
/// compatible with what user input fields display and expects from FDateTime - until such time when I
/// understand why neither FDateTime::ToString nor FTextChronoFormatter::AsDateTime are able to output
/// the intended strings... -_-
inline int32 GetLocalTimeOffsetCode()
{
	// This is definitely a hack, but our FPlatformTime doesn't do timezones
	const FDateTime LocalNow = FDateTime::Now();
	const FDateTime UTCNow = FDateTime::UtcNow();

	const FTimespan Difference = LocalNow - UTCNow;

	const int32 MinutesDifference = FMath::RoundToInt(Difference.GetTotalMinutes());

	const int32 Hours = MinutesDifference / 60;
	const int32 Minutes = MinutesDifference % 60;

	const int32 Timezone = (Hours * 100) + Minutes;
	return Timezone;
}

/// Prints the UTC date and time to a string suitable for input in a FDateTime user field (eg. in the
/// Outliner), ie appending the time zone explicitly. This function uses the local time zone, eg. you will
/// get "2023.06.14-11.00.00 +0200" in GMT+2 when the UTC date is "2023.06.14-09.00.00"
inline FString UTCDateTimeToStringLocalTime(FDateTime const& DateTimeUtc)
{
	return FString::Printf(TEXT("%s %+04d"), *DateTimeUtc.ToString(), GetLocalTimeOffsetCode());
}

/// Prints the UTC date and time to a string suitable for input in a FDateTime user field (eg. in the
/// Outliner), ie appending the UTC time zone +0000 explicitly, eg. you will simply get
/// "2023.06.14-09.00.00 +0000" when the UTC date is "2023.06.14-09.00.00"
inline FString UTCDateTimeToString(FDateTime const& DateTimeUtc)
{
	return FString::Printf(TEXT("%s +0000"), *DateTimeUtc.ToString());
}

} // ns ITwin::Time
