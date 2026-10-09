/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedules.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSynchro4DSchedules.h>
#include <ITwinDynamicShadingProperty.h>
#include <ITwinDynamicShadingProperty.inl>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinIModelSettings.h>
#include <Network/JsonQueriesCache.h>
#include <Timeline/AnchorPoint.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/SchedulesStructs.h>
#include <IncludeCesium3DTileset.h>
#include <Cesium3DTilesSelection/GltfModifierVersionExtension.h>

#include <Components/StaticMeshComponent.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformFileManager.h>
#include <Logging/LogMacros.h>
#include <Materials/MaterialInstance.h>
#include <Materials/MaterialInterface.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <UObject/ConstructorHelpers.h>

#include <mutex>
#include <optional>
#include <unordered_set>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/GltfTuner.h>
#include <Compil/AfterNonUnrealIncludes.h>

DECLARE_LOG_CATEGORY_EXTERN(LogITwinSched, Log, All);
DEFINE_LOG_CATEGORY(LogITwinSched);

namespace ITwin
{
	ITWINRUNTIME_API void SetSynchroDateToSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap,
													const FDateTime& InDate)
	{
		for (auto const& [_, Synchro4DSchedules] : SchedMap)
			if (IsValid(Synchro4DSchedules)
				// no need to wait for IsAvailable
				&& Synchro4DSchedules->GetDateRange() != FDateRange())
			{
				Synchro4DSchedules->SetScheduleTime(InDate);
			}
	}
}


class UITwinSynchro4DSchedules::FImpl
{
	friend class UITwinSynchro4DSchedules;
	friend FITwinSynchro4DSchedulesInternals& GetInternals(UITwinSynchro4DSchedules&);
	friend FITwinSynchro4DSchedulesInternals const& GetInternals(UITwinSynchro4DSchedules const&);

	UITwinSynchro4DSchedules& Owner;
	bool bResetSchedulesNeeded = true; ///< Has precedence over bUpdateConnectionIfReadyNeeded
	bool bUpdateConnectionIfReadyNeeded = false;
	std::optional<FITwinSchedule> Schedule;
	FString FormerScheduleId;
	FITwinSynchro4DAnimator Animator;
	FITwinSynchro4DSchedulesInternals Internals; // <== must be declared LAST

	bool UpdateGltfTunerRules();
	/// \param bHasReloadedTimelines (out) Whether timelines were (re-)built/updated due to bNeedFinalizeTimelines
	///		being true
	/// \param bTilesWillReload (out) Whether currently displayed tiles will be unloaded and reloaded from the glTF
	///		(because of the need to re-"tune" them): useful to optimize out lengthy calls
	void CheckFinalizeScheduleLoading(float TickDeltaTime, bool& bHasReloadedTimelines, bool& bTilesWillReload);

public: // for TPimplPtr
	FImpl(UITwinSynchro4DSchedules& InOwner, bool const InDoNotBuildTimelines)
		: Owner(InOwner), FormerScheduleId(TEXT("#unset#")), Animator(InOwner)
		, Internals(InOwner, InDoNotBuildTimelines, Schedule, Animator)
	{
	}
};

void UITwinSynchro4DSchedules::FImpl::CheckFinalizeScheduleLoading(float TickDeltaTime, bool& bHasReloadedTimelines,
																   bool& bTilesWillReload)
{
	bHasReloadedTimelines = false;
	bTilesWillReload = false;
	if (Internals.bNeedFinalizeTimelines)
	{
		bHasReloadedTimelines = true;
		Internals.bNeedFinalizeTimelines = false;
		Internals.Animator.ResetAnimation();
		Internals.Builder.FinalizeTimeline(*Schedule);
		BE_LOGI("ITwin4DImp", "Timelines finalized.");
		bTilesWillReload = UpdateGltfTunerRules();
		if (!Owner.DebugDumpAsJsonAfterQueryAll.IsEmpty())
		{
			Internals.GetTimeline()
				.SetJsonPrintingWithHumanReadableTimes(Owner.bDebugDumpUseHumanReadableTimes);
			Internals.GetTimeline().SetJsonPrintingNumberOfDecimals(Owner.DebugDumpLimitDecimals);
			Internals.Builder.DebugDumpFullTimelinesAsJson(Owner.DebugDumpAsJsonAfterQueryAll);
		}
		bool const bIncrementalUpdate =
			FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone == Internals.ApplySchedule;
		Internals.ApplySchedule = FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone;
		if (!bTilesWillReload)
		{
			Internals.HandleReceivedElements(bIncrementalUpdate);
			// Force update 4D for all timelines - may still be spread over several ticks though
			Animator.TickAnimation(TickDeltaTime, /*update all*/true);
		}
	}
	else if (!Internals.SchedulesApi.HasFetchingErrors()) // schedule is simply empty
		Internals.ApplySchedule = FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone;
}

bool UITwinSynchro4DSchedules::FImpl::UpdateGltfTunerRules()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!ensure(IModel))
		return false;
	if (!ensure(Internals.GltfTuner))
	{
		// TODO_GCO: but existing tiles will not be setup for 4D :/
		Internals.MinGltfTunerVersionForAnimation = -1;// (for debugging) to apply schedule nonetheless
		return false;
	}
	// Note: timelines with neither partial translucency nor transformation (ie only opaque colors
	// and cut planes) can be ignored here as they don't require Element separation.
	auto SceneMappingLocked = GetInternals(*IModel).SceneMapping->GetAutoLock();
	auto& SceneMapping = *SceneMappingLocked;
	std::optional<BeUtils::GltfTuner::Rules::Anim4DGroup> TranslucentNoTransfoGroup;
	if (EITwin4DGlTFTranslucencyRule::Unlimited == Owner.GlTFTranslucencyRule)
	{
		TranslucentNoTransfoGroup.emplace();
		TranslucentNoTransfoGroup->elements_.reserve(
			static_cast<size_t>(std::ceil(0.1 * SceneMapping.NumElements())));
	}
	BeUtils::GltfTuner::Rules AnimRules;
	if (EITwin4DGlTFTranslucencyRule::PerElement == Owner.GlTFTranslucencyRule)
		AnimRules.anim4DGroups_.reserve(static_cast<size_t>(std::ceil(0.1 * SceneMapping.NumElements())));
	//! Removes useless transformation-disabling keyframes added at the end of
	//! FITwinScheduleTimelineBuilder::FImpl::CreateAnimationBindingKeyframes but only necessary
	//! when other bindings for the same resources have non-null transfos (static or 3D path).
	//! If we don't simplify the timelines, the transformation-disabling keyframe triggers retuning
	//! of all the Elements, which led to explosion of the loading times :-(
	for (auto& ObjectTimeline : Internals.Builder.GetTimeline().GetContainer())
		if (ObjectTimeline->Transform.HasNoEffect())
			ObjectTimeline->Transform.Values.clear();
	// Groups of transformability needing Elements, grouped by commonality of transforming timelines:
	// all Elements transformed by the same timeline(s) (one timeline = one or more tasks assignment) can
	// remain in a single mesh, because transformation operates on the "4D Resource [Group]" as a whole.
	std::unordered_map<BeUtils::SmallVec<int32_t, 2>, std::vector<uint64_t>> PerTimelineGroups;
	auto const& MainTimeline = Internals.Builder.GetTimeline();
	SceneMapping.MutateElements([&MainTimeline, &TranslucentNoTransfoGroup, &PerTimelineGroups,
								 &Anim4DGroups = AnimRules.anim4DGroups_, &Sched=Owner]
		(FITwinElement& Elem)
		{
			if (Elem.AnimationKeys.empty())
				return;
			// Like in InsertAnimatedMeshSubElemsRecursively, assume no children (= leaf Element) means
			// that the Element will have bHasMesh=true at some point (but usually not yet!)
			if (!Elem.SubElemsInVec.empty())
				return;
			BeUtils::SmallVec<int32_t, 2> PerTimelineIDs;
			// bNeedTranslucentMat may have been set in FITwinSceneMapping::OnElementsTimelineModified, where
			// bDisableVisibilities and bDisablePartialVisibilities are not tested (TODO_GCO: do it) and/or
			// in case this is not the first time UpdateGltfTunerRules is called! (TODO_GCO: optim?)
			bool bReallyNeedTranslucentMat = false;
			for (auto&& AnimKey : Elem.AnimationKeys)
			{
				int TimelineIndex = -1;
				auto const* Timeline = MainTimeline.GetElementTimelineFor(AnimKey, &TimelineIndex);
				if (/*ensure*/(Timeline))// splitting timelines may empty some and orphan their anim key
				{
					if (// !Elem.Requirements.bNeedTranslucentMat <== NO, need the push_back...! &&
						Timeline->HasPartialVisibility()
						&& !Sched.bDisableVisibilities && !Sched.bDisablePartialVisibilities)
					{
						if (EITwin4DGlTFTranslucencyRule::PerTimeline == Sched.GlTFTranslucencyRule)
							PerTimelineIDs.push_back(TimelineIndex);
						Elem.Requirements.bNeedTranslucentMat = true;
						bReallyNeedTranslucentMat = true;
					}
					Elem.Requirements.bNeedCuttingPlaneTex |= (!Timeline->ClippingPlane.Values.empty())
						&& (!Sched.bDisableCuttingPlanes);
					if (!Timeline->Transform.Values.empty() && (!Sched.bDisableTransforms))
					{
						if (EITwin4DGlTFTranslucencyRule::PerElement != Sched.GlTFTranslucencyRule
							|| !bReallyNeedTranslucentMat)
						{
							PerTimelineIDs.push_back(TimelineIndex);
						}
						Elem.Requirements.bNeedBeTransformable = true;
					}
				}
			}
			// see comment over tex creation in FITwinSceneMapping::OnElementsTimelineModified:
			Elem.Requirements.bNeedHiliteAndOpaTex = true;
			if (EITwin4DGlTFTranslucencyRule::Unlimited == Sched.GlTFTranslucencyRule
				&& PerTimelineIDs.empty()) // ie !Elem.Requirements.bNeedBeTransformable, in this case
			{
				if (bReallyNeedTranslucentMat)
					TranslucentNoTransfoGroup->elements_.push_back(Elem.ElementID.value());
			}
			else if (EITwin4DGlTFTranslucencyRule::PerElement == Sched.GlTFTranslucencyRule
				&& bReallyNeedTranslucentMat)
			{
				Anim4DGroups.emplace_back(BeUtils::GltfTuner::Rules::Anim4DGroup{
					.elements_ = { Elem.ElementID.value() }, .ids_ = uint64_t(Elem.ElementID.value()) });
			}
			else if (!PerTimelineIDs.empty())
			{
				std::sort(PerTimelineIDs.begin(), PerTimelineIDs.end());
				// bNeedTranslucentMat may have been set by non-transforming timelines: need to put
				// transformable Elements in different groups depending on their need for translucency!
				// NOT needed when grouping by timeline, since translucent timelines are also in the list
				// (neither when grouping by Element, obviously)
				if (EITwin4DGlTFTranslucencyRule::Unlimited == Sched.GlTFTranslucencyRule)
				{
					if (bReallyNeedTranslucentMat)
					{
						for (auto& Timeline : PerTimelineIDs)
							Timeline = -Timeline;
					}
				}
				auto Found = PerTimelineGroups.try_emplace(
					PerTimelineIDs, std::vector<uint64_t>{ Elem.ElementID.value() });
				if (!Found.second) // was not inserted
					Found.first->second.push_back(Elem.ElementID.value());
			}
		});
	if (EITwin4DGlTFTranslucencyRule::PerElement != Owner.GlTFTranslucencyRule)
		AnimRules.anim4DGroups_.reserve(PerTimelineGroups.size()
			+ (TranslucentNoTransfoGroup && TranslucentNoTransfoGroup->elements_.empty() ? 0 : 1));
	if (TranslucentNoTransfoGroup && !TranslucentNoTransfoGroup->elements_.empty())
		AnimRules.anim4DGroups_.emplace_back(std::move(*TranslucentNoTransfoGroup));
	// Move the transform-only groups into the rules vector, possibly already populated with the
	// translu-no-transfo group ('Unlimited' case), or single-Elem monogroups ('PerElement' case):
	for (auto It = PerTimelineGroups.begin(); It != PerTimelineGroups.end(); )
	{
		// extract(X) invalidates X (and only X), so (post-)increment It now so that it stays valid
		auto const Current = It++;
		// Efficiently move both key and value from map to vector (C++17).
		// Note: std::move(It->first) compiled but "first" being constant, it was actually copied.
		auto NodeHandle = PerTimelineGroups.extract(Current);
		AnimRules.anim4DGroups_.emplace_back(BeUtils::GltfTuner::Rules::Anim4DGroup{
			std::move(NodeHandle.mapped()), std::move(NodeHandle.key()) });
	}
	return Internals.GltfTuner->SetAnim4DRules(std::move(AnimRules), Internals.MinGltfTunerVersionForAnimation,
		// Legacy schedules will never be updated (only when there is a new changeset but in that case the iModel
		// and tileset are recreated, and 4D reloaded and reapplied from scratch), no need to sort the rules:
		/*bOptimizeForRegularUpdates*/EITwinSchedulesGeneration::NextGen == Internals.Schedule->Generation);
}

static FITwinCoordConversions const& GetIModel2UnrealCoordConv(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // we can reach this for the CDO...
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		auto SceneMappingLocked = IModelInternals.SceneMapping->GetAutoLock();
		return SceneMappingLocked->GetIModel2UnrealCoordConv();
	}
	else
	{
		static FITwinCoordConversions Dummy;
		return Dummy;
	}
}

//---------------------------------------------------------------------------------------
// class FITwinSynchro4DSchedulesInternals
//---------------------------------------------------------------------------------------

FITwinSynchro4DSchedulesInternals& GetInternals(UITwinSynchro4DSchedules& Schedules)
{
	return Schedules.Impl->Internals;
}

FITwinSynchro4DSchedulesInternals const& GetInternals(UITwinSynchro4DSchedules const& Schedules)
{
	return Schedules.Impl->Internals;
}

FITwinSynchro4DSchedulesInternals::FITwinSynchro4DSchedulesInternals(UITwinSynchro4DSchedules& InOwner,
	bool const InDoNotBuildTimelines, std::optional<FITwinSchedule>& InSchedule, FITwinSynchro4DAnimator& InAnimator)
:
	Owner(InOwner), bDoNotBuildTimelines(InDoNotBuildTimelines)
	, Builder(InOwner, GetIModel2UnrealCoordConv(InOwner))
	, SchedulesApi(InOwner, Mutex, InSchedule), Schedule(InSchedule)
	, Animator(InAnimator)
{
}

void FITwinSynchro4DSchedulesInternals::SetGltfTuner(std::shared_ptr<BeUtils::GltfTuner> const& Tuner)
{
	GltfTuner = Tuner;
}

void FITwinSynchro4DSchedulesInternals::CheckInitialized(AITwinIModel& IModel)
{
	if (!Uniniter)
	{
		Uniniter = GetInternals(IModel).Uniniter;
		Uniniter->Register([this] {
			SchedulesApi.Uninitialize();
			Builder.Uninitialize();
		});
	}
}

FITwinScheduleTimeline& FITwinSynchro4DSchedulesInternals::Timeline()
{
	return Builder.Timeline();
}

FITwinScheduleTimeline const& FITwinSynchro4DSchedulesInternals::GetTimeline() const
{
	return Builder.GetTimeline();
}

void FITwinSynchro4DSchedulesInternals::SetScheduleTimeRangeIsKnown()
{
	// NOT Owner.GetDateRange(), which relies on ScheduleTimeRangeIsKnownAndValid set below!
	auto const& TimelineRange = GetTimeline().GetDateRange();
	if (TimelineRange != FDateRange())
	{
		ScheduleTimeRangeIsKnownAndValid = true;
		// Now we can call Owner.GetDateRange(), which handles rounding
		auto const& DateRange = Owner.GetDateRange();
		Owner.OnScheduleTimeRangeKnown.Broadcast(DateRange.GetLowerBoundValue(), DateRange.GetUpperBoundValue());
	}
	else
	{
		ScheduleTimeRangeIsKnownAndValid = false;
		OnDownloadProgressed(100., false);
		Owner.OnScheduleInformationReceived.Broadcast(Cast<AITwinIModel>(Owner.GetOwner()), {}, {});
		Owner.OnScheduleTimeRangeKnown.Broadcast(FDateTime::MinValue(), FDateTime::MinValue());
	}
}

void FITwinSynchro4DSchedulesInternals::ForEachElementTimeline(ITwinElementID const ElementID,
	std::function<void(FITwinElementTimeline const&)> const& Func) const
{
	auto const& MainTimeline = GetTimeline();
	auto SceneMappingLocked = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping->GetAutoLock();
	auto const& Elem = SceneMappingLocked->GetElement(ElementID);
	for (auto&& AnimKey : Elem.AnimationKeys)
	{
		auto const* Timeline = MainTimeline.GetElementTimelineFor(AnimKey);
		if (Timeline)
			Func(*Timeline);
	}
}

FString FITwinSynchro4DSchedulesInternals::ElementTimelineAsString(ITwinElementID const ElementID) const
{
	auto const& MainTimeline = GetTimeline();
	FString Result;
	ForEachElementTimeline(ElementID, [this, &Result](FITwinElementTimeline const& Timeline)
		{
			Timeline.SetJsonPrintingWithHumanReadableTimes(Owner.bDebugDumpUseHumanReadableTimes);
			Timeline.SetJsonPrintingNumberOfDecimals(Owner.DebugDumpLimitDecimals);
			Result.Append(Timeline.ToPrettyJsonString());
		});
	return Result;
}

bool FITwinSynchro4DSchedulesInternals::TileCompatibleWithSchedule(ITwinScene::TileIdx const& TileRank) const
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return false;
	auto SceneMappingLocked = GetInternals(*IModel).SceneMapping->GetAutoLock();
	return TileCompatibleWithSchedule(SceneMappingLocked->KnownTile(TileRank));
}

bool FITwinSynchro4DSchedulesInternals::TileCompatibleWithSchedule(const TITwinSceneTilePtr& SceneTilePtr) const
{
	if (!/*ensure*/(GltfTuner)) // might be used for debugging: return true to apply 4D nonetheless
		return true;
	return TileTunedForSchedule(SceneTilePtr);
}

bool FITwinSynchro4DSchedulesInternals::TileTunedForSchedule(const TITwinSceneTilePtr& SceneTilePtr) const
{
	auto SceneTileLock = SceneTilePtr->GetAutoLock();
	auto& SceneTile = *SceneTileLock;
	if (!SceneTile.pCesiumTile)
		return false;
	auto* Model = SceneTile.pCesiumTile->GetGltfModel();
	if (!Model)
		return true;
	// This means the schedule is either not yet available, OR does not need any tuning: do not test ModelVer,
	// which may be anything since tuning may have occurred for some other reason, like material assignment!
	if (MinGltfTunerVersionForAnimation == FITwinSynchro4DSchedulesInternals::UntunedGltfVersionForAnimation)
		return true;
	// When using the glTF tuner, no use storing stuff about loaded tiles until schedule is fully available:
	// retuning (assuming it is actually needed) will unload all the SceneTile's anyway (even though the Cesium
	// native tiles are not)!
	auto const ModelVer = Cesium3DTilesSelection::GltfModifierVersionExtension::getVersion(*Model);
	return ModelVer && MinGltfTunerVersionForAnimation <= (*ModelVer);
}

/// Most of the handling is delayed until the beginning of the next tick: this was because of past
/// misunderstandings and especially before OnNewTileBuilt was added. Could simplify...
void FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt(ITwinScene::TileIdx const& TileRank,
	std::unordered_set<ITwinScene::ElemIdx>&& MeshElements)
{
	if (MeshElements.empty())
	{
		return;
	}
	// MeshElements is actually moved only in case of insertion, otherwise it is untouched
	auto Entry = ElementsReceived.try_emplace(TileRank, std::move(MeshElements));
	if (!Entry.second) // was not inserted, merge with existing set:
	{
		for (auto&& Elem : MeshElements)
			Entry.first->second.insert(Elem);
	}
}

void FITwinSynchro4DSchedulesInternals::UnloadKnownTile(const TITwinSceneTilePtr& /*SceneTilePtr*/,
														ITwinScene::TileIdx const& TileRank)
{
	ElementsReceived.erase(TileRank);
}

bool FITwinSynchro4DSchedulesInternals::IsAvailableAndApplied() const
{
	return EApplySchedule::InitialPassDone == ApplySchedule;
}

bool FITwinSynchro4DSchedulesInternals::OnNewTileBuilt(const TITwinSceneTilePtr& SceneTilePtr)
{
	auto SceneTileLock = SceneTilePtr->GetAutoLock();
	auto& SceneTile = *SceneTileLock;
	if (IsAvailableAndApplied()
		// we may have received no mesh notif for this tile, in that case we don't have the native ptr
		&& SceneTile.pCesiumTile)
	{
		SceneTile.pCesiumTile->SetRenderReady(false);
		SetupAndApply4DAnimationSingleTile(SceneTilePtr);
		return true;
	}
	return false;
}

void FITwinSynchro4DSchedulesInternals::HideNonAnimatedDuplicates(const TITwinSceneTilePtr& SceneTilePtr,
																  FElementsGroup const& NonAnimatedDuplicates)
{
	auto SceneTileLock = SceneTilePtr->GetAutoLock();
	auto& SceneTile = *SceneTileLock;
	if (!SceneTile.HighlightsAndOpacities) // may not exist (SceneTile.bVisible == false, for example)
		return;
	// Just iterate on the smallest collection, but both branches do the same thing of course
	if (NonAnimatedDuplicates.size() < SceneTile.NumElementsFeatures())
	{
		for (auto&& ElemID : NonAnimatedDuplicates)
		{
			auto* ElemInTile = SceneTile.FindElementFeaturesSLOW(ElemID);
			if (ElemInTile)
				SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElemInTile->Features, 0);
		}
	}
	else
	{
		SceneTile.ForEachElementFeatures(
			[&SceneTile, &NonAnimatedDuplicates](FITwinElementFeaturesInTile& ElemInTile)
			{
				if (NonAnimatedDuplicates.end() != NonAnimatedDuplicates.find(ElemInTile.ElementID))
					SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElemInTile.Features, 0);
			});
	}
}

void FITwinSynchro4DSchedulesInternals::SetupAndApply4DAnimationSingleTile(const TITwinSceneTilePtr& SceneTilePtr)
{
	if (!TileCompatibleWithSchedule(SceneTilePtr))
	{
		auto SceneMappingLocked = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping->GetAutoLock();
		ElementsReceived.erase(SceneMappingLocked->KnownTileRank(SceneTilePtr));
		// Tile remains non-render-ready: except if you want to for debugging purposes, then uncomment:
		// SceneTile.pCesiumTile->SetRenderReady(true);
		return;
	}
	{
		bool bIsSetupFor4DAnimation;
		{
			auto SceneTileLock = SceneTilePtr->GetRAutoLock();
			auto& SceneTile = *SceneTileLock;
			bIsSetupFor4DAnimation = SceneTile.bIsSetupFor4DAnimation;
		}
		if (!bIsSetupFor4DAnimation)
		{
			Setup4DAnimationSingleTile(SceneTilePtr, {}, nullptr);
		}
	}
	Animator.ApplyAnimationOnTile(SceneTilePtr);
}

void FITwinSynchro4DSchedulesInternals::SetMeshesDynamicShadows(bool bDynamic)
{
	auto SceneMappingLocked = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping->GetAutoLock();
	SceneMappingLocked->ForEachKnownTile([bDynamic](const TITwinSceneTilePtr& SceneTilePtr)
		{
			auto SceneTileLock = SceneTilePtr->GetAutoLock();
			auto& SceneTile = *SceneTileLock;
			if (SceneTile.TimelinesIndices.empty())
				return;
			for (auto& Mesh : SceneTile.GltfMeshWrappers())
				if (UStaticMeshComponent* MeshComp = Mesh.MeshComponent())
				{
					auto shadowCacheInvalidationBehavior = bDynamic ? EShadowCacheInvalidationBehavior::Always : EShadowCacheInvalidationBehavior::Auto;
					if (MeshComp->ShadowCacheInvalidationBehavior != shadowCacheInvalidationBehavior)
					{
						MeshComp->ShadowCacheInvalidationBehavior = shadowCacheInvalidationBehavior;
						MeshComp->MarkRenderStateDirty();
					}
				}
		});

	useDynamicShadows = bDynamic;
}

void FITwinSynchro4DSchedulesInternals::Setup4DAnimationSingleTile(const TITwinSceneTilePtr& SceneTilePtr,
	std::optional<ITwinScene::TileIdx> TileRank, std::unordered_set<ITwinScene::ElemIdx> const* Elements)
{
	auto SceneMappingLocked = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping->GetAutoLock();
	auto& SceneMapping = *SceneMappingLocked;

	if (!TileRank)
		TileRank.emplace(SceneMapping.KnownTileRank(SceneTilePtr));
	std::optional<typename decltype(ElementsReceived)::iterator> Pending;
	auto SceneTileLock = SceneTilePtr->GetAutoLock();
	auto& SceneTile = *SceneTileLock;
	if (!Elements)
	{
		Pending.emplace(ElementsReceived.find(*TileRank));
		if (ElementsReceived.end() == (*Pending))
		{
			if (ensure(SceneTile.IsLoaded() && SceneTile.MaxFeatureID == ITwin::NOT_FEATURE))
			{
				SceneTile.bIsSetupFor4DAnimation = true;//not really, but needed to mark it render-ready
			}
			return;
		}
		Elements = &((*Pending)->second);
	}
	std::unordered_set<FIModelElementsKey> Timelines;
	auto&& MainTimeline = Timeline();
	if (!ensure(SceneTile.IsLoaded() && !SceneTile.bIsSetupFor4DAnimation))
		return;
	SceneTile.bIsSetupFor4DAnimation = true;
	if (SceneTile.TimelinesIndices.empty()) // preserved when tile is unloaded then reloaded
	{
		for (auto&& Elem : (*Elements))
			for (auto&& AnimKey : SceneMapping.GetElement(Elem).AnimationKeys)
				Timelines.insert(AnimKey);
		SceneTile.TimelinesIndices.reserve(Timelines.size());
		for (auto&& AnimKey : Timelines)
		{
			int Index;
			auto* ElementTimeline = MainTimeline.GetElementTimelineFor(AnimKey, &Index);
			if (/*ensure*/(ElementTimeline)) // splitting timelines may empty some and orphan their anim key
				SceneTile.TimelinesIndices.push_back(Index);
		}
	}
	if (Pending)
		ElementsReceived.erase(*Pending);
	auto&& AllTimelines = MainTimeline.GetContainer();
	for (auto&& Index : SceneTile.TimelinesIndices)
	{
		SceneMapping.OnElementsTimelineModified(*TileRank, *AllTimelines[Index]
			// can't pass this, the expected param is a vector ptr (but only because
			// InsertAnimatedMeshSubElemsRecursively says so, it could be changed),
			// BUT we only handle fully loaded tiles anyway:
			, nullptr/*&TileMeshElements.second*/
			, TileTunedForSchedule(SceneTilePtr)
			, Index);
	}
	HideNonAnimatedDuplicates(SceneTilePtr, MainTimeline.GetNonAnimatedDuplicates());
	
	if (!SceneTile.TimelinesIndices.empty())
	{
		for (auto& Mesh : SceneTile.GltfMeshWrappers())
			if (UStaticMeshComponent* MeshComp = Mesh.MeshComponent())
			{
				auto shadowCacheInvalidationBehavior = useDynamicShadows ? EShadowCacheInvalidationBehavior::Always : EShadowCacheInvalidationBehavior::Auto;
				if (MeshComp->ShadowCacheInvalidationBehavior != shadowCacheInvalidationBehavior)
				{
					MeshComp->ShadowCacheInvalidationBehavior = shadowCacheInvalidationBehavior;
					MeshComp->MarkRenderStateDirty();
				}
			}
	}
}

/// Legacy schedule: used by initial pass (not for setting up tiles (re-)loaded afterwards).
/// NextGen schedule: used by initial pass as well as incremental schedule updates affecting the 4D animation
void FITwinSynchro4DSchedulesInternals::HandleReceivedElements(bool bIncrementalUpdate)
{
	if ((!bIncrementalUpdate && ElementsReceived.empty()) || !ensure(Owner.IsAvailable()))
		return;
	auto SceneMappingLocked = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping->GetAutoLock();
	auto const& SceneMapping = *SceneMappingLocked;
	if (bIncrementalUpdate)
	{
		ForceTilesToReSetupFor4DAnimation(SceneMapping); // TODO: not efficient (memory spike) see comment in method
	}
	for (auto const& [TileRank, TileElems] : ElementsReceived)
	{
		auto SceneTilePtr = SceneMapping.KnownTile(TileRank);
		bool sceneTileIsLoaded;
		{
			auto SceneTileLock = SceneTilePtr->GetRAutoLock();
			auto& SceneTile = *SceneTileLock;
			sceneTileIsLoaded = SceneTile.IsLoaded();
		}
		// may have been unloaded while waiting for ElementsReceived to be processed
		if (sceneTileIsLoaded && TileCompatibleWithSchedule(SceneTilePtr))
			Setup4DAnimationSingleTile(SceneTilePtr, TileRank, &TileElems);
	}
	ElementsReceived.clear();
}

UMaterialInterface* FITwinSynchro4DSchedulesInternals::GetMasterMaterial(
	ECesiumMaterialType Type, UITwinSynchro4DSchedules& SchedulesComp)
{
	switch (Type)
	{
	case ECesiumMaterialType::Opaque:
		return SchedulesComp.BaseMaterialMasked;
	case ECesiumMaterialType::Translucent:
		return SchedulesComp.BaseMaterialTranslucent;
	case ECesiumMaterialType::Water:
		checkf(false, TEXT("Water material not implemented for Synchro4D"));
		break;
	}
	return nullptr;
}

/*static*/
/// \param ElementsBBoxCenter in World-UE space AS IF iModel were untransformed
FTransform FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe(
	FITwinCoordConversions const& CoordConv, ITwin::Timeline::PTransform const& TransfoKey, 
	FVector const& ElementsBBoxCenter, bool const bWantsResultAsIfIModelUntransformed)
{
	if (ITwin::Timeline::EAnchorPoint::Static == TransfoKey.DefrdAnchor.AnchorPoint)
	{
		// Desperate solution: just switch back to iModel space!
		// See comments in AddStaticTransformToTimeline and RequestTransfoAssignment...
		// Maybe it was all related to FTransform::Inverse being buggy...
		if (bWantsResultAsIfIModelUntransformed)
		{
			return UITwinUtilityLibrary::Inverse(CoordConv.IModelToUntransformedIModelInUE)
				 * FTransform(TransfoKey.Rotation)
				 * FTransform(TransfoKey.Position)
				 * CoordConv.IModelToUntransformedIModelInUE;
		}
		else
		{
			return CoordConv.UnrealToIModel
				 * FTransform(TransfoKey.Rotation)
				 * FTransform(TransfoKey.Position)
				 * CoordConv.IModelToUnreal;
		}
	}
	else
	{
		// For 'Original [Position]' anchoring, Keyframes simply store relative translations.
		bool const bPositionIsRelative =
			ITwin::Timeline::EAnchorPoint::Original == TransfoKey.DefrdAnchor.AnchorPoint;
		// Location of the Element's reference point (origin of its local CRS) is unknown, since
		// the local CRS is lost when Elements are merged into the Gltf meshes by the Mesh export!
		// The only case where it seemed to me that it would be needed is when rotating a single
		// Element using the 'Original' anchor, in which case I assumed the Element's origin should be
		// used instead of the group's BBox center: but we don't have actual examples of bugs coming
		// from the current code, and it's not even sure SynchroPro has knowledge of the Element's
		// local base: see azdev#1582839, where additional geometry is used to enforce the desired BBox
		// center for rotation!
		ensure(!bWantsResultAsIfIModelUntransformed);
		FVector const ElemGroupAnchor = CoordConv.IModelTilesetTransform.TransformPosition(ElementsBBoxCenter)
			- TransfoKey.DefrdAnchor.Offset;
		return FTransform(-ElemGroupAnchor)
			* FTransform(TransfoKey.Rotation)
			* (bPositionIsRelative
				? FTransform(ElemGroupAnchor + TransfoKey.Position)
				: FTransform(TransfoKey.Position));
	}
}

/*static*/
void FITwinSynchro4DSchedulesInternals::FinalizeCuttingPlaneEquation(FITwinCoordConversions const& CoordConv,
	ITwin::Timeline::FDeferredPlaneEquation const& Deferred, FBox const& OriginalElementsBox)
{
	// Must have been "finalized" before us:
	ensure(!Deferred.TransformKeyframe
		|| (!Deferred.TransformKeyframe->DefrdAnchor.IsDeferred()
			&& ITwin::Timeline::EAnchorPoint::Static == Deferred.TransformKeyframe->DefrdAnchor.AnchorPoint)
		// shouldn't happen but did: maybe an untransformed KF overwrote a normal KF at exactly the same time? :/
		|| !Deferred.TransformKeyframe->bIsTransformed);
	ensure(Deferred.PlaneOrientation.IsUnit());
	// Necessarily static assignment - growth simulation disabled along 3D Paths
	std::optional<FBox> AsAssignedBox;
	if (Deferred.TransformKeyframe && Deferred.TransformKeyframe->bIsTransformed)
	{
		// Use the transformed box instead of the transformed object's box: can lead to errors (large ones, in
		// border cases) but the only alternative is to compute the world BBox of the rotated object, which is
		// much more CPU-intensive...)
		AsAssignedBox.emplace(OriginalElementsBox.TransformBy(
			FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe(CoordConv,
				*Deferred.TransformKeyframe, OriginalElementsBox.GetCenter(),
				/*bWantsResultAsIfIModelUntransformed*/true)
			.ToMatrixNoScale()));
	}
	// OriginalElementsBox, like Deferred.PlaneOrientation, is in World-UE space AS IF iModel
	// were untransformed
	FBox const& ElementsBox = AsAssignedBox ? (*AsAssignedBox) : OriginalElementsBox;
	FBox const ExpandedBox = ElementsBox.ExpandBy(0.01 * ElementsBox.GetSize());
	FVector Position;
	switch (Deferred.GrowthStatus)
	{
	case ITwin::Timeline::EGrowthStatus::FullyGrown:
	case ITwin::Timeline::EGrowthStatus::DeferredFullyGrown:
		Position = FVector((Deferred.PlaneOrientation.X > 0) ? ExpandedBox.Max.X : ExpandedBox.Min.X,
						   (Deferred.PlaneOrientation.Y > 0) ? ExpandedBox.Max.Y : ExpandedBox.Min.Y,
						   (Deferred.PlaneOrientation.Z > 0) ? ExpandedBox.Max.Z : ExpandedBox.Min.Z);
		Deferred.GrowthStatus = ITwin::Timeline::EGrowthStatus::FullyGrown;
		break;
	case ITwin::Timeline::EGrowthStatus::FullyRemoved:
	case ITwin::Timeline::EGrowthStatus::DeferredFullyRemoved:
		Position = FVector((Deferred.PlaneOrientation.X > 0) ? ExpandedBox.Min.X : ExpandedBox.Max.X,
						   (Deferred.PlaneOrientation.Y > 0) ? ExpandedBox.Min.Y : ExpandedBox.Max.Y,
						   (Deferred.PlaneOrientation.Z > 0) ? ExpandedBox.Min.Z : ExpandedBox.Max.Z);
		Deferred.GrowthStatus = ITwin::Timeline::EGrowthStatus::FullyRemoved;
		break;
	default: [[unlikely]]
		ensure(false);
		Position = ExpandedBox.GetCenter();
		break;
	}
	Position = CoordConv.IModelTilesetTransform.TransformPosition(Position);
	FVector PlaneOrientationUE =
		CoordConv.IModelTilesetTransform.TransformVector(FVector(Deferred.PlaneOrientation));
	PlaneOrientationUE.Normalize();
	// Note: PlaneOrientation and PlaneW could be merged again into a single TVector4 now that PlaneOrientation
	// is also mutable, but be careful that TVector(const UE::Math::TVector4<T>& V); is NOT explicit, which is
	// a shame IMHO esp. since conversions between float/double variants are.
	Deferred.PlaneW = static_cast<float>(Position.Dot(PlaneOrientationUE));
	Deferred.PlaneOrientation = FVector3f(PlaneOrientationUE);
}

/*static*/
void FITwinSynchro4DSchedulesInternals::FinalizeAnchorPos(FITwinCoordConversions const& CoordConv,
	ITwin::Timeline::FDeferredAnchor const& Deferred, FBox const& ElementsBox)
{
	ensure(Deferred.bDeferred);
	FVector Center, Extents;
	// ElementsBox is in World-UE space AS IF iModel were untransformed
	ElementsBox.GetCenterAndExtents(Center, Extents);
	// Note: 'Extents' is half (Max - Min)
	switch (Deferred.AnchorPoint)
	{
	case ITwin::Timeline::EAnchorPoint::Custom:
		// Note: Add3DPathTransformToTimeline already transforms the custom offset with
		// IModel2UnrealTransfo, so Y inversion and iModel/tileset transform are included
		Deferred.bDeferred = false;
		return;

	case ITwin::Timeline::EAnchorPoint::Original: [[unlikely]] // shouldn't be deferred
	case ITwin::Timeline::EAnchorPoint::Static:   [[unlikely]] // shouldn't be deferred
	default: [[unlikely]]
		ensure(false);
		Deferred.bDeferred = false;
		return;

	case ITwin::Timeline::EAnchorPoint::Center:
		Deferred.Offset = FVector::ZeroVector;
		break;
	case ITwin::Timeline::EAnchorPoint::MinX:
		Deferred.Offset = FVector(Extents.X, 0, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MaxX:
		Deferred.Offset = FVector(-Extents.X, 0, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MinY:
		Deferred.Offset = FVector(0, -Extents.Y, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MaxY:
		Deferred.Offset = FVector(0, Extents.Y, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MinZ:
		Deferred.Offset = FVector(0, 0, Extents.Z);
		break;
	case ITwin::Timeline::EAnchorPoint::MaxZ:
		Deferred.Offset = FVector(0, 0, -Extents.Z);
		break;
	}
	Deferred.Offset = CoordConv.IModelTilesetTransform.TransformVector(Deferred.Offset);
	Deferred.bDeferred = false;
}

bool FITwinSynchro4DSchedulesInternals::IsReadyToQuery() const
{
	return SchedulesApi.IsReadyToQuery(); // other members need no particular init
}

void FITwinSynchro4DSchedulesInternals::Reset()
{
	ApplySchedule = EApplySchedule::WaitForFullSchedule;
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedule);
	// Clear 'Schedule' AFTER FITwinSchedulesImport::Impl is deleted above, because 1/ schedules can be
	// accessed by FromPool.AsyncRoutine until they're all finished, which is waited on in
	// FReusableJsonQueries::FImpl, and 2/ clear() here is called without locking Mutex:
	if (Schedule)
	{
		if (bDoNotReuseScheduleMetadata)
			Schedule.reset();
		else
			// Keep "metadata": this will skip them in RequestSchedules (?! did I mean ResetConnection?),
			// speeding up Reset a lot by avoiding a useless repetition of the request.
			// NOTE: keep the explicit FITwinSchedule ctor here otherwise the copy is made /after/ the
			// optional has been reset and crashes!
			Schedule.emplace(FITwinSchedule(Schedule->Id, Schedule->Name, Schedule->Generation));
	}
	bDoNotReuseScheduleMetadata = false;
	// See comment below about ordering between SchedulesApi and Builder:
	Builder.Uninitialize();
	Builder = FITwinScheduleTimelineBuilder(Owner, GetIModel2UnrealCoordConv(Owner));
	ScheduleTimeRangeIsKnownAndValid.reset();
	if (!bDoNotBuildTimelines)
	{
		SchedulesApi.SetSchedulesImportConnectors(
			// getting Builder's pointer here should be safe, because SchedulesApi is deleted /before/
			// Builder, (both above and in the destructor, as per the members' declaration order), which
			// will ensure no more request callbacks and thus no more calls to this subsequent callback:
			std::bind(&FITwinScheduleTimelineBuilder::OnReceivedScheduleStats, &Builder, std::placeholders::_1));
	}
	Owner.OnScheduleTimeRangeKnown.AddUniqueDynamic(&Owner,
		&UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived);
}

FITwinSchedulesImport& FITwinSynchro4DSchedulesInternals::GetSchedulesApiReadyForUnitTesting()
{
	ensure(IsReadyToQuery() || ResetSchedules());
	return SchedulesApi;
}

void FITwinSynchro4DSchedulesInternals::UpdateConnection(bool const bOnlyIfReady)
{
	if (!bOnlyIfReady || IsReadyToQuery())
	{
		AITwinIModel& IModel = *Cast<AITwinIModel>(Owner.GetOwner());
		if (ensure(IModel.bResolvedChangesetIdValid)
			&& !GetInternals(IModel).HasSynchro4DSchedulesMetadataQueryingError())
		{
			SchedulesApi.ResetConnection(IModel.ITwinId, IModel.IModelId, IModel.GetSelectedChangeset());
		}
		else
		{
			Owner.OnScheduleQueryingStatusChanged.Broadcast(false);
		}
	}
}

// Note: must have been called at least once before any actual querying.
bool FITwinSynchro4DSchedulesInternals::ResetSchedules()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return false;
	if (IModel->ITwinId.IsEmpty()) // happens transitorily in iTwinTestApp...
		return false;
	if (!IModel->ServerConnection)
		return false; // e.g. happens when an iModel is created from scratch by the user
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);

	auto SceneMappingLocked = IModelInternals.SceneMapping->GetAutoLock();
	SceneMappingLocked->SetTimelineGetter(
		std::bind(&FITwinSynchro4DSchedulesInternals::GetTimeline, this));

	SceneMappingLocked->SetMaterialGetter(
		std::bind(&FITwinSynchro4DSchedulesInternals::GetMasterMaterial, this,
					std::placeholders::_1, std::ref(Owner)));

	// this deletes the Builder, and clears all data structures which have the scope of the timeline even
	// though they may be stored somewhere else more appropriate, like FITwinElementTimeline::ExtraData and
	// FITwinSceneTile::TimelinesIndices
	Reset();

	Builder.Initialize(std::bind(
		&FITwinIModelInternals::OnElementsTimelineModified, &IModelInternals,
															std::placeholders::_1, std::placeholders::_2));
	UpdateConnection(false);
	ForceTilesToReSetupFor4DAnimation(*SceneMappingLocked);
	return true;
}

void FITwinSynchro4DSchedulesInternals::ForceTilesToReSetupFor4DAnimation(FITwinSceneMapping const& SceneMapping)
{
	// If the tileset is already loaded, we need to re-fill ElementsReceived with all tiles and Elements,
	// so that the Timeline optimization structures (FITwinElementTimeline::ExtraData) are re-created.
	// We "need" to list all Elements explicitly because Setup4DAnimationSingleTile currently expects it, but it
	// could be changed to list them from FITwinSceneTile::ElementsFeatures instead when not directly supplied.
	// Initially we thought a Cesium tile could be loaded mesh by mesh over several ticks, but it is not the case, so
	// we probably no longer need to pass the list of ElementIDs received to OnNewTileMeshBuilt (to be confirmed tho)
	ElementsReceived.clear();
	SceneMapping.ForEachKnownTile(
		[&AllReceived = this->ElementsReceived, &SceneMapping]
		(const TITwinSceneTilePtr& SceneTilePtr)
		{
			std::unordered_set<ITwinScene::ElemIdx> TileElems;
			{
				auto SceneTileLock = SceneTilePtr->GetAutoLock();
				auto& SceneTile = *SceneTileLock;
				if (!SceneTile.IsLoaded())
					return;
				SceneTile.bIsSetupFor4DAnimation = false;
				SceneTile.ForEachElementFeatures([&TileElems](FITwinElementFeaturesInTile const& ElemInTile)
					{
						TileElems.insert(ElemInTile.SceneRank);
					});
			}
			AllReceived.emplace(SceneMapping.KnownTileRank(SceneTilePtr), std::move(TileElems));
		});
}

void FITwinSynchro4DSchedulesInternals::OnDownloadProgressed(double PercentComplete,
															 bool bHasPlayableSchedule/*=false*/)
{
	ensure(IsInGameThread());
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	if (bHasPlayableSchedule)
	{
		bNeedFinalizeTimelines = true;
		IModelInternals.Update4DScheduleDownloadStatus(FITwinIModelInternals::E4DScheduleStatus::Finished);
	}
	else if (100. == PercentComplete)
	{
		IModelInternals.Update4DScheduleDownloadStatus(FITwinIModelInternals::E4DScheduleStatus::NoneOrEmpty);
	}
	else
	{
		IModelInternals.Update4DScheduleDownloadStatus(FITwinIModelInternals::E4DScheduleStatus::Loading, PercentComplete);
	}
}

void FITwinSynchro4DSchedulesInternals::UpdateS4DClassDefaults()
{
	auto* Settings = GetMutableDefault<UITwinIModelSettings>();
	Settings->Synchro4DMaxTimelineUpdateMilliseconds = Owner.MaxTimelineUpdateMilliseconds;
	Settings->Synchro4DQueriesDefaultPagination = Owner.ScheduleQueriesServerPagination;
	Settings->Synchro4DQueriesBindingsPagination = Owner.ScheduleQueriesBindingsPagination;
	Settings->IModelDataQueriesPagination = Owner.IModelDataQueriesPagination;
	Settings->Synchro4DGlTFTranslucencyRule = Owner.GlTFTranslucencyRule;
	Settings->bSynchro4DDisableColoring = Owner.bDisableColoring;
	Settings->bSynchro4DDisableVisibilities = Owner.bDisableVisibilities;
	Settings->bSynchro4DDisablePartialVisibilities = Owner.bDisablePartialVisibilities;
	Settings->bSynchro4DDisableCuttingPlanes = Owner.bDisableCuttingPlanes;
	//Settings->bSynchro4DFavorNextGenSchedule = Owner.bFavorNextGenSchedule; <= but then this applies to next Levels too :/
}

//---------------------------------------------------------------------------------------
// class UITwinSynchro4DSchedules
//---------------------------------------------------------------------------------------

UITwinSynchro4DSchedules::UITwinSynchro4DSchedules()
	: UITwinSynchro4DSchedules(false)
{
}

UITwinSynchro4DSchedules::UITwinSynchro4DSchedules(bool bDoNotBuildTimelines)
	: Impl(MakePimpl<FImpl>(*this, bDoNotBuildTimelines))
{
	// Do like in UCesiumGltfComponent's ctor to avoid crashes when changing level?
	// (from Carrot's Dashboard typically...)
	// Structure to hold one-time initialization
	struct FConstructorStatics {
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialMasked;
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialTranslucent;
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialTranslucent_TwoSided;
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialGlass;
		FConstructorStatics()
			: BaseMaterialMasked(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstance"))
			, BaseMaterialTranslucent(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent"))
			, BaseMaterialTranslucent_TwoSided(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent_TwoSided"))
			, BaseMaterialGlass(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinGlass"))
		{}
	};
	static FConstructorStatics ConstructorStatics;
	this->BaseMaterialMasked = ConstructorStatics.BaseMaterialMasked.Object;
	this->BaseMaterialTranslucent = ConstructorStatics.BaseMaterialTranslucent.Object;
	this->BaseMaterialTranslucent_TwoSided = ConstructorStatics.BaseMaterialTranslucent_TwoSided.Object;
	this->BaseMaterialGlass = ConstructorStatics.BaseMaterialGlass.Object;
}

UITwinSynchro4DSchedules::~UITwinSynchro4DSchedules()
{
	if (Impl->Internals.Uniniter) // CDO has none
		Impl->Internals.Uniniter->Run();
}

void UITwinSynchro4DSchedules::DebugProcessScheduleUpdateIncrement()
{
	if (bDebugFreezeIncrementalScheduleUpdates)
	{
		Impl->Internals.SchedulesApi.DebugProcessScheduleUpdateIncrement();
	}
}

#if WITH_TESTS
// Note : FTimeRangeInSeconds is in a Private header
bool UITwinSynchro4DSchedules::SimulateScheduleForTest(FSimulatedScheduleOptions const& Options)
{
	// Need a valid (and unique) GUID, see IsValidId() - don't overwrite to avoid passing the suffix everytime,
	// assume a valid Id means it was already called once and values do not need to be reset
	if (!HasValidId())
	{
		ScheduleId = FString::Printf(TEXT("01234567-abcd-dcba-4321-ba987654321%c"), Options.DigitSuffix());
		ScheduleName = FString::Printf(TEXT("SimulatedScheduleName_%c"), Options.DigitSuffix());
	}
	Impl->Internals.Schedule.emplace(FITwinSchedule(ScheduleId, ScheduleName, EITwinSchedulesGeneration::NextGen));
	Impl->Internals.SchedulesApi.SimulateScheduleForTest(Options);
	Impl->Internals.bSimulatedScheduleForTest = true;
	if (Options.NotifyScheduleId())
		OnScheduleInformationReceived.Broadcast(Cast<AITwinIModel>(GetOwner()), ScheduleId, ScheduleName);
	if (ITwin::Time::Undefined() != Options.TimeRange())
		Impl->Internals.Timeline().IncludeTimeRange(Options.TimeRange());
	if (Options.NotifyTimerange())
		Impl->Internals.SetScheduleTimeRangeIsKnown();
	else
		Impl->Internals.ScheduleTimeRangeIsKnownAndValid = (ITwin::Time::Undefined() != Options.TimeRange());
	if (Options.NotifyQueryingStopped())
		OnQueryLoopStatusChange(false, false);
	return true;
}
#endif // WITH_TESTS

FDateRange UITwinSynchro4DSchedules::GetDateRange() const
{
	if (Impl->Internals.ScheduleTimeRangeIsKnownAndValid && *Impl->Internals.ScheduleTimeRangeIsKnownAndValid)
		// Round to nearest second, because keyframes can be present slightly outside the original schedule's
		// timerange. This is compensated by the snapping to the schedule's start/end times in SetScheduleTime.
		return ITwin::Time::ToNearestSecond(Impl->Internals.GetTimeline().GetDateRange());
	else
		return FDateRange();
}

FDateTime UITwinSynchro4DSchedules::GetPlannedStartDate() const
{
	auto&& ScheduleRange = GetDateRange();
	return (ScheduleRange != FDateRange()) ? ScheduleRange.GetLowerBoundValue() : FDateTime();
}

FDateTime UITwinSynchro4DSchedules::GetPlannedEndDate() const
{
	auto&& ScheduleRange = GetDateRange();
	return (ScheduleRange != FDateRange()) ? ScheduleRange.GetUpperBoundValue() : FDateTime();
}

namespace Detail
{
	static const TCHAR* ErrPrefix = TEXT("Unknown:");
}

bool UITwinSynchro4DSchedules::HasValidId() const
{
	FGuid ValidScheduleGuid;
	return !ScheduleId.IsEmpty() && !ScheduleId.StartsWith(Detail::ErrPrefix)
		&& FGuid::Parse(ScheduleId, ValidScheduleGuid);
}

void UITwinSynchro4DSchedules::TickSchedules(float DeltaSeconds)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return; // fine, happens between constructor and registration to parent iModel
	Impl->Internals.CheckInitialized(*IModel);

	if (!IModel->ServerConnection // happens when an iModel is created from scratch by the user
		|| IModel->ITwinId.IsEmpty()) // happens transitorily in iTwinTestApp...
	{
		if (!HasValidId())
		{
			ScheduleId = Detail::ErrPrefix;
			if (!IModel->ServerConnection)
				ScheduleId += TEXT("NoServerConnection!");
			if (IModel->ITwinId.IsEmpty())
				ScheduleId += TEXT("NoITwinId!");
			ScheduleGeneration = EITwinSchedulesGeneration::Unknown;
		}
		return;
	}
	if (Impl->Schedule && !HasValidId())
	{
		ScheduleId = Impl->Schedule->Id;
		ScheduleName = Impl->Schedule->Name;
		ScheduleGeneration = Impl->Schedule->Generation;
		OnScheduleInformationReceived.Broadcast(IModel, ScheduleId, ScheduleName);
	}
	// eg. set manually or saved in a Level
	else if (!Impl->Schedule && HasValidId())
	{
		Impl->Schedule.emplace(FITwinSchedule(ScheduleId, ScheduleName, EITwinSchedulesGeneration::Unknown));
		OnScheduleInformationReceived.Broadcast(IModel, ScheduleId, ScheduleName);
	}
	else if (EITwinSchedulesGeneration::Unknown == ScheduleGeneration
		&& Impl->Schedule && EITwinSchedulesGeneration::Unknown != Impl->Schedule->Generation)
	{
		ScheduleGeneration = Impl->Schedule->Generation; // was inferred or re-queried by SchedulesApi (corner cases)
	}
	if (Impl->bResetSchedulesNeeded)
	{
		Impl->bResetSchedulesNeeded = false;
		Impl->bUpdateConnectionIfReadyNeeded = false;// does both
		Impl->Internals.ResetSchedules();
	}
	else if (Impl->bUpdateConnectionIfReadyNeeded)
	{
		Impl->bUpdateConnectionIfReadyNeeded = false;
		Impl->Internals.UpdateConnection(true);
	}
	else if (GetInternals(*IModel).HasSynchro4DSchedulesMetadataQueryingError())
	{
		return;
	}
	else
	{
		bool bNeedHandleQueries = (EITwinSchedulesGeneration::NextGen == Impl->Internals.Schedule->Generation);
		if (FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone != Impl->Internals.ApplySchedule)
		{
			if (IsAvailable())
			{
				// Needed for some case but which one...?
				// 'coz usually it's already done in the wrap-up batch of 4D queries
				// Now done _before_ what's below to avoid raising bNeedFinalizeTimelines a second time
				OnQueryLoopStatusChange(false);
				bool unused;
				Impl->CheckFinalizeScheduleLoading(DeltaSeconds, unused, unused);
			}
			else
				bNeedHandleQueries = true;
		}
		else
		{
			bool bHasReloadedTimelines, bTilesWillReload;
			Impl->CheckFinalizeScheduleLoading(DeltaSeconds, bHasReloadedTimelines, bTilesWillReload);
			// If timelines are reloaded, either tiles won't reload, and TickAnimation(.., *true*) was already called,
			// or we have to wait for tiles to reload anyway. If timelines were not reloaded, just update the 4D anim
			// incrementally as usual:
			if (!bHasReloadedTimelines)
				Impl->Animator.TickAnimation(DeltaSeconds, /*update all*/false);
		}
		if (bNeedHandleQueries)
		{
			// After loading the schedule, keep handling queries for NextGen incremental schedule updates
			// (even when updates are frozen for debugging: we may have in-flight requests)
			Impl->Internals.SchedulesApi.HandlePendingQueries();
		}
	}
}

void UITwinSynchro4DSchedules::OnVisibilityChanged(const TITwinSceneTilePtr& SceneTilePtr, bool bVisible)
{
	if (!IsAvailable())
		return;
	if (bVisible)
	{
		{
			auto SceneTileLock = SceneTilePtr->GetRAutoLock();
			auto& SceneTile = *SceneTileLock;
			ensure(!SceneTile.bVisible);
		}
		// Note: usually the tile was set up in OnNewTileBuilt but it is still possible, that
		// SceneTile.bIsSetupFor4DAnimation is false here: it happens when OnNewTileBuilt has been
		// called before the schedule was fully loaded, but OnVisibilityChanged is called after.
		// I could call TickSchedules _after_ HandleTilesHavingChangedVisibility in
		// AITwinIModel::Tick, but it would most likely lead to other problems...
		Impl->Internals.SetupAndApply4DAnimationSingleTile(SceneTilePtr);
	}
	//SceneTile->bVisible = bVisible; <== NO, done by FITwinIModelInternals::OnVisibilityChanged
}

bool UITwinSynchro4DSchedules::IsAvailable() const
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return false;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	return Impl->Internals.SchedulesApi.HasFinishedPrefetching() && !Impl->Internals.SchedulesApi.HasFetchingErrors()
		&& (Impl->Internals.bSimulatedScheduleForTest || IModelInternals.AreSynchro4DSchedulesMetadataLoadedOrCancelled());
}

double UITwinSynchro4DSchedules::PercentageLoadedFromCache() const
{
	if (IsAvailable())
	{
		AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
		if (!IModel)
			return false;
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		double FromCache = Impl->Internals.SchedulesApi.FetchedFromCache()
			+ IModelInternals.ElementsMetadataFetchedFromCache();
		double FromRemote = Impl->Internals.SchedulesApi.FetchedFromRemote()
			+ IModelInternals.ElementsMetadataFetchedFromRemote();
		if ((FromCache + FromRemote) != 0)
			return 100. * FromCache / (FromCache + FromRemote);
		else
			return 100.;
	}
	else
		return 0;
}

bool UITwinSynchro4DSchedules::SchedulesListingFailed() const
{
	return Impl->Internals.SchedulesApi.HasSchedulesListingFailed();
}

bool UITwinSynchro4DSchedules::Has4DAPIFetchingErrors() const
{
	return Impl->Internals.SchedulesApi.HasFetchingErrors();
}

bool UITwinSynchro4DSchedules::HasFetchingErrors() const
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return false;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	return Has4DAPIFetchingErrors() || IModelInternals.HasSynchro4DSchedulesMetadataQueryingError();
}

FString UITwinSynchro4DSchedules::FirstFetchingErrorString() const
{
	return FirstRequestErrorString();
}

FString UITwinSynchro4DSchedules::FirstRequestErrorString() const
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (IModel)
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		if (IModelInternals.HasSynchro4DSchedulesMetadataQueryingError())
			return IModelInternals.ElementsMetadataFirstErrorString();
	}
	if (SchedulesListingFailed() || Has4DAPIFetchingErrors())
		return Impl->Internals.SchedulesApi.FirstFetchingErrorString();
	else
		return FString();
}

EHttpResponseCodes::Type UITwinSynchro4DSchedules::FirstRequestErrorCode() const
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (IModel)
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		if (IModelInternals.HasSynchro4DSchedulesMetadataQueryingError())
			return IModelInternals.ElementsMetadataFirstErrorCode();
	}
	if (SchedulesListingFailed() || Has4DAPIFetchingErrors())
		return Impl->Internals.SchedulesApi.FirstFetchingErrorCode();
	else
		return EHttpResponseCodes::Ok;
}

bool UITwinSynchro4DSchedules::IsAvailableAsNextGenSchedule() const
{
	return IsAvailable() && Impl->Schedule
		&& EITwinSchedulesGeneration::NextGen == Impl->Schedule->Generation;
}

void UITwinSynchro4DSchedules::UpdateConnection()
{
	if (Impl->Internals.IsReadyToQuery())
		Impl->bUpdateConnectionIfReadyNeeded = true;
}

void UITwinSynchro4DSchedules::ResetSchedules()
{
	Impl->bResetSchedulesNeeded = true;
}

void UITwinSynchro4DSchedules::OnQueryLoopStatusChange(bool bQueryLoopIsRunning, bool logFullScheduleStats/*= true*/)
{
	ensure(IsInGameThread());
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!ensure(IModel)) return;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	if (bQueryLoopIsRunning)
	{
		BE_LOGI("ITwin4DImp", "Query loop (re)started...");
		IModelInternals.Update4DScheduleDownloadStatus(FITwinIModelInternals::E4DScheduleStatus::Unknown);
	}
	else if (Impl->Internals.SchedulesApi.HasFinishedPrefetching())
	{
		if (IsAvailable())
		{
			if (logFullScheduleStats)
			{
				BE_LOGI("ITwin4DImp", "Query loop now idling. "
					<< TCHAR_TO_UTF8(*Impl->Internals.SchedulesApi.ToString()));
			}
		}
		else if (HasFetchingErrors())
		{
			BE_LOGI("ITwin4DImp", "Query loop now idling following final query errors.");
		}
		else
		{
			BE_LOGI("ITwin4DImp", "Query loop now idling, waiting for iModel metadata...");
		}
		Impl->Internals.OnDownloadProgressed(100., Impl->Schedule && !Impl->Schedule->AnimationBindings.empty());
	}
	else
	{
		BE_LOGE("ITwin4DImp", "Query loop idling but prefetching not finished?!");
	}
	// This delegate accounts for Elements metadata queries completeness, too
	OnScheduleQueryingStatusChanged.Broadcast(
		// "query loop" running (or waiting for metadata... or stopped on error!)
		false == (IsAvailable() || HasFetchingErrors()));
}

void UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived(FDateTime StartTime, FDateTime EndTime)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!ensure(IModel)) return;
	if (!HasValidId())
	{
		BE_LOGI("ITwin4DImp", "Finished querying, no schedule for iModel " << TCHAR_TO_UTF8(*IModel->IModelId));
	}
	else if (Impl->Internals.SchedulesApi.NumTasks() > 0)
	{
		BE_LOGI("ITwin4DImp", "Schedule tasks received: " << Impl->Internals.SchedulesApi.NumTasks()
			<< " between "  << TCHAR_TO_UTF8(*StartTime.ToString())  << " and " << TCHAR_TO_UTF8(*EndTime.ToString())
			<< " for iModel " << TCHAR_TO_UTF8(*IModel->IModelId));
	}
	else
	{
		BE_LOGI("ITwin4DImp", "Finished querying, empty schedule for iModel " << TCHAR_TO_UTF8(*IModel->IModelId));
	}
}

void SetNeedForcedShadowUpdate(AActor* Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner);
	if (!IModel) return;
	GetInternals(*IModel).SetNeedForcedShadowUpdate();
}

void UITwinSynchro4DSchedules::Play()
{
	Impl->Animator.Play();
	//SetNeedForcedShadowUpdate(GetOwner()); <== handled by AITwinIModel::FImpl::ForceShadowUpdatesIfNeeded()
}

bool UITwinSynchro4DSchedules::IsPlaying() const
{
	return Impl->Animator.IsPlaying();
}

void UITwinSynchro4DSchedules::SetMeshesDynamicShadows(bool bDynamic)
{
	GetInternals(*this).SetMeshesDynamicShadows(bDynamic);
}

void UITwinSynchro4DSchedules::Pause()
{
	Impl->Animator.Pause();
	SetNeedForcedShadowUpdate(GetOwner());
}

bool UITwinSynchro4DSchedules::IsPaused() const
{
	return Impl->Animator.IsPaused();
}

void UITwinSynchro4DSchedules::Stop()
{
	Impl->Animator.Stop();
	SetNeedForcedShadowUpdate(GetOwner());
}

bool UITwinSynchro4DSchedules::IsStopped() const
{
	return Impl->Animator.IsStopped();
}

void UITwinSynchro4DSchedules::JumpToBeginning()
{
	auto const DateRange = GetDateRange();
	if (DateRange != FDateRange())
	{
		SetScheduleTime(DateRange.GetLowerBoundValue());
	}
}

void UITwinSynchro4DSchedules::JumpToEnd()
{
	auto const DateRange = GetDateRange();
	if (DateRange != FDateRange())
	{
		SetScheduleTime(DateRange.GetUpperBoundValue());
	}
}

void UITwinSynchro4DSchedules::AutoReplaySpeed(int ReplayPlayTime)
{
	auto const& TimeRange = Impl->Internals.GetTimeline().GetTimeRange();
	if (TimeRange.first < TimeRange.second)
	{
		SetReplaySpeed(FTimespan::FromHours( // round the number of hours per second
			std::ceil((TimeRange.second - TimeRange.first) / (3600 * ReplayPlayTime))));
	}
}

FDateTime UITwinSynchro4DSchedules::GetScheduleTime() const
{
	return ScheduleTime;
}

void UITwinSynchro4DSchedules::SetScheduleTime(FDateTime NewScheduleTime)
{
	//if (ScheduleTime != NewScheduleTime) <== don't: see PostEditChangeProperty
	auto const& TimeRange = Impl->Internals.GetTimeline().GetTimeRange();
	// Clamp but also snap to range boundaries to compensate for the rounding in SetScheduleTimeRangeIsKnown and not
	// affect the 4D animation's initial and final aspects, which usually rely on keyframes only spaced by some epsilon
	if ((ITwin::Time::FromDateTime(NewScheduleTime) - TimeRange.first) < 0.5/*second*/)
		NewScheduleTime = ITwin::Time::ToDateTime(TimeRange.first);
	else if ((ITwin::Time::FromDateTime(NewScheduleTime) - TimeRange.second) > (-0.5)/*second*/)
		NewScheduleTime = ITwin::Time::ToDateTime(TimeRange.second);
	ScheduleTime = NewScheduleTime;
	SetNeedForcedShadowUpdate(GetOwner());
	// This call used to call TickAnimation, in effect applying the new time right away! This could be a
	// problem in some border cases, and also if SetScheduleTime was called several times in a given frame.
	// The call is not actually needed since it will happen in the next iModel tick anyway.
	//Impl->Animator.OnChangedScheduleTime(false);
}

FTimespan UITwinSynchro4DSchedules::GetReplaySpeed() const
{
	return ReplaySpeed;
}

void UITwinSynchro4DSchedules::SetReplaySpeed(FTimespan NewReplaySpeed)
{
	//if (ReplaySpeed != NewReplaySpeed) <== don't: see PostEditChangeProperty
	ReplaySpeed = NewReplaySpeed;
	Impl->Animator.OnChangedAnimationSpeed();
}

void UITwinSynchro4DSchedules::ClearCacheOnlyThis()
{
	ClearCacheWithConfirmation();
}

bool UITwinSynchro4DSchedules::ClearCacheWithConfirmation()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel) {
		ensure(false); return false;
	}
	if (!HasValidId())
		return true; // OK, no schedule cache to delete
	if (Impl->Internals.SchedulesApi.HasFinishedPrefetching()
		&& GetInternals(*IModel).AreSynchro4DSchedulesMetadataLoadedOrCancelled())
	{
		FString const CacheName = Impl->Internals.SchedulesApi.ComputeCacheName();
		if (ensure(!CacheName.IsEmpty()))
		{
			// If it's a folder, it's the requests cache:
			if (IFileManager::Get().DirectoryExists(*CacheName))
			{
				return IFileManager::Get().DeleteDirectory(*CacheName, /*requireExists*/false, /*recurse*/true);
			}
			// Could be the full schedule cached as json, which we should delete as well:
			// (in that case, normally the folder does not exist)
			if (IFileManager::Get().FileExists(*(CacheName + TEXT(".json"))))
			{
				return IFileManager::Get().Delete(*(CacheName + TEXT(".json")));
			}
		}
	}
	return false;
}

void UITwinSynchro4DSchedules::ResetAnimationInTile(void* SceneTile)
{
	Impl->Animator.ResetAnimationInTile(*(TITwinSceneTilePtr*)SceneTile);
}

#if WITH_EDITOR
void UITwinSynchro4DSchedules::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	auto const Name = PropertyChangedEvent.Property->GetFName();
	bool bUpdateClassDefaults = false;
	if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ScheduleTime))
	{
		SetScheduleTime(ScheduleTime);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ReplaySpeed))
	{
		SetReplaySpeed(ReplaySpeed);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableColoring)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableVisibilities)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisablePartialVisibilities)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableCuttingPlanes)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableTransforms)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, GlTFTranslucencyRule)
	) {
		if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableVisibilities)
		 || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisablePartialVisibilities)
		 || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableTransforms)
		 || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, GlTFTranslucencyRule)
		) {
			Impl->UpdateGltfTunerRules();
		}
		Impl->Animator.OnChangedScheduleRenderSetting();
		SetNeedForcedShadowUpdate(GetOwner());
		bUpdateClassDefaults = true;
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, MaxTimelineUpdateMilliseconds)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ScheduleQueriesServerPagination)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ScheduleQueriesBindingsPagination))
	{
		bUpdateClassDefaults = true;
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bFadeOutNonAnimatedElements))
	{
		Impl->Animator.OnFadeOutNonAnimatedElements();
		SetNeedForcedShadowUpdate(GetOwner());
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bMaskOutNonAnimatedElements))
	{
		Impl->Animator.OnMaskOutNonAnimatedElements();
		SetNeedForcedShadowUpdate(GetOwner());
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, DebugRecordSessionQueries)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, DebugSimulateSessionQueries)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableCaching))
	{
		ResetSchedules();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ScheduleId))
	{
		FGuid SchedGuid;
		// Debug stuff: assume iModel matches schedule, and generation is that "favored" by bFavorNextGenSchedule:
		if (ScheduleId != Impl->FormerScheduleId && FGuid::Parse(ScheduleId, SchedGuid))
		{
			Impl->FormerScheduleId = ScheduleId;
			ResetSchedules();
		}
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bFavorNextGenSchedule))
	{
		// Added for bFavorNextGenSchedule:
		Impl->Internals.bDoNotReuseScheduleMetadata = true;
		ResetSchedules();
		bUpdateClassDefaults = true;
	}
	if (bUpdateClassDefaults)
	{
		Impl->Internals.UpdateS4DClassDefaults();
	}
}

#endif // WITH_EDITOR
