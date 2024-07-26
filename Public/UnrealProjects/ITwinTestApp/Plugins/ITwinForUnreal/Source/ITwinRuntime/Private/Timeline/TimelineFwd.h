/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineFwd.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

namespace ITwin
{
	namespace Timeline
	{
		class MainTimeline;
		class ElementTimelineEx;
		enum class EGrowthStatus : uint8_t;
	}
}
using FITwinScheduleTimeline = ITwin::Timeline::MainTimeline;
using FITwinElementTimeline = ITwin::Timeline::ElementTimelineEx;
