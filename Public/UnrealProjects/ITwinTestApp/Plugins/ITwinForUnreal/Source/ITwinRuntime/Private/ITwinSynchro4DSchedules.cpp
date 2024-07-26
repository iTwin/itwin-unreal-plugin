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
#include <Timeline/AnchorPoint.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/SchedulesStructs.h>

#include <HAL/PlatformFileManager.h>
#include <Materials/MaterialInterface.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <mutex>
#include <unordered_set>

constexpr double AUTO_SCRIPT_DURATION = 30.;

class UITwinSynchro4DSchedules::FImpl
{
	friend class UITwinSynchro4DSchedules;
	friend FITwinSynchro4DSchedulesInternals& GetInternals(UITwinSynchro4DSchedules&);
	friend FITwinSynchro4DSchedulesInternals const& GetInternals(UITwinSynchro4DSchedules const&);

	UITwinSynchro4DSchedules& Owner;
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

static std::optional<FTransform> const& GetCesiumToUnrealTransform(UITwinSynchro4DSchedules& Owner)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (/*ensure*/(IModel)) // the CDO is in that case...
	{
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		return IModelInternals.SceneMapping.GetCesiumToUnrealTransform();
	}
	else
	{
		static std::optional<FTransform> Dummy; // left uninit, will error out later anyway
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
	std::vector<FITwinSchedule>& InSchedules)
:
	Owner(InOwner), bDoNotBuildTimelines(InDoNotBuildTimelines)
	, Builder(InOwner, GetCesiumToUnrealTransform(InOwner))
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

FString FITwinSynchro4DSchedulesInternals::ElementTimelineAsString(ITwinElementID const ElementID) const
{
	auto const& MainTimeline = GetTimeline();
	FString Result;
	MainTimeline.ForEachElementTimeline(ElementID, [&Result](FITwinElementTimeline const& Timeline)
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

/// Most of the processing is delayed until the beginnng of the next tick, in hope a given tile would be fully
/// loaded before calling OnElementsTimelineModified, to avoid resizing property textures. But it might not
/// be sufficient if 1/ meshes of a same tile are loaded by different ticks (which DOES happen, UNLESS it's
/// only an effect of our GltfTuner?!) - and 2/ new FeatureIDs are discovered in non-first ticks...
void FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt(CesiumTileID const& TileId,
														   std::set<ITwinElementID>&& MeshElementIDs)
{
	if (MeshElementIDs.empty())
		return;
	// MeshElementIDs is actually moved only in case of insertion, otherwise it is untouched
	auto Entry = ElementsReceived.try_emplace(TileId, std::move(MeshElementIDs));
	if (!Entry.second) // was not inserted, merge with existing set:
	{
		for (auto&& Elem : MeshElementIDs)
			Entry.first->second.insert(Elem);
	}
}

void FITwinSynchro4DSchedulesInternals::HandleReceivedElements(bool& bNewTilesReceived)
{
	if (!MakeReady())
		return;

	// In theory, OnElementsTimelineModified must be called for each timeline applying to an Element (or a
	// group containing an Element) that has been received, with the exact set of Elements received, because
	// the code depends on the kind of keyframes present, and flags are set on ElementFeatureInTile
	// individually. But finding the timelines and determining the affected sets of Elements for each is far
	// from obvious (std::set_intersection was attempted but too slow), so let's just replicate the setup made
	// in other tiles for the same Element, it's very likely that's what we'd end up with anyway.
	// An alternative could be to map each Element to all its timelines, if it's worth it...? I'm not even sure
	// we'd not be better off creating all property textures for all tiles indiscriminately...
	auto& SceneMapping = GetInternals(*Cast<AITwinIModel>(Owner.GetOwner())).SceneMapping;
	for (auto const& TileMeshElements : ElementsReceived)
	{
		SceneMapping.ReplicateKnownElementsSetupInTile(TileMeshElements);
	}
	if (!ElementsReceived.empty())
	{
		// ElementIDs are already mapped in the SchedulesApi structures to avoid redundant requests, so it
		// seems redundant to merge the sets here:
		//if (ElementsReceived.size() == 1)
		//	SchedulesApi.QueryElementsTasks(ElementsReceived.begin()->second);//empties the set
		//else
		//{
		//	std::set<ITwinElementID> MergedSet;
		//	auto SetsIt = ElementsReceived.begin();
		//	MergedSet.swap(SetsIt->second);
		//	for (++SetsIt; ; ++SetsIt)
		//	{
		//		for (auto&& Elem : SetsIt->second)
		//		{
		//			MergedSet.insert(Elem);
		//		}
		//		SetsIt->second.clear();
		//	}
		//	ElementsReceived.clear();
		//	SchedulesApi.QueryElementsTasks(MergedSet);
		//}
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
			for (auto&& TileElements : ElementsReceived)
				SchedulesApi.QueryElementsTasks(TileElements.second);//empties the set
		}
		ElementsReceived.clear();
	}
}

/*static*/
void FITwinSynchro4DSchedulesInternals::GetAnimatableMaterials(UMaterialInterface*& OpaqueMat,
	UMaterialInterface*& TranslucentMat, UObject& MaterialOwner)
{
	static UMaterialInterface* BaseMaterialMasked = nullptr;
	static UMaterialInterface* BaseMaterialTranslucent = nullptr;
	if (!BaseMaterialMasked)
	{
		BaseMaterialMasked = Cast<UMaterialInterface>(StaticLoadObject(
			UMaterialInterface::StaticClass(), &MaterialOwner,
			TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstance")));
		BaseMaterialTranslucent = Cast<UMaterialInterface>(StaticLoadObject(
			UMaterialInterface::StaticClass(), &MaterialOwner,
			TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent")));
	}
	if (BaseMaterialMasked && BaseMaterialTranslucent)
	{
		OpaqueMat = BaseMaterialMasked;
		TranslucentMat = BaseMaterialTranslucent;
	}
	else
	{
		OpaqueMat = nullptr;
		TranslucentMat = nullptr;
		check(false);
	}
}

UMaterialInterface* FITwinSynchro4DSchedulesInternals::GetMasterMaterial(
	ECesiumMaterialType Type, UObject& MaterialOwner)
{
	UMaterialInterface* OpaqueMat, * TranslucentMat;
	GetAnimatableMaterials(OpaqueMat, TranslucentMat, MaterialOwner);
	switch (Type)
	{
	case ECesiumMaterialType::Opaque:
		return OpaqueMat;
	case ECesiumMaterialType::Translucent:
		return TranslucentMat;
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
	FVector const Position =
		(ITwin::Timeline::EGrowthStatus::DeferredFullyGrown == Deferred.GrowthStatus)
			? FVector((Deferred.PlaneOrientation.X > 0) ? ExpandedBox.Max.X : ExpandedBox.Min.X,
					  (Deferred.PlaneOrientation.Y > 0) ? ExpandedBox.Max.Y : ExpandedBox.Min.Y,
					  (Deferred.PlaneOrientation.Z > 0) ? ExpandedBox.Max.Z : ExpandedBox.Min.Z)
			: FVector((Deferred.PlaneOrientation.X > 0) ? ExpandedBox.Min.X : ExpandedBox.Max.X,
					  (Deferred.PlaneOrientation.Y > 0) ? ExpandedBox.Min.Y : ExpandedBox.Max.Y,
					  (Deferred.PlaneOrientation.Z > 0) ? ExpandedBox.Min.Z : ExpandedBox.Max.Z);
	// Note: TVector(const UE::Math::TVector4<T>& V); is NOT explicit, which is a shame IMHO
	Deferred.PlaneW = FVector3f(Position).Dot(Deferred.PlaneOrientation);
	if (ITwin::Timeline::EGrowthStatus::DeferredFullyGrown == Deferred.GrowthStatus)
		Deferred.GrowthStatus = ITwin::Timeline::EGrowthStatus::FullyGrown;
	else
		Deferred.GrowthStatus = ITwin::Timeline::EGrowthStatus::FullyRemoved;
}

/*static*/
void FITwinSynchro4DSchedulesInternals::FinalizeAnchorPos(
	ITwin::Timeline::FDeferredAnchorPos const& Deferred, FBox const& ElementsWorldBox)
{
	FVector Center, Extents;
	ElementsWorldBox.GetCenterAndExtents(Center, Extents);
	switch (Deferred.AnchorPoint)
	{
	// What to do with Original? Used in Stadium-RN-QA - TODO_GCO: ask about it or test in SynchroPro
	case ITwin::Timeline::EAnchorPoint::Original:
	case ITwin::Timeline::EAnchorPoint::Center:
		Deferred.Pos = Center;
		break;
	case ITwin::Timeline::EAnchorPoint::Bottom:
		Deferred.Pos = Center - Extents.Z; // yes, Extents is half (Max - Min)
		break;
	case ITwin::Timeline::EAnchorPoint::Top:
		Deferred.Pos = Center + Extents.Z;
		break;
	case ITwin::Timeline::EAnchorPoint::Left:
		Deferred.Pos = Center - Extents.X;
		break;
	case ITwin::Timeline::EAnchorPoint::Right:
		Deferred.Pos = Center + Extents.X;
		break;
	case ITwin::Timeline::EAnchorPoint::Front:
		Deferred.Pos = Center - Extents.Y;
		break;
	case ITwin::Timeline::EAnchorPoint::Back:
		Deferred.Pos = Center + Extents.Y;
		break;
	case ITwin::Timeline::EAnchorPoint::Custom:
	default:
		ensure(false); // 'Custom' is not deferred, the rest is invalid
		break;
	}
	Deferred.AnchorPoint = ITwin::Timeline::EAnchorPoint::Custom;
}

bool FITwinSynchro4DSchedulesInternals::IsReady() const
{
	return SchedulesApi.IsReady(); // other members need no particular init
}

bool FITwinSynchro4DSchedulesInternals::MakeReady()
{
	if (!IsReady())
	{
		return ResetSchedules();
	}
	return true;
}

void FITwinSynchro4DSchedulesInternals::Reset()
{
	Schedules.clear();
	// see comment below about ordering:
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedules);
	Builder = FITwinScheduleTimelineBuilder(Owner, GetCesiumToUnrealTransform(Owner));
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
}

FITwinSchedulesImport& FITwinSynchro4DSchedulesInternals::GetSchedulesApiReadyForUnitTesting()
{
	ensure(MakeReady());
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
	PrimaryComponentTick.bCanEverTick = true;
}

void UITwinSynchro4DSchedules::OnRegister()
{
	Super::OnRegister();
	SetComponentTickEnabled(true);
}

void UITwinSynchro4DSchedules::OnUnregister()
{
	SetComponentTickEnabled(false);
	Super::OnUnregister();
}

void UITwinSynchro4DSchedules::TickComponent(float DeltaTime, enum ELevelTick /*TickType*/,
											 FActorComponentTickFunction* /*ThisTickFunction*/)
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	static const TCHAR* ErrPrefix = TEXT("Unknown:");
	if (!IModel->ServerConnection // happens when an iModel is created from scratch by the user
		|| IModel->ITwinId.IsEmpty()) // happens transitorily in iTwinTestApp...
	{
		if (ScheduleId.IsEmpty() || ScheduleId.StartsWith(ErrPrefix))
		{
			ScheduleId = ErrPrefix;
			if (!IModel->ServerConnection)
				ScheduleId += TEXT("NoServerConnection!");
			if (!IModel->ServerConnection)
				ScheduleId += TEXT("NoITwinId!");
		}
		return;
	}
	if (!Impl->Schedules.empty() && (ScheduleId.IsEmpty() || ScheduleId.StartsWith(ErrPrefix)))
	{
		ScheduleId = Impl->Schedules[0].Id;
		ScheduleName = Impl->Schedules[0].Name;
	}
	bool bNewTilesReceived = false;
	Impl->Internals.HandleReceivedElements(bNewTilesReceived);
	Impl->Internals.SchedulesApi.HandlePendingQueries();
	Impl->Animator.TickAnimation(DeltaTime, bNewTilesReceived);
}

void FITwinSynchro4DSchedulesInternals::UpdateConnection(bool const bOnlyIfReady)
{
	if (!bOnlyIfReady || IsReady())
	{
		AITwinIModel& IModel = *Cast<AITwinIModel>(Owner.GetOwner());
		SchedulesApi.ResetConnection(IModel.ServerConnection, IModel.ITwinId, IModel.IModelId);
	}
}

void UITwinSynchro4DSchedules::UpdateConnection()
{
	Impl->Internals.UpdateConnection(true);
}

void UITwinSynchro4DSchedules::ResetSchedules()
{
	Impl->Internals.ResetSchedules();
}

// Note: must have been called at least once before any actual querying.
// Use Impl->Internals.MakeReady() for that.
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
					std::placeholders::_1, std::ref(*IModel)));

	Reset();
	Builder.SetOnElementsTimelineModified(std::bind(
		&FITwinIModelInternals::OnElementsTimelineModified, &IModelInternals,
															std::placeholders::_1, std::placeholders::_2));
	UpdateConnection(false);
	return true;
}

void UITwinSynchro4DSchedules::QueryAll()
{
	if (!Impl->Internals.MakeReady()) return;
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
			FFileHelper::SaveStringToFile(TimelineAsJson, *Path);
		});
}

void UITwinSynchro4DSchedules::QueryAroundElementTasks(FString const ElementID,
	FTimespan const MarginFromStart, FTimespan const MarginFromEnd)
{
	if (!ensure(Impl->Internals.MakeReady())) return;
	Impl->Internals.SchedulesApi.QueryAroundElementTasks(ITwin::ParseElementID(ElementID),
														 MarginFromStart, MarginFromEnd);
}

void UITwinSynchro4DSchedules::QueryElementsTasks(TArray<FString> const& Elements)
{
	if (!ensure(Impl->Internals.MakeReady())) return;
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
	auto&& ScheduleStart = Impl->Internals.GetTimeline().GetDateRange().GetLowerBound();
	if (ScheduleStart.IsClosed() && ScheduleTime != ScheduleStart.GetValue())
	{
		ScheduleTime = ScheduleStart.GetValue();
		Impl->Animator.OnChangedScheduleTime(false);
	}
}

void UITwinSynchro4DSchedules::JumpToEnd()
{
	auto&& ScheduleEnd = Impl->Internals.GetTimeline().GetDateRange().GetUpperBound();
	if (ScheduleEnd.IsClosed() && ScheduleTime != ScheduleEnd.GetValue())
	{
		ScheduleTime = ScheduleEnd.GetValue();
		Impl->Animator.OnChangedScheduleTime(false);
	}
}

void UITwinSynchro4DSchedules::AutoReplaySpeed()
{
	auto const& TimeRange = Impl->Internals.GetTimeline().GetTimeRange();
	if (TimeRange.first < TimeRange.second)
	{
		ReplaySpeed = REPLAYSPEED_FACTOR_SECONDS_TO_UI
			* std::ceil((TimeRange.second - TimeRange.first) / AUTO_SCRIPT_DURATION);
		Impl->Animator.OnChangedAnimationSpeed();
	}
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
		Impl->Animator.OnChangedScheduleTime(false);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, ReplaySpeed))
	{
		Impl->Animator.OnChangedAnimationSpeed();
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
		  || Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, DebugSimulateSessionQueries))
	{
		ResetSchedules();
	}
}
#endif // WITH_EDITOR
