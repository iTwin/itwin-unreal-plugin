/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedules.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinServerConnection.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <Network/JsonQueriesCache.h>
#include <Timeline/AnchorPoint.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/SchedulesStructs.h>

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

DECLARE_LOG_CATEGORY_EXTERN(LogITwinSched, Log, All);
DEFINE_LOG_CATEGORY(LogITwinSched);

constexpr double AUTO_SCRIPT_DURATION = 30.;

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

	void UpdateConnection(bool const bOnlyIfReady);

public: // for TPimplPtr
	FImpl(UITwinSynchro4DSchedules& InOwner, bool const InDoNotBuildTimelines)
		: Owner(InOwner), Animator(InOwner)
		, Internals(InOwner, InDoNotBuildTimelines, Mutex, Schedules)
	{
	}
};

static std::optional<FTransform> const& GetIModel2UnrealTransfo(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // the CDO is in that case...
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		return IModelInternals.SceneMapping.GetIModel2UnrealTransfo();
	}
	else
	{
		static std::optional<FTransform> Dummy; // left uninit, will error out later anyway
		return Dummy;
	}
}

static FVector const& GetSynchro4DOriginUE(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // the CDO is in that case...
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		return IModelInternals.SceneMapping.GetSynchro4DOriginUE();
	}
	else
	{
		return FVector::ZeroVector;
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
	std::vector<FITwinSchedule>& InSchedules)
:
	Owner(InOwner), bDoNotBuildTimelines(InDoNotBuildTimelines)
	, Builder(InOwner, GetIModel2UnrealTransfo(InOwner), GetSynchro4DOriginUE(InOwner))
	, SchedulesApi(InOwner, InMutex, InSchedules), Mutex(InMutex), Schedules(InSchedules)
{
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
	// NOT Owner.GetDateRange(), which relies on ScheduleTimeRangeIsKnown set below!
	auto const& DateRange = GetTimeline().GetDateRange();
	if (DateRange != FDateRange())
	{
		ScheduleTimeRangeIsKnown = true;
		Owner.OnScheduleTimeRangeKnown.Broadcast(DateRange.GetLowerBoundValue(),
												 DateRange.GetUpperBoundValue());
	}
	else
	{
		ScheduleTimeRangeIsKnown = false;
		Owner.OnScheduleTimeRangeKnown.Broadcast(FDateTime::MinValue(), FDateTime::MinValue());
	}
}

void FITwinSynchro4DSchedulesInternals::ForEachElementTimeline(ITwinElementID const ElementID,
	std::function<void(FITwinElementTimeline const&)> const& Func) const
{
	auto const& MainTimeline = GetTimeline();
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	auto const& Elem = SceneMapping.GetElement(ElementID);
	for (auto AnimKey : Elem.AnimationKeys)
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

/// Most of the handling is delayed until the beginning of the next tick, in hope a given tile would be fully
/// loaded before calling OnElementsTimelineModified, to avoid resizing property textures. But it might not
/// be sufficient if 1/ meshes of a same tile are loaded by different ticks (which DOES happen, UNLESS it's
/// only an effect of our GltfTuner?!) - and 2/ new FeatureIDs are discovered in non-first ticks...
void FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt(CesiumTileID const& TileId,
	std::set<ITwinElementID>&& MeshElementIDs, const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
	bool const /*bFirstTimeSeenTile*/, FITwinSceneTile& SceneTile)
{
	if (MeshElementIDs.empty()
		|| (PrefetchAllElementAnimationBindings() && EApplySchedule::InitialPassDone != ApplySchedule))
	{
		// schedule not yet applied so we don't care - irrelevant if !s_bMaskTilesUntilFullyAnimated
		SceneTile.bNewMeshesToAnimate = false;
		// Note: don't use bFirstTimeSeenTile because we never know when a tile is finished loading anyway :/
		return;
	}
	if (ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated)
	{
		// When schedule is applied, default to "invisible" to avoid popping meshes (you get popping holes
		// instead, much better! :-))
		FITwinSceneMapping::SetForcedOpacity(pMaterial, 0.f);
	}
	// MeshElementIDs is actually moved only in case of insertion, otherwise it is untouched
	auto Entry = ElementsReceived.try_emplace(TileId, std::move(MeshElementIDs));
	if (!Entry.second) // was not inserted, merge with existing set:
	{
		for (auto&& Elem : MeshElementIDs)
			Entry.first->second.insert(Elem);
	}
}

bool FITwinSynchro4DSchedulesInternals::PrefetchAllElementAnimationBindings() const
{
	return Owner.bPrefetchAllElementAnimationBindings
		&& !Owner.bDebugWithDummyTimelines;
}

bool FITwinSynchro4DSchedulesInternals::IsPrefetchedAvailableAndApplied() const
{
	return PrefetchAllElementAnimationBindings() && EApplySchedule::InitialPassDone == ApplySchedule;
}

void FITwinSynchro4DSchedulesInternals::HandleReceivedElements(bool& bNewTilesReceived)
{
	if (!IsReadyToQuery() || ElementsReceived.empty())
		return;

	// In principle, OnElementsTimelineModified must be called for each timeline applying to an Element (or
	// one of its ancester node Element, or a group containing an Element) that has been received, with the
	// exact set of Elements received, because the code depends on the kind of keyframes present, and flags
	// are set on ElementFeaturesInTile individually.
	// Initially, before we pre-fetched all animation bindings, we had no direct mapping from the Elements to
	// their timeline(s), so ReplicateAnimatedElementsSetupInTile and the TileRequirements system was added
	// to take care of Elements already animated _in other tiles_. Elements not yet animated were passed on
	// to QueryElementsTasks anyway, so OnElementsTimelineModified would be called for them later if needed.
	// With PrefetchAllElementAnimationBindings, the situation is reversed: we have all bindings (once
	// IsAvailable() returns true), so OnElementsTimelineModified needs to be called on all Elements, because
	// no new query will be made.

	// But we will do as if we received all Elements on any given timeline at the same time, to
	// avoid calculating the map<Timeline, vector<ElementsReceived>> we would need...
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	if (!PrefetchAllElementAnimationBindings() || EApplySchedule::InitialPassDone == ApplySchedule)
	{
		for (auto const& TileMeshElements : ElementsReceived)
		{
			bNewTilesReceived |= SceneMapping.ReplicateAnimatedElementsSetupInTile(TileMeshElements);
		}
	}
	if (!PrefetchAllElementAnimationBindings())
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
					if (!MergedSet.insert(pElem->Id).second)
						break; // if already present, all its parents are, too
					if (ITwinScene::NOT_ELEM == pElem->ParentInVec)
						break;
					pElem = &SceneMapping.GetElement(pElem->ParentInVec);
				}
			}
			SetsIt->second.clear();
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
				Sched.Groups.emplace_back();
				Sched.Groups[GroupIdx].swap(TileElements.second);
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
void FITwinSynchro4DSchedulesInternals::FinalizeCuttingPlaneEquation(
	ITwin::Timeline::FDeferredPlaneEquation const& Deferred, FBox const& ElementsWorldBox)
{
	ensure(Deferred.PlaneOrientation.IsUnit());
	FBox const ExpandedBox = ElementsWorldBox.ExpandBy(0.01 * ElementsWorldBox.GetSize());
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
	default:
		ensure(false);
		Position = ExpandedBox.GetCenter();
		break;
	}
	// Note: TVector(const UE::Math::TVector4<T>& V); is NOT explicit, which is a shame IMHO
	Deferred.PlaneW = FVector3f(Position).Dot(Deferred.PlaneOrientation);
}

/*static*/
void FITwinSynchro4DSchedulesInternals::FinalizeAnchorPos(
	ITwin::Timeline::FDeferredAnchor const& Deferred, FBox const& ElementsWorldBox)
{
	ensure(Deferred.bDeferred);
	FVector Center, Extents;
	ElementsWorldBox.GetCenterAndExtents(Center, Extents);
	// Note: 'Extents' is half (Max - Min)
	switch (Deferred.AnchorPoint)
	{
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
		Deferred.Offset = FVector(0, Extents.Y, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MaxY:
		Deferred.Offset = FVector(0, -Extents.Y, 0);
		break;
	case ITwin::Timeline::EAnchorPoint::MinZ:
		Deferred.Offset = FVector(0, 0, Extents.Z);
		break;
	case ITwin::Timeline::EAnchorPoint::MaxZ:
		Deferred.Offset = FVector(0, 0, -Extents.Z);
		break;
	case ITwin::Timeline::EAnchorPoint::Custom:
		break;
	case ITwin::Timeline::EAnchorPoint::Original: // shouldn't be deferred
	case ITwin::Timeline::EAnchorPoint::Static: // shouldn't be deferred
	default:
		ensure(false);
		break;
	}
	Deferred.bDeferred = false;
}

bool FITwinSynchro4DSchedulesInternals::IsReadyToQuery() const
{
	return SchedulesApi.IsReadyToQuery(); // other members need no particular init
}

void FITwinSynchro4DSchedulesInternals::Reset()
{
	ApplySchedule = EApplySchedule::WaitForFullSchedule;
	Schedules.clear();
	// see comment below about ordering:
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedules);
	Builder = FITwinScheduleTimelineBuilder(
		Owner, GetIModel2UnrealTransfo(Owner), GetSynchro4DOriginUE(Owner));
	if (!bDoNotBuildTimelines)
	{
		SchedulesApi.SetSchedulesImportObservers(
			// getting Builder's pointer here should be safe, because SchedulesApi is deleted /before/
			// Builder, (both above and in the destructor, as per the members' declaration order), which
			// will ensure no more request callbacks and thus no more calls to this subsequent callback:
			std::bind(&FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline, &Builder,
					  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
			std::bind(&FITwinScheduleTimelineBuilder::UpdateAnimationGroupInTimeline, &Builder,
					  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}
#if WITH_EDITOR
	if (!PrefetchAllElementAnimationBindings()
		&& !Owner.OnScheduleQueryingStatusChanged.IsAlreadyBound(&Owner,
				&UITwinSynchro4DSchedules::LogStatisticsUponQueryLoopStatusChange))
	{
		Owner.OnScheduleQueryingStatusChanged.AddDynamic(&Owner,
			&UITwinSynchro4DSchedules::LogStatisticsUponQueryLoopStatusChange);
	}
	if (!Owner.OnScheduleTimeRangeKnown.IsAlreadyBound(&Owner,
		&UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived))
	{
		Owner.OnScheduleTimeRangeKnown.AddDynamic(&Owner,
			&UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived);
	}
#endif
}

FITwinSchedulesImport& FITwinSynchro4DSchedulesInternals::GetSchedulesApiReadyForUnitTesting()
{
	ensure(IsReadyToQuery() || ResetSchedules());
	return SchedulesApi;
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
	// Do like in UITwinCesiumGltfComponent's ctor to avoid crashes when changing level?
	// (from Carrot's Dashboard typically...)
	// Structure to hold one-time initialization
	struct FConstructorStatics {
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialMasked;
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialTranslucent;
		FConstructorStatics()
			: BaseMaterialMasked(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstance"))
			, BaseMaterialTranslucent(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent"))
		{}
	};
	static FConstructorStatics ConstructorStatics;
	this->BaseMaterialMasked = ConstructorStatics.BaseMaterialMasked.Object;
	this->BaseMaterialTranslucent = ConstructorStatics.BaseMaterialTranslucent.Object;
}

FDateRange UITwinSynchro4DSchedules::GetDateRange() const
{
	if (Impl->Internals.ScheduleTimeRangeIsKnown && *Impl->Internals.ScheduleTimeRangeIsKnown)
		return Impl->Internals.GetTimeline().GetDateRange();
	else
		return FDateRange();
}

void UITwinSynchro4DSchedules::TickSchedules(float DeltaTime)
{
	static const TCHAR* ErrPrefix = TEXT("Unknown:");
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return; // fine, happens between constructor and registration to parent iModel

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
	else if (Impl->Internals.PrefetchAllElementAnimationBindings()
		&& FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone != Impl->Internals.ApplySchedule)
	{
		if (IsAvailable())
		{
			auto const& Timelines = Impl->Internals.Builder.Timeline().GetContainer();
			auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(GetOwner())).SceneMapping;
			for (auto& [TileID, SceneTile] : SceneMapping.KnownTiles)
				for (auto& ElementTimeline : Timelines)
					SceneMapping.OnElementsTimelineModified(TileID, SceneTile, *ElementTimeline);
			Impl->Internals.ApplySchedule = FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone;
			Impl->Animator.TickAnimation(DeltaTime, true);
		}
		else
		{
			Impl->Internals.SchedulesApi.HandlePendingQueries();
			// For selection textures: not needed, UpdateSelectionAndHighlightTextures called from iModel tick
			//GetInternals(*Cast<AITwinIModel>(GetOwner())).SceneMapping.UpdateAllTextures();
		}
	}
	else
	{
		bool bNewTilesReceived = false;
		Impl->Internals.HandleReceivedElements(bNewTilesReceived);
		Impl->Internals.SchedulesApi.HandlePendingQueries();
		Impl->Animator.TickAnimation(DeltaTime, bNewTilesReceived);
	}
}

void FITwinSynchro4DSchedulesInternals::UpdateConnection(bool const bOnlyIfReady)
{
	if (!bOnlyIfReady || IsReadyToQuery())
	{
		AITwinIModel& IModel = *Cast<AITwinIModel>(Owner.GetOwner());
		SchedulesApi.ResetConnection(IModel.ITwinId, IModel.IModelId, IModel.ChangesetId);
	}
}

bool UITwinSynchro4DSchedules::IsAvailable() const
{
	return Impl->Internals.SchedulesApi.HasFullSchedule();
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

void UITwinSynchro4DSchedules::LogStatisticsUponQueryLoopStatusChange(bool bQueryLoopIsRunning)
{
	if (bQueryLoopIsRunning)
	{
		UE_LOG(LogITwinSched, Display, TEXT("Query loop (re)started..."));
	}
	else
	{
		UE_LOG(LogITwinSched, Display, TEXT("Query loop now idling. %s"),
										*Impl->Internals.SchedulesApi.ToString());
	}
}

void UITwinSynchro4DSchedules::LogStatisticsUponFullScheduleReceived(FDateTime StartTime, FDateTime EndTime)
{
	UE_LOG(LogITwinSched, Display, TEXT("Schedule tasks received: %llu between %s and %s"),
		Impl->Internals.SchedulesApi.NumTasks(), *StartTime.ToString(), *EndTime.ToString());
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

	Reset();
	Builder.SetOnElementsTimelineModified(std::bind(
		&FITwinIModelInternals::OnElementsTimelineModified, &IModelInternals,
															std::placeholders::_1, std::placeholders::_2));
	UpdateConnection(false);

	// If the tileset is already loaded, we need to trigger QueryElementsTasks for all Elements for which we
	// have already received some mesh parts, but also for all their parents/ancesters, which may have anim
	// bindings that will also animate the children.
	auto const& AllElems = IModelInternals.SceneMapping.GetElements();
	if (!PrefetchAllElementAnimationBindings() && !AllElems.empty())
	{
		std::set<ITwinElementID> ElementIDs;
		for (auto const& Elem : AllElems)
		{
			if (Elem.bHasMesh) // start from leaves (can intermediate nodes have their own geom too?)
			{
				FITwinElement const* pElem = &Elem;
				while (true)
				{
					if (!ElementIDs.insert(pElem->Id).second)
						break; // if already present, all its parents are, too
					if (ITwinScene::NOT_ELEM == pElem->ParentInVec)
						break;
					pElem = &IModelInternals.SceneMapping.GetElement(pElem->ParentInVec);
				}
			}
		}
		SchedulesApi.QueryElementsTasks(ElementIDs);
	}
	return true;
}

void UITwinSynchro4DSchedules::QueryAll()
{
	if (!Impl->Internals.IsReadyToQuery()) return;
	Impl->Internals.SchedulesApi.QueryEntireSchedules(QueryAllFromTime, QueryAllUntilTime,
		DebugDumpAsJsonAfterQueryAll.IsEmpty() ? std::function<void(bool)>() :
		[this, Dest=DebugDumpAsJsonAfterQueryAll] (bool bSuccess)
		{
			if (!bSuccess) return;
			FString TimelineAsJson = GetInternals(*this).GetTimeline().ToPrettyJsonString();
			IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
			FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			Path.Append(Dest);
			if (!Dest.EndsWith(".json"))
				Path.Append(".json");
			if (FileManager.FileExists(*Path))
				FileManager.DeleteFile(*Path);
			FFileHelper::SaveStringToFile(TimelineAsJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
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
}

void UITwinSynchro4DSchedules::Pause()
{
	Impl->Animator.Pause();
}

void UITwinSynchro4DSchedules::Stop()
{
	Impl->Animator.Stop();
}

void UITwinSynchro4DSchedules::JumpToBeginning()
{
	auto const DateRange = GetDateRange();
	if (DateRange != FDateRange())
	{
		ScheduleTime = DateRange.GetLowerBoundValue();
		Impl->Animator.OnChangedScheduleTime(false);
	}
}

void UITwinSynchro4DSchedules::JumpToEnd()
{
	auto const DateRange = GetDateRange();
	if (DateRange != FDateRange())
	{
		ScheduleTime = DateRange.GetUpperBoundValue();
		Impl->Animator.OnChangedScheduleTime(false);
	}
}

void UITwinSynchro4DSchedules::AutoReplaySpeed()
{
	auto const& TimeRange = Impl->Internals.GetTimeline().GetTimeRange();
	if (TimeRange.first < TimeRange.second)
	{
		SetReplaySpeed(FTimespan::FromHours( // round the number of hours per second
			std::ceil((TimeRange.second - TimeRange.first) / (3600. * AUTO_SCRIPT_DURATION))));
	}
}

FDateTime UITwinSynchro4DSchedules::GetScheduleTime()
{
	return ScheduleTime;
}

void UITwinSynchro4DSchedules::SetScheduleTime(FDateTime NewScheduleTime)
{
	//if (ScheduleTime != NewScheduleTime) <== don't: see PostEditChangeProperty
	ScheduleTime = NewScheduleTime;
	Impl->Animator.OnChangedScheduleTime(false);
}

FTimespan UITwinSynchro4DSchedules::GetReplaySpeed()
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
			IModel->ServerConnection->Environment, IModel->ITwinId, IModel->IModelId, IModel->ChangesetId,
			ScheduleId);
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

void UITwinSynchro4DSchedules::ToggleMaskTilesUntilFullyAnimated()
{
	ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated = !ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated;
	bMaskTilesUntilFullyAnimated = ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated;
}

void UITwinSynchro4DSchedules::OnIModelEndPlay()
{
	Impl->Internals.SchedulesApi.UninitializeCache();
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
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableCuttingPlanes)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableTransforms)
	) {
		Impl->Animator.OnChangedScheduleRenderSetting();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bFadeOutNonAnimatedElements))
	{
		Impl->Animator.OnFadeOutNonAnimatedElements();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bMaskOutNonAnimatedElements))
	{
		Impl->Animator.OnMaskOutNonAnimatedElements();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, DebugRecordSessionQueries)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, DebugSimulateSessionQueries)
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bDisableCaching))
	{
		ResetSchedules();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, bMaskTilesUntilFullyAnimated))
	{
		ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated = bMaskTilesUntilFullyAnimated;
	}
}
#endif // WITH_EDITOR
