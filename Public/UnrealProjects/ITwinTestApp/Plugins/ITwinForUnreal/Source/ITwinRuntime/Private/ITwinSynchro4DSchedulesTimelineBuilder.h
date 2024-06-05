/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <Timeline/Timeline.h>

using FOnElementTimelineModified = std::function<void(FITwinElementTimeline const&)>;
class FAnimationBinding;
class FAppearanceProfile;

class FITwinScheduleTimelineBuilder
{
	friend class FITwinSynchro4DSchedulesInternals;

	UITwinSynchro4DSchedules* Owner;
	FITwinScheduleTimeline MainTimeline;
	FOnElementTimelineModified OnElementTimelineModified;

	void AddAnimationBindingToTimeline(FAnimationBinding const& AnimationBinding,
									   FAppearanceProfile const& Profile);

public:
	FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules& InOwner) : Owner(&InOwner)
	{
	}

	FITwinScheduleTimeline& Timeline() { return MainTimeline; }
	FITwinScheduleTimeline const& GetTimeline() const { return MainTimeline; }

	void SetOnElementTimelineModified(FOnElementTimelineModified const& InOnElementTimelineModified)
	{
		check(!OnElementTimelineModified);
		OnElementTimelineModified = InOnElementTimelineModified;
	}
};