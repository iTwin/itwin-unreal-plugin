/*--------------------------------------------------------------------------------------+
|
|     $Source: BakedAnimKeyFrames.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Containers/Array.h>
//#include <Containers/Map.h>
//#include <GameFramework/Actor.h>
#include <Math/MathFwd.h>
#include <Misc/EnumRange.h>
#include <UObject/Object.h>
//#include <Templates/PimplPtr.h>
//#include <Spline/ITwinSplineHelper.h>

#include <optional>

#include "BakedAnimKeyFrames.generated.h"

class UWorld;

namespace AdvViz::SDK
{
	class RefID;
}

enum class EBakedKeyFramesStatus : uint8
{
	Invalid,
	NeedsUpdate,
	InProgress,
	Ready
};


UCLASS()
class UBakedAnimKeyFrames : public UObject
{
	GENERATED_BODY()
public:
	UBakedAnimKeyFrames() {}

	void MarkForUpdate();
	bool NeedsUpdate() const;
	bool IsReady() const;

	float GetTotalTime() const;
	float GetTotalLength() const;
	int32 GetLaneIndex() const;
	void SetSpeed(float InSpeed);
	// Helper to get the speed this keyframe data is baked for (in cm/s)
	float GetSpeed() const { return TotalLength > 0.f && TotalTime > 0.f ? TotalLength / TotalTime : 0.f; }
	
	void BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed, int32 InLaneIdx, float InOffset/*corresponds to the offset of the lane*/);
	int32 GetKeyframeIndex(float Time);
	FTransform GetTransform(float Time, bool bNeedAlignmentFix, bool bReverse);
	void CopyFromData(int32 InLaneIdx, float InSpeed, float InTotalLength, const TArray<FTransform>& InTransforms);

	static float GetSamplingStep(bool HasCurvedIntervals, bool IsMultilane)
	{
		// For lanes with offset, we use a smaller step to ensure we trace a smooth curve
		// along the central spline (otherwise it might lead to visual inconsistencies 
		// with multi-lane paths at sharp turns)
		return HasCurvedIntervals && IsMultilane ? 0.2f*BakedFramesStep : BakedFramesStep;
	}
	static float GetBakedFramesStep() { return BakedFramesStep; }
private:
	TArray<FTransform> Transforms;
	float TotalLength = 0.f;
	float TotalTime = 0.f;
	static float BakedFramesStep;
	int32 LaneIdx = 0;
	EBakedKeyFramesStatus Status = EBakedKeyFramesStatus::Invalid;
};
