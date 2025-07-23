/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinModelType.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Engine/Blueprint.h>

#include <string>
#include <utility>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Assert.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

//! A "model" designates any dataset that can be loaded by the plugin.
UENUM(BlueprintType)
enum class EITwinModelType : uint8
{
	IModel,
	RealityData,
	AnimationKeyframe,
	Scene,

	Count UMETA(Hidden),
	Invalid = Count UMETA(Hidden)
};

namespace ITwin
{
	using ModelLink = std::pair<EITwinModelType, FString>;
	using ModelDecorationIdentifier = ModelLink;


	// For communication with the decoration service:

	inline std::string ModelTypeToString(EITwinModelType ModelType)
	{
		switch (ModelType)
		{
		case EITwinModelType::IModel:				return "iModel";
		case EITwinModelType::RealityData:			return "RealityData";
		case EITwinModelType::AnimationKeyframe:	return "AnimationKeyframe";

		case EITwinModelType::Invalid:
			BE_ISSUE("invalid model type");
			break;
		default:
			BE_ISSUE("Unknown model type", static_cast<uint8_t>(ModelType));
			break;
		}
		return {};
	}

	inline EITwinModelType StrToModelType(std::string const& Str, bool bAssertIfInvalid = true)
	{
		if (Str == "iModel")
			return EITwinModelType::IModel;
		else if (Str == "RealityData")
			return EITwinModelType::RealityData;
		else if (Str == "AnimationKeyframe")
			return EITwinModelType::AnimationKeyframe;
		if (bAssertIfInvalid)
		{
			BE_ISSUE("Unknown model type", Str);
		}
		return EITwinModelType::Invalid;
	}

	[[nodiscard]] inline bool GetModelType(std::string const& str, EITwinModelType& OutModelType)
	{
		OutModelType = StrToModelType(str, false);
		return OutModelType != EITwinModelType::Invalid;
	}

}
