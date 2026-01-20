/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialDefaultTexturesHolder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Material/ITwinMaterialDefaultTexturesHolder.h>
#include <Core/ITwinAPI/ITwinMaterial.h>
#include <Engine/Texture2D.h>
#include <UObject/ConstructorHelpers.h>
#include <Core/Tools/Assert.h>

UITwinMaterialDefaultTexturesHolder::UITwinMaterialDefaultTexturesHolder()
{
	// Fetch default textures for material tuning. Note that they are provided by the Cesium plugin.
	// Use a structure to hold one-time initialization like in #UITwinSynchro4DSchedules's ctor
	struct FConstructorStatics {
		ConstructorHelpers::FObjectFinder<UTexture2D> NoColor;
		ConstructorHelpers::FObjectFinder<UTexture2D> NoNormal;
		ConstructorHelpers::FObjectFinder<UTexture2D> NoMetallicRoughness;
		FConstructorStatics()
			: NoColor(TEXT("/ITwinForUnreal/Textures/NoColorTexture"))
			, NoNormal(TEXT("/ITwinForUnreal/Textures/NoNormalTexture"))
			, NoMetallicRoughness(TEXT("/ITwinForUnreal/Textures/NoMetallicRoughnessTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	this->NoColorTexture = ConstructorStatics.NoColor.Object;
	this->NoNormalTexture = ConstructorStatics.NoNormal.Object;
	this->NoMetallicRoughnessTexture = ConstructorStatics.NoMetallicRoughness.Object;
}


UTexture2D* UITwinMaterialDefaultTexturesHolder::GetDefaultTextureForChannel(AdvViz::SDK::EChannelType Channel) const
{
	switch (Channel)
	{
	case AdvViz::SDK::EChannelType::Color:
	case AdvViz::SDK::EChannelType::Alpha:
	case AdvViz::SDK::EChannelType::Transparency:
	case AdvViz::SDK::EChannelType::AmbientOcclusion:
		// Btw the 'NoColorTexture' is used as default texture for more than just color...
		// You can see it in CesiumGlTFFunction (and MF_CesiumGlTF) material function
		return NoColorTexture;

	case AdvViz::SDK::EChannelType::Normal:
		return NoNormalTexture;

	case AdvViz::SDK::EChannelType::Metallic:
	case AdvViz::SDK::EChannelType::Roughness:
		return NoMetallicRoughnessTexture;

	default:
		BE_ISSUE("No default texture for channel", (uint8_t)Channel);
		return NoColorTexture;
	}
}

