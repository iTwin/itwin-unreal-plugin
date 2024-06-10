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
	return x.time_ == y.time_ && x.interpolation_ == y.interpolation_;
}

bool operator <(const PropertyEntryBase& x, const PropertyEntryBase& y)
{
	return x.time_ < y.time_;
}

std::size_t hash_value(const PropertyEntryBase& v) noexcept
{
	std::size_t seed = 0;
	boost::hash_combine(seed, v.time_);
	boost::hash_combine(seed, static_cast<int32_t>(v.interpolation_));
	return seed;
}

} // namespace ITwin::Timeline
