/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimPlaybackHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <PathAnimation/PathAnimPlaybackHelper.h>
#include <PathAnimation/BakedAnimKeyFrames.h>
#include <ProfilingDebugging/CpuProfilerTrace.h>


FTransform PathAnimPlaybackHelper::GetTransform(float DeltaTime, bool bTimelineMode, PathAnimPlaybackSettings Settings)
{
	if (!keyFrames_.IsValid() || keyFrames_->NeedsUpdate())
		return initTransform_;

	// When camera timeline is open, DeltaTime is actually current timeline time, not the delta time since last frame.
	if (bTimelineMode)
		curTime_ = DeltaTime + startTime_ - Settings.Delay;
	else
		curTime_ += DeltaTime;

	if (curTime_ < 0)
		return initTransform_;

	lastTransform_ = EvaluateAtCurTime(curTime_, Settings);
	return lastTransform_;
}

FTransform PathAnimPlaybackHelper::ComputeTransformAtTime(float TimelineTime, const PathAnimPlaybackSettings& Settings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PathAnimPlaybackHelper_ComputeTransformAtTime);
	if (!keyFrames_.IsValid() || keyFrames_->NeedsUpdate())
		return initTransform_;

	const float curTime = TimelineTime + startTime_ - Settings.Delay;
	if (curTime < 0)
		return initTransform_;

	return EvaluateAtCurTime(curTime, Settings);
}

FTransform PathAnimPlaybackHelper::EvaluateAtCurTime(float curTime, const PathAnimPlaybackSettings& Settings) const
{
	// Take repeat mode into account to compute the current animation time.
	float animTime(curTime);
	float duration(keyFrames_->GetTotalTime());
	bool bReverse(Settings.bReverse);
	if (Settings.repeatMode == EITwinAnimPathRepeatMode::None)
	{
		if (curTime > duration)
		{
			animTime = duration;
		}
	}
	else if (curTime > duration)
	{
		animTime = FMath::Fmod(curTime, duration);
		if (Settings.repeatMode == EITwinAnimPathRepeatMode::PingPong)
		{
			int32 cycle = FMath::FloorToInt(curTime / duration);
			if (cycle % 2 == 1)
				bReverse = !bReverse;
		}
	}

	// Modulate current time with speed variation if any.
	if (speedController_.speedVarInfo.size() > 0 && speedController_.laneSpeed > 0.f)
	{
		float speedVarTime = speedController_.repeatAfter > 0.f ? std::fmod(animTime, speedController_.repeatAfter) : animTime;
		// If the current time falls within the speed variation cycle, compute the current time with speed variation.
		if (speedVarTime > speedController_.speedVarInfo.front().startTime &&
			speedVarTime < speedController_.speedVarInfo.back().startTime + speedController_.speedVarInfo.back().duration)
		{
			for (auto& speedVar : speedController_.speedVarInfo)
			{
				if (speedVarTime <= speedVar.startTime)
					break;
				animTime += speedVar.speedDelta * std::min(speedVar.duration, speedVarTime - speedVar.startTime) / speedController_.laneSpeed;
			}
		}
	}
	return keyFrames_->GetTransform(animTime, Settings.bNeedAlignmentFix, bReverse);
}

void PathAnimPlaybackHelper::SetKeyFrames(UBakedAnimKeyFrames* keyFramesPtr)
{
	keyFrames_ = keyFramesPtr;
	curTime_ = 0.f;
}

void PathAnimPlaybackHelper::SetStartTransform(FTransform StartTransform)
{
	initTransform_ = StartTransform;
	lastTransform_ = StartTransform;
}

void PathAnimPlaybackHelper::ResetAnimation(float Delay,
	std::optional<float> StartTime/* = std::nullopt*/,
	std::optional<FTransform> StartTransform/* = std::nullopt*/)
{
	if (StartTime.has_value())
		startTime_ = StartTime.value();
	if (StartTransform.has_value())
		initTransform_ = StartTransform.value();

	curTime_ = startTime_ - Delay;
	lastTransform_ = initTransform_;
}

int32 PathAnimPlaybackHelper::GetLaneIndex() const
{
	if (keyFrames_.IsValid())
		return keyFrames_->GetLaneIndex();
	return 0;
}

// Speed variation for the object along the path. It is defined by a set of time intervals
// where the object speed will be different from the lane speed.
// Here maxDeltaDist is the maximum distance that the object can cover when accelerating or decelerating 
// without coming too close to the object in front of him or behind him.
void PathAnimPlaybackHelper::InitSpeedVariation(float laneSpeed, float maxDeltaDist)
{
	// Define min/max delta speed that can be applied to the lane speed for the objects whose speed will be variating.
	float minDeltaSpeed(0.1f * laneSpeed);
	float maxDeltaSpeed(0.5f * laneSpeed);

	// Initialize time intervals (start time and duration) where object speed will be different from the lane speed;
	// these changes will be applied in a loop.
	speedController_.speedVarInfo.resize(3 + FMath::RoundToInt(FMath::FRand() * 15));
	speedController_.laneSpeed = laneSpeed;
	float prevEndTime(0.f);
	for (int32 i(0); i < speedController_.speedVarInfo.size(); i++)
	{
		speedController_.speedVarInfo[i].startTime = prevEndTime + 1.f + FMath::FRand() * 4.f;
		speedController_.speedVarInfo[i].duration = 1.f + FMath::FRand() * (maxDeltaDist / minDeltaSpeed);
		prevEndTime = speedController_.speedVarInfo[i].startTime + speedController_.speedVarInfo[i].duration;
	}
	speedController_.repeatAfter = prevEndTime + FMath::FRand() * 3.f;

	// Generate random speed variations for all the time intervals except the last one.

	// Maximum distance that this object can cover when accelerating without coming too close to the object in front of him.
	float deltaDistToNext = maxDeltaDist;
	// Maximum distance that this object can cover when decelerating without coming too close to the object behind him.
	float deltaDistToPrev = maxDeltaDist;
	for (int32 i(0); i < speedController_.speedVarInfo.size() - 1; i++)
	{
		// Alternate accelerations and slow downs depending on the distance to the next and previous objects.
		float duration = speedController_.speedVarInfo[i].duration;
		float deltaSpeed = (deltaDistToNext >= deltaDistToPrev) ? FMath::FRand() * std::min(maxDeltaSpeed, deltaDistToNext / duration)
			: -FMath::FRand() * std::min(maxDeltaSpeed, deltaDistToPrev / duration);
		deltaDistToNext -= duration * deltaSpeed;
		deltaDistToPrev += duration * deltaSpeed;
		speedController_.speedVarInfo[i].speedDelta = deltaSpeed;
	}
	// Last interval: return the object to its initial position once the speed variation cycle is finished
	// (to avoid object overlapping in consequent cycles).
	speedController_.speedVarInfo.back().speedDelta = (deltaDistToNext - maxDeltaDist) / speedController_.speedVarInfo.back().duration;
}

void PathAnimPlaybackHelper::RemoveSpeedVariation()
{
	speedController_.speedVarInfo.clear();
	speedController_.laneSpeed = 0.f;
	speedController_.repeatAfter = 0.f;
}