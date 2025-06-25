/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialParameters.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>
#include <MaterialTypes.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <array>

namespace ITwin
{

	// Cesium glTF materials usually edit 2 sets of parameters, one Global and one Layer
	// See how #SetGltfParameterValues is called in #loadPrimitiveGameThreadPart.
	struct ChannelParamInfos
	{
		ChannelParamInfos(FName const& ParamName)
			: GlobalParamInfo(ParamName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE)
			, LayerParamInfo(ParamName, EMaterialParameterAssociation::LayerParameter, 0)
		{

		}

		FMaterialParameterInfo const GlobalParamInfo;
		FMaterialParameterInfo const LayerParamInfo;
	};

	// Cache the (constant by channel) parameter info, to avoid constructing a FName hundreds of time.
	using FChannelParamInfosOpt = std::optional<ChannelParamInfos>;

	using FPerChannelParamInfos = std::array<FChannelParamInfosOpt, (size_t)AdvViz::SDK::EChannelType::ENUM_END>;

} // ITwin

