/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingCustomPrimitiveDataHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>

#include <Clipping/ITwinClippingTool.h>

#include <IncludeCesium3DTileset.h>

#include <Components/StaticMeshComponent.h>

// If we increment the maximum number of planes or boxes, we must synchronize the encoding of their
// activation in the Custom Primitive Data (see the CPD parameters defined in MF_GlobalClipping)
static_assert(ITwin::MAX_CLIPPING_PLANES <= 32, "");
static_assert(ITwin::MAX_CLIPPING_BOXES <= 32, "");


void UITwinClippingCustomPrimitiveDataHelper::SetModelIdentifier(const ITwin::ModelLink& InModelIdentifier)
{
	ensureMsgf(ModelIdentifier == ITwin::ModelLink() || ModelIdentifier == InModelIdentifier,
		TEXT("Once set, the model identifier should be constant over time"));
	ModelIdentifier = InModelIdentifier;
}

bool UITwinClippingCustomPrimitiveDataHelper::UpdateCPDFlagsFromClippingSelection(AITwinClippingTool const& ClippingTool)
{
	const int32 NumPlanes = ClippingTool.NumEffects(EITwinClippingPrimitiveType::Plane);
	int ActivePlanes_0_15 = 0;
	for (int32 i = 0; i < std::min(16, NumPlanes); i++)
	{
		if (ClippingTool.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Plane, i, ModelIdentifier))
			ActivePlanes_0_15 |= (1 << i);
	}

	int ActivePlanes_16_31 = 0;
	for (int32 i = 0; i < std::min(16, NumPlanes - 16); i++)
	{
		if (ClippingTool.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Plane, 16 + i, ModelIdentifier))
			ActivePlanes_16_31 |= (1 << i);
	}

	const int32 NumBoxes = ClippingTool.NumEffects(EITwinClippingPrimitiveType::Box);
	int ActiveBoxes_0_15 = 0;
	for (int32 i = 0; i < std::min(16, NumBoxes); i++)
	{
		if (ClippingTool.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Box, i, ModelIdentifier))
			ActiveBoxes_0_15 |= (1 << i);
	}

	int ActiveBoxes_16_31 = 0;
	for (int32 i = 0; i < std::min(16, NumBoxes - 16); i++)
	{
		if (ClippingTool.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Box, 16 + i, ModelIdentifier))
			ActiveBoxes_16_31 |= (1 << i);
	}
	bool bModified = false;
	auto const updateScalar = [&bModified](float& DstScalar, int SrcValue)
	{
		if (std::fabs(DstScalar - static_cast<float>(SrcValue)) > 0.5f)
		{
			DstScalar = static_cast<float>(SrcValue);
			bModified = true;
		}
	};
	updateScalar(ScalarActivePlanes_0_15, ActivePlanes_0_15);
	updateScalar(ScalarActivePlanes_16_31, ActivePlanes_16_31);
	updateScalar(ScalarActiveBoxes_0_15, ActiveBoxes_0_15);
	updateScalar(ScalarActiveBoxes_16_31, ActiveBoxes_16_31);

	return bModified;
}

void UITwinClippingCustomPrimitiveDataHelper::ApplyCPDFlagsToMeshComponent(UPrimitiveComponent& Component) const
{
	// the following indices (0, 1, 2, 3) are defined in ITwin/Materials/MF_GlobalClipping.uasset
	Component.SetCustomPrimitiveDataFloat(0, ScalarActivePlanes_0_15);
	Component.SetCustomPrimitiveDataFloat(1, ScalarActivePlanes_16_31);
	Component.SetCustomPrimitiveDataFloat(2, ScalarActiveBoxes_0_15);
	Component.SetCustomPrimitiveDataFloat(3, ScalarActiveBoxes_16_31);
}

void UITwinClippingCustomPrimitiveDataHelper::ApplyCPDFlagsToAllMeshComponentsInTileset(ACesium3DTileset const& Tileset)
{
	TArray<UMeshComponent*> GltfMeshes;
	Tileset.GetComponents<UMeshComponent>(GltfMeshes, true);
	for (UMeshComponent* MeshComponent : GltfMeshes)
	{
		ApplyCPDFlagsToMeshComponent(*MeshComponent);
	}
}

void UITwinClippingCustomPrimitiveDataHelper::OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim)
{
	ApplyCPDFlagsToMeshComponent(TilePrim.GetMeshComponent());
}
