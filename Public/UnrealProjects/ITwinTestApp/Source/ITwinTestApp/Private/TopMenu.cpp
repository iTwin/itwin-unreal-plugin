/*--------------------------------------------------------------------------------------+
|
|     $Source: TopMenu.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <TopMenu.h>
#include <ITwinRuntime/Private/Compil/SanitizedPlatformHeaders.h>
#include <TopMenuWidgetImpl.h>
#include <ITwinIModel.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/GameplayStatics.h>
#include <Kismet/KismetMathLibrary.h>
#include <TimerManager.h>
#include <UObject/StrongObjectPtr.h>
#include <ITwinUtilityLibrary.h>

void ATopMenu::BeginPlay()
{
	Super::BeginPlay();
	// had a crash there once (when calling GetPawn tho, not GetPawnOrSpectator)... depends on init order?
	if (auto* Pawn = GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator())
		Pawn->SetActorEnableCollision(false);
	// Create UI
	UI = CreateWidget<UTopMenuWidgetImpl>(GetWorld()->GetFirstPlayerController(), LoadClass<UTopMenuWidgetImpl>(nullptr,
		TEXT("/Script/UMGEditor.WidgetBlueprint'/Game/UX/TopMenuWidget.TopMenuWidget_C'")));
	UI->AddToViewport();
	UpdateElementId(false, "");
	ITwinWebService = NewObject<UITwinWebServices>(this);
	// Connect get saved views callback
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
	FITwinIModel3DInfo& DstInfo = (CoordSystem == EITwinCoordSystem::UE)
		? IModel3dInfo_UE
		: IModel3dInfo_ITwin;
	if (CoordSystem == EITwinCoordSystem::ITwin)
	{
		// See comment inside AITwinIModel::GetModel3DInfo
		IModel3dInfo_ITwin.ModelCenter = FVector::ZeroVector;
	}
	DstInfo = IModelInfo;
}

static AITwinIModel* GetTheIModel(const UObject* WorldContextObject)
{
	return Cast<AITwinIModel>(
		UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinIModel::StaticClass()));
}

void ATopMenu::GetAllSavedViews()
{
	AITwinIModel* IModel = GetTheIModel(GetWorld());
	if (!ensure(IModel != nullptr))
		return;

	ITwinWebService->OnGetSavedViewsComplete.AddUniqueDynamic(IModel, &AITwinIModel::OnSavedViewsRetrieved);
	UITwinWebServices* IModelWebServices = IModel->GetMutableWebServices();
	if (IModelWebServices
		&& !IModelWebServices->OnAddSavedViewComplete.IsAlreadyBound(this, &ATopMenu::SavedViewAdded))
	{
		// bind the add/delete saved view callback so that we update the list of saved views in the UI
		IModelWebServices->OnAddSavedViewComplete.AddDynamic(this, &ATopMenu::SavedViewAdded);
		IModelWebServices->OnDeleteSavedViewComplete.AddDynamic(this, &ATopMenu::SavedViewDeleted);
	}
	ITwinWebService->GetAllSavedViews(ITwinId, IModelId);
}

void ATopMenu::ZoomOnIModel()
{
	AITwinIModel* IModel = GetTheIModel(GetWorld());
	if (!ensure(IModel != nullptr))
		return;
	IModel->ZoomOnIModel();
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
	AITwinIModel* IModel = GetTheIModel(GetWorld());
	if (!ensure(IModel != nullptr))
		return;
	OutBlendTime = BlendTime;
	Transform = UITwinUtilityLibrary::GetSavedViewUnrealTransform(IModel, SavedView);
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

FITwinIModel3DInfo const& ATopMenu::GetIModel3DInfoInCoordSystem(EITwinCoordSystem CoordSystem) const
{
	return (CoordSystem == EITwinCoordSystem::UE) ? IModel3dInfo_UE : IModel3dInfo_ITwin;
}
