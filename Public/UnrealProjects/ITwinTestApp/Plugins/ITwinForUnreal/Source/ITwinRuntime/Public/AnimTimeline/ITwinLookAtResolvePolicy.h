/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLookAtResolvePolicy.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

//! Retry policy for resolving a tracked instance by RefID when the cached index becomes invalid.
//!
//! Goal: avoid O(N) RefID->index scans every frame when an instance is missing, while still
//! allowing recovery when it can legitimately reappear.
//!
//! Behavior summary:
//! - Resolved: cached index is valid; no retry throttling.
//! - MissingNoRetry: used for static targets during active playback/export after the one allowed
//!   retry in that playback window.
//! - MissingRetryThrottled: periodic retries (time-gated) for edit-time/static recovery and for
//!   path-animated targets.
//!
//! Policy rules:
//! - Static + playback/export: at most one retry per playback window.
//! - Static + non-playback: throttled retries (low frequency).
//! - Path-animated: throttled retries (allows eventual recovery without per-frame scans).
//!
//! Call `OnPlaybackStarted()` when entering playback to re-arm the single static retry, and
//! call `OnResolveSucceeded()` as soon as the target is found again.
struct FITwinLookAtResolvePolicy
{
	enum class ETargetResolveState : uint8
	{
		Resolved,
		MissingNoRetry,
		MissingRetryThrottled
	};

	ETargetResolveState State = ETargetResolveState::Resolved;
	double NextRetryTimeSec = 0.0;
	bool bRetriedStaticMissingThisPlayback = false;
	//! Guard to avoid repeating the same "missing target" log every update while unresolved.
	bool bReportedMissingTarget = false;

	static constexpr double PathRetrySec = 0.5;
	static constexpr double StaticEditRetrySec = 1.0;

	bool ShouldRetryNow(bool bIsPathAnimated, bool bPlaybackActive, double NowSec)
	{
		if (State == ETargetResolveState::Resolved)
			return true;

		if (bIsPathAnimated)
			return NowSec >= NextRetryTimeSec;

		if (bPlaybackActive)
		{
			if (bRetriedStaticMissingThisPlayback)
				return false;
			bRetriedStaticMissingThisPlayback = true;
			return true;
		}
		return NowSec >= NextRetryTimeSec;
	}

	bool ShouldLogMissingTargetOnce()
	{
		if (bReportedMissingTarget)
			return false;
		bReportedMissingTarget = true;
		return true;
	}

	void OnResolveFailed(bool bIsPathAnimated, bool bPlaybackActive, double NowSec)
	{
		if (bIsPathAnimated)
		{
			State = ETargetResolveState::MissingRetryThrottled;
			NextRetryTimeSec = NowSec + PathRetrySec;
		}
		else if (bPlaybackActive)
		{
			State = ETargetResolveState::MissingNoRetry;
		}
		else
		{
			State = ETargetResolveState::MissingRetryThrottled;
			NextRetryTimeSec = NowSec + StaticEditRetrySec;
		}
	}

	void OnResolveSucceeded()
	{
		State = ETargetResolveState::Resolved;
		NextRetryTimeSec = 0.0;
		bReportedMissingTarget = false;
	}

	void OnPlaybackStarted()
	{
		bRetriedStaticMissingThisPlayback = false;
		// keep bReportedMissingTarget unchanged: missing status did not change by itself.
	}
};
