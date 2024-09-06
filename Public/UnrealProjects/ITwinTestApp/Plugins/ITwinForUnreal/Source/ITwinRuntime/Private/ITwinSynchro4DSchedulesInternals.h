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
#include <CesiumTileIDHash.h>

#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

using CesiumTileID = Cesium3DTilesSelection::TileID;

class UMaterialInterface;
class UObject;
class FITwinSchedule;

namespace TestSynchro4DQueries
{
	void MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);
}

class FITwinSynchro4DSchedulesInternals
{
	friend class UITwinSynchro4DSchedules;
	// TODO_GCO: can contain several schedules for a given iModel:
	// TODO_GCO: should have one timeline ('Builder') for each!
	UITwinSynchro4DSchedules& Owner;
	const bool bDoNotBuildTimelines; ///< defaults to false, true only for internal unit testing
	FITwinScheduleTimelineBuilder Builder;
	/// The value tells whether the range is valid or not (empty schedule)
	std::optional<bool> ScheduleTimeRangeIsKnown;
	FITwinSchedulesImport SchedulesApi; // <== must be declared AFTER Builder
	std::recursive_mutex& Mutex;
	std::vector<FITwinSchedule>& Schedules;

	/// Query deferred to the next tick because otherwise textures (highlights/opacities, cut planes...) may
	/// be allocated once before the full tile was notified, and would have had to be resized later...
	/// Not straightforward to handle, and this way we'll have fewer (batches of) queries anyway.
	/// The map value needs to be ordered because of the set_intersection in 
	/// FITwinSynchro4DSchedulesInternals::HandleReceivedElements :/
	std::unordered_map<CesiumTileID, std::set<ITwinElementID>> ElementsReceived;

	friend void TestSynchro4DQueries::MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);

	void MutateSchedules(std::function<void(std::vector<FITwinSchedule>&)> const& Func);
	/// Deferred processing of the Elements which were notified during the last tick ('last' to avoid doing
	/// anything before the whole tile has been loaded, since we are notified of the tile meshes one by one)
	void HandleReceivedElements(bool& bNewTilesReceived);
	void UpdateConnection(bool const bOnlyIfReady);
	bool ResetSchedules();
	void Reset();
	bool IsReady() const;

public:
	FITwinSynchro4DSchedulesInternals(UITwinSynchro4DSchedules& Owner, bool const InDoNotBuildTimelines,
									  std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules);

	static void GetAnimatableMaterials(UMaterialInterface*& OpaqueMat, UMaterialInterface*& TranslucentMat,
									   UObject& MaterialOwner);
	UMaterialInterface* GetMasterMaterial(ECesiumMaterialType Type,
										  UObject& MaterialOwner);

	[[nodiscard]] FITwinScheduleTimeline& Timeline();
	[[nodiscard]] FITwinScheduleTimeline const& GetTimeline() const;
	void ForEachElementTimeline(ITwinElementID const ElementID,
								std::function<void(FITwinElementTimeline const&)> const& Func) const;
	[[nodiscard]] FString ElementTimelineAsString(ITwinElementID const ElementID) const;
	/// \param Func Function to execute for each schedule. Returning false will skip the visit of the
	///		remaining schedules not yet visited.
	void VisitSchedules(std::function<bool(FITwinSchedule const&)> const& Func) const;

	void OnNewTileMeshBuilt(CesiumTileID const& TileId, std::set<ITwinElementID>&& MeshElementIDs);
	void SetScheduleTimeRangeIsKnown();

	FITwinSchedulesImport& GetSchedulesApiReadyForUnitTesting();

	/// \param Deferred Passed as const because the whole timeline replay and interpolation code is
	///		const, but FDeferredPlaneEquation::planeEquation_ is mutable for the purpose of this method.
	/// \param ElementWorldBox We can only have it in world coordinates, precisely because Elements are
	///		batched, and we need it in world anyway, for the same reason (applying the same cutting plane
	///		to all Features of a given Element)
	static void FinalizeCuttingPlaneEquation(ITwin::Timeline::FDeferredPlaneEquation const& Deferred,
											 FBox const& ElementsWorldBox);
	static void FinalizeAnchorPos(ITwin::Timeline::FDeferredAnchorPos const& Deferred,
								  FBox const& ElementsWorldBox);
};
