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
	const bool bDoNotBuildTimelines; ///< defaults to false, true only for internal unit testing
	std::recursive_mutex Mutex;
	std::vector<FITwinSchedule> Schedules;
	FITwinSynchro4DAnimator Animator;
	FITwinSynchro4DSchedulesInternals Internals; // <== must be declared LAST

	void UpdateConnection(bool const bOnlyIfReady);

public: // for TPimplPtr
	FImpl(UITwinSynchro4DSchedules& InOwner, bool InDoNotBuildTimelines)
		: Owner(InOwner), bDoNotBuildTimelines(InDoNotBuildTimelines), Animator(InOwner)
		, Internals(InOwner, Mutex, Schedules)
	{
	}
};

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
		std::recursive_mutex& InMutex, std::vector<FITwinSchedule>& InSchedules)
	: Owner(InOwner), Builder(InOwner), SchedulesApi(InOwner, InMutex, InSchedules)
	, Mutex(InMutex), Schedules(InSchedules)
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

FITwinElementTimeline const& FITwinSynchro4DSchedulesInternals::GetElementTimeline(
	ITwinElementID const ElementID) const
{
	static FITwinElementTimeline NotFound(ITwin::NOT_ELEMENT);
	auto const& MainTimeline = GetTimeline();
	int const Index = MainTimeline.GetElementTimelineIndex(ElementID);
	if (-1 != Index)
	{
		return MainTimeline.GetElementTimelineByIndex(Index);
	}
	return NotFound;
}

FString FITwinSynchro4DSchedulesInternals::ElementTimelineAsString(ITwinElementID const ElementID) const
{
	auto const& MainTimeline = GetTimeline();
	int const Index = MainTimeline.GetElementTimelineIndex(ElementID);
	if (-1 != Index)
	{
		return MainTimeline.GetElementTimelineByIndex(Index).ToPrettyJsonString();
	}
	else return FString();
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
	ITwin::Timeline::FDeferredPlaneEquation const& Deferred,
	FBox const& ElementWorldBox)
{
	check(Deferred.planeEquation_.IsUnit3());
	FBox const ExpandedBox = ElementWorldBox.ExpandBy(0.01 * ElementWorldBox.GetSize());
	FVector const Position =
		(ITwin::Timeline::EGrowthBoundary::FullyGrown == Deferred.growthBoundary_)
			? FVector((Deferred.planeEquation_.X > 0) ? ExpandedBox.Max.X : ExpandedBox.Min.X,
					  (Deferred.planeEquation_.Y > 0) ? ExpandedBox.Max.Y : ExpandedBox.Min.Y,
					  (Deferred.planeEquation_.Z > 0) ? ExpandedBox.Max.Z : ExpandedBox.Min.Z)
			: FVector((Deferred.planeEquation_.X > 0) ? ExpandedBox.Min.X : ExpandedBox.Max.X,
					  (Deferred.planeEquation_.Y > 0) ? ExpandedBox.Min.Y : ExpandedBox.Max.Y,
					  (Deferred.planeEquation_.Z > 0) ? ExpandedBox.Min.Z : ExpandedBox.Max.Z);
	// Note: TVector(const UE::Math::TVector4<T>& V); is NOT explicit, which is a shame IMHO
	Deferred.planeEquation_.W = Position.Dot(FVector(Deferred.planeEquation_));
}

bool FITwinSynchro4DSchedulesInternals::IsReady() const
{
	return SchedulesApi.IsReady(); // other members need no particular init
}

bool FITwinSynchro4DSchedulesInternals::MakeReady()
{
	if (!IsReady())
	{
		return Owner.Reset();
	}
	return true;
}

void FITwinSynchro4DSchedulesInternals::Reset(bool bDoNotBuildTimelines)
{
	Schedules.clear();
	// see comment below about ordering:
	SchedulesApi = FITwinSchedulesImport(Owner, Mutex, Schedules);
	Builder = FITwinScheduleTimelineBuilder(Owner);
	if (!bDoNotBuildTimelines)
	{
		SchedulesApi.SetOnAnimationBindingAdded(
			// getting Builder's pointer here should be safe, because SchedulesApi is deleted /before/
			// Builder, (both above and in the destructor, as per the members' declaration order), which
			// will ensure no more request callbacks and thus no more calls to this subsequent callback:
			std::bind(&FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline, &Builder,
					  std::placeholders::_1, std::placeholders::_2));
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
	Impl->Internals.SchedulesApi.HandlePendingQueries();
	Impl->Animator.TickAnimation(DeltaTime);
}

void UITwinSynchro4DSchedules::FImpl::UpdateConnection(bool const bOnlyIfReady)
{
	if (!bOnlyIfReady || Internals.IsReady())
	{
		AITwinIModel& IModel = *Cast<AITwinIModel>(Owner.GetOwner());
		Internals.SchedulesApi.ResetConnection(IModel.ServerConnection, IModel.ITwinId, IModel.IModelId);
	}
}

void UITwinSynchro4DSchedules::UpdateConnection()
{
	Impl->UpdateConnection(true);
}

// Note: must have been called at least once before any actual querying.
// Use Impl->Internals.MakeReady() for that.
bool UITwinSynchro4DSchedules::Reset()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(GetOwner());
	if (!IModel)
		return false;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);

	IModelInternals.SceneMapping.SetTimelineGetter(
		std::bind(&FITwinSynchro4DSchedulesInternals::GetTimeline, &Impl->Internals));

	IModelInternals.SceneMapping.SetMaterialGetter(
		std::bind(&FITwinSynchro4DSchedulesInternals::GetMasterMaterial, &Impl->Internals,
					std::placeholders::_1, std::ref(*IModel)));

	Impl->Internals.Reset(Impl->bDoNotBuildTimelines);
	Impl->Internals.Builder.SetOnElementTimelineModified(std::bind(
		&FITwinIModelInternals::OnElementTimelineModified, &IModelInternals, std::placeholders::_1));

	Impl->UpdateConnection(false);
	return true;
}

void UITwinSynchro4DSchedules::QueryAll()
{
	if (!ensure(Impl->Internals.MakeReady())) return;
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
	std::vector<ITwinElementID> ElementIDs(Elements.Num());
	auto ElementIDsIt = ElementIDs.begin();
	size_t Inval = 0;
	for (auto&& Elem : Elements)
	{
		auto const Id = ITwin::ParseElementID(Elem);
		if (ITwin::NOT_ELEMENT != Id)
		{
			*ElementIDsIt = Id;
			++ElementIDsIt;
		}
		else ++Inval;
	}
	if (Inval > 0)
		ElementIDs.resize(ElementIDs.size() - Inval);
	Impl->Internals.SchedulesApi.QueryElementsTasks(std::move(ElementIDs));
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

void UITwinSynchro4DSchedules::ResetAnimationTime()
{
	auto&& ScheduleStart = Impl->Internals.GetTimeline().GetDateRange().GetLowerBound();
	if (ScheduleStart.IsClosed() && AnimationTime != ScheduleStart.GetValue())
	{
		AnimationTime = ScheduleStart.GetValue();
		Impl->Animator.OnChangedAnimationTime();
	}
}

void UITwinSynchro4DSchedules::AutoAnimationSpeed()
{
	auto const& TimeRange = Impl->Internals.GetTimeline().GetTimeRange();
	if (TimeRange.first < TimeRange.second)
	{
		AnimationSpeed = std::ceil((TimeRange.second - TimeRange.first) / AUTO_SCRIPT_DURATION);
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
	if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, AnimationTime))
	{
		Impl->Animator.OnChangedAnimationTime();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UITwinSynchro4DSchedules, AnimationSpeed))
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
}
#endif // WITH_EDITOR
