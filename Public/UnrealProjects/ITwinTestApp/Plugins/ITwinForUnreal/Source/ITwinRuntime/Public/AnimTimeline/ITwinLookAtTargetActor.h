/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLookAtTargetActor.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <AnimTimeline/ITwinLookAtResolvePolicy.h>
#include <CoreMinimal.h>
#include <GameFramework/Actor.h>

#include "ITwinLookAtTargetActor.generated.h"

class AITwinPopulation;
class ACineCameraActor;
struct FCameraLookatTrackingSettings;

namespace ITwinLookAt
{
	//! Mirrors ACineCameraActor::GetLookatLocation(), which is protected in the engine.
	//! Keep in sync with CineCameraActor.cpp.
	ITWINRUNTIME_API FVector GetLookatLocation(FCameraLookatTrackingSettings const& Settings);

	//! Mirrors the look-at block of ACineCameraActor::Tick(), but for an arbitrary viewer
	//! location (the player pawn) rather than the camera actor's own location, so that the
	//! manually-navigated view and the cine camera behave identically.
	//! Updates Settings.LastLookatTrackingRotation, like the engine does.
	ITWINRUNTIME_API FRotator ComputeLookAtRotation(FCameraLookatTrackingSettings& Settings,
		FVector const& ViewLocation, FRotator const& CurrentRotation,
		float DeltaTime, bool bResetInterpolation);
}

//! Invisible actor whose transform mirrors one instance of an AITwinPopulation's instanced
//! static mesh component, so that engine features requiring an AActor target can follow it
//! (ISM instances are not UObjects).
//! Used as ACineCameraActor::LookatTrackingSettings::ActorToTrack by the timeline clips:
//!  - during playback/export the clip camera is the view target and ACineCameraActor::Tick
//!    does the look-at itself;
//!  - outside playback the view is the player pawn (see ScreenUtils::SetCurrentView), so
//!    this actor aims it directly, using the very same settings and formula.
UCLASS(NotPlaceable)
class ITWINRUNTIME_API AITwinLookAtTargetActor : public AActor
{
	GENERATED_BODY()

public:
	AITwinLookAtTargetActor();

	//! Spawns a proxy following the given instance, and makes CameraToDrive depend on it
	//! so the proxy is always ticked first (avoids a one-frame lag on the camera).
	//! \param CameraToDrive Camera whose LookatTrackingSettings this proxy applies to the view.
	//!		Pass nullptr for a proxy that only provides an instance pose (follow mode): a
	//!		non-null camera makes the proxy *aim* the view, which only the look-at proxy may do.
	static AITwinLookAtTargetActor* Spawn(AITwinPopulation& Population, int32 InstanceIndex,
										  ACineCameraActor* CameraToDrive);

	void SetTarget(AITwinPopulation& Population, int32 InstanceIndex);

	//! Returns false when the population or the instance no longer exists.
	bool IsTargetValid() const;

	//! How the camera offset is anchored to the tracked instance.
	enum class EFollowSpace : uint8
	{
		FullRelative,  //!< offset follows location *and* rotation: fixed seat in the car
		LocationOnly,  //!< offset follows location only: camera trails, keeping its own heading
	};

	//! Camera placement, in the tracked instance's frame. Not persisted: follow mode is a
	//! runtime-only override for now (keyframes keep storing GCS world transforms).
	void SetFollowOffset(FTransform const& Offset, EFollowSpace Space);
	void ClearFollowOffset();
	bool HasFollowOffset() const { return bHasFollowOffset; }

	//! World transform the camera should have this frame, or unset when follow is inactive
	//! or the target is gone (in which case the caller must keep the keyframed transform).
	TOptional<FTransform> GetFollowWorldTransform() const;

	//! Builds the offset that reproduces the given world transform at the current instance
	//! pose - used to seed follow mode from wherever the user has placed the view.
	FTransform MakeFollowOffsetFromWorld(FTransform const& World, EFollowSpace Space) const;

	//! World <-> instance space conversions, for keyframe capture and for the manual view.
	//! Identity-safe: returns the input unchanged when the target is invalid.
	FTransform WorldToTargetSpace(FTransform const& World) const;
	FTransform TargetSpaceToWorld(FTransform const& Relative) const;

	//! When true, this proxy re-aims the player view every tick (manual navigation case).
	//! Only the proxy of the *current* clip may do so: otherwise the proxies of all tracking
	//! clips fight over the view rotation every frame. The instance transform, on the other
	//! hand, is always kept up to date, since the cine camera reads it during playback.
	void SetDrivesPlayerView(bool bInDrivesPlayerView);
	bool DrivesPlayerView() const { return bDrivesPlayerView; }

	//! Set by the timeline while this proxy's clip is being played back. Single source of truth
	//! with UITwinLookAtCameraModifier::bActiveForPlayback: during playback the modifier owns
	//! the POV, and this actor must not touch the view at all.
	void SetPlaybackActive(bool bActive);

	//! Returns whether the playback look-at must snap instead of interpolating, and clears the
	//! flag. Separate from the manual path's bResetInterp: the two aim from different locations
	//! (the pawn vs the cine camera) through the same shared LastLookatTrackingRotation, so each
	//! needs its own latch to avoid swinging in from the other's rotation.
	bool ConsumePlaybackInterpReset()
	{
		bool const bReset = bResetInterpPlayback;
		bResetInterpPlayback = false;
		return bReset;
	}

	virtual void Tick(float DeltaTime) override;

	// Force a sync from current ISM instance transform to this proxy actor.
	void RefreshFromInstance();

private:
	void UpdateTransformFromInstance();

	double GetNowSeconds() const;

	//! Missing-target resolution policy:
	//! - Static populations: never scan every frame; during playback/export, retry at most once
	//!   per playback window, and outside playback retry at a low rate.
	//! - Path-animated populations: retry at a throttled rate, since runtime systems may move
	//!   ownership/state and we still want eventual recovery without per-frame O(N).
	FITwinLookAtResolvePolicy ResolvePolicy;

	UPROPERTY(Transient)
	TWeakObjectPtr<AITwinPopulation> Population;

	//! Index of the tracked instance in the population. Only a cache: it is revalidated
	//! against InstanceRefIdValue every update, and re-resolved when instances have shifted.
	int32 CachedInstanceIndex = INDEX_NONE;

	//! Stable identity of the instance, stored as the SDK RefID value.
	uint64 InstanceRefIdValue = 0;

	TWeakObjectPtr<ACineCameraActor> CameraToDrive;
	bool bDrivesPlayerView = false;
	bool bPlaybackActive = false;
	bool bResetInterp = true;
	//! Set whenever we are not playing back (see Tick) so that each playback starts snapped.
	bool bResetInterpPlayback = true;

	FTransform FollowOffset = FTransform::Identity;
	EFollowSpace FollowSpace = EFollowSpace::FullRelative;
	bool bHasFollowOffset = false;
};
