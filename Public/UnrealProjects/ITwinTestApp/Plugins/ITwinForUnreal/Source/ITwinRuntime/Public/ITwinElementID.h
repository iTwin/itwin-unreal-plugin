/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinElementID.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/StrongTypes/Skills.h>
	// Included directly here and not only where it seems to be needed (eg. ITwinSceneMapping.h)
	// otherwise it wouldn't compile! (with incomplete messages from Visual so...)
	#include <BeHeaders/StrongTypes/TaggedValue_hash.h>
	#include <BeHeaders/StrongTypes/TaggedValueFW.h>
	#include <unordered_set>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

/// IModel Element ID type as stored in Cesium tiles metadata. Note that an ElementID is unique inside a
/// given iModel but not in general inside the iTwin.
DEFINE_STRONG_UINT64(ITwinElementID);

/// iModel materials are referenced with the same kind of identifiers (for example we access their properties
/// through Rpc with a function 'getElementProps').
/// Named with 'RenderMaterial' as they actually refer to RenderMaterial class.
/// See https://www.itwinjs.org/reference/core-backend/elements/rendermaterialelement/
/// We should really avoid confusion with materials saved within the Decoration Service
/// (AdvViz::SDK::ITwinMaterial).
DEFINE_STRONG_UINT64(ITwinRenderMaterialElementID);
/// Identifies a material as defined in the Decoration Service. By default, with the 'Identity' material
/// mapping, the material ID is the same as the one of the iModel material it is based on, but it can be
/// different when using a 'Custom' mapping.
DEFINE_STRONG_UINT64(ITwinMaterialID);

class FString;

namespace ITwin
{
	/// Zero is defined as the invalid id: https://www.itwinjs.org/v2/learning/common/id64/
	constexpr ITwinElementID NOT_ELEMENT{ 0 };
	/// Zero is not a valid material id either, *but* is used as default value for parts using a default
	/// material), so it's preferable to use a distinct value for NOT_MATERIAL:
	constexpr ITwinRenderMaterialElementID NOT_IMODEL_MATERIAL{ 0xFFFFFFFFFFFFFFFF };
	constexpr ITwinMaterialID NOT_MATERIAL{ 0xFFFFFFFFFFFFFFFF };

	// ITwinIModel.cpp
	[[nodiscard]] ITwinElementID ParseElementID(FString FromStr);
	[[nodiscard]] FString ToString(ITwinElementID const& Elem);
	ITWINRUNTIME_API void IncrementElementID(FString& ElemStr);
	[[nodiscard]] std::unordered_set<ITwinElementID> InsertParsedIDs(const std::vector<std::string>& inputIds);
}
