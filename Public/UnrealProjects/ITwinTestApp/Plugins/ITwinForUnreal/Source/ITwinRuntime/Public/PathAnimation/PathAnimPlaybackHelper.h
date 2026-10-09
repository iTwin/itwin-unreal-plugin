/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimPlaybackHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <PathAnimation/ITwinAnimPathHelper.h>

class UBakedAnimKeyFrames;

// Class responsible for animation playback and speed variation control.

namespace {

	struct SpeedVarInfo
	{
		float startTime = 0.f;
		float duration = 0.f;
		float speedDelta = 0.f;
	};

	struct SpeedController
	{
		// Vector of speed changes for the given object (applied in a loop).
		std::vector<SpeedVarInfo> speedVarInfo;
		float laneSpeed = 0.f;
		float repeatAfter = 0.f;
	};
}

struct PathAnimPlaybackSettings
{
	EITwinAnimPathRepeatMode repeatMode = EITwinAnimPathRepeatMode::Loop;
	bool bReverse = false;
	float Delay = 0.f;
	bool bNeedAlignmentFix = false; // currently required for all non-articulated vehicles of our library

	PathAnimPlaybackSettings(UITwinAnimPathHelper* PathHelper, int32 Lane, bool NeedAlignmentFix = false)
	{
		repeatMode = PathHelper->GetRepeatMode();
		Delay = PathHelper->GetDelay();
		bReverse = PathHelper->IsInvDirLane(Lane);
		bNeedAlignmentFix = NeedAlignmentFix;
	}
};

class ITWINRUNTIME_API PathAnimPlaybackHelper
{
public:
	FTransform GetTransform(float DeltaTime, bool bTimelineMode, PathAnimPlaybackSettings Settings);

	/** Side-effect-free equivalent of GetTransform(TimelineTime, /*bTimelineMode*=/true, Settings): does not
	 *  write curTime_/lastTransform_, so it can safely be called concurrently for the same instance
	 *  (e.g. from several Mass worker threads animating sections of one articulated vehicle). */
	FTransform ComputeTransformAtTime(float TimelineTime, const PathAnimPlaybackSettings& Settings) const;

	void SetKeyFrames(UBakedAnimKeyFrames* keyFramesPtr);
	void SetStartTransform(FTransform StartTransform);

	void ResetAnimation(float Delay,
		std::optional<float> StartTime = std::nullopt,
		std::optional<FTransform> StartTransform = std::nullopt);
	int32 GetLaneIndex() const;

	// Speed variation for the object along the path. It is defined by a set of time intervals
	// where the object speed will be different from the lane speed.
	// Here maxDeltaDist is the maximum distance that the object can cover when accelerating or decelerating 
	// without coming too close to the object in front of him or behind him.
	void InitSpeedVariation(float laneSpeed, float maxDeltaDist);
	void RemoveSpeedVariation();

protected:
	FTransform EvaluateAtCurTime(float curTime, const PathAnimPlaybackSettings& Settings) const;

	FTransform initTransform_;
	FTransform lastTransform_;
	TWeakObjectPtr<UBakedAnimKeyFrames> keyFrames_;
	SpeedController speedController_;
	float curTime_ = 0.f;
	float startTime_ = 0.f; // per instance start time (used to distribute objects on the path, for example)
};
