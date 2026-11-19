/*--------------------------------------------------------------------------------------+
|
|     $Source: CameraViewHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <CameraViewHelper.h>

#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <TimerManager.h>

namespace ITwin {

void StartBlendedCameraMovement(UWorld* World, const FTransform& NewCameraTransform, float BlendTime/* = 3.f*/)
{
	if (!ensure(World))
		return;

	ACameraViewHelper* ViewHelper = World->SpawnActor<ACameraViewHelper>(ACameraViewHelper::StaticClass());
	if (ensure(ViewHelper))
	{
		ViewHelper->StartMovement(NewCameraTransform, BlendTime);
	}
}

}

void ACameraViewHelper::StartMovement(const FTransform& NewCameraTransform, float BlendTime)
{
	TargetTransform = NewCameraTransform;

	UWorld* World = GetWorld();
	APlayerController* PlayerController = ensure(World) ? World->GetFirstPlayerController() : nullptr;
	if (!ensure(PlayerController))
	{
		Destroy();
		return;
	}

	OverviewCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), NewCameraTransform);
	if (!ensure(OverviewCamera))
	{
		Destroy();
		return;
	}
	OverviewCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	PlayerController->SetViewTargetWithBlend(OverviewCamera, BlendTime, VTBlend_Linear, 0, true);

	World->GetTimerManager().SetTimer(TimerHandle, this, &ACameraViewHelper::EndMovement, BlendTime, false);
}

void ACameraViewHelper::EndMovement()
{
	if (OverviewCamera)
	{
		OverviewCamera->Destroy();
		OverviewCamera = nullptr;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = ensure(World) ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = ensure(PlayerController) ? PlayerController->GetPawnOrSpectator() : nullptr;
	if (!ensure(Pawn))
	{
		Destroy();
		return;
	}

	Pawn->SetActorLocation(TargetTransform.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	FRotator Rot = TargetTransform.Rotator();
	PlayerController->SetControlRotation(Rot);
	PlayerController->GetPawnOrSpectator()->SetActorRotation(Rot);
	PlayerController->SetViewTargetWithBlend(Pawn);

	Destroy();
}
