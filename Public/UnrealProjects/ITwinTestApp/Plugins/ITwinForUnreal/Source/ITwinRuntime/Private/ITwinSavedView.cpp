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
	AITwinSavedView& Owner;
	bool bSavedViewTransformIsSet = false;
	UTimelineComponent* TimelineComponent;
	UCurveFloat* CurveFloat;
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

void AITwinSavedView::OnSavedViewDeleted(bool bSuccess, FString const& Response)
{
	if (!bSuccess)
		return;
	GetWorld()->DestroyActor(this);
}

void AITwinSavedView::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif
	//convert iTwin cam to UE
	auto svLocation = SavedView.Origin * FVector(100, -100, 100);
	auto svRotation = SavedView.Angles;
	svRotation.Yaw *= -1;
	svRotation = svRotation.GetInverse();
	svRotation = UKismetMathLibrary::ComposeRotators(FRotator(-90, -90, 0), svRotation);
	//set actor transform
	SetActorLocation(svLocation);
	SetActorRotation(svRotation);
	Impl->bSavedViewTransformIsSet = true;
}

void AITwinSavedView::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;
	//Rename
#if WITH_EDITOR
	SetActorLabel(SavedViewInfo.DisplayName);
#endif
	//Retake
	//convert itwin to ue
	auto camPos = SavedView.Origin * FVector(100, -100, 100);
	auto camRot = SavedView.Angles;
	camRot.Yaw *= -1;
	camRot = camRot.GetInverse();
	camRot = UKismetMathLibrary::ComposeRotators(FRotator(-90, -90, 0), camRot);
	this->SetActorLocation(camPos);
	this->SetActorRotation(camRot);
}

void AITwinSavedView::UpdateSavedView()
{
	check(!SavedViewId.IsEmpty());
	check(ServerConnection);
	if (!Children.IsEmpty())
		return;

	UpdateWebServices();
	if (WebServices && !SavedViewId.IsEmpty())
		WebServices->GetSavedView(SavedViewId);
}

void AITwinSavedView::MoveToSavedView()
{
	check(Impl->bSavedViewTransformIsSet);
	if (!Impl->bSavedViewTransformIsSet)
		return;
	if (!GetWorld() ||
		!GetWorld()->GetFirstPlayerController() ||
		!GetWorld()->GetFirstPlayerController()->GetPawn())
		return;
	auto camPos = GetActorLocation();
	auto camRot = GetActorRotation();
	GetWorld()->GetFirstPlayerController()->GetPawn()->bUseControllerRotationPitch = 0;
	GetWorld()->GetFirstPlayerController()->GetPawn()->bUseControllerRotationYaw = 0;
	Impl->TimelineComponent->PlayFromStart();
	camRot.Roll = 0;
	GetWorld()->GetFirstPlayerController()->GetPawn()->bUseControllerRotationPitch = 1;
	GetWorld()->GetFirstPlayerController()->GetPawn()->bUseControllerRotationYaw = 1;
	GetWorld()->GetFirstPlayerController()->SetControlRotation(camRot);
}

void AITwinSavedView::DeleteSavedView()
{
	check(!SavedViewId.IsEmpty());
	check(ServerConnection);
	UpdateWebServices();
	if (WebServices && !SavedViewId.IsEmpty())
		WebServices->DeleteSavedView(SavedViewId);
}

void AITwinSavedView::RenameSavedView()
{
	check(!SavedViewId.IsEmpty());
	check(ServerConnection);
	check(Impl->bSavedViewTransformIsSet);
	if (!Impl->bSavedViewTransformIsSet)
		return;
	auto camPos = GetActorLocation();
	auto camRot = GetActorRotation();
	UE_LOG(LogTemp, Log, TEXT("GetActorLocation UE: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), camPos.X, camPos.Y, camPos.Z));
	UE_LOG(LogTemp, Log, TEXT("GetActorRotation UE: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), camRot.Yaw, camRot.Pitch, camRot.Roll));
	//convert pos to itwin
	camPos /= FVector(100.0, -100.0, 100.0);
	//convert rot to itwin
	FRotator rot(-90.0, -90.0, 0.0);
	camRot = UKismetMathLibrary::ComposeRotators(rot.GetInverse(), camRot);
	camRot = camRot.GetInverse();
	camRot.Yaw *= -1.0;
	UE_LOG(LogTemp, Log, TEXT("GetActorLocation ITWIN: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), camPos.X, camPos.Y, camPos.Z));
	UE_LOG(LogTemp, Log, TEXT("GetActorRotation ITWIN: %s"), *FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), camRot.Yaw, camRot.Pitch, camRot.Roll));
	UpdateWebServices();
	if (WebServices && !SavedViewId.IsEmpty() && !DisplayName.IsEmpty())
	{
		WebServices->EditSavedView({ camPos, FVector(0.0, 0.0, 0.0), camRot },
			{ SavedViewId, DisplayName, true });
	}
}

void AITwinSavedView::RetakeSavedView()
{
	check(!SavedViewId.IsEmpty());
	check(ServerConnection);
	FString displayName = GetActorNameOrLabel();
	auto camPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	auto camRot = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorRotation();
	//convert pos to itwin
	camPos /= FVector(100.0, -100.0, 100.0);
	//convert rot to itwin
	FRotator rot(-90.0, -90.0, 0.0);
	camRot = UKismetMathLibrary::ComposeRotators(rot.GetInverse(), camRot);
	camRot = camRot.GetInverse();
	camRot.Yaw *= -1.0;
	UpdateWebServices();
	if (WebServices && !SavedViewId.IsEmpty() && !displayName.IsEmpty())
	{
		WebServices->EditSavedView({ camPos, FVector(0.0, 0.0, 0.0), camRot },
			{ SavedViewId, displayName, true });
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
