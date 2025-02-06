/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <ITwinElementID.h>
#include <Timeline/Timeline.h>

#include <functional>
#include <mutex>
#include <optional>
#include <vector>

using FOnElementsTimelineModified =
	std::function<void(FITwinElementTimeline&,  std::vector<ITwinElementID> const*)>;
class FITwinCoordConversions;
class FITwinSchedule;
using FSchedLock = std::lock_guard<std::recursive_mutex>;

class FITwinScheduleTimelineBuilder
{
	friend class FITwinSynchro4DSchedulesInternals;

	UITwinSynchro4DSchedules const* Owner;
	FITwinCoordConversions const* CoordConversions;
	FITwinScheduleTimeline MainTimeline;
	FOnElementsTimelineModified OnElementsTimelineModified;
	enum class EInit : uint8_t { Pending, Ready, Disposable };
	EInit InitState = EInit::Pending;

	void AddAnimationBindingToTimeline(FITwinSchedule const& Schedule, size_t const AnimationBindingIndex,
									   FSchedLock& Lock);
	void UpdateAnimationGroupInTimeline(size_t const GroupIdx, FElementsGroup const& GroupElements,
										FSchedLock&);

public:
	FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules const& InOwner,
								  FITwinCoordConversions const& InCoordConv);
	FITwinScheduleTimelineBuilder(FITwinScheduleTimelineBuilder&& Other) { *this = std::move(Other); }
	FITwinScheduleTimelineBuilder& operator=(FITwinScheduleTimelineBuilder&& Other);
	~FITwinScheduleTimelineBuilder();
	void Initialize(FOnElementsTimelineModified&& InOnElementsTimelineModified);
	/// We need to uninitialize manually before the destructor is called: this is because the data we access
	/// belongs to the iModel SceneMapping and, counter-intuitively, the iModel is destroyed _before_ its
	/// ScheduleComponent (when stopping PIE or closing the app)!
	void Uninitialize();

	FITwinScheduleTimeline& Timeline() { return MainTimeline; }
	FITwinScheduleTimeline const& GetTimeline() const { return MainTimeline; }
};