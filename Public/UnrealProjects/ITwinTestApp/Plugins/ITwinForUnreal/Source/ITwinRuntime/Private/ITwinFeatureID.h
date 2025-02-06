/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinFeatureID.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "HAL/Platform.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/StrongTypes/Skills.h>
	// Included directly here and not only where it seems to be needed (where an unordered_map/set is used)
	// otherwise it wouldn't compile! (with incomplete messages from Visual so...)
	#include <BeHeaders/StrongTypes/TaggedValue_hash.h>
	#include <BeHeaders/StrongTypes/TaggedValueFW.h>
#include <Compil/AfterNonUnrealIncludes.h>

/// Represents a FeatureID as stored in the Cesium tiles: it is actually 64bits too, but I guess we can
/// assume less than (2**32)-1 FeatureIDs per tile!
DEFINE_STRONG_UINT32(ITwinFeatureID);
namespace ITwin
{
	constexpr ITwinFeatureID NOT_FEATURE{ 0xFFFFFFFF };
}
