/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesInternals.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <ITwinElementID.h>
#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/Timeline.h>
#include <CesiumMaterialType.h>

#include <mutex>
#include <unordered_set>

class UMaterialInterface;
class UObject;
class FITwinSchedule;

namespace TestSynchro4DQueries
{
	void MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);
}

namespace ITwinIModelImpl
{
	void OnNewTileMeshInScene(FITwinSynchro4DSchedulesInternals&, std::set<ITwinElementID> const&);
}

class FITwinSynchro4DSchedulesInternals
{
	friend class UITwinSynchro4DSchedules;
	// TODO_GCO: can contain several schedules for a given iModel:
	// TODO_GCO: should have one timeline ('Builder') for each!
	UITwinSynchro4DSchedules& Owner;
	FITwinScheduleTimelineBuilder Builder;
	FITwinSchedulesImport SchedulesApi; // <== must be declared AFTER Builder
	std::recursive_mutex& Mutex;
	std::vector<FITwinSchedule>& Schedules;

	friend void TestSynchro4DQueries::MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);
	friend void ITwinIModelImpl::OnNewTileMeshInScene(FITwinSynchro4DSchedulesInternals&,
													  std::set<ITwinElementID> const&);
	void MutateSchedules(std::function<void(std::vector<FITwinSchedule>&)> const& Func);

public:
	FITwinSynchro4DSchedulesInternals(UITwinSynchro4DSchedules& Owner, std::recursive_mutex& Mutex,
									  std::vector<FITwinSchedule>& Schedules);
	bool IsReady() const;
	bool MakeReady();
	void Reset(bool bDoNotBuildTimelines);

	static void GetAnimatableMaterials(UMaterialInterface*& OpaqueMat, UMaterialInterface*& TranslucentMat,
									   UObject& MaterialOwner);
	UMaterialInterface* GetMasterMaterial(ECesiumMaterialType Type,
										  UObject& MaterialOwner);

	[[nodiscard]] FITwinScheduleTimeline& Timeline();
	[[nodiscard]] FITwinScheduleTimeline const& GetTimeline() const;
	[[nodiscard]] FITwinElementTimeline const& GetElementTimeline(ITwinElementID const ElementID) const;
	[[nodiscard]] FString ElementTimelineAsString(ITwinElementID const ElementID) const;
	/// \param Func Function to execute for each schedule. Returning false will skip the visit of the
	///		remaining schedules not yet visited.
	void VisitSchedules(std::function<bool(FITwinSchedule const&)> const& Func) const;

	FITwinSchedulesImport& GetSchedulesApiReadyForUnitTesting();

	/// \param Deferred Passed as const because the whole timeline replay and interpolation code is
	///		const, but FDeferredPlaneEquation::planeEquation_ is mutable for the purpose of this method.
	/// \param ElementWorldBox We can only have it in world coordinates, precisely because Elements are
	///		batched, and we need it in world anyway, for the same reason (applying the same cutting plane
	///		to all Features of a given Element)
	static void FinalizeCuttingPlaneEquation(ITwin::Timeline::FDeferredPlaneEquation const& Deferred,
											 FBox const& ElementWorldBox);
};
