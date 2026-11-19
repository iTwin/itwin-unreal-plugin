/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLookAtCameraModifier.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <AnimTimeline/ITwinLookAtCameraModifier.h>
#include <AnimTimeline/ITwinLookAtTargetActor.h>

#include <Camera/PlayerCameraManager.h>
#include <CineCameraActor.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>


#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Log.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

UITwinLookAtCameraModifier::UITwinLookAtCameraModifier()
{
	Priority = 0; // run first
}

bool UITwinLookAtCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	ACineCameraActor* Cam = CineCam.Get();
	if (!bActiveForPlayback || !Cam)
		return false;

	// Follow first: the camera rides the instance, overriding the POV the transform track just
	// produced. Done here rather than from a tick for the same reason as the look-at - the track
	// rewrites the camera every frame, and placing the view from a tick would instead steal the
	// view target from the cut camera (see the class doc).
	if (AITwinLookAtTargetActor* F = FollowProxy.Get())
	{
		F->RefreshFromInstance(); // important: avoid dependence on proxy tick ordering
		if (TOptional<FTransform> const FollowTM = F->GetFollowWorldTransform())
		{
			InOutPOV.Location = FollowTM->GetLocation();
			InOutPOV.Rotation = FollowTM->Rotator();
		}
	}

	// Then aim, from the (possibly just overridden) eye position: this ordering is what gives
	// the "passenger riding a car while watching a building" case.
	AITwinLookAtTargetActor* P = Proxy.Get();
	if (P)
	{
		P->RefreshFromInstance(); // same rationale for look-at target
	}
	if (P && P->IsTargetValid() && Cam->LookatTrackingSettings.bEnableLookAtTracking)
	{
		InOutPOV.Rotation = ITwinLookAt::ComputeLookAtRotation(
			Cam->LookatTrackingSettings,
			InOutPOV.Location,
			InOutPOV.Rotation,
			DeltaTime,
			P->ConsumePlaybackInterpReset());
	}

	// Keep pawn/controller synced for systems that sample them (e.g., tiles selection),
	// but do NOT change view target.
	if (APlayerCameraManager* PCM = CameraOwner)
	{
		APlayerController* PC = Cast<APlayerController>(PCM->GetOwningPlayerController());
		if (PC)
		{
			PC->SetControlRotation(InOutPOV.Rotation);
			APawn* Pawn = PC->GetPawnOrSpectator();
			if (Pawn)
			{
				Pawn->SetActorLocation(InOutPOV.Location, false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->SetActorRotation(InOutPOV.Rotation);
			}
		}
	}

	const bool bOwnsFinalPOV =
		(FollowProxy.IsValid() || Proxy.IsValid());

	return bOwnsFinalPOV; // authoritative in playback follow/look-at modes
}
