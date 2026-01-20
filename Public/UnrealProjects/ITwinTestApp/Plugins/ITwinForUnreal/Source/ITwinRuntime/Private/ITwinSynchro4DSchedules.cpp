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
#include <ITwinServerConnection.h>
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

#include <Components/StaticMeshComponent.h>
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
	ITWINRUNTIME_API bool GetSynchroDateFromSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap, FDateTime& OutDate, FString& ScheduleIDOut)
	{
		for (auto const& [Synchro4DSchedulesId, Synchro4DSchedules] : SchedMap)
			if (IsValid(Synchro4DSchedules) && Synchro4DSchedules->IsAvailable())
			{
				OutDate = Synchro4DSchedules->GetScheduleTime();
				auto&& ScheduleRange = Synchro4DSchedules->GetDateRange();
				if (ScheduleRange != FDateRange())
				{
					if (OutDate < ScheduleRange.GetLowerBoundValue())
						OutDate = ScheduleRange.GetLowerBoundValue();
					else if (OutDate > ScheduleRange.GetUpperBoundValue())
						OutDate = ScheduleRange.GetUpperBoundValue();
					ScheduleIDOut = Synchro4DSchedulesId;
					return true;
				}
			}
		return false;
	}

	ITWINRUNTIME_API void SetSynchroDateToSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap,
													const FDateTime& InDate)
	{
		for (auto const& [_, Synchro4DSchedules] : SchedMap)
			if (IsValid(Synchro4DSchedules) && Synchro4DSchedules->IsAvailable())
				Synchro4DSchedules->SetScheduleTime(InDate);
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
	std::recursive_mutex Mutex;
	std::vector<FITwinSchedule> Schedules;
	FITwinSynchro4DAnimator Animator;
	FITwinSynchro4DSchedulesInternals Internals; // <== must be declared LAST

	void UpdateGltfTunerRules();

public: // for TPimplPtr
	FImpl(UITwinSynchro4DSchedules& InOwner, bool const InDoNotBuildTimelines)
		: Owner(InOwner), Animator(InOwner)
		, Internals(InOwner, InDoNotBuildTimelines, Mutex, Schedules, Animator)
	{
	}
};

void UITwinSynchro4DSchedules::FImpl::UpdateGltfTunerRules()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!ensure(IModel))
		return;
	if (!ensure(Internals.GltfTuner))
	{
		// TODO_GCO: but existing tiles will not be setup for 4D :/
		Internals.MinGltfTunerVersionForAnimation = -1;//to apply schedule nonetheless
		return;
	}
	// Called explicitly when toggling flag off from UI, we need to reset 4D tuning rules:
	if (!Owner.bUseGltfTunerInsteadOfMeshExtraction
		&& std::numeric_limits<int>::max() != Internals.MinGltfTunerVersionForAnimation)
	{
		Internals.MinGltfTunerVersionForAnimation = Internals.GltfTuner->SetAnim4DRules(
			BeUtils::GltfTuner::Rules());
		return;
	}
	// Note: timelines with neither partial translucency nor transformation (ie only opaque colors
	// and cut planes) can be ignored here as they don't require Element separation.
	auto& SceneMapping = GetInternals(*IModel).SceneMapping;
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
			if (Sched.bPrefetchAllElementAnimationBindings)
			{
				// Like in InsertAnimatedMeshSubElemsRecursively, assume no children (= leaf Element) means
				// that the Element will have bHasMesh=true at some point (but usually not yet!)
				if (!Elem.SubElemsInVec.empty())
					return;
			}
			else if (!Elem.bHasMesh)
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
				if (ensure(Timeline))
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
	Internals.MinGltfTunerVersionForAnimation = Internals.GltfTuner->SetAnim4DRules(std::move(AnimRules));
}

static FITwinCoordConversions const& GetIModel2UnrealCoordConv(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // we can reach this for the CDO...
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		return IModelInternals.SceneMapping.GetIModel2UnrealCoordConv();
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
	bool const InDoNotBuildTimelines, std::recursive_mutex& InMutex,
	std::vector<FITwinSchedule>& InSchedules, FITwinSynchro4DAnimator& InAnimator)
:
	Owner(InOwner), bDoNotBuildTimelines(InDoNotBuildTimelines)
	, Builder(InOwner, GetIModel2UnrealCoordConv(InOwner))
	, SchedulesApi(InOwner, InMutex, InSchedules), Mutex(InMutex), Schedules(InSchedules)
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
			SchedulesApi.UninitializeCache();
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
	auto const& DateRange = GetTimeline().GetDateRange();
	if (DateRange != FDateRange())
	{
		ScheduleTimeRangeIsKnownAndValid = true;
		Owner.OnScheduleTimeRangeKnown.Broadcast(DateRange.GetLowerBoundValue(),
												 DateRange.GetUpperBoundValue());
	}
	else
	{
		ScheduleTimeRangeIsKnownAndValid = false;
		OnDownloadProgressed(100.);
		Owner.OnScheduleTimeRangeKnown.Broadcast(FDateTime::MinValue(), FDateTime::MinValue());
	}
}

void FITwinSynchro4DSchedulesInternals::ForEachElementTimeline(ITwinElementID const ElementID,
	std::function<void(FITwinElementTimeline const&)> const& Func) const
{
	auto const& MainTimeline = GetTimeline();
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	auto const& Elem = SceneMapping.GetElement(ElementID);
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
	ForEachElementTimeline(ElementID, [&Result](FITwinElementTimeline const& Timeline)
		{ Result.Append(Timeline.ToPrettyJsonString()); });
	return Result;
}

void FITwinSynchro4DSchedulesInternals::VisitSchedules(
	std::function<bool(FITwinSchedule const&)> const& Func) const
{
	std::lock_guard<std::recursive_mutex> Lock(Mutex);
	for (auto&& Sched : Schedules)
	{
		if (!Func(Sched))
			break;
	}
}

void FITwinSynchro4DSchedulesInternals::MutateSchedules(
	std::function<void(std::vector<FITwinSchedule>&)> const& Func)
{
	std::lock_guard<std::recursive_mutex> Lock(Mutex);
	Func(Schedules);
}

bool FITwinSynchro4DSchedulesInternals::TileCompatibleWithSchedule(ITwinScene::TileIdx const& TileRank) const
{
	if (!Owner.bUseGltfTunerInsteadOfMeshExtraction)
		return true;
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return false;
	return TileCompatibleWithSchedule(GetInternals(*IModel).SceneMapping.KnownTile(TileRank));
}

bool FITwinSynchro4DSchedulesInternals::TileCompatibleWithSchedule(FITwinSceneTile const& SceneTile) const
{
	if (!/*ensure*/(GltfTuner)) // might be used for debugging: return true to apply 4D nonetheless
		return true;
	if (!Owner.bUseGltfTunerInsteadOfMeshExtraction)
		return true;
	return TileTunedForSchedule(SceneTile);
}

bool FITwinSynchro4DSchedulesInternals::TileTunedForSchedule(FITwinSceneTile const& SceneTile) const
{
	if (!SceneTile.pCesiumTile)
		return false;
	auto* Model = SceneTile.pCesiumTile->GetGltfModel();
	if (!Model)
		return true;
	// When using the glTF tuner, no use storing stuff about loaded tiles until schedule is fully available:
	// retuning will unload all the SceneTile's anyway (even though the Cesium native tiles are not)!
	return (!Model->version || MinGltfTunerVersionForAnimation <= (*Model->version));
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

void FITwinSynchro4DSchedulesInternals::UnloadKnownTile(FITwinSceneTile& /*SceneTile*/,
														ITwinScene::TileIdx const& TileRank)
{
	ElementsReceived.erase(TileRank);
}

bool FITwinSynchro4DSchedulesInternals::PrefetchWholeSchedule() const
{
	return Owner.bPrefetchAllElementAnimationBindings
		&& !Owner.bDebugWithDummyTimelines;
}

bool FITwinSynchro4DSchedulesInternals::IsPrefetchedAvailableAndApplied() const
{
	return PrefetchWholeSchedule() && EApplySchedule::InitialPassDone == ApplySchedule;
}

bool FITwinSynchro4DSchedulesInternals::OnNewTileBuilt(FITwinSceneTile& SceneTile)
{
	if (IsPrefetchedAvailableAndApplied())
	{
		SceneTile.pCesiumTile->SetRenderReady(false);
		SetupAndApply4DAnimationSingleTile(SceneTile);
		return true;
	}
	return false;
}

void FITwinSynchro4DSchedulesInternals::HideNonAnimatedDuplicates(FITwinSceneTile& SceneTile,
																  FElementsGroup const& NonAnimatedDuplicates)
{
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

void FITwinSynchro4DSchedulesInternals::SetupAndApply4DAnimationSingleTile(FITwinSceneTile& SceneTile)
{
	if (!TileCompatibleWithSchedule(SceneTile))
	{
		auto const& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
		ElementsReceived.erase(SceneMapping.KnownTileRank(SceneTile));
		// Tile remains non-render-ready: except if you want to for debugging purposes, then uncomment:
		// SceneTile.pCesiumTile->SetRenderReady(true);
		return;
	}
	if (!SceneTile.bIsSetupFor4DAnimation)
	{
		Setup4DAnimationSingleTile(SceneTile, {}, nullptr);
	}
	Animator.ApplyAnimationOnTile(SceneTile);
}

void FITwinSynchro4DSchedulesInternals::SetMeshesDynamicShadows(bool bDynamic)
{
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	SceneMapping.ForEachKnownTile([bDynamic](FITwinSceneTile& SceneTile)
		{
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

void FITwinSynchro4DSchedulesInternals::Setup4DAnimationSingleTile(FITwinSceneTile& SceneTile,
	std::optional<ITwinScene::TileIdx> TileRank, std::unordered_set<ITwinScene::ElemIdx> const* Elements)
{
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	if (!TileRank)
		TileRank.emplace(SceneMapping.KnownTileRank(SceneTile));
	std::optional<typename decltype(ElementsReceived)::iterator> Pending;
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
			if (ensure(ElementTimeline))
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
			, Owner.bUseGltfTunerInsteadOfMeshExtraction
			, TileTunedForSchedule(SceneTile)
			, Index);
	}
	HideNonAnimatedDuplicates(SceneTile, MainTimeline.GetNonAnimatedDuplicates());
	
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

void FITwinSynchro4DSchedulesInternals::HandleReceivedElements(bool& bNew4DAnimTexToUpdate)
{
	if (ElementsReceived.empty())
		return;

	// In principle, OnElementsTimelineModified must be called for each timeline applying to an Element (or
	// one of its ancester node Element, or a group containing an Element) that has been received, with the
	// exact set of Elements received, because the code depends on the kind of keyframes present, and flags
	// are set on ElementFeaturesInTile individually.
	// Initially, before we pre-fetched animation bindings, we had no direct mapping from the Elements to their
	// timeline(s), so ReplicateAnimElemTextureSetupInTile & the FElemAnimRequirements system was added
	// to take care of Elements already animated _in other tiles_. Elements not yet animated were passed on
	// to QueryElementsTasks anyway, so OnElementsTimelineModified would be called for them later if needed.
	// With PrefetchWholeSchedule, the situation is reversed: we have all bindings (once
	// IsAvailable() returns true), so OnElementsTimelineModified needs to be called on all Elements, because
	// no new query will be made.
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;

	// Note: only used by initial pass now
	if (PrefetchWholeSchedule() && ensure(Owner.IsAvailable()))
	{
		for (auto const& [TileRank, TileElems] : ElementsReceived)
		{
			auto& SceneTile = SceneMapping.KnownTile(TileRank);
			// may have been unloaded while waiting for ElementsReceived to be processed
			if (SceneTile.IsLoaded() && TileCompatibleWithSchedule(SceneTile))
				Setup4DAnimationSingleTile(SceneTile, TileRank, &TileElems);
		}
	}
	else if (IsReadyToQuery() || Owner.bDebugWithDummyTimelines)
	{
		for (auto const& TileMeshElements : ElementsReceived)
		{
			bNew4DAnimTexToUpdate |= SceneMapping.ReplicateAnimElemTextureSetupInTile(TileMeshElements);
		}
		if (Owner.bDebugWithDummyTimelines)
		{
			std::lock_guard<std::recursive_mutex> Lock(Mutex);
			if (Schedules.empty())
				Schedules.emplace_back(FITwinSchedule{ TEXT("DummySchedId"), TEXT("DummySchedule") });
			FITwinSchedule& Sched = Schedules[0];
			for (auto&& TileElements : ElementsReceived)
			{
				size_t const BindingIdx = Sched.AnimationBindings.size();
				size_t const GroupIdx = Sched.Groups.size();
				Sched.AnimationBindings.emplace_back();
				auto& Group = Sched.Groups.emplace_back();
				for (auto&& Elem : TileElements.second)
					Group.insert(SceneMapping.GetElement(Elem).ElementID);
				// Set just enough stuff to use AddAnimationBindingToTimeline
				auto& Binding = Sched.AnimationBindings[BindingIdx];
				Binding.AnimatedEntities = FString::Printf(TEXT("DummyGroup%llu"), GroupIdx);
				Binding.GroupInVec = GroupIdx;
				Binding.NotifiedVersion = VersionToken::None;
				Builder.AddAnimationBindingToTimeline(Sched, BindingIdx, Lock);
				Binding.NotifiedVersion = VersionToken::InitialVersion;
			}
		}
		else
		{
			// ElementIDs are already mapped in the SchedulesApi structures to avoid redundant requests, so it
			// was redundant to merge the sets here, until we needed to add the parent Elements as well:
			std::set<ITwinElementID> MergedSet;
			for (auto SetsIt = ElementsReceived.begin(); SetsIt != ElementsReceived.end(); ++SetsIt)
			{
				for (auto const& ElemID : SetsIt->second)
				{
					FITwinElement const* pElem = &SceneMapping.GetElement(ElemID);
					while (true)
					{
						if (!MergedSet.insert(pElem->ElementID).second)
							break; // if already present, all its parents are, too
						if (ITwinScene::NOT_ELEM == pElem->ParentInVec)
							break;
						pElem = &SceneMapping.GetElement(pElem->ParentInVec);
					}
				}
				SetsIt->second.clear();
			}
			SchedulesApi.QueryElementsTasks(MergedSet);
		}
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
			&& ITwin::Timeline::EAnchorPoint::Static == Deferred.TransformKeyframe->DefrdAnchor.AnchorPoint));
	ensure(Deferred.PlaneOrientation.IsUnit());
	// Necessarily static assignment - growth simulation disabled along 3D Paths
	std::optional<FBox> AsAssignedBox;
	if (Deferred.TransformKeyframe)
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
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedules);
	// Clear Schedules AFTER FITwinSchedulesImport::Impl is deleted above, because 1/ schedules can be
	// accessed by FromPool.AsyncRoutine until they're all finished, which is waited on in
	// FReusableJsonQueries::FImpl, and 2/ clear() here is called without locking Mutex:
	for (auto& Sched : Schedules)
	{
		// Keep "metadata": this will skip them in RequestSchedules, speeding up Reset a lot by avoiding
		// a useless repetition of the request.
		Sched = FITwinSchedule{ Sched.Id, Sched.Name, Sched.Generation };
	}
	// See comment below about ordering between SchedulesApi and Builder:
	Builder.Uninitialize();
	Builder = FITwinScheduleTimelineBuilder(Owner, GetIModel2UnrealCoordConv(Owner));
	if (!bDoNotBuildTimelines)
	{
		SchedulesApi.SetSchedulesImportConnectors(
			// getting Builder's pointer here should be safe, because SchedulesApi is deleted /before/
			// Builder, (both above and in the destructor, as per the members' declaration order), which
			// will ensure no more request callbacks and thus no more calls to this subsequent callback:
			std::bind(&FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline, &Builder,
					  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			std::bind(&FITwinScheduleTimelineBuilder::UpdateAnimationGroupInTimeline, &Builder,
					  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			std::bind(&FITwinSceneMapping::FindElementIDForGUID,
					  &GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping,
					  std::placeholders::_1, std::placeholders::_2));
	}
#if WITH_EDITOR
	if (!PrefetchWholeSchedule())
	{
		Owner.OnScheduleQueryingStatusChanged.AddUniqueDynamic(&Owner,
			&UITwinSynchro4DSchedules::OnQueryLoopStatusChange);
	}
	Owner.OnScheduleTimeRangeKnown.AddUniqueDynamic(&Owner,
		&UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived);
#endif
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
		if (ensure(IModel.bResolvedChangesetIdValid))
			SchedulesApi.ResetConnection(IModel.ITwinId, IModel.IModelId, IModel.GetSelectedChangeset());
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

	IModelInternals.SceneMapping.SetTimelineGetter(
		std::bind(&FITwinSynchro4DSchedulesInternals::GetTimeline, this));

	IModelInternals.SceneMapping.SetMaterialGetter(
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

	if (PrefetchWholeSchedule())
	{
		// If the tileset is already loaded, we need to re-fill ElementsReceived with all tiles and Elements,
		// so that the Timeline optimization structures (FITwinElementTimeline::ExtraData) are re-created
		ElementsReceived.clear();
		IModelInternals.SceneMapping.ForEachKnownTile(
			[&AllReceived=this->ElementsReceived, &SceneMapping=IModelInternals.SceneMapping]
			(FITwinSceneTile& SceneTile)
			{
				if (!SceneTile.IsLoaded())
					return;
				SceneTile.bIsSetupFor4DAnimation = false;
				std::unordered_set<ITwinScene::ElemIdx> TileElems;
				SceneTile.ForEachElementFeatures([&TileElems](FITwinElementFeaturesInTile const& ElemInTile)
					{
						TileElems.insert(ElemInTile.SceneRank);
					});
				AllReceived.emplace(SceneMapping.KnownTileRank(SceneTile), std::move(TileElems));
			});
	}
	else
	{
		// If the tileset is already loaded, we need to trigger QueryElementsTasks for all Elements for which
		// we have already received some mesh parts, but also for all their parents/ancesters, which may have
		// anim bindings that will also animate the children.
		auto const& AllElems = IModelInternals.SceneMapping.GetElements();
		std::set<ITwinElementID> ElementIDs;
		for (auto const& Elem : AllElems)
		{
			if (Elem.bHasMesh) // start from leaves (can intermediate nodes have their own geom too?)
			{
				FITwinElement const* pElem = &Elem;
				while (true)
				{
					if (!ElementIDs.insert(pElem->ElementID).second)
						break; // if already present, all its parents are, too
					if (ITwinScene::NOT_ELEM == pElem->ParentInVec)
						break;
					pElem = &IModelInternals.SceneMapping.GetElement(pElem->ParentInVec);
				}
			}
		}
		if (!ElementIDs.empty())
			SchedulesApi.QueryElementsTasks(ElementIDs);
	}
	return true;
}

void FITwinSynchro4DSchedulesInternals::OnDownloadProgressed(double PercentComplete)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	IModelInternals.OnScheduleDownloadProgressed(PercentComplete);
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

FDateRange UITwinSynchro4DSchedules::GetDateRange() const
{
	if (Impl->Internals.ScheduleTimeRangeIsKnownAndValid && *Impl->Internals.ScheduleTimeRangeIsKnownAndValid)
		return Impl->Internals.GetTimeline().GetDateRange();
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

void UITwinSynchro4DSchedules::TickSchedules(float DeltaTime)
{
	static const TCHAR* ErrPrefix = TEXT("Unknown:");
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return; // fine, happens between constructor and registration to parent iModel
	Impl->Internals.CheckInitialized(*IModel);

	if (!IModel->ServerConnection // happens when an iModel is created from scratch by the user
		|| IModel->ITwinId.IsEmpty()) // happens transitorily in iTwinTestApp...
	{
		if (ScheduleId.IsEmpty() || ScheduleId.StartsWith(ErrPrefix))
		{
			ScheduleId = ErrPrefix;
			if (!IModel->ServerConnection)
				ScheduleId += TEXT("NoServerConnection!");
			if (IModel->ITwinId.IsEmpty())
				ScheduleId += TEXT("NoITwinId!");
		}
		return;
	}
	if (!Impl->Schedules.empty() && (ScheduleId.IsEmpty() || ScheduleId.StartsWith(ErrPrefix)))
	{
		ScheduleId = Impl->Schedules[0].Id;
		ScheduleName = Impl->Schedules[0].Name;
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
	else if (Impl->Internals.PrefetchWholeSchedule()
		&& FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone != Impl->Internals.ApplySchedule)
	{
		if (IsAvailable())
		{
			if (bUseGltfTunerInsteadOfMeshExtraction
				&& std::numeric_limits<int>::max() == Impl->Internals.MinGltfTunerVersionForAnimation)
			{
				ensure(false); // should have been called already
				Impl->UpdateGltfTunerRules();
			}
			bool dummy = false;
			Impl->Internals.HandleReceivedElements(dummy);
			Impl->Internals.ApplySchedule = FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone;
			Impl->Animator.TickAnimation(DeltaTime, true);
		}
		else
		{
			Impl->Internals.SchedulesApi.HandlePendingQueries();
			// For selection textures: not needed, UpdateSelectingAndHidingTextures called from iModel tick
			//GetInternals(*Cast<AITwinIModel>(GetOwner())).SceneMapping.Update4DAnimTextures();
		}
	}
	else
	{
		bool bNew4DAnimTexToUpdate = false;
		if (!Impl->Internals.PrefetchWholeSchedule())
		{
			Impl->Internals.HandleReceivedElements(bNew4DAnimTexToUpdate);
		}
		ensure(Impl->Internals.ElementsReceived.empty());
		Impl->Internals.SchedulesApi.HandlePendingQueries();
		Impl->Animator.TickAnimation(DeltaTime, bNew4DAnimTexToUpdate);
	}
}

void UITwinSynchro4DSchedules::OnVisibilityChanged(FITwinSceneTile& SceneTile, bool bVisible)
{
	if (!IsAvailable())
		return;
	if (bVisible)
	{
		ensure(!SceneTile.bVisible);
		// Note: usually the tile was set up in OnNewTileBuilt but it is still possible, that
		// SceneTile.bIsSetupFor4DAnimation is false here: it happens when OnNewTileBuilt has been
		// called before the schedule was fully loaded, but OnVisibilityChanged is called after.
		// I could call TickSchedules _after_ HandleTilesHavingChangedVisibility in
		// AITwinIModel::Tick, but it would most likely lead to other problems...
		Impl->Internals.SetupAndApply4DAnimationSingleTile(SceneTile);
	}
	//SceneTile->bVisible = bVisible; <== NO, done by FITwinIModelInternals::OnVisibilityChanged
}

bool UITwinSynchro4DSchedules::IsAvailable() const
{
	return Impl->Internals.SchedulesApi.HasFinishedPrefetching();
}

bool UITwinSynchro4DSchedules::IsAvailableWithErrors() const
{
	return IsAvailable() && Impl->Internals.SchedulesApi.HasFetchingErrors();
}

FString UITwinSynchro4DSchedules::FirstFetchingErrorString() const
{
	if (IsAvailableWithErrors())
		return Impl->Internals.SchedulesApi.FirstFetchingErrorString();
	else
		return FString();
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

void UITwinSynchro4DSchedules::OnQueryLoopStatusChange(bool bQueryLoopIsRunning)
{
	if (bQueryLoopIsRunning)
	{
		UE_LOG(LogITwinSched, Display, TEXT("Query loop (re)started..."));
	}
	else if (IsAvailable())
	{
		UE_LOG(LogITwinSched, Display, TEXT("Query loop now idling. %s"),
										*Impl->Internals.SchedulesApi.ToString());
		Impl->Internals.OnDownloadProgressed(100.);
		if (bUseGltfTunerInsteadOfMeshExtraction)
			Impl->UpdateGltfTunerRules();
		if (!DebugDumpAsJsonAfterQueryAll.IsEmpty())
			Impl->Internals.Builder.DebugDumpFullTimelinesAsJson(DebugDumpAsJsonAfterQueryAll);
	}
	else
	{
		UE_LOG(LogITwinSched, Display, TEXT("Query loop now idling: no schedule available."));
	}
}

void UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived(FDateTime StartTime, FDateTime EndTime)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!ensure(IModel)) return;
	if (ScheduleId.IsEmpty() || !ensure(!ScheduleId.StartsWith(TEXT("Unknown"))))
	{
		UE_LOG(LogITwinSched, Display, TEXT("Finished querying, no schedule for iModel %s"),
			   *IModel->IModelId);
	}
	else if (Impl->Internals.SchedulesApi.NumTasks() > 0)
	{
		UE_LOG(LogITwinSched, Display, TEXT("Schedule tasks received: %llu between %s and %s for iModel %s"),
			   Impl->Internals.SchedulesApi.NumTasks(), *StartTime.ToString(), *EndTime.ToString(),
			   *IModel->IModelId);
	}
	else
	{
		UE_LOG(LogITwinSched, Display, TEXT("Finished querying, empty schedule for iModel %s"),
			   *IModel->IModelId);
	}
}

void UITwinSynchro4DSchedules::QueryAll()
{
	if (!Impl->Internals.IsReadyToQuery()) return;
	Impl->Internals.SchedulesApi.QueryEntireSchedules(QueryAllFromTime, QueryAllUntilTime,
		[This=this](bool bSuccess)
		{
			if (bSuccess && IsValid(This) && !This->DebugDumpAsJsonAfterQueryAll.IsEmpty())
				GetInternals(*This).Builder.DebugDumpFullTimelinesAsJson(This->DebugDumpAsJsonAfterQueryAll);
		});
}

void UITwinSynchro4DSchedules::QueryAroundElementTasks(FString const ElementID,
	FTimespan const MarginFromStart, FTimespan const MarginFromEnd)
{
	if (!Impl->Internals.IsReadyToQuery()) return;
	Impl->Internals.SchedulesApi.QueryAroundElementTasks(ITwin::ParseElementID(ElementID),
														 MarginFromStart, MarginFromEnd);
}

void UITwinSynchro4DSchedules::QueryElementsTasks(TArray<FString> const& Elements)
{
	if (!Impl->Internals.IsReadyToQuery()) return;
	std::set<ITwinElementID> ElementIDs;
	for (auto&& Elem : Elements)
	{
		auto const Id = ITwin::ParseElementID(Elem);
		if (ITwin::NOT_ELEMENT != Id)
			ElementIDs.insert(Id);
	}
	Impl->Internals.SchedulesApi.QueryElementsTasks(ElementIDs);
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

void SetNeedForcedShadowUpdate(AActor* Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner);
	if (!IModel) return;
	GetInternals(*IModel).SetNeedForcedShadowUpdate();
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

void UITwinSynchro4DSchedules::Stop()
{
	Impl->Animator.Stop();
	SetNeedForcedShadowUpdate(GetOwner());
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
	if (!ScheduleId.IsEmpty() && !ScheduleId.StartsWith(TEXT("Unknown")))
	{
		AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
		if (!IModel) {
			ensure(false); return;
		}
		FString const CacheFolder = QueriesCache::GetCacheFolder(QueriesCache::ESubtype::Schedules,
			IModel->ServerConnection->Environment, IModel->ITwinId, IModel->IModelId,
			IModel->ResolvedChangesetId,
			bStream4DFromAPIM ? (FString("APIM_") + ScheduleId) : ScheduleId);
		if (ensure(!CacheFolder.IsEmpty()))
			IFileManager::Get().DeleteDirectory(*CacheFolder, /*requireExists*/false, /*recurse*/true);
	}
}

void UITwinSynchro4DSchedules::ClearCacheAllSchedules()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel) {
		ensure(false); return;
	}
	FString const CacheFolder = QueriesCache::GetCacheFolder(QueriesCache::ESubtype::Schedules,
		IModel->ServerConnection->Environment, {}, {}, {});
	if (ensure(!CacheFolder.IsEmpty()))
		IFileManager::Get().DeleteDirectory(*CacheFolder, /*requireExists*/false, /*recurse*/true);
}

void UITwinSynchro4DSchedules::DisableAnimationInTile(void* SceneTile)
{
	Impl->Animator.DisableAnimationInTile(*(FITwinSceneTile*)SceneTile);
}

#if WITH_EDITOR
void UITwinSynchro4DSchedules::SendPartialQuery()
{
	if (QueryOnlyThisElementSchedule.IsEmpty()) return;
	// ~1000 years = hack to allow direct testing of QueryElementsTasks
	if (QueryScheduleBeforeAndAfterElement < FTimespan::FromDays(-365000))
		QueryElementsTasks({ QueryOnlyThisElementSchedule });
	else
		QueryAroundElementTasks(QueryOnlyThisElementSchedule,
			-QueryScheduleBeforeAndAfterElement, QueryScheduleBeforeAndAfterElement);
}
#endif // WITH_EDITOR

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
		  || (bUseGltfTunerInsteadOfMeshExtraction
			  && Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, GlTFTranslucencyRule))
	) {
		if (bUseGltfTunerInsteadOfMeshExtraction
			&& (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableVisibilities)
				|| Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisablePartialVisibilities)
				|| Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableTransforms)
				|| Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, GlTFTranslucencyRule))
		) {
			Impl->UpdateGltfTunerRules();
		}
		Impl->Animator.OnChangedScheduleRenderSetting();
		SetNeedForcedShadowUpdate(GetOwner());
		bUpdateClassDefaults = true;
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bUseGltfTunerInsteadOfMeshExtraction))
	{
		// needed to reset tuning, because in that case, ResetSchedules won't call it
		if (!bUseGltfTunerInsteadOfMeshExtraction)
			Impl->UpdateGltfTunerRules();
		ResetSchedules();
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
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bStream4DFromAPIM))
	{
		ResetSchedules();
		bUpdateClassDefaults = true;
	}
	if (bUpdateClassDefaults)
	{
		auto* Settings = GetMutableDefault<UITwinIModelSettings>();
		Settings->Synchro4DMaxTimelineUpdateMilliseconds = MaxTimelineUpdateMilliseconds;
		Settings->Synchro4DQueriesDefaultPagination = ScheduleQueriesServerPagination;
		Settings->Synchro4DQueriesBindingsPagination = ScheduleQueriesBindingsPagination;
		Settings->bSynchro4DUseGltfTunerInsteadOfMeshExtraction = bUseGltfTunerInsteadOfMeshExtraction;
		Settings->Synchro4DGlTFTranslucencyRule = GlTFTranslucencyRule;
		Settings->bSynchro4DDisableColoring = bDisableColoring;
		Settings->bSynchro4DDisableVisibilities = bDisableVisibilities;
		Settings->bSynchro4DDisablePartialVisibilities = bDisablePartialVisibilities;
		Settings->bSynchro4DDisableCuttingPlanes = bDisableCuttingPlanes;
		Settings->bSynchro4DUseAPIM = bStream4DFromAPIM;
	}
}
#endif // WITH_EDITOR
