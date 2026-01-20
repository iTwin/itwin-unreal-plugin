/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DAnimator.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Templates/PimplPtr.h>

class UITwinSynchro4DSchedules;
class FITwinSceneTile;
class FITwinSynchro4DSchedulesInternals;

/// Class owned by an UITwinSynchro4DSchedules component which role is to enact the construction
/// schedules' animations for the iModel. It manages an internal mapping from "game time" to script time,
/// since the animation is typically played at a much faster than realtime speed, and can be
/// played/paused/stopped/slowed/accelerated/reversed independently of the actual game time.
class FITwinSynchro4DAnimator
{
	UITwinSynchro4DSchedules& Owner;
public:
	FITwinSynchro4DAnimator(UITwinSynchro4DSchedules& Owner);

	void OnChangedAnimationSpeed();
	void OnChangedScheduleRenderSetting();
	void OnMaskOutNonAnimatedElements();
	void OnFadeOutNonAnimatedElements();
	void ApplyAnimationOnTile(FITwinSceneTile& SceneTile);
	void DisableAnimationInTile(FITwinSceneTile& SceneTile);

	void Play();
	bool IsPlaying() const;
	void Pause();
	void Stop();

	void ManageMeshDynamicShadows(FITwinSynchro4DSchedulesInternals& SchedInternals);

	void TickAnimation(float DeltaTime, bool const bForceUpdateAll);

	class FImpl;
private:
	TPimplPtr<FImpl> Impl;
};
