/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelperInternals.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

namespace ITwin::SequencerHelper
{
	/// Picks Count evenly spaced indices within [0, Size-1], first and last included.
	/// Linear interpolation on the index, rounded to a real index: the caller therefore
	/// always ends up using values that actually exist in the source array.
	/// The result is strictly increasing as long as Count <= Size.
	TArray<int32> ComputeEvenlySpacedIndices(int32 Count, int32 Size);
}
