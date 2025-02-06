/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineBase.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "TimelineBase.h"

namespace ITwin::Timeline {

bool operator <(const PropertyEntryBase& x, const PropertyEntryBase& y)
{
	return x.Time < y.Time;
}

} // namespace ITwin::Timeline
