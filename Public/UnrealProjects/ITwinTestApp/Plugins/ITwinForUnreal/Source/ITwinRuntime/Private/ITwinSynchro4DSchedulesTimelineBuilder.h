/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <ITwinElementID.h>
#include <Timeline/Timeline.h>

#include <Templates/PimplPtr.h>

#include <functional>
#include <mutex>
#include <vector>

using FOnElementsTimelineModified =
	std::function<void(FITwinElementTimeline&,  std::vector<ITwinElementID> const*)>;
struct FITwinCoordConversions;
class FITwinSchedule;
class FITwinScheduleStats;
using FSchedLock = std::lock_guard<std::recursive_mutex>;

class FITwinScheduleTimelineBuilder
{
public:
	FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules const& InOwner,
								  FITwinCoordConversions const& InCoordConv);
	FITwinScheduleTimelineBuilder(FITwinScheduleTimelineBuilder&& Other) { *this = std::move(Other); }
	FITwinScheduleTimelineBuilder& operator=(FITwinScheduleTimelineBuilder&& Other);
	~FITwinScheduleTimelineBuilder();
	void FinalizeTimeline(FITwinSchedule& Schedule);
	static FITwinScheduleTimelineBuilder CreateForUnitTesting(FITwinCoordConversions const&);
	void Initialize(FOnElementsTimelineModified&& InOnElementsTimelineModified);
	/// We need to uninitialize manually before the destructor is called: this is because the data we access
	/// belongs to the iModel SceneMapping and, counter-intuitively, the iModel is destroyed _before_ its
	/// ScheduleComponent (when stopping PIE or closing the app)!
	void Uninitialize();

	FITwinScheduleTimeline& Timeline();
	FITwinScheduleTimeline const& GetTimeline() const;

	void DebugDumpFullTimelinesAsJson(FString const& RelPath) const;

private:
	friend class FITwinSynchro4DSchedulesInternals;
	friend class FSynchro4DImportTestHelper;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	enum class EInit : uint8_t { Pending, Ready, Disposable };
	EInit InitState = EInit::Pending;

	void AddAnimationBindingToTimeline(FITwinSchedule const& Schedule, size_t const AnimationBindingIndex,
									   FSchedLock& Lock);
	void OnReceivedScheduleStats(FITwinScheduleStats const& Stats, FSchedLock&);
	FITwinScheduleTimelineBuilder();
	bool IsUnitTesting() const;
};