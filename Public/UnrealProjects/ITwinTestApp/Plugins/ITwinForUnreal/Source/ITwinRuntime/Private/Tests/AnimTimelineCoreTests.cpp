/*--------------------------------------------------------------------------------------+
|
|     $Source: AnimTimelineCoreTests.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_DEV_AUTOMATION_TESTS

#include <AnimTimeline/ITwinLookAtResolvePolicy.h>
#include <AnimTimeline/ITwinTimelineClipMapping.h>
#include <AnimTimeline/ITwinLookAtCameraModifier.h>
#include <AnimTimeline/ITwinLookAtTargetActor.h>
#include <Tests/WebTestHelpers.h>

#include <Camera/CameraTypes.h>
#include <CineCameraActor.h>
#include <Engine/World.h>
#include <Misc/AutomationTest.h>


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtResolvePolicy_StaticPlaybackOneRetry,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.StaticPlaybackOneRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_StaticPlaybackOneRetry::RunTest(const FString&)
{
	FITwinLookAtResolvePolicy P;
	P.OnResolveFailed(/*bIsPathAnimated*/false, /*bPlaybackActive*/true, 0.0);

	TestTrue(TEXT("First retry allowed"), P.ShouldRetryNow(false, true, 0.0));
	TestFalse(TEXT("Second retry denied"), P.ShouldRetryNow(false, true, 0.1));

	P.OnPlaybackStarted();
	TestTrue(TEXT("Retry re-armed on next playback"), P.ShouldRetryNow(false, true, 0.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtResolvePolicy_PathThrottledRetry,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.PathThrottledRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_PathThrottledRetry::RunTest(const FString&)
{
	FITwinLookAtResolvePolicy P;
	P.OnResolveFailed(/*bIsPathAnimated*/true, /*bPlaybackActive*/true, 10.0);

	TestFalse(TEXT("No retry before throttle"), P.ShouldRetryNow(true, true, 10.1));
	TestTrue(TEXT("Retry after throttle"), P.ShouldRetryNow(true, true, 10.6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinLookAtResolvePolicy_LogMissingOnce_Test,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.LogMissingOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_LogMissingOnce_Test::RunTest(const FString& Parameters)
{
	FITwinLookAtResolvePolicy Policy;

	TestTrue(TEXT("First missing-target log must be emitted"),
		Policy.ShouldLogMissingTargetOnce());

	TestFalse(TEXT("Second consecutive missing-target log must be suppressed"),
		Policy.ShouldLogMissingTargetOnce());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinLookAtResolvePolicy_LogResetOnResolveSuccess_Test,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.LogResetOnResolveSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_LogResetOnResolveSuccess_Test::RunTest(const FString& Parameters)
{
	FITwinLookAtResolvePolicy Policy;

	TestTrue(TEXT("Initial missing-target log should be emitted"),
		Policy.ShouldLogMissingTargetOnce());

	Policy.OnResolveSucceeded();

	TestTrue(TEXT("After resolve success, missing-target log should be re-armed"),
		Policy.ShouldLogMissingTargetOnce());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinLookAtResolvePolicy_PlaybackStartDoesNotResetLog_Test,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.PlaybackStartDoesNotResetMissingLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_PlaybackStartDoesNotResetLog_Test::RunTest(const FString& Parameters)
{
	FITwinLookAtResolvePolicy Policy;

	TestTrue(TEXT("Initial missing-target log should be emitted"),
		Policy.ShouldLogMissingTargetOnce());

	Policy.OnPlaybackStarted();

	TestFalse(TEXT("Playback start should not reset missing-target log guard"),
		Policy.ShouldLogMissingTargetOnce());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtResolvePolicy_ResolvedAlwaysRetries,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.ResolvedAlwaysRetries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_ResolvedAlwaysRetries::RunTest(const FString&)
{
	FITwinLookAtResolvePolicy P;
	P.State = FITwinLookAtResolvePolicy::ETargetResolveState::Resolved;

	TestTrue(TEXT("Resolved: retry for static/non-playback"), P.ShouldRetryNow(false, false, 0.0));
	TestTrue(TEXT("Resolved: retry for path/playback"), P.ShouldRetryNow(true, true, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtResolvePolicy_StaticNonPlaybackThrottledRetry,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.StaticNonPlaybackThrottledRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_StaticNonPlaybackThrottledRetry::RunTest(const FString&)
{
	FITwinLookAtResolvePolicy P;
	P.OnResolveFailed(/*bIsPathAnimated*/false, /*bPlaybackActive*/false, 5.0);

	TestEqual(TEXT("State is throttled retry"),
		P.State, FITwinLookAtResolvePolicy::ETargetResolveState::MissingRetryThrottled);
	TestFalse(TEXT("No retry before throttle"), P.ShouldRetryNow(false, false, 5.2));
	TestTrue(TEXT("Retry after throttle"), P.ShouldRetryNow(false, false, 6.1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtResolvePolicy_OnResolveSucceededResetsState,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtResolvePolicy.OnResolveSucceededResetsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtResolvePolicy_OnResolveSucceededResetsState::RunTest(const FString&)
{
	FITwinLookAtResolvePolicy P;
	P.OnResolveFailed(/*bIsPathAnimated*/true, /*bPlaybackActive*/true, 10.0);
	TestTrue(TEXT("Precondition: not resolved"),
		P.State != FITwinLookAtResolvePolicy::ETargetResolveState::Resolved);

	P.OnResolveSucceeded();

	TestEqual(TEXT("State reset to resolved"),
		P.State, FITwinLookAtResolvePolicy::ETargetResolveState::Resolved);
	TestEqual(TEXT("Next retry time reset"), P.NextRetryTimeSec, 0.0);
	TestTrue(TEXT("Missing log is re-armed after resolve"), P.ShouldLogMissingTargetOnce());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinTimelineClipMapping_InvalidInput,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.ClipMapping.InvalidInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinTimelineClipMapping_InvalidInput::RunTest(const FString&)
{
	using ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes;

	TestEqual(TEXT("Negative time -> invalid"), GetClipIdxAndLocalTimeFromStartTimes({0.f, 1.f}, -0.1f).first, -1);
	TestEqual(TEXT("Too few start times -> invalid"), GetClipIdxAndLocalTimeFromStartTimes({0.f}, 0.2f).first, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinTimelineClipMapping_InsideRanges,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.ClipMapping.InsideRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinTimelineClipMapping_InsideRanges::RunTest(const FString&)
{
	using ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes;

	// Clip starts + end: [0, 2, 5, 9] -> clips: [0..2), [2..5), [5..9)
	const TArray<float> Starts = { 0.f, 2.f, 5.f, 9.f };

	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 1.25f);
		TestEqual(TEXT("Clip 0 index"), R.first, 0);
		TestEqual(TEXT("Clip 0 local time"), R.second, 1.25f);
	}
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 3.5f);
		TestEqual(TEXT("Clip 1 index"), R.first, 1);
		TestEqual(TEXT("Clip 1 local time"), R.second, 1.5f);
	}
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 8.0f);
		TestEqual(TEXT("Clip 2 index"), R.first, 2);
		TestEqual(TEXT("Clip 2 local time"), R.second, 3.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinTimelineClipMapping_BoundariesAndOutOfRange,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.ClipMapping.BoundariesAndOutOfRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinTimelineClipMapping_BoundariesAndOutOfRange::RunTest(const FString&)
{
	using ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes;

	const TArray<float> Starts = { 0.f, 2.f, 5.f, 9.f };

	// By design (strict <), exact boundary belongs to the next clip.
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 2.0f);
		TestEqual(TEXT("Boundary at 2.0 -> clip 1"), R.first, 1);
		TestEqual(TEXT("Boundary local time"), R.second, 0.0f);
	}
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 9.0f);
		TestEqual(TEXT("End boundary -> invalid"), R.first, -1);
	}
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 10.0f);
		TestEqual(TEXT("Past end -> invalid"), R.first, -1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinTimelineClipMapping_RepeatedStartsSingleClipPlayback,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.ClipMapping.RepeatedStartsSingleClipPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinTimelineClipMapping_RepeatedStartsSingleClipPlayback::RunTest(const FString&)
{
	using ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes;

	// Simulate 3 clips where only clip #1 is linked for playback:
	// CurrentCutTrackStartTimes_ pattern can be [0, 0, End, End]
	const TArray<float> Starts = { 0.f, 0.f, 4.f, 4.f };

	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 0.1f);
		TestEqual(TEXT("Playback local time should map to selected clip #1"), R.first, 1);
		TestEqual(TEXT("Local time preserved"), R.second, 0.1f);
	}
	{
		auto R = GetClipIdxAndLocalTimeFromStartTimes(Starts, 3.9f);
		TestEqual(TEXT("Still selected clip #1 near end"), R.first, 1);
		TestEqual(TEXT("Local time near end"), R.second, 3.9f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAt_ComputeLookAtRotation_NearZeroKeepsCurrent,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAt.ComputeLookAtRotation.NearZeroKeepsCurrent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAt_ComputeLookAtRotation_NearZeroKeepsCurrent::RunTest(const FString&)
{
	FCameraLookatTrackingSettings S;
	S.RelativeOffset = FVector::ZeroVector;
	S.LookAtTrackingInterpSpeed = 5.0f;
	S.bAllowRoll = false;
	S.LastLookatTrackingRotation = FRotator(1.0f, 2.0f, 3.0f);

	const FVector ViewLocation = FVector::ZeroVector;
	const FRotator Current(10.0f, 20.0f, 30.0f);

	const FRotator Out = ITwinLookAt::ComputeLookAtRotation(
		S, ViewLocation, Current, 1.0f / 60.0f, /*bResetInterpolation*/true);

	TestEqual(TEXT("Near-zero vector keeps current rotation"), Out, Current);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAt_ComputeLookAtRotation_ResetSnapsToTarget,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAt.ComputeLookAtRotation.ResetSnapsToTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAt_ComputeLookAtRotation_ResetSnapsToTarget::RunTest(const FString&)
{
	FCameraLookatTrackingSettings S;
	S.RelativeOffset = FVector(100.0f, 0.0f, 0.0f); // +X => yaw/pitch ~ 0
	S.LookAtTrackingInterpSpeed = 1.0f;
	S.bAllowRoll = false;
	S.LastLookatTrackingRotation = FRotator(0.0f, 180.0f, 45.0f);

	const FVector ViewLocation = FVector::ZeroVector;
	const FRotator Current(0.0f, 90.0f, 20.0f);

	const FRotator Out = ITwinLookAt::ComputeLookAtRotation(
		S, ViewLocation, Current, 1.0f / 60.0f, /*bResetInterpolation*/true);

	TestTrue(TEXT("Snap yaw close to expected"), FMath::IsNearlyEqual(Out.Yaw, 0.0f, 1e-2f));
	TestTrue(TEXT("Snap pitch close to expected"), FMath::IsNearlyEqual(Out.Pitch, 0.0f, 1e-2f));
	TestTrue(TEXT("Roll overwritten when bAllowRoll=false"), FMath::IsNearlyEqual(Out.Roll, 0.0f, 1e-2f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAt_ComputeLookAtRotation_AllowRollPreservesCurrentRoll,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAt.ComputeLookAtRotation.AllowRollPreservesCurrentRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAt_ComputeLookAtRotation_AllowRollPreservesCurrentRoll::RunTest(const FString&)
{
	FCameraLookatTrackingSettings S;
	S.RelativeOffset = FVector(0.0f, 100.0f, 0.0f); // +Y => yaw ~ 90
	S.LookAtTrackingInterpSpeed = 1.0f;
	S.bAllowRoll = true;
	S.LastLookatTrackingRotation = FRotator::ZeroRotator;

	const FVector ViewLocation = FVector::ZeroVector;
	const FRotator Current(0.0f, 0.0f, 37.0f);

	const FRotator Out = ITwinLookAt::ComputeLookAtRotation(
		S, ViewLocation, Current, 1.0f / 60.0f, /*bResetInterpolation*/true);

	TestTrue(TEXT("Roll preserved"), FMath::IsNearlyEqual(Out.Roll, 37.0f, 1e-2f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAt_ComputeLookAtRotation_UpdatesLastLookAtRotation,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAt.ComputeLookAtRotation.UpdatesLastLookAtRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAt_ComputeLookAtRotation_UpdatesLastLookAtRotation::RunTest(const FString&)
{
	FCameraLookatTrackingSettings S;
	S.RelativeOffset = FVector(0.0f, 100.0f, 0.0f);
	S.LookAtTrackingInterpSpeed = 2.0f;
	S.bAllowRoll = false;
	S.LastLookatTrackingRotation = FRotator(0.0f, 0.0f, 0.0f);

	const FVector ViewLocation = FVector::ZeroVector;
	const FRotator Current = FRotator::ZeroRotator;

	const FRotator Out = ITwinLookAt::ComputeLookAtRotation(
		S, ViewLocation, Current, 1.0f / 60.0f, /*bResetInterpolation*/false);

	TestEqual(TEXT("LastLookatTrackingRotation updated"), S.LastLookatTrackingRotation, Out);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_DefaultPriority,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.DefaultPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_DefaultPriority::RunTest(const FString&)
{
	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	TestEqual(TEXT("Modifier priority should run first"), M->Priority, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_InactiveReturnsFalse,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.InactiveReturnsFalse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_InactiveReturnsFalse::RunTest(const FString&)
{
	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	M->bActiveForPlayback = false;

	FMinimalViewInfo POV;
	POV.Location = FVector(1.0, 2.0, 3.0);
	POV.Rotation = FRotator(4.0, 5.0, 6.0);

	const bool bResult = M->ModifyCamera(1.0f / 60.0f, POV);
	TestFalse(TEXT("Inactive modifier should not own POV"), bResult);
	TestEqual(TEXT("POV location unchanged"), POV.Location, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("POV rotation unchanged"), POV.Rotation, FRotator(4.0, 5.0, 6.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_ActiveNoCamReturnsFalse,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.ActiveNoCamReturnsFalse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_ActiveNoCamReturnsFalse::RunTest(const FString&)
{
	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	M->bActiveForPlayback = true;
	M->CineCam = nullptr; // explicit

	FMinimalViewInfo POV;
	const bool bResult = M->ModifyCamera(1.0f / 60.0f, POV);
	TestFalse(TEXT("Active modifier without cine cam should not run"), bResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_ActiveWithValidProxyOwnsPOV,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.ActiveWithValidProxyOwnsPOV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_ActiveWithValidProxyOwnsPOV::RunTest(const FString&)
{
	UWorld* World = FITwinAPITestHelperBase::GetTestWorld();
	TestNotNull(TEXT("World is available"), World);
	if (!World)
		return false;

	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	ACineCameraActor* Cam = World->SpawnActor<ACineCameraActor>();
	AITwinLookAtTargetActor* Proxy = World->SpawnActor<AITwinLookAtTargetActor>();
	TestNotNull(TEXT("CineCam spawned"), Cam);
	TestNotNull(TEXT("Proxy spawned"), Proxy);
	if (!Cam || !Proxy)
		return false;

	M->bActiveForPlayback = true;
	M->CineCam = Cam;
	M->FollowProxy = Proxy; // valid weak ptr => bOwnsFinalPOV should be true

	FMinimalViewInfo POV;
	const bool bResult = M->ModifyCamera(1.0f / 60.0f, POV);
	TestTrue(TEXT("Modifier should own POV when a proxy is valid"), bResult);

	Proxy->Destroy();
	Cam->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_ActiveWithValidLookAtProxyOwnsPOV,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.ActiveWithValidLookAtProxyOwnsPOV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_ActiveWithValidLookAtProxyOwnsPOV::RunTest(const FString&)
{
	UWorld* World = FITwinAPITestHelperBase::GetTestWorld();
	TestNotNull(TEXT("World is available"), World);
	if (!World)
		return false;

	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	ACineCameraActor* Cam = World->SpawnActor<ACineCameraActor>();
	AITwinLookAtTargetActor* Proxy = World->SpawnActor<AITwinLookAtTargetActor>();
	TestNotNull(TEXT("CineCam spawned"), Cam);
	TestNotNull(TEXT("Look-at proxy spawned"), Proxy);
	if (!Cam || !Proxy)
	{
		if (Proxy) Proxy->Destroy();
		if (Cam) Cam->Destroy();
		return false;
	}

	M->bActiveForPlayback = true;
	M->CineCam = Cam;
	M->Proxy = Proxy; // look-at proxy path
	M->FollowProxy = nullptr;

	FMinimalViewInfo POV;
	const bool bResult = M->ModifyCamera(1.0f / 60.0f, POV);
	TestTrue(TEXT("Modifier should own POV when look-at proxy is valid"), bResult);

	Proxy->Destroy();
	Cam->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FITwinLookAtCameraModifier_ActiveNoProxyDoesNotOwnPOV,
	"Bentley.ITwinForUnreal.ITwinRuntime.AnimTimeline.LookAtCameraModifier.ActiveNoProxyDoesNotOwnPOV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLookAtCameraModifier_ActiveNoProxyDoesNotOwnPOV::RunTest(const FString&)
{
	UWorld* World = FITwinAPITestHelperBase::GetTestWorld();
	TestNotNull(TEXT("World is available"), World);
	if (!World)
		return false;

	UITwinLookAtCameraModifier* M = NewObject<UITwinLookAtCameraModifier>();
	TestNotNull(TEXT("Modifier created"), M);
	if (!M)
		return false;

	ACineCameraActor* Cam = World->SpawnActor<ACineCameraActor>();
	TestNotNull(TEXT("CineCam spawned"), Cam);
	if (!Cam)
		return false;

	M->bActiveForPlayback = true;
	M->CineCam = Cam;
	M->Proxy = nullptr;
	M->FollowProxy = nullptr;

	FMinimalViewInfo POV;
	const bool bResult = M->ModifyCamera(1.0f / 60.0f, POV);
	TestFalse(TEXT("Modifier should not own POV when no proxy is set"), bResult);

	Cam->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
