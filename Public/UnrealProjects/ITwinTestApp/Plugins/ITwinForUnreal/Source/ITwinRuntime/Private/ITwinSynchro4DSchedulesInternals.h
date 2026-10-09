/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesInternals.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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
class FITwinSceneMapping;
class FITwinSchedule;
class FITwinSceneTile;
class TITwinSceneTilePtr;
class FITwinSynchro4DAnimator;
class UMaterialInstanceDynamic;

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
	std::recursive_mutex Mutex;
	FITwinSchedulesImport SchedulesApi; // <== must be declared AFTER Builder
	std::optional<FITwinSchedule>& Schedule;
	FITwinSynchro4DAnimator& Animator;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner;
	/// @see GetMinGltfTunerVersionForAnimation
	static const int64_t UntunedGltfVersionForAnimation = std::numeric_limits<int64_t>::max();
	int64_t MinGltfTunerVersionForAnimation = UntunedGltfVersionForAnimation;
	bool bNeedFinalizeTimelines = false;
	std::shared_ptr<FIModelUninitializer> Uniniter;

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

	void CheckInitialized(AITwinIModel& IModel);
	void SetupAndApply4DAnimationSingleTile(const TITwinSceneTilePtr& SceneTilePtr);
	void Setup4DAnimationSingleTile(const TITwinSceneTilePtr& SceneTilePtr, std::optional<ITwinScene::TileIdx> TileRank,
		std::unordered_set<ITwinScene::ElemIdx> const* Elements);
	void HandleReceivedElements(bool bIncrementalUpdate);
	void UpdateConnection(bool const bOnlyIfReady);
	bool ResetSchedules();
	void ForceTilesToReSetupFor4DAnimation(FITwinSceneMapping const& SceneMapping);
	void Reset();
	bool IsReadyToQuery() const;
	bool TileCompatibleWithSchedule(ITwinScene::TileIdx const& TileRank) const;
	bool TileCompatibleWithSchedule(const TITwinSceneTilePtr& SceneTilePtr) const;
	bool useDynamicShadows = false;
	bool bDoNotReuseScheduleMetadata = false;
	bool bSimulatedScheduleForTest = false;

public:
	FITwinSynchro4DSchedulesInternals(UITwinSynchro4DSchedules& Owner, bool const InDoNotBuildTimelines,
		std::optional<FITwinSchedule>& Schedule, FITwinSynchro4DAnimator& Animator);

	UMaterialInterface* GetMasterMaterial(ECesiumMaterialType Type, UITwinSynchro4DSchedules& SchedulesComp);
	/// When Owner.IsAvailable() returns true, returns the minimum gltf tuning version for which the loaded
	/// meshes will be compatible with this Schedule's 4D animation. Otherwise, returns -1.
	int64_t GetMinGltfTunerVersionForAnimation() const { return MinGltfTunerVersionForAnimation; }
	bool TileTunedForSchedule(const TITwinSceneTilePtr& SceneTilePtr) const;
	void SetGltfTuner(std::shared_ptr<BeUtils::GltfTuner> const& Tuner);
	[[nodiscard]] FITwinScheduleTimeline& Timeline();
	[[nodiscard]] FITwinScheduleTimeline const& GetTimeline() const;
	void ForEachElementTimeline(ITwinElementID const ElementID,
								std::function<void(FITwinElementTimeline const&)> const& Func) const;
	[[nodiscard]] FString ElementTimelineAsString(ITwinElementID const ElementID) const;
	bool IsAvailableAndApplied() const;
	/// \return Whether the tile's render-readiness was toggled *off*
	bool OnNewTileBuilt(const TITwinSceneTilePtr& SceneTilePtr);
	void UnloadKnownTile(const TITwinSceneTilePtr& SceneTilePtr, ITwinScene::TileIdx const& TileRank);
	void OnNewTileMeshBuilt(ITwinScene::TileIdx const& TileRank,
							std::unordered_set<ITwinScene::ElemIdx>&& MeshElements);
	void SetScheduleTimeRangeIsKnown();
	void HideNonAnimatedDuplicates(const TITwinSceneTilePtr& SceneTilePtr,
								   FElementsGroup const& NonAnimatedDuplicates);
	void OnDownloadProgressed(double PercentComplete, bool bHasPlayableSchedule = false);
	FITwinSchedulesImport& GetSchedulesApiReadyForUnitTesting();
	void SetMeshesDynamicShadows(bool bDynamic);
	void UpdateS4DClassDefaults();

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
};
