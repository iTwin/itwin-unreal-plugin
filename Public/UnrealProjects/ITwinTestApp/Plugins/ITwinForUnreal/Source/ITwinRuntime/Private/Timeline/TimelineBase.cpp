/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBase.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "TimelineBase.h"

namespace ITwin::Timeline {

bool operator ==(const PropertyEntryBase& x, const PropertyEntryBase& y)
{
	return x.Time == y.Time && x.Interpolation == y.Interpolation;
}

bool operator <(const PropertyEntryBase& x, const PropertyEntryBase& y)
{
	return x.Time < y.Time;
}

std::size_t hash_value(const PropertyEntryBase& v) noexcept
{
	std::size_t seed = 0;
	boost::hash_combine(seed, v.Time);
	boost::hash_combine(seed, static_cast<int32_t>(v.Interpolation));
	return seed;
}

} // namespace ITwin::Timeline
