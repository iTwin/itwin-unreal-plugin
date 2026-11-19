/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLookAtTargetActor.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <AnimTimeline/ITwinLookAtTargetActor.h>
#include <AnimTimeline/ITwinTimelineActor.h>
#include <Population/ITwinPopulation.h>

#include <CineCameraActor.h>
#include <Engine/World.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Tools/Log.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


FVector ITwinLookAt::GetLookatLocation(FCameraLookatTrackingSettings const& Settings)
{
	if (AActor* ActorToTrack = Settings.ActorToTrack.Get())
	{
		FTransform const BaseTransform = ActorToTrack->GetActorTransform();
		return BaseTransform.TransformPosition(Settings.RelativeOffset);
	}
	return Settings.RelativeOffset;
}

FRotator ITwinLookAt::ComputeLookAtRotation(FCameraLookatTrackingSettings& Settings,
	FVector const& ViewLocation, FRotator const& CurrentRotation,
	float DeltaTime, bool bResetInterpolation)
{
	FVector const LookatLoc = GetLookatLocation(Settings);
	FVector const ToLookat = LookatLoc - ViewLocation;
	if (ToLookat.IsNearlyZero())
		return CurrentRotation;

	FRotator FinalRot =
		bResetInterpolation
		? ToLookat.Rotation()
		: FMath::RInterpTo(Settings.LastLookatTrackingRotation, ToLookat.Rotation(),
						   DeltaTime, Settings.LookAtTrackingInterpSpeed);

	if (Settings.bAllowRoll)
		FinalRot.Roll = CurrentRotation.Roll;

	Settings.LastLookatTrackingRotation = FinalRot;
	return FinalRot;
}


AITwinLookAtTargetActor::AITwinLookAtTargetActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Must run before the camera reads our transform.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// A bare AActor has no RootComponent: SetActorTransform() would be a silent no-op and
	// GetActorLocation() would always return the world origin.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// USceneComponent defaults to Static mobility, which cannot be moved at runtime.
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

AITwinLookAtTargetActor* AITwinLookAtTargetActor::Spawn(AITwinPopulation& InPopulation,
	int32 InstanceIndex, ACineCameraActor* CameraToDrive)
{
	UWorld* World = InPopulation.GetWorld();
	if (!World)
		return nullptr;

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.ObjectFlags |= RF_Transient; // runtime-only helper, never saved with the level
	auto* Proxy = World->SpawnActor<AITwinLookAtTargetActor>(SpawnInfo);
	if (!Proxy)
		return nullptr;

	Proxy->SetTarget(InPopulation, InstanceIndex);
	Proxy->CameraToDrive = CameraToDrive;
	if (CameraToDrive)
		CameraToDrive->AddTickPrerequisiteActor(Proxy);
	return Proxy;
}

void AITwinLookAtTargetActor::SetTarget(AITwinPopulation& InPopulation, int32 InstanceIndex)
{
	// Path-animated instances are written to the ISM component by AITwinPopulation::Tick
	// (FITwinPopulationWithPathExt::UpdatePopulationInstances flushes the pending transforms
	// there). Both actors tick in TG_PrePhysics, where the order is otherwise undefined, so we
	// would read a one-frame-stale transform - which the camera then inherits.
	if (AITwinPopulation* PrevPop = Population.Get(); PrevPop && PrevPop != &InPopulation)
		RemoveTickPrerequisiteActor(PrevPop);
	AddTickPrerequisiteActor(&InPopulation);

	Population = &InPopulation;
	CachedInstanceIndex = InstanceIndex;
	InstanceRefIdValue = (uint64)InPopulation.GetInstanceRefId(InstanceIndex).ID();
	ResolvePolicy.OnResolveSucceeded();

	UpdateTransformFromInstance();
	bResetInterp = true;
	bResetInterpPlayback = true;
}

bool AITwinLookAtTargetActor::IsTargetValid() const
{
	return Population.IsValid() && CachedInstanceIndex != INDEX_NONE;
}

namespace
{
	//! The frame the offset is expressed in. In LocationOnly the instance's rotation is
	//! discarded, so that a turning car translates the camera without swinging it around.
	FTransform FollowBase(FTransform const& InstanceTM,
						  AITwinLookAtTargetActor::EFollowSpace Space)
	{
		if (Space == AITwinLookAtTargetActor::EFollowSpace::LocationOnly)
			return FTransform(FRotator::ZeroRotator, InstanceTM.GetLocation());
		return FTransform(InstanceTM.GetRotation(), InstanceTM.GetLocation()); // drop scale
	}
}

void AITwinLookAtTargetActor::SetFollowOffset(FTransform const& Offset, EFollowSpace Space)
{
	FollowOffset = Offset;
	FollowSpace = Space;
	bHasFollowOffset = true;
}

void AITwinLookAtTargetActor::ClearFollowOffset()
{
	bHasFollowOffset = false;
}

FTransform AITwinLookAtTargetActor::MakeFollowOffsetFromWorld(FTransform const& World,
															  EFollowSpace Space) const
{
	if (!IsTargetValid())
		return FTransform::Identity;
	return World.GetRelativeTransform(FollowBase(GetActorTransform(), Space));
}

TOptional<FTransform> AITwinLookAtTargetActor::GetFollowWorldTransform() const
{
	if (!bHasFollowOffset || !IsTargetValid())
		return {};
	return FollowOffset * FollowBase(GetActorTransform(), FollowSpace);
}

FTransform AITwinLookAtTargetActor::WorldToTargetSpace(FTransform const& World) const
{
	if (!IsTargetValid())
		return World;
	return World.GetRelativeTransform(GetActorTransform());
}

FTransform AITwinLookAtTargetActor::TargetSpaceToWorld(FTransform const& Relative) const
{
	if (!IsTargetValid())
		return Relative;
	return Relative * GetActorTransform();
}

void AITwinLookAtTargetActor::SetDrivesPlayerView(bool bInDrivesPlayerView)
{
	if (bInDrivesPlayerView && !bDrivesPlayerView)
	{
		// Taking over the view: LastLookatTrackingRotation may be stale (it was last written
		// by another clip's proxy or by the cine camera), so snap instead of interpolating
		// from an unrelated orientation.
		bResetInterp = true;
		bResetInterpPlayback = true;
	}
	bDrivesPlayerView = bInDrivesPlayerView;
}

void AITwinLookAtTargetActor::SetPlaybackActive(bool bActive)
{
	if (bPlaybackActive != bActive)
	{
		// Switching path: each one aims from a different location (the cine camera vs the pawn)
		// through the same shared LastLookatTrackingRotation, so both must snap rather than
		// interpolate from the other's result.
		bResetInterp = true;
		bResetInterpPlayback = true;

		if (bActive)
		{
			// New playback window: allow one static-instance re-check.
			ResolvePolicy.OnPlaybackStarted();
		}
	}
	bPlaybackActive = bActive;
}

void AITwinLookAtTargetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Playback/export: modifier owns POV and refreshes proxy transforms explicitly.
	// Avoid double updates from two different frame phases.
	if (bPlaybackActive)
		return;

	// Always mirror the instance, even when this proxy does not drive the view: the cine camera
	// reads our transform through LookatTrackingSettings.ActorToTrack during playback.
	UpdateTransformFromInstance();

	if (!bDrivesPlayerView || !IsTargetValid())
		return;

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
		return;

	// Not playing back: make sure the next playback snaps to its target instead of
	// interpolating from whatever LastLookatTrackingRotation the manual path left behind
	// (the two paths aim from different locations - the pawn vs the cine camera).
	bResetInterpPlayback = true;

	// Follow mode: the camera rides the instance, so place the view outright. The look-at (if
	// any) then re-aims it, which is what gives the "passenger looking at a building" case.
	TOptional<FTransform> const FollowTM = GetFollowWorldTransform();
	if (FollowTM)
	{
		ScreenUtils::SetCurrentView(GetWorld(), FollowTM->GetLocation(), FollowTM->Rotator());
	}

	ACineCameraActor* CineCam = CameraToDrive.Get();
	if (!CineCam || !CineCam->LookatTrackingSettings.bEnableLookAtTracking)
		return;

	FVector ViewPos(FVector::ZeroVector);
	FRotator ViewRot(FRotator::ZeroRotator);
	ScreenUtils::GetCurrentView(GetWorld(), ViewPos, ViewRot);

	FRotator const LookAt = ITwinLookAt::ComputeLookAtRotation(
		CineCam->LookatTrackingSettings, ViewPos, ViewRot, DeltaTime, bResetInterp);
	bResetInterp = false;

	ScreenUtils::SetCurrentViewRotation(GetWorld(), LookAt);
}

double AITwinLookAtTargetActor::GetNowSeconds() const
{
	if (UWorld* World = GetWorld())
		return static_cast<double>(World->GetTimeSeconds());
	return 0.0;
}

void AITwinLookAtTargetActor::UpdateTransformFromInstance()
{
	AITwinPopulation* Pop = Population.Get();
	if (!Pop)
		return;

	// Instance indices shift when instances are removed (the SDK then calls
	// FITwinInstance::OnIndexChanged, but that is plain bookkeeping, not a notification we can
	// subscribe to). So we keep the index cached and only pay for the RefID -> index lookup
	// when the cached index no longer designates our instance.
	const AdvViz::SDK::RefID RefId = AdvViz::SDK::RefID::FromUInt64(InstanceRefIdValue);
	const bool bCacheStale =
		CachedInstanceIndex == INDEX_NONE
		|| CachedInstanceIndex >= Pop->GetNumberOfInstances()
		|| Pop->GetInstanceRefId(CachedInstanceIndex) != RefId;
	if (bCacheStale)
	{
		const bool bIsPathAnimated = Pop->IsPartOfPathAnimation();
		const double NowSec = GetNowSeconds();

		if (!ResolvePolicy.ShouldRetryNow(bIsPathAnimated, bPlaybackActive, NowSec))
			return;

		// O(N): only reached under policy above.
		CachedInstanceIndex = Pop->GetInstanceIndexFromRefId(RefId);
		if (CachedInstanceIndex == INDEX_NONE)
		{
			if (ResolvePolicy.ShouldLogMissingTargetOnce())
			{
				BE_LOGI("Timeline", "Tracked instance " << InstanceRefIdValue
					<< " no longer exists, keeping last known transform");
			}
			ResolvePolicy.OnResolveFailed(bIsPathAnimated, bPlaybackActive, NowSec);
			return;
		}

		ResolvePolicy.OnResolveSucceeded();
	}

	const bool bMoved = SetActorTransform(Pop->GetInstanceTransform(CachedInstanceIndex));
	ensureMsgf(bMoved, TEXT("Look-at proxy could not be moved (root component missing or static?)"));
}

void AITwinLookAtTargetActor::RefreshFromInstance()
{
	UpdateTransformFromInstance();
}
