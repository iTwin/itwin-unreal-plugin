/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTimelineClipMapping.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

namespace ITwin::Timeline
{
	//! Map a global sequence time to (clip index, local clip time), using clip start times
	//! with a trailing end time: [c0Start, c1Start, ..., end].
	//! Returns {-1, 0} when out of range or invalid input.
	inline std::pair<int, float> GetClipIdxAndLocalTimeFromStartTimes(
		const TArray<float>& StartTimes,
		float SeqTime)
	{
		if (SeqTime < 0.f || StartTimes.Num() < 2)
			return { -1, 0.f };

		for (int32 i = 1; i < StartTimes.Num(); ++i)
		{
			if (SeqTime < StartTimes[i])
				return { i - 1, SeqTime - StartTimes[i - 1] };
		}
		return { -1, 0.f };
	}
}
