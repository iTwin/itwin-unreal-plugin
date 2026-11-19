/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLookAtCameraModifier.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Camera/CameraModifier.h>

#include "ITwinLookAtCameraModifier.generated.h"

class AITwinLookAtTargetActor;
class ACineCameraActor;

//! Applies a timeline clip's look-at tracking to the final camera POV during playback.
//!
//! WHY A CAMERA MODIFIER, AND NOT A TICK
//! -------------------------------------
//! Outside playback the view target is the player pawn, and AITwinLookAtTargetActor::Tick aims
//! it directly - that path works and is untouched by this class. During playback the camera cut
//! track makes the clip's cine camera the view target, and the problem becomes one of ordering:
//!
//!  - the level sequence evaluates the clip's transform track and writes the camera's rotation
//!    from the key-frames, every frame;
//!  - ACineCameraActor::Tick does compute the look-at correctly (verified: its
//!    LastLookatTrackingRotation points at the target), but the track evaluation overwrites it;
//!  - writing the rotation back from a later tick group (TG_PostUpdateWork) does not help
//!    either: the write succeeds, but the POV has already been sampled by
//!    APlayerCameraManager::UpdateCamera - called from APlayerController::PlayerTick, long
//!    before - and the next evaluation overwrites our value before anything reads it again.
//!    Measured: our aim Yaw ~= -73 deg, rendered Yaw ~= +44 deg (the keyframed move).
//!
//! Two earlier attempts failed for related reasons and must not be retried:
//!  - clip->SetTransformRotationEnabled(false) stops the track clobbering the rotation, but
//!    GetTransformAtTime then reads an incomplete track, which breaks scrubbing;
//!  - moving the proxy actor to TG_PostUpdateWork breaks scrubbing *and* the manual view, which
//!    both need the proxy to run before PlayerCameraManager::UpdateCamera.
//!
//! A camera modifier is invoked by PlayerCameraManager::UpdateCamera itself, *after* the view
//! target has produced its POV and *before* that POV is used. It is therefore immune to tick
//! and evaluation order: there is no longer a race to win.
//!
//! Note that this modifies the POV, not the actor: ACineCameraActor::GetActorRotation() keeps
//! returning the keyframed value, which is expected and keeps the transform track intact (it
//! still holds the GCS world keys used for persistence and by the iTwin web viewer).
UCLASS()
class ITWINRUNTIME_API UITwinLookAtCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UITwinLookAtCameraModifier();
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;


	//! Proxy mirroring the instance the camera *aims at*, if any.
	TWeakObjectPtr<AITwinLookAtTargetActor> Proxy;
	//! Proxy mirroring the instance the camera *rides on*, if any. Distinct from Proxy: a camera
	//! may sit in a car (follow) while aiming at a building (look-at).
	TWeakObjectPtr<AITwinLookAtTargetActor> FollowProxy;
	//! Clip camera this modifier applies to. The modifier is registered on the player camera
	//! manager, which is global, so it must only act while this very camera is the view target.
	TWeakObjectPtr<ACineCameraActor> CineCam;

	//! Set by the timeline when this clip's camera is the one being played back. Deliberately
	//! explicit: the view target cannot be used here, because camera cuts blend through
	//! PendingViewTarget and the answer changes within a single frame.
	bool bActiveForPlayback = false;
};
