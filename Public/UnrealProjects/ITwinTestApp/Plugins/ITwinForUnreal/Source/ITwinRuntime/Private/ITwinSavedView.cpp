/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSavedView.h>
#include <ITwinServerConnection.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <HttpModule.h>
#include <Kismet/KismetMathLibrary.h>
#include <Components/TimelineComponent.h>
#include <Curves/CurveFloat.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/GameplayStatics.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>

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
	bool bSavedViewTransformIsSet = false;
	EPendingOperation PendingOperation = EPendingOperation::None;
	UTimelineComponent* TimelineComponent = nullptr;
	UCurveFloat* CurveFloat = nullptr;

	FImpl(AITwinSavedView& InOwner)
		:Owner(InOwner)
	{
		TimelineComponent = Owner.CreateDefaultSubobject<UTimelineComponent>(TEXT("TimelineComponent"));
		TimelineComponent->SetLooping(false);
		FOnTimelineFloat TimelineTickDelegate;
		TimelineTickDelegate.BindUFunction(&Owner, "OnTimelineTick");
		CurveFloat = NewObject<UCurveFloat>();
		CurveFloat->FloatCurve.UpdateOrAddKey(0.f, 0.f);
		CurveFloat->FloatCurve.UpdateOrAddKey(1.f, 1.f);
		TimelineComponent->AddInterpFloat(CurveFloat, TimelineTickDelegate);
		TimelineComponent->SetTimelineLengthMode(ETimelineLengthMode::TL_TimelineLength);
	}
	void DestroyChildren()
	{
		const auto ChildrenCopy = Owner.Children;
		for (auto& Child : ChildrenCopy)
			Owner.GetWorld()->DestroyActor(Child);
		Owner.Children.Empty();
	}
};

AITwinSavedView::AITwinSavedView()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinSavedView::OnTimelineTick(const float& output)
{
	auto svPos = GetActorLocation();
	auto svRot = GetActorRotation();
	auto startPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	auto startRot = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorRotation();
	GetWorld()->GetFirstPlayerController()->GetPawn()->SetActorLocation(UKismetMathLibrary::VLerp(startPos, svPos, output),false, nullptr, ETeleportType::TeleportPhysics);
	svRot.Roll = 0;
	GetWorld()->GetFirstPlayerController()->GetPawn()->SetActorRotation(UKismetMathLibrary::RLerp(startRot, svRot, output, true));
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

/*static*/ void AITwinSavedView::HideElements(const UObject* WorldContextObject, FSavedView const& SavedView)
{
	if (!IsValid(WorldContextObject))
		return;
	AITwinIModel* const iModel = Cast<AITwinIModel>(UGameplayStatics::GetActorOfClass(WorldContextObject->GetWorld(), AITwinIModel::StaticClass()));
	TArray<FString> allHiddenIds;
	allHiddenIds.Append(SavedView.HiddenElements);
	allHiddenIds.Append(SavedView.HiddenCategories);
	allHiddenIds.Append(SavedView.HiddenModels);
	std::vector<ITwinElementID> mergedIds;
	FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
	for (auto& elId : allHiddenIds)
	{
		// Update selection highlight
		ITwinElementID PickedEltID = ITwin::ParseElementID(elId);// ex: "0x20000001241"
		auto const& CategoryIDToElementIDs = IModelInternals.SceneMapping.CategoryIDToElementIDs[PickedEltID];
		auto const& ModelIDToElementIDs = IModelInternals.SceneMapping.ModelIDToElementIDs[PickedEltID];
		bool isElementID = CategoryIDToElementIDs.empty() && ModelIDToElementIDs.empty();
		mergedIds.insert(mergedIds.end(), CategoryIDToElementIDs.begin(), CategoryIDToElementIDs.end());
		mergedIds.insert(mergedIds.end(), ModelIDToElementIDs.begin(), ModelIDToElementIDs.end());
		if (isElementID)
			mergedIds.push_back(PickedEltID);
	}
	IModelInternals.HideElements(mergedIds);
}

void AITwinSavedView::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, 
										   FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;

	OnSavedViewEdited(bSuccess, SavedView, SavedViewInfo);

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
	if (CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
	{
		// No authorization yet: postpone the actual update (see UpdateOnSuccessfulAuthorization)
		return;
	}
	if (WebServices)
	{
		WebServices->GetSavedView(SavedViewId);
	}
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
	if (!Pawn)
		return;

	if (!Impl->bSavedViewTransformIsSet)
	{
		// fetch the saved view data before we can move to it
		Impl->PendingOperation = FImpl::EPendingOperation::Move;
		UpdateSavedView();
		return;
	}
	auto camPos = GetActorLocation();
	auto camRot = GetActorRotation();
	Pawn->bUseControllerRotationPitch = 0;
	Pawn->bUseControllerRotationYaw = 0;
	Impl->TimelineComponent->PlayFromStart();

	checkSlow(GetWorld()->GetFirstPlayerController() == Controller && Controller->GetPawn() == Pawn);
	camRot.Roll = 0;
	Pawn->bUseControllerRotationPitch = 1;
	Pawn->bUseControllerRotationYaw = 1;
	Controller->SetControlRotation(camRot);
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
	FSavedView const CurrentSV = UITwinUtilityLibrary::GetSavedViewFromUnrealTransform(OwnerIModel,
		// Not GetActorTransform? To skip scaling?
		FTransform(GetActorRotation(), GetActorLocation()));

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
