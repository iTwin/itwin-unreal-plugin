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
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetMathLibrary.h>
#include <TimerManager.h>
#include <UObject/StrongObjectPtr.h>

void ATopMenu::BeginPlay()
{
	Super::BeginPlay();
	// Create UI
	UI = CreateWidget<UTopMenuWidgetImpl>(GetWorld()->GetFirstPlayerController(), LoadClass<UTopMenuWidgetImpl>(nullptr,
		TEXT("/Script/UMGEditor.WidgetBlueprint'/Game/UX/TopMenuWidget.TopMenuWidget_C'")));
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
	SetIModel3DInfoInCoordSystem(IModelInfo, EITwinCoordSystem::ITwin);
}

void ATopMenu::SetIModel3DInfoInCoordSystem(const FITwinIModel3DInfo& IModelInfo, EITwinCoordSystem CoordSystem)
{
	// see comment in #StartCameraMovementToSavedView
	ensureMsgf(CoordSystem == EITwinCoordSystem::UE || IModelInfo.ModelCenter == FVector(0, 0, 0),
		TEXT("No offset needed for SavedViews with Cesium tiles"));

	FITwinIModel3DInfo& DstInfo = (CoordSystem == EITwinCoordSystem::UE)
		? IModel3dInfo_UE
		: IModel3dInfo_ITwin;
	DstInfo = IModelInfo;
}

void ATopMenu::GetAllSavedViews()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(UGameplayStatics::GetActorOfClass(GetWorld(), AITwinIModel::StaticClass()));
	if (ensure(IModel != nullptr))
	{
		ITwinWebService->OnGetSavedViewsComplete.AddDynamic(IModel, &AITwinIModel::OnSavedViewsRetrieved);

		UITwinWebServices* IModelWebServices = IModel->GetMutableWebServices();
		if (IModelWebServices)
		{
			// bind the add/delete saved view callback so that we update the list of saved views in the UI
			IModelWebServices->OnAddSavedViewComplete.AddDynamic(this, &ATopMenu::SavedViewAdded);
			IModelWebServices->OnDeleteSavedViewComplete.AddDynamic(this, &ATopMenu::SavedViewDeleted);
		}
	}

	ITwinWebService->GetAllSavedViews(ITwinId, IModelId);
}

void ATopMenu::ZoomOnIModel()
{
	// working in ITwin coordinate system here like in former 3DFT plugin would make no sense at all here...
	auto const& IModel3dInfo = GetIModel3DInfoInCoordSystem(EITwinCoordSystem::UE);

	GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator()->SetActorLocation(
		(0.5 * (IModel3dInfo.BoundingBoxMin + IModel3dInfo.BoundingBoxMax))
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

void ATopMenu::SavedViewAdded(bool bSuccess, FSavedViewInfo SavedViewInfo)
{
	if (bSuccess)
	{
		UI->AddSavedView(SavedViewInfo.DisplayName, SavedViewInfo.Id);
	}
}

void ATopMenu::SavedViewDeleted(bool bSuccess, FString SavedViewId, FString Response)
{
	if (!bSuccess)
	{
		// we could display the error message...
		return;
	}
	UI->RemoveSavedView(SavedViewId);
}

void ATopMenu::OnZoom()
{
	ZoomOnIModel();
}

void ATopMenu::StartCameraMovementToSavedView(float& OutBlendTime, ACameraActor*& Actor, FTransform& Transform, const FSavedView& SavedView, float BlendTime)
{
	OutBlendTime = BlendTime;
	FVector UEPos;
	// Note that we work in iTwin coordinate system here, and then convert to UE, as done in former
	// 3DFT plugin:
	auto const& IModel3dInfo = GetIModel3DInfoInCoordSystem(EITwinCoordSystem::ITwin);
	// With Cesium tiles, there is no need (and it would be wrong) to subtract the 'ModelCenter' here as done
	// in 3DFT plugin (this requirement was induced by former 3DFT geometry handling).
	// To avoid enforcing users to change all blueprints based on the default level of the 3DFT plugin, we
	// have decided to reset this ModelCenter to zero.
	checkfSlow(IModel3dInfo.ModelCenter == FVector(0, 0, 0), TEXT("No offset needed for SavedViews with Cesium tiles"));
	ITwinPositionToUE(UEPos, SavedView.Origin, IModel3dInfo.ModelCenter);
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
	UEPos = (ITwinPos - ModelOrigin) * FVector(100, -100, 100);
}

FITwinIModel3DInfo const& ATopMenu::GetIModel3DInfoInCoordSystem(EITwinCoordSystem CoordSystem) const
{
	return (CoordSystem == EITwinCoordSystem::UE) ? IModel3dInfo_UE : IModel3dInfo_ITwin;
}
