/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedules.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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
		, Internals(InOwner, InDoNotBuildTimelines, Mutex, Schedules, Animator)
	{
	}
};

static FITwinCoordConversions const& GetIModel2UnrealCoordConv(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // the CDO is in that case...
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

/// Most of the handling is delayed until the beginning of the next tick, in hope a given tile would be fully
/// loaded before calling OnElementsTimelineModified, to avoid resizing property textures. But it might not
/// be sufficient if 1/ meshes of a same tile are loaded by different ticks (which DOES happen, UNLESS it's
/// only an effect of our GltfTuner?!) - and 2/ new FeatureIDs are discovered in non-first ticks...
void FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt(ITwinScene::TileIdx const& TileRank,
	std::unordered_set<ITwinScene::ElemIdx>&& MeshElements,
	const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial, FITwinSceneTile& SceneTile)
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

bool FITwinSynchro4DSchedulesInternals::PrefetchAllElementAnimationBindings() const
{
	return Owner.bPrefetchAllElementAnimationBindings
		&& !Owner.bDebugWithDummyTimelines;
}

bool FITwinSynchro4DSchedulesInternals::IsPrefetchedAvailableAndApplied() const
{
	return PrefetchAllElementAnimationBindings() && EApplySchedule::InitialPassDone == ApplySchedule;
}

bool FITwinSynchro4DSchedulesInternals::OnNewTileBuilt(FITwinSceneTile& SceneTile)
{
	if (IsPrefetchedAvailableAndApplied())
	{
		SceneTile.pCesiumTile->setRenderEngineReadiness(false);
		SetupAndApply4DAnimationSingleTile(SceneTile);
		return true;
	}
	return false;
}

void FITwinSynchro4DSchedulesInternals::HideNonAnimatedDuplicates(FITwinSceneTile& SceneTile,
																  FElementsGroup const& NonAnimatedDuplicates)
{
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
	if (!SceneTile.bIsSetupFor4DAnimation)
	{
		Setup4DAnimationSingleTile(SceneTile, {}, nullptr);
	}
	Animator.ApplyAnimationOnTile(SceneTile);
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
		if (!ensure(ElementsReceived.end() != Pending))
			return;
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
			/* , &TileMeshElements.second*/);
	}
	if (SceneTile.HighlightsAndOpacities)
		HideNonAnimatedDuplicates(SceneTile, MainTimeline.GetNonAnimatedDuplicates());
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
	// With PrefetchAllElementAnimationBindings, the situation is reversed: we have all bindings (once
	// IsAvailable() returns true), so OnElementsTimelineModified needs to be called on all Elements, because
	// no new query will be made.
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;

	// Note: only used by initial pass now
	if (PrefetchAllElementAnimationBindings() && ensure(Owner.IsAvailable()))
	{
		for (auto const& [TileRank, TileElems] : ElementsReceived)
		{
			auto& SceneTile = SceneMapping.KnownTile(TileRank);
			// may have been unloaded while waiting for ElementsReceived to be processed, typically while
			// waiting for the schedule to be fully available!
			if (SceneTile.IsLoaded())
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
	Schedules.clear();
	// see comment below about ordering:
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedules);
	Builder.Uninitialize();
	Builder = FITwinScheduleTimelineBuilder(Owner, GetIModel2UnrealCoordConv(Owner));
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

void FITwinSynchro4DSchedulesInternals::UpdateConnection(bool const bOnlyIfReady)
{
	if (!bOnlyIfReady || IsReadyToQuery())
	{
		AITwinIModel& IModel = *Cast<AITwinIModel>(Owner.GetOwner());
		// See comment about empty changeset in QueriesCache::GetCacheFolder():
		ensureMsgf(!IModel.GetSelectedChangeset().IsEmpty() || IModel.ChangesetId.IsEmpty(),
				   TEXT("Selected changeset shouldn't be empty!"));
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

	if (PrefetchAllElementAnimationBindings())
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
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialGlass;
		FConstructorStatics()
			: BaseMaterialMasked(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstance"))
			, BaseMaterialTranslucent(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent"))
			, BaseMaterialGlass(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinGlass"))
		{}
	};
	static FConstructorStatics ConstructorStatics;
	this->BaseMaterialMasked = ConstructorStatics.BaseMaterialMasked.Object;
	this->BaseMaterialTranslucent = ConstructorStatics.BaseMaterialTranslucent.Object;
	this->BaseMaterialGlass = ConstructorStatics.BaseMaterialGlass.Object;
}

UITwinSynchro4DSchedules::~UITwinSynchro4DSchedules()
{
	if (Impl->Internals.Uniniter) // CDO has none
		Impl->Internals.Uniniter->Run();
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
	else if (Impl->Internals.PrefetchAllElementAnimationBindings()
		&& FITwinSynchro4DSchedulesInternals::EApplySchedule::InitialPassDone != Impl->Internals.ApplySchedule)
	{
		if (IsAvailable())
		{
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
		if (!Impl->Internals.PrefetchAllElementAnimationBindings())
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

FDateTime UITwinSynchro4DSchedules::GetScheduleTime() const
{
	return ScheduleTime;
}

void UITwinSynchro4DSchedules::SetScheduleTime(FDateTime NewScheduleTime)
{
	//if (ScheduleTime != NewScheduleTime) <== don't: see PostEditChangeProperty
	ScheduleTime = NewScheduleTime;
	Impl->Animator.OnChangedScheduleTime(false);
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
}
#endif // WITH_EDITOR
