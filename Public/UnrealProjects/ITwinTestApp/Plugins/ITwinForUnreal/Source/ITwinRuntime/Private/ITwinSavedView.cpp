/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSavedView.h>
#include <ITwinServerConnection.h>
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
	void DestroyChildren()
	{
		const auto ChildrenCopy = Owner.Children;
		for (auto& Child : ChildrenCopy)
			Owner.GetWorld()->DestroyActor(Child);
		Owner.Children.Empty();
	}
	void ApplyScheduleTime()
	{
		// saved views are owned by an iModel actor (except those created manually from scratch)
		AITwinIModel* OwnerIModel = Cast<AITwinIModel>(Owner.GetOwner());
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
	void StartCameraMovementToSavedView(float& OutBlendTime, ACameraActor*& Actor, const FTransform& Transform, float BlendTime)
	{
		AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
		if (!ensure(IModel != nullptr))
			return;
		OutBlendTime = BlendTime;
		TObjectIterator<APlayerController> Itr;
		ensure(*Itr != nullptr);
		APlayerController* PlayerController = *Itr;
		Actor = Itr->GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Transform);
		Actor->GetCameraComponent()->SetConstraintAspectRatio(false);
		PlayerController->SetViewTargetWithBlend(Actor, BlendTime, VTBlend_Linear, 0, true);
	}

	void EndCameraMovement(ACameraActor* Actor, const FTransform& Transform)
	{
		Actor->Destroy();
		TObjectIterator<APlayerController> Itr;
		ensure(*Itr != nullptr);
		APlayerController* PlayerController = *Itr;
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
	// usually, saved views are owned by a AITwinIModel actor (except those created manually from scratch)
	AActor* OwnerActor = GetOwner();
	AITwinServiceActor* OwnerSrvActor = OwnerActor ? Cast<AITwinServiceActor>(OwnerActor) : nullptr;

	if (bSuccess && ensure(InSavedViewId == this->SavedViewId))
	{
		GetWorld()->DestroyActor(this);
	}

	UITwinWebServices const* ParentWebServices = OwnerSrvActor ? OwnerSrvActor->GetWebServices() : nullptr;
	if (ParentWebServices && ParentWebServices != this->GetWebServices())
	{
		// propagate information to parent iModel
		ParentWebServices->OnSavedViewDeleted(bSuccess, InSavedViewId, Response);
	}
}

/*static*/ void AITwinSavedView::HideElements(AITwinIModel* iModel, FSavedView const& SavedView)
{
	if (!IsValid(iModel))
		return;
	TArray<FString> allHiddenIds;
	allHiddenIds.Append(SavedView.HiddenElements);
	allHiddenIds.Append(SavedView.HiddenCategories);
	allHiddenIds.Append(SavedView.HiddenModels);
	std::unordered_set<ITwinElementID> mergedIds;
	FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
	for (auto& elId : allHiddenIds)
	{
		// Update selection highlight
		ITwinElementID PickedEltID = ITwin::ParseElementID(elId);// ex: "0x20000001241"
		auto const& CategoryIDToElementIDs = IModelInternals.SceneMapping.CategoryIDToElementIDs[PickedEltID];
		auto const& ModelIDToElementIDs = IModelInternals.SceneMapping.ModelIDToElementIDs[PickedEltID];
		bool isElementID = CategoryIDToElementIDs.empty() && ModelIDToElementIDs.empty();
		mergedIds.insert(CategoryIDToElementIDs.begin(), CategoryIDToElementIDs.end());
		mergedIds.insert(ModelIDToElementIDs.begin(), ModelIDToElementIDs.end());
		if (isElementID)
			mergedIds.insert(PickedEltID);
	}
	IModelInternals.HideElements(mergedIds, false);
}

void AITwinSavedView::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, 
										   FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;

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
		return;

	// rename
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif

	// usually, saved views are owned by a AITwinIModel actor (except those created manually from scratch)
	AITwinIModel* OwnerIModel = Cast<AITwinIModel>(GetOwner());
	if (!OwnerIModel)
		return;
	FTransform const& Transform = UITwinUtilityLibrary::GetSavedViewUnrealTransform(OwnerIModel, SavedView);
	// Not SetActorTransform? To preserve scaling?
	SetActorLocation(Transform.GetLocation());
	SetActorRotation(Transform.GetRotation());
	Impl->bSavedViewTransformIsSet = true;
}

void AITwinSavedView::UpdateSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView has no SavedViewId");
		return;
	}
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
	UpdateWebServices();
	if (WebServices)
		WebServices->GetSavedViewThumbnail(SavedViewId);
}

void AITwinSavedView::OnSavedViewThumbnailRetrieved(bool bSuccess, FString const& InSavedViewId,
	TArray<uint8> const& Buffer)
{
	if (!bSuccess)
		return;

	UTexture2D* Tex2D = FImageUtils::ImportBufferAsTexture2D(Buffer);
	RetrievedThumbnail.Broadcast(SavedViewId, Tex2D);
}

void AITwinSavedView::OnSavedViewThumbnailUpdated(bool bSuccess, FString const& InSavedViewId, FString const& Response)
{

}

void AITwinSavedView::MoveToSavedView()
{
	if (SavedViewId.IsEmpty())
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
		EndRot.Roll = 0.;
		if (Pawn)
		{
			checkSlow(GetWorld()->GetFirstPlayerController() == Controller && Controller->GetPawn() == Pawn);
			auto StartRot = Pawn->GetActorRotation();
			float BlendTime;
			ACameraActor* Actor;
			FTransform Transform(GetActorRotation(), GetActorLocation());
			Impl->StartCameraMovementToSavedView(BlendTime, Actor, Transform, 3);
			GetWorldTimerManager().SetTimer(ITwinSavedView::TimerHandle, FTimerDelegate::CreateLambda([=, this, _ = TStrongObjectPtr<AITwinSavedView>(this)]
				{
					if (!Impl || !IsValid(this))
						return;
					Impl->EndCameraMovement(Actor, Transform);
					Impl->ApplyScheduleTime();
					FinishedMovingToSavedView.Broadcast();
				}), BlendTime, false);
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
		AITwinIModel* const iModel = Cast<AITwinIModel>(GetAttachParentActor());
		HideElements(iModel, Impl->SavedViewData);
	}
	else // fetch the saved view data before we can move to it
	{
		Impl->PendingOperation = FImpl::EPendingOperation::Move;
		UpdateSavedView();
	}
}

void AITwinSavedView::DeleteSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinSavedView with no SavedViewId cannot be deleted");
		return;
	}
	UpdateWebServices();
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
	if (!Impl->bSavedViewTransformIsSet)
	{
		// fetch the saved view data before we can rename it
		Impl->PendingOperation = FImpl::EPendingOperation::Rename;
		UpdateSavedView();
		return;
	}

	// usually, saved views are owned by a AITwinIModel actor (except those created manually from scratch)
	AITwinIModel* OwnerIModel = Cast<AITwinIModel>(GetOwner());
	if (!OwnerIModel)
		return;
	FSavedView CurrentSV = UITwinUtilityLibrary::GetSavedViewFromUnrealTransform(OwnerIModel,
		// Not GetActorTransform? To skip scaling?
		FTransform(GetActorRotation(), GetActorLocation()));
	UITwinSynchro4DSchedules* Synchro4DSchedules = OwnerIModel->Synchro4DSchedules;
	if (ensure(Synchro4DSchedules != nullptr) && !(Synchro4DSchedules->ScheduleId.IsEmpty() || Synchro4DSchedules->ScheduleId.StartsWith(TEXT("Unknown"))))
	{
		//get current time of animation if any
		const auto& currentTime = Synchro4DSchedules->GetScheduleTime();
		CurrentSV.DisplayStyle.RenderTimeline = "0x20000003cda"; //fake Id for now
		CurrentSV.DisplayStyle.TimePoint = currentTime.ToUnixTimestamp();
	}
	UpdateWebServices();
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

	// usually, saved views are owned by a AITwinIModel actor (except those created manually from scratch)
	AITwinIModel* OwnerIModel = Cast<AITwinIModel>(GetOwner());
	if (!OwnerIModel)
		return;
	FSavedView ModifiedSV;
	if (!UITwinUtilityLibrary::GetSavedViewFromPlayerController(OwnerIModel, ModifiedSV))
	{
		return;
	}
	FString const displayName = GetActorNameOrLabel();

	UpdateWebServices();
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
