/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesInternals.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinFwd.h>
#include <ITwinElementID.h>
#include <ITwinSceneMappingTypes.h>
#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/Timeline.h>
#include <CesiumMaterialType.h>

#include <UObject/WeakObjectPtrTemplates.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

class UMaterialInterface;
class UObject;
class FIModelUninitializer;
struct FITwinCoordConversions;
class FITwinSchedule;
class FITwinSceneTile;
class FITwinSynchro4DAnimator;
class UMaterialInstanceDynamic;

namespace TestSynchro4DQueries
{
	void MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);
}

namespace BeUtils { class GltfTuner; }

class FITwinSynchro4DSchedulesInternals
{
	friend class UITwinSynchro4DSchedules;
	// TODO_GCO: can contain several schedules for a given iModel:
	// TODO_GCO: should have one timeline ('Builder') for each!
	UITwinSynchro4DSchedules& Owner;
	const bool bDoNotBuildTimelines; ///< defaults to false, true only for internal unit testing
	FITwinScheduleTimelineBuilder Builder;
	/// The value tells whether the range is valid or not (empty schedule)
	std::optional<bool> ScheduleTimeRangeIsKnownAndValid;
	FITwinSchedulesImport SchedulesApi; // <== must be declared AFTER Builder
	std::recursive_mutex& Mutex;
	std::vector<FITwinSchedule>& Schedules;
	FITwinSynchro4DAnimator& Animator;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner;
	/// @see GetMinGltfTunerVersionForAnimation
	int MinGltfTunerVersionForAnimation = std::numeric_limits<int>::max();
	std::shared_ptr<FIModelUninitializer> Uniniter;

	/// For use when PrefetchWholeSchedule() returns true only
	enum class EApplySchedule
	{
		WaitForFullSchedule, ///< Do nothing until full schedule has been received
		/// Timelines have been applied once after full schedule received (but only for Elements currently
		/// present in the scene, of course)
		InitialPassDone
	};
	EApplySchedule ApplySchedule = EApplySchedule::WaitForFullSchedule;

	/// Query deferred to the next tick because otherwise textures (highlights/opacities, cut planes...) may
	/// be allocated once before the full tile was notified, and would have had to be resized later...
	/// Not straightforward to handle, and this way we'll have fewer (batches of) queries anyway.
	/// The map value needs to be ordered because of the set_intersection in 
	/// FITwinSynchro4DSchedulesInternals::HandleReceivedElements :/
	std::unordered_map<ITwinScene::TileIdx, std::unordered_set<ITwinScene::ElemIdx>> ElementsReceived;

	friend void TestSynchro4DQueries::MakeDummySchedule(FITwinSynchro4DSchedulesInternals&);

	void CheckInitialized(AITwinIModel& IModel);
	void MutateSchedules(std::function<void(std::vector<FITwinSchedule>&)> const& Func);
	void SetupAndApply4DAnimationSingleTile(FITwinSceneTile& SceneTile);
	void Setup4DAnimationSingleTile(FITwinSceneTile& SceneTile, std::optional<ITwinScene::TileIdx> TileRank,
		std::unordered_set<ITwinScene::ElemIdx> const* Elements);
	/// Deferred processing of the Elements which were notified during the last tick ('last' to avoid doing
	/// anything before the whole tile has been loaded, since we are notified of the tile meshes one by one)
	void HandleReceivedElements(bool& bNew4DAnimTexToUpdate);
	void UpdateConnection(bool const bOnlyIfReady);
	bool ResetSchedules();
	void Reset();
	bool IsReadyToQuery() const;
	bool TileCompatibleWithSchedule(ITwinScene::TileIdx const& TileRank) const;
	bool TileCompatibleWithSchedule(FITwinSceneTile const& SceneTile) const;

	bool useDynamicShadows = false;

public:
	FITwinSynchro4DSchedulesInternals(UITwinSynchro4DSchedules& Owner, bool const InDoNotBuildTimelines,
									  std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules,
									  FITwinSynchro4DAnimator& Animator);

	UMaterialInterface* GetMasterMaterial(ECesiumMaterialType Type,
										  UITwinSynchro4DSchedules& SchedulesComp);
	/// When Owner.IsAvailable() returns true, returns the minimum gltf tuning version for which the loaded
	/// meshes will be compatible with this Schedule's 4D animation. Otherwise, returns -1.
	int GetMinGltfTunerVersionForAnimation() const { return MinGltfTunerVersionForAnimation; }
	bool TileTunedForSchedule(FITwinSceneTile const& SceneTile) const;
	void SetGltfTuner(std::shared_ptr<BeUtils::GltfTuner> const& Tuner);
	[[nodiscard]] FITwinScheduleTimeline& Timeline();
	[[nodiscard]] FITwinScheduleTimeline const& GetTimeline() const;
	void ForEachElementTimeline(ITwinElementID const ElementID,
								std::function<void(FITwinElementTimeline const&)> const& Func) const;
	[[nodiscard]] FString ElementTimelineAsString(ITwinElementID const ElementID) const;
	/// \param Func Function to execute for each schedule. Returning false will skip the visit of the
	///		remaining schedules not yet visited.
	void VisitSchedules(std::function<bool(FITwinSchedule const&)> const& Func) const;
	bool PrefetchWholeSchedule() const;
	bool IsPrefetchedAvailableAndApplied() const;
	/// \return Whether the tile's render-readiness was toggled *off*
	bool OnNewTileBuilt(FITwinSceneTile& SceneTile);
	void UnloadKnownTile(FITwinSceneTile& SceneTile, ITwinScene::TileIdx const& TileRank);
	void OnNewTileMeshBuilt(ITwinScene::TileIdx const& TileRank,
							std::unordered_set<ITwinScene::ElemIdx>&& MeshElements);
	void SetScheduleTimeRangeIsKnown();
	void HideNonAnimatedDuplicates(FITwinSceneTile& SceneTile, FElementsGroup const& NonAnimatedDuplicates);
	void OnDownloadProgressed(double PercentComplete);
	FITwinSchedulesImport& GetSchedulesApiReadyForUnitTesting();

	static FTransform ComputeTransformFromFinalizedKeyframe(FITwinCoordConversions const& CoordConv,
		ITwin::Timeline::PTransform const& TransfoKey, FVector const& ElementsBBoxCenter,
		bool const bWantsResultAsIfIModelUntransformed);
	/// \param Deferred Passed as const because the whole timeline replay and interpolation code is
	///		const, but FDeferredPlaneEquation::planeEquation_ is mutable for the purpose of this method.
	/// \param ElementWorldBox We can only have it in world coordinates, precisely because Elements are
	///		batched, and we need it in world anyway, for the same reason (applying the same cutting plane
	///		to all Features of a given Element)
	static void FinalizeCuttingPlaneEquation(FITwinCoordConversions const& CoordConv,
		ITwin::Timeline::FDeferredPlaneEquation const& Deferred, FBox const& ElementsBox);
	static void FinalizeAnchorPos(FITwinCoordConversions const& CoordConv,
		ITwin::Timeline::FDeferredAnchor const& Deferred, FBox const& ElementsBox);

	void SetMeshesDynamicShadows(bool bDynamic);
};
