/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <ITwinElementID.h>
#include <Timeline/Timeline.h>

#include <mutex>
#include <set>
#include <unordered_set>

using FOnElementsTimelineModified =
	std::function<void(FITwinElementTimeline&,  std::vector<ITwinElementID> const*)>;
class FITwinSchedule;
using FSchedLock = std::lock_guard<std::recursive_mutex>;

class FITwinScheduleTimelineBuilder
{
	friend class FITwinSynchro4DSchedulesInternals;

	UITwinSynchro4DSchedules const* Owner;
	std::optional<FTransform> const* IModel2UnrealTransfo;
	FVector SynchroOriginUE;
	FITwinScheduleTimeline MainTimeline;
	FOnElementsTimelineModified OnElementsTimelineModified;

	void AddAnimationBindingToTimeline(FITwinSchedule const& Schedule, size_t const AnimationBindingIndex,
									   FSchedLock& Lock);
	void UpdateAnimationGroupInTimeline(size_t const GroupIdx, std::set<ITwinElementID> const& GroupElements,
										FSchedLock&);

public:
	FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules const& InOwner,
			std::optional<FTransform> const& InIModel2UnrealTransfo, FVector const& InSynchroOriginUE)
		: Owner(&InOwner)
		, IModel2UnrealTransfo(&InIModel2UnrealTransfo)
		, SynchroOriginUE(InSynchroOriginUE)
	{
	}

	FITwinScheduleTimeline& Timeline() { return MainTimeline; }
	FITwinScheduleTimeline const& GetTimeline() const { return MainTimeline; }

	void SetOnElementsTimelineModified(FOnElementsTimelineModified const& InOnElementsTimelineModified)
	{
		check(!OnElementsTimelineModified);
		OnElementsTimelineModified = InOnElementsTimelineModified;
	}
};