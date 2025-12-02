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
	#include <unordered_set>
#include <Compil/AfterNonUnrealIncludes.h>

/// IModel Element ID type as stored in Cesium tiles metadata. Note that an ElementID is unique inside a
/// given iModel but not in general inside the iTwin.
DEFINE_STRONG_UINT64(ITwinElementID);

/// IModel materials are referenced with the same kind of identifiers (for example we access their properties
/// through Rpc with a function 'getElementProps').
DEFINE_STRONG_UINT64(ITwinMaterialID);

class FString;

namespace ITwin
{
	/// Zero is defined as the invalid id: https://www.itwinjs.org/v2/learning/common/id64/
	constexpr ITwinElementID NOT_ELEMENT{ 0 };
	/// Zero is not a valid material id either, *but* is used as default value for parts using a default
	/// material), so it's preferable to use a distinct value for NOT_MATERIAL:
	constexpr ITwinMaterialID NOT_MATERIAL{ 0xFFFFFFFFFFFFFFFF };

	// ITwinIModel.cpp
	[[nodiscard]] ITwinElementID ParseElementID(FString FromStr);
	[[nodiscard]] FString ToString(ITwinElementID const& Elem);
	ITWINRUNTIME_API void IncrementElementID(FString& ElemStr);
	[[nodiscard]] std::unordered_set<ITwinElementID> InsertParsedIDs(const std::vector<std::string>& inputIds);
}
