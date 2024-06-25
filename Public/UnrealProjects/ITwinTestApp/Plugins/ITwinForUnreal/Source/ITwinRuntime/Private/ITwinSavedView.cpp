/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSavedView.h>
#include <ITwinServerConnection.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <HttpModule.h>
#include <Kismet/KismetMathLibrary.h>
#include <Components/TimelineComponent.h>
#include <Curves/CurveFloat.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>

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

namespace
{
	// Conversions from iTwin to UE and vice versa
	inline void ITwinCameraToUE(
		FVector const& Location_ITwin, FRotator const& Rotation_ITwin,
		FVector& Location_UE, FRotator& Rotation_UE)
	{
		// Convert iTwin camera to UE
		Location_UE = Location_ITwin * FVector(100, -100, 100);

		Rotation_UE = Rotation_ITwin;
		Rotation_UE.Yaw *= -1;
		Rotation_UE = Rotation_UE.GetInverse();
		Rotation_UE = UKismetMathLibrary::ComposeRotators(FRotator(-90, -90, 0), Rotation_UE);
	}

	inline void ITwinSavedViewToUE(
		FSavedView const& SavedView,
		FVector& Location_UE, FRotator& Rotation_UE)
	{
		ITwinCameraToUE(SavedView.Origin, SavedView.Angles, Location_UE, Rotation_UE);
	}

	inline void UECameraToITwin(
		FVector const& Location_UE, FRotator const& Rotation_UE,
		FVector& Location_ITwin, FRotator& Rotation_ITwin)
	{
		//UE_LOG(LogTemp, Log, TEXT("Location UE: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), Location_UE.X, Location_UE.Y, Location_UE.Z));
		//UE_LOG(LogTemp, Log, TEXT("Rotation UE: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), Rotation_UE.Yaw, Rotation_UE.Pitch, Rotation_UE.Roll));

		// Convert position to iTwin
		Location_ITwin = Location_UE;
		Location_ITwin /= FVector(100.0, -100.0, 100.0);

		// Convert rotation to iTwin
		Rotation_ITwin = Rotation_UE;
		FRotator const rot(-90.0, -90.0, 0.0);
		Rotation_ITwin = UKismetMathLibrary::ComposeRotators(rot.GetInverse(), Rotation_ITwin);
		Rotation_ITwin = Rotation_ITwin.GetInverse();
		Rotation_ITwin.Yaw *= -1.0;

		//UE_LOG(LogTemp, Log, TEXT("Location ITwin: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), Location_ITwin.X, Location_ITwin.Y, Location_ITwin.Z));
		//UE_LOG(LogTemp, Log, TEXT("Rotation ITwin: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), Rotation_ITwin.Yaw, Rotation_ITwin.Pitch, Rotation_ITwin.Roll));
	}
}

namespace ITwin
{
	void UECameraToSavedView(AActor const& Pawn, FSavedView& OutSavedView)
	{
		UECameraToITwin(Pawn.GetActorLocation(), Pawn.GetActorRotation(),
			OutSavedView.Origin, OutSavedView.Angles);
	}

	bool GetSavedViewFromPlayerController(UWorld const* World, FSavedView& OutSavedView)
	{
		APlayerController const* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn const* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
			return false;
		UECameraToSavedView(*Pawn, OutSavedView);
		return true;
	}
}

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

void AITwinSavedView::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif

	// convert iTwin cam to UE
	FVector svLocation;
	FRotator svRotation;
	ITwinSavedViewToUE(SavedView, svLocation, svRotation);

	// set actor transform
	SetActorLocation(svLocation);
	SetActorRotation(svRotation);
	Impl->bSavedViewTransformIsSet = true;

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

void AITwinSavedView::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;
	//Rename
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif
	// Retake
	// convert itwin to ue
	FVector camPos;
	FRotator camRot;
	ITwinSavedViewToUE(SavedView, camPos, camRot);
	this->SetActorLocation(camPos);
	this->SetActorRotation(camRot);
}

void AITwinSavedView::UpdateSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("ITwinSavedView has no SavedViewId"));
		return;
	}
	if (CheckServerConnection() != AITwinServiceActor::EConnectionStatus::Connected)
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
		UE_LOG(LogTemp, Error, TEXT("ITwinSavedView has no SavedViewId - cannot move to it"));
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
		UE_LOG(LogTemp, Error, TEXT("ITwinSavedView with no SavedViewId cannot be deleted"));
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
		UE_LOG(LogTemp, Error, TEXT("ITwinSavedView with no SavedViewId cannot be renamed"));
		return;
	}
	if (!Impl->bSavedViewTransformIsSet)
	{
		// fetch the saved view data before we can rename it
		Impl->PendingOperation = FImpl::EPendingOperation::Rename;
		UpdateSavedView();
		return;
	}

	FVector camPos_ITwin;
	FRotator camRot_ITwin;
	UECameraToITwin(GetActorLocation(), GetActorRotation(), camPos_ITwin, camRot_ITwin);

	UpdateWebServices();
	if (WebServices && !DisplayName.IsEmpty())
	{
		WebServices->EditSavedView({ camPos_ITwin, FVector(0.0, 0.0, 0.0), camRot_ITwin },
			{ SavedViewId, DisplayName, true });
	}
}

void AITwinSavedView::RetakeSavedView()
{
	if (SavedViewId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("ITwinSavedView with no SavedViewId cannot be edited"));
		return;
	}

	FSavedView ModifiedSV;
	if (!ITwin::GetSavedViewFromPlayerController(GetWorld(), ModifiedSV))
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
