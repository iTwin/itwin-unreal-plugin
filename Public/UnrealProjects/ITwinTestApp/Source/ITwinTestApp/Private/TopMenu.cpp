/*--------------------------------------------------------------------------------------+
|
|     $Source: TopMenu.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <TopMenu.h>
#include <TopMenuWidgetImpl.h>
#include <ITwinIModel.h>
#include <iTwinWebServices/iTwinWebServices.h>
#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetMathLibrary.h>

void ATopMenu::BeginPlay()
{
	Super::BeginPlay();
	// Create UI
	UI = CreateWidget<UTopMenuWidgetImpl>(GetWorld()->GetFirstPlayerController(), LoadClass<UTopMenuWidgetImpl>(nullptr,
		L"/Script/UMGEditor.WidgetBlueprint'/Game/UX/TopMenuWidget.TopMenuWidget_C'"));
	UI->AddToViewport();
	UpdateElementId(false, "");
	ITwinWebService = NewObject<UITwinWebServices>(this);
	// Get saved views
	ITwinWebService->OnGetSavedViewsComplete.AddDynamic(this, &ATopMenu::OnSavedViews);
	// Saved view
	UI->OnSavedViewSelected.AddDynamic(this, &ATopMenu::SavedViewSelected);
	ITwinWebService->OnGetSavedViewComplete.AddDynamic(this, &ATopMenu::GetSavedView);
	// OnZoom
	UI->OnZoomPressed.AddDynamic(this, &ATopMenu::OnZoom);
}

void ATopMenu::SetIModelInfo(const FString& InITwinId, const FString& InIModelId, const FITwinIModel3DInfo& IModelInfo)
{
	ITwinId = InITwinId;
	IModelId = InIModelId;
	IModel3dInfo = IModelInfo;
}

void ATopMenu::GetAllSavedViews()
{
	AITwinIModel const& IModel = *Cast<AITwinIModel>(UGameplayStatics::GetActorOfClass(GetWorld(), AITwinIModel::StaticClass()));
	ITwinWebService->OnGetSavedViewsComplete.AddDynamic(&IModel, &AITwinIModel::OnSavedViewsRetrieved);

	ITwinWebService->GetAllSavedViews(ITwinId, IModelId);
}

void ATopMenu::ZoomOnIModel()
{
	GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator()->SetActorLocation(
		IModel3dInfo.ModelCenter
			// "0.5" is empirical, let's not be too far from the center of things, iModels tend to have
			// a large context around the actual area of interest...
			- FMath::Max(0.5 * FVector::Distance(IModel3dInfo.BoundingBoxMin, IModel3dInfo.BoundingBoxMax),
						 10000)
				* ((AActor*)GetWorld()->GetFirstPlayerController())->GetActorForwardVector(),
		false, nullptr, ETeleportType::TeleportPhysics);
}

void ATopMenu::UpdateElementId(bool bVisible, const FString& ElementId)
{
	UI->UpdateElementId(bVisible, ElementId);
}

void ATopMenu::OnSavedViews(bool bSuccess, FSavedViewInfos SavedViews)
{
	for (const auto& SavedView: SavedViews.SavedViews)
		UI->AddSavedView(SavedView.DisplayName, SavedView.Id);
}

void ATopMenu::SavedViewSelected(FString DisplayName, FString Value)
{
	ITwinWebService->GetSavedView(Value);
}

void ATopMenu::GetSavedView(bool bSuccess, FSavedView SavedView, FSavedViewInfo SavedViewInfo)
{
	float BlendTime;
	ACameraActor* Actor;
	FTransform Transform;
	StartCameraMovementToSavedView(BlendTime, Actor, Transform, SavedView, 3);
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([=, this, _ = TStrongObjectPtr<ATopMenu>(this)]
		{
			EndCameraMovement(Actor, Transform);
		}), BlendTime, false);
}

void ATopMenu::OnZoom()
{
	ZoomOnIModel();
}

void ATopMenu::StartCameraMovementToSavedView(float& OutBlendTime, ACameraActor*& Actor, FTransform& Transform, const FSavedView& SavedView, float BlendTime)
{
	OutBlendTime = BlendTime;
	FVector UEPos;
	ITwinPositionToUE(UEPos, SavedView.Origin, {0.0,0.0,0.0});
	FRotator UERotation;
	ITwinRotationToUE(UERotation, SavedView.Angles);
	Transform = FTransform(UERotation, UEPos);
	Actor = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Transform);
	Actor->GetCameraComponent()->SetConstraintAspectRatio(false);
	GetWorld()->GetFirstPlayerController()->SetViewTargetWithBlend(Actor, BlendTime, VTBlend_Linear, 0, true);
}

void ATopMenu::EndCameraMovement(ACameraActor* Actor, const FTransform& Transform)
{
	Actor->Destroy();
	GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator()->SetActorLocation(Transform.GetLocation(),
		false, nullptr, ETeleportType::TeleportPhysics);
	GetWorld()->GetFirstPlayerController()->SetControlRotation(Transform.Rotator());
	GetWorld()->GetFirstPlayerController()->SetViewTargetWithBlend(GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator());
}

void ATopMenu::ITwinRotationToUE(FRotator& UERotation, const FRotator& ITwinRotation)
{
	UERotation = UKismetMathLibrary::ComposeRotators(
		FRotator(-90, 0, -90),
		FRotator(ITwinRotation.Pitch, ITwinRotation.Yaw*(-1), ITwinRotation.Roll).GetInverse());
}

void ATopMenu::ITwinPositionToUE(FVector& UEPos, const FVector& ITwinPos, const FVector& ModelOrigin)
{
	UEPos = (ITwinPos-ModelOrigin)*FVector(100, -100, 100);
}
