/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSavedView.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <HttpModule.h>
#include <Kismet/KismetMathLibrary.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include "Camera/CameraComponent.h"
#include <Kismet/GameplayStatics.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <Camera/CameraActor.h>
#include <ImageUtils.h>
#include <Misc/Base64.h>
#include <Misc/FileHelper.h>
#include <UObject/UObjectIterator.h>
#include <TimerManager.h>
#include <UObject/StrongObjectPtr.h>

#if WITH_EDITOR
	#include "Editor.h"
	#include "LevelEditorViewport.h"
#endif // WITH_EDITOR

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwinSavedView
{
	FTimerHandle TimerHandle;
}

class AITwinSavedView::FImpl
{
public:
	// Some operations require to first fetch the saved view data: in such case, we postpone the actual
	// operation until the authorization and SavedView data is retrieved.
	enum class EPendingOperation : uint8
	{
		None,
		Move,
		ApplyVisibility,
		Rename
	};
	AITwinSavedView& Owner;
	FSavedView SavedViewData;
	bool bSavedViewTransformIsSet = false;
	EPendingOperation PendingOperation = EPendingOperation::None;

	FImpl(AITwinSavedView& InOwner)
		: Owner(InOwner)
	{
	}

	AITwinIModel* ResolveSavedViewIModel()
	{
		// Saved views created programmatically are owned by an AITwinIModel actor, but those created manually aren't.
		// => In that case, are they attached to the iModel actor instead? Only when dragging it manually under the
		// iModel actor in the Outliner maybe?
		if (AITwinIModel* const AttachedIModel = Cast<AITwinIModel>(Owner.GetAttachParentActor()))
			return AttachedIModel;
		return Cast<AITwinIModel>(Owner.GetOwner());
	}

	void DestroyChildren()
	{
		const auto ChildrenCopy = Owner.Children;
		for (auto& Child : ChildrenCopy)
			if (Child)
				Owner.GetWorld()->DestroyActor(Child);
		Owner.Children.Empty();
	}

	void ApplyScheduleTime()
	{
		AITwinIModel* OwnerIModel = ResolveSavedViewIModel();
		if (OwnerIModel && OwnerIModel->Synchro4DSchedules
			&& !SavedViewData.DisplayStyle.RenderTimeline.IsEmpty())
		{
			OwnerIModel->Synchro4DSchedules->SetScheduleTime(
				FDateTime::FromUnixTimestamp(SavedViewData.DisplayStyle.TimePoint));
			// If playing, it makes sense to pause replay when moving to saved view.
			// If not, setting the schedule time will have no effect! In that case, "Pause()" actually
			// has the effect of redisplaying the schedule, without changing the ScheduleTime
			OwnerIModel->Synchro4DSchedules->Pause();
		}
	}

	void StartCameraMovementToSavedView(float& OutBlendTime, ACameraActor*& Actor, const FTransform& Transform,
										float BlendTime)
	{
		OutBlendTime = BlendTime;
		TObjectIterator<APlayerController> Itr;
		if (!ensure(Itr))
			return;
		APlayerController* PlayerController = *Itr;
		if (!ensure(PlayerController))
			return;
		Actor = Itr->GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Transform);
		Actor->GetCameraComponent()->SetConstraintAspectRatio(false);
		PlayerController->SetViewTargetWithBlend(Actor, BlendTime, VTBlend_Linear, 0, true);
	}

	void EndCameraMovement(ACameraActor* Actor, const FTransform& Transform)
	{
		Actor->Destroy();
		TObjectIterator<APlayerController> Itr;
		if (!ensure(Itr))
			return;
		APlayerController* PlayerController = *Itr;
		if (!ensure(PlayerController))
			return;
		PlayerController->GetPawnOrSpectator()->SetActorLocation(Transform.GetLocation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		FRotator Rot = Transform.Rotator();
		PlayerController->SetControlRotation(Rot);
		PlayerController->GetPawnOrSpectator()->SetActorRotation(Rot);
		PlayerController->SetViewTargetWithBlend(PlayerController->GetPawnOrSpectator());
	}
};

AITwinSavedView::AITwinSavedView()
	: Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinSavedView::OnSavedViewDeleted(bool bSuccess, FString const& InSavedViewId, FString const& Response)
{
	BE_LOGI("ITwinAPI", "SavedView deleted: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	AITwinIModel* OwnerIModel = Impl->ResolveSavedViewIModel();
	if (bSuccess && ensure(InSavedViewId == this->SavedViewId))
	{
		GetWorld()->DestroyActor(this);
	}
	UITwinWebServices const* ParentWebServices = OwnerIModel ? OwnerIModel->GetWebServices() : nullptr;
	if (ParentWebServices && ParentWebServices != this->GetWebServices())
	{
		// propagate information to parent iModel
		ParentWebServices->OnSavedViewDeleted(bSuccess, InSavedViewId, Response);
	}
}

namespace
{
	std::unordered_set<ITwinElementID> IntersectSets(const std::unordered_set<ITwinElementID>& SetA,
													 const std::unordered_set<ITwinElementID>& SetB)
	{
		std::unordered_set<ITwinElementID> Result;

		const std::unordered_set<ITwinElementID>& Smaller = (SetA.size() < SetB.size()) ? SetA : SetB;
		const std::unordered_set<ITwinElementID>& Larger = (SetA.size() < SetB.size()) ? SetB : SetA;

		for (const ITwinElementID& Element : Smaller)
		{
			if (Larger.find(Element) != Larger.end())
			{
				Result.insert(Element);
			}
		}

		return Result;
	}

	FString ToString(FPerModelCategoryVisibilityProps const& PerModelCat)
	{
		return FString::Printf(TEXT("Model:%s,Category:%s"), *PerModelCat.ModelId, *PerModelCat.CategoryId);
	}

	FString ToString(FString const& Str) { return Str; }

	template<typename T, size_t S>
	FString ToString(TSet<T> const& Set)
	{
		if (Set.IsEmpty())
			return TEXT("{}");
		FString Result = TEXT("{");
		Result.Reserve((std::max(100, Set.Num()) + 1) * S);
		int N = 1;
		for (const T& Element : Set)
		{
			if (N > 100)
				break;
			++N;
			Result += ToString(Element);
			Result += TCHAR(',');
		}
		if (N > 100)
			Result += TEXT(" ... }");
		else
			Result[Result.Len() - 1] = TCHAR('}');
		return Result;
	}

	template<size_t S, typename Container>
	FString ToString(Container const& Set)
	{
		return ToString<typename Container::ElementType, S>(Set);
	}
}

/*static*/ void AITwinSavedView::HideElements(AITwinIModel* iModel, FSavedView const& SavedView)
{
	if (!IsValid(iModel))
		return;
	FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
	auto InsertParsedIDs = [](const auto& inputIds){
		std::unordered_set<ITwinElementID> res;
		res.reserve(inputIds.Num());
		for (const auto& Id : inputIds)
		{
			ITwinElementID PickedID = ITwin::ParseElementID(Id);// ex: "0x20000001241"
			res.insert(PickedID);
		}
		return res;
	};
	auto InsertPairParsedIDs = [](const auto& inputIds) {
		std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> res;
		res.reserve(inputIds.Num());
		for (const auto& Id : inputIds)
		{
			res.insert({ ITwin::ParseElementID(Id.CategoryId),
						 ITwin::ParseElementID(Id.ModelId) });
		}
		return res;
	};
	BE_LOGI("ITwinAPI", "HiddenElements: " << TCHAR_TO_UTF8(*ToString<20>(SavedView.HiddenElements)));
	BE_LOGI("ITwinAPI", "AlwaysDrawnElements: " << TCHAR_TO_UTF8(*ToString<20>(SavedView.AlwaysDrawnElements)));
	BE_LOGI("ITwinAPI", "HiddenModels: " << TCHAR_TO_UTF8(*ToString<20>(SavedView.HiddenModels)));
	BE_LOGI("ITwinAPI", "HiddenCategories: " << TCHAR_TO_UTF8(*ToString<20>(SavedView.HiddenCategories)));
	BE_LOGI("ITwinAPI", "HiddenCategoriesPerModel: "
						<< TCHAR_TO_UTF8(*ToString<60>(SavedView.HiddenCategoriesPerModel)));
	BE_LOGI("ITwinAPI", "AlwaysDrawnCategoriesPerModel: "
						<< TCHAR_TO_UTF8(*ToString<60>(SavedView.AlwaysDrawnCategoriesPerModel)));
	auto HiddenElements = InsertParsedIDs(SavedView.HiddenElements);
	auto AlwaysDrawnElements = InsertParsedIDs(SavedView.AlwaysDrawnElements);
	auto HiddenModels = InsertParsedIDs(SavedView.HiddenModels);
	auto HiddenCategories = InsertParsedIDs(SavedView.HiddenCategories);
	auto HiddenCategoriesPerModel = InsertPairParsedIDs(SavedView.HiddenCategoriesPerModel);
	auto AlwaysDrawnCategoriesPerModel = InsertPairParsedIDs(SavedView.AlwaysDrawnCategoriesPerModel);
	//remove alwaysdrawncategoriespermodel from the list if their model is hidden
	for (auto it = AlwaysDrawnCategoriesPerModel.begin(); it != AlwaysDrawnCategoriesPerModel.end(); )
	{
		if (HiddenModels.count(it->second))
			it = AlwaysDrawnCategoriesPerModel.erase(it);
		else
			++it;
	}
	IModelInternals.HideCategories(HiddenCategories, true);
	IModelInternals.HideModels(HiddenModels, true);
	IModelInternals.HideCategories(HiddenCategories, true); // a 2nd time on purpose (but rationale is lost...)
	IModelInternals.HideCategoriesPerModel(HiddenCategoriesPerModel, true);
	IModelInternals.ShowCategoriesPerModel(AlwaysDrawnCategoriesPerModel, true);
	IModelInternals.HideElements(HiddenElements, false, true);
	IModelInternals.ShowElements(AlwaysDrawnElements, true);
	auto SceneMappingLocked = IModelInternals.SceneMapping->GetRAutoLock();
	std::unordered_set<ITwinElementID> EmptyElements;
	const std::unordered_set<ITwinElementID>* pConstructionElementsToHide = &EmptyElements;
	auto GeometryIDToElementIDsLock = SceneMappingLocked->GeometryIDToElementIDs->GetRAutoLock();
	auto& GeometryIDToElementIDs = *GeometryIDToElementIDsLock;
	if (!iModel->bShowConstructionData)
		pConstructionElementsToHide = &GeometryIDToElementIDs.at(1);
	IModelInternals.HideElements(*pConstructionElementsToHide, true, true);
}

void AITwinSavedView::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, 
										   FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
	{
		BE_LOGE("ITwinAPI", "SavedView retrieval failed: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
		return;
	}
	BE_LOGI("ITwinAPI", "SavedView information retrieved: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	OnSavedViewEdited(bSuccess, SavedView, SavedViewInfo);
	Impl->SavedViewData = SavedView;
	// Perform pending operation now, if any
	switch (Impl->PendingOperation)
	{
	case FImpl::EPendingOperation::None:
		break;
	case FImpl::EPendingOperation::Move:
		MoveToSavedView();
		break;
	case FImpl::EPendingOperation::ApplyVisibility:
		ApplySavedViewVisibility();
		break;
	case FImpl::EPendingOperation::Rename:
		RenameSavedView();
		break;
	}
	Impl->PendingOperation = FImpl::EPendingOperation::None;
}

void AITwinSavedView::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView,
										FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
	{
		BE_LOGE("ITwinAPI", "SavedView edition failed: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
		return;
	}
	// rename
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif

	AITwinIModel* OwnerIModel = Impl->ResolveSavedViewIModel();
	if (!OwnerIModel)
		return;
	FTransform const& Transform = UITwinUtilityLibrary::GetSavedViewUnrealTransform(OwnerIModel, SavedView);
	// Not SetActorTransform? To preserve scaling?
	SetActorLocation(Transform.GetLocation());
	SetActorRotation(Transform.GetRotation());
	Impl->bSavedViewTransformIsSet = true;
	BE_LOGI("ITwinAPI", "Editing SavedView, translation: " << TCHAR_TO_UTF8(*Transform.GetLocation().ToString())
		<< " and rotation " << TCHAR_TO_UTF8(*Transform.GetRotation().ToString()));
}

void AITwinSavedView::UpdateSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView has no SavedViewId");
		return;
	}
	BE_LOGI("ITwinAPI", "Will update SavedView: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		// No authorization yet: postpone the actual update (see UpdateOnSuccessfulAuthorization)
		return;
	}
	if (WebServices)
	{
		WebServices->GetSavedView(SavedViewId);
	}
}

void AITwinSavedView::UpdateThumbnail(const FString& FullFilePath)
{
	BE_LOGI("ITwinAPI", "SavedView will update its thumbnail: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	FString prefixURL = TEXT("data:image/png;base64,");
	TArray<uint8> RawBuffer;
	if (!FFileHelper::LoadFileToArray(RawBuffer, *FullFilePath))
	{
		return;
	}
	FString ThumbnailURL = FBase64::Encode(RawBuffer);
	UpdateWebServices();
	if (WebServices)
		WebServices->UpdateSavedViewThumbnail(SavedViewId, prefixURL + ThumbnailURL);
}

void AITwinSavedView::GetThumbnail()
{
	BE_LOGI("ITwinAPI", "SavedView will query its thumbnail: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	UpdateWebServices();
	if (WebServices)
		WebServices->GetSavedViewThumbnail(SavedViewId);
}

void AITwinSavedView::OnSavedViewThumbnailRetrieved(bool bSuccess, FString const& InSavedViewId,
	TArray<uint8> const& Buffer)
{
	if (!bSuccess)
	{
		BE_LOGE("ITwinAPI", "SavedView thumbnail retrieval failed: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
		return;
	}
	BE_LOGI("ITwinAPI", "SavedView thumbnail retrieved: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	UTexture2D* Tex2D = FImageUtils::ImportBufferAsTexture2D(Buffer);
	RetrievedThumbnail.Broadcast(SavedViewId, Tex2D);
}

void AITwinSavedView::OnSavedViewThumbnailUpdated(bool bSuccess, FString const& InSavedViewId, FString const& Response)
{
	if (!bSuccess)
	{
		BE_LOGE("ITwinAPI", "SavedView thumbnail update failed: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
		return;
	}
	BE_LOGI("ITwinAPI", "SavedView thumbnail updated: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
}

void AITwinSavedView::MoveToSavedView()
{
	BE_LOGI("ITwinAPI", "Will move to SavedView: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	if (SavedViewId.IsEmpty() && !Impl->bSavedViewTransformIsSet)
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView has no SavedViewId - cannot move to it");
		return;
	}
	UWorld const* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Impl->bSavedViewTransformIsSet)
	{
		auto EndRot = GetActorRotation();
		BE_LOGI("ITwinAPI", "Applying translation: " << TCHAR_TO_UTF8(*GetActorLocation().ToString())
			<< ", rotation: " << TCHAR_TO_UTF8(*EndRot.ToString())
			<< " and schedule time: " << Impl->SavedViewData.DisplayStyle.TimePoint);
		EndRot.Roll = 0.;
		if (Pawn)
		{
			checkSlow(GetWorld()->GetFirstPlayerController() == Controller && Controller->GetPawn() == Pawn);
			auto StartRot = Pawn->GetActorRotation();
			float BlendTime = 3.f;
			ACameraActor* Actor = nullptr;
			FTransform Transform(GetActorRotation(), GetActorLocation());
			Impl->StartCameraMovementToSavedView(BlendTime, Actor, Transform, 3.f);
			GetWorldTimerManager().SetTimer(ITwinSavedView::TimerHandle,
				FTimerDelegate::CreateLambda([=, ThisPtr = TWeakObjectPtr<AITwinSavedView>(this)]
				{
					if (!ThisPtr.IsValid())
						return;
					ThisPtr->Impl->EndCameraMovement(Actor, Transform);
					ThisPtr->Impl->ApplyScheduleTime();
					ThisPtr->FinishedMovingToSavedView.Broadcast();
				}),
				BlendTime,
				false);
		}
		else // no Pawn (nor Controller): we're probably in the Editor
		{
		#if WITH_EDITOR
			auto* PoV = StaticCast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
			if (PoV)
			{
				// Interp doesn't work, even with the ShouldTickIfViewportsOnly override: I stepped as far as
				// FloatEntry.InterpFunc.ExecuteIfBound(Val): the functor is bound, but the delegate
				// (AITwinSavedView::OnTimelineTick) is not called!?!
				// (I also tried with PrimaryActorTick.bCanEverTick = true; on the Saved view actor but, no,
				// actor and component are ticked independently it seems.
				//
				//Impl->WillMoveToSavedViewFrom(PoV->GetViewLocation(), PoV->GetViewRotation(),
				//	[](FVector const& TickPos, FRotator const& TickRot)
				//	{
				//		auto* PoV =
				//			StaticCast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
				//		if (PoV)
				//		{
				//			PoV->SetViewLocation(TickPos);
				//			PoV->SetViewRotation(TickRot);
				//		}
				//	});
				//Impl->TimelineComponent->PlayFromStart();

				// We could bypass TimelineComponent entirely and interpolate manually from the iModel global
				// ticker (or AITwinSavedView::Tick, using ShouldTickIfViewportsOnly?), but is it worth it?
				// => let's just teleport there for the time being:
				PoV->SetViewLocation(GetActorLocation());
				PoV->SetViewRotation(EndRot);
				Impl->ApplyScheduleTime();
			}
		#endif // WITH_EDITOR
		}
		BE_LOGI("ITwinAPI",
			"Applying show/hide requirements from SavedView " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
		ApplySavedViewVisibility();
	}
	else // fetch the saved view data before we can move to it
	{
		Impl->PendingOperation = FImpl::EPendingOperation::Move;
		UpdateSavedView();
	}
}

void AITwinSavedView::ApplySavedViewVisibility()
{
	if (SavedViewId.IsEmpty() && !Impl->bSavedViewTransformIsSet)
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView has no SavedViewId - cannot apply saved view visibility");
		return;
	}
	if (!Impl->bSavedViewTransformIsSet)
	{
		Impl->PendingOperation = FImpl::EPendingOperation::ApplyVisibility;
		UpdateSavedView();
		return;
	}
	HideElements(Impl->ResolveSavedViewIModel(), Impl->SavedViewData);
}

void AITwinSavedView::DeleteSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView with no SavedViewId cannot be deleted");
		return;
	}
	UpdateWebServices();
	BE_LOGI("ITwinAPI", "Deleting SavedView: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	if (WebServices)
	{
		WebServices->DeleteSavedView(SavedViewId);
	}
}

void AITwinSavedView::RenameSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView with no SavedViewId cannot be renamed");
		return;
	}
	BE_LOGI("ITwinAPI", "Will rename SavedView: " << TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	if (!Impl->bSavedViewTransformIsSet)
	{
		// fetch the saved view data before we can rename it
		Impl->PendingOperation = FImpl::EPendingOperation::Rename;
		UpdateSavedView();
		return;
	}
	AITwinIModel* OwnerIModel = Impl->ResolveSavedViewIModel();
	if (!OwnerIModel)
		return;
	FSavedView CurrentSV = UITwinUtilityLibrary::GetSavedViewFromUnrealTransform(OwnerIModel,
		// Not GetActorTransform? To skip scaling?
		FTransform(GetActorRotation(), GetActorLocation()));
	UITwinSynchro4DSchedules* Synchro4DSchedules = OwnerIModel->Synchro4DSchedules;
	if (ensure(IsValid(Synchro4DSchedules)) && Synchro4DSchedules->HasValidId())
	{
		//get current time of animation if any
		const auto& currentTime = Synchro4DSchedules->GetScheduleTime();
		CurrentSV.DisplayStyle.RenderTimeline = "0x20000003cda"; //fake Id for now
		CurrentSV.DisplayStyle.TimePoint = currentTime.ToUnixTimestamp();
	}
	UpdateWebServices();
	BE_LOGI("ITwinAPI", "Renaming SavedView to " << TCHAR_TO_UTF8(*DisplayName));
	if (WebServices && !DisplayName.IsEmpty())
	{
		WebServices->EditSavedView(CurrentSV, { SavedViewId, DisplayName, true });
	}
}

void AITwinSavedView::RetakeSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView with no SavedViewId cannot be edited");
		return;
	}
	AITwinIModel* OwnerIModel = Impl->ResolveSavedViewIModel();
	if (!OwnerIModel)
		return;
	FSavedView ModifiedSV;
	if (!UITwinUtilityLibrary::GetSavedViewFromPlayerController(OwnerIModel, ModifiedSV))
	{
		return;
	}
	FString const displayName = GetActorNameOrLabel();
	UpdateWebServices();
	BE_LOGI("ITwinAPI", "Retaking SavedView: " << TCHAR_TO_UTF8(*displayName)
		<< ", new origin: " << TCHAR_TO_UTF8(*ModifiedSV.Origin.ToString())
		<< ", new angles: " << TCHAR_TO_UTF8(*ModifiedSV.Angles.ToString()));
	if (WebServices && !displayName.IsEmpty())
	{
		WebServices->EditSavedView(ModifiedSV, { SavedViewId, displayName, true });
	}
}

#if WITH_EDITOR
void AITwinSavedView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinSavedView, DisplayName))
		RenameSavedView();
}
#endif

void AITwinSavedView::Destroyed()
{
	Impl->DestroyChildren();
}

const TCHAR* AITwinSavedView::GetObserverName() const
{
	return TEXT("ITwinSavedView");
}

void AITwinSavedView::UpdateOnSuccessfulAuthorization()
{
	UpdateSavedView();
}

void AITwinSavedView::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("ITwinSavedView cannot add SavedViews"));
}
