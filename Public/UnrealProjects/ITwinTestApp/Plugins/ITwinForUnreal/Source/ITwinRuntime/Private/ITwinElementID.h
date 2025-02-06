/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinElementID.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/StrongTypes/Skills.h>
	// Included directly here and not only where it seems to be needed (eg. ITwinSceneMapping.h)
	// otherwise it wouldn't compile! (with incomplete messages from Visual so...)
	#include <BeHeaders/StrongTypes/TaggedValue_hash.h>
	#include <BeHeaders/StrongTypes/TaggedValueFW.h>
#include <Compil/AfterNonUnrealIncludes.h>

/// IModel Element ID type as stored in Cesium tiles metadata. Note that an ElementID is unique inside a
/// given iModel but not in general inside the iTwin.
DEFINE_STRONG_UINT64(ITwinElementID);

class FString;

namespace ITwin
{
	/// Zero is defined as the invalid id: https://www.itwinjs.org/v2/learning/common/id64/
	constexpr ITwinElementID NOT_ELEMENT{ 0 };

	ITwinElementID ParseElementID(FString FromStr); // SchedulesImport.cpp
	FString ToString(ITwinElementID const& Elem); // ITwinIModel.cpp
}
