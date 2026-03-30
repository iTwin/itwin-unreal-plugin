/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClipping3DTilesetHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClipping3DTilesetHelper.h>

#include <Clipping/ITwinClippingTool.h>
#include <Compil/IsUsingBentleyUnreal.h>
#include <Helpers/WorldSingleton.h>
#include <IncludeCesium3DTileset.h>
#include <ITwinTilesetAccess.h>

#include <CesiumPolygonRasterOverlay.h>

#include <Components/StaticMeshComponent.h>

#if BE_IS_USING_BENTLEY_UNREAL
#	include <Chaos/TriangleMeshImplicitObject.h>
#	include <PhysicsEngine/BodySetup.h>
#endif

// If we increment the maximum number of planes or boxes, we must synchronize the encoding of their
// activation in the Custom Primitive Data (see the CPD parameters defined in MF_GlobalClipping)
static_assert(ITwin::MAX_CLIPPING_PLANES <= 32, "");
static_assert(ITwin::MAX_CLIPPING_BOXES <= 32, "");


UITwinClipping3DTilesetHelper::UITwinClipping3DTilesetHelper()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SetClippingTool(
			TWorldSingleton<AITwinClippingTool>().Get(GetWorld()));
	}
}

void UITwinClipping3DTilesetHelper::InitWith(FITwinTilesetAccess const& TilesetAccess)
{
	SetModelIdentifier(TilesetAccess.GetDecorationKey());
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
	{
		SetCutoutOverlay(ITwin::GetCutoutOverlay(*Tileset));
	}
}

void UITwinClipping3DTilesetHelper::SetModelIdentifier(const ITwin::ModelLink& InModelIdentifier)
{
	ensureMsgf(ModelIdentifier == ITwin::ModelLink() || ModelIdentifier == InModelIdentifier,
		TEXT("Once set, the model identifier should be constant over time"));
	ModelIdentifier = InModelIdentifier;
}

void UITwinClipping3DTilesetHelper::SetClippingTool(const AITwinClippingTool* InClippingTool)
{
	ClippingToolPtr = InClippingTool;
}

void UITwinClipping3DTilesetHelper::SetCutoutOverlay(const UCesiumPolygonRasterOverlay* InPolygonRasterOverlay)
{
	CutoutOverlayPtr = InPolygonRasterOverlay;
}

bool UITwinClipping3DTilesetHelper::UpdateCPDFlagsFromClippingSelection(AITwinClippingTool const& ClippingTool)
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

void UITwinClipping3DTilesetHelper::ApplyCPDFlagsToMeshComponent(UPrimitiveComponent& Component) const
{
	// the following indices (0, 1, 2, 3) are defined in ITwin/Materials/MF_GlobalClipping.uasset
	Component.SetCustomPrimitiveDataFloat(0, ScalarActivePlanes_0_15);
	Component.SetCustomPrimitiveDataFloat(1, ScalarActivePlanes_16_31);
	Component.SetCustomPrimitiveDataFloat(2, ScalarActiveBoxes_0_15);
	Component.SetCustomPrimitiveDataFloat(3, ScalarActiveBoxes_16_31);
}

void UITwinClipping3DTilesetHelper::ApplyCPDFlagsToAllMeshComponentsInTileset(ACesium3DTileset const& Tileset)
{
	TArray<UMeshComponent*> GltfMeshes;
	Tileset.GetComponents<UMeshComponent>(GltfMeshes, true);
	for (UMeshComponent* MeshComponent : GltfMeshes)
	{
		ApplyCPDFlagsToMeshComponent(*MeshComponent);
	}
}

void UITwinClipping3DTilesetHelper::OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim)
{
	ApplyCPDFlagsToMeshComponent(TilePrim.GetMeshComponent());

#if BE_IS_USING_BENTLEY_UNREAL // using BeUE <=> CMake's BE_USE_OFFICIAL_UNREAL is OFF
	auto& MeshComponent = TilePrim.GetMeshComponent();
	for (auto const& pCollisionMesh : MeshComponent.GetBodySetup()->TriMeshGeometries)
	{
		pCollisionMesh->SetTriangleHitFilter([this, &MeshComponent]
			(FVector const& /*Position*/, uint32/*FaceIndex*/, uint32, uint32, uint32 /*VertexIndex A, B and C*/)
			{
#if 0 // Still needs testing...
				return !ShouldCutOut(MeshComponent.GetComponentTransform().TransformPosition(Position));
#endif
				return true;
			});
	}
#endif

}

bool UITwinClipping3DTilesetHelper::ShouldCutOut(FVector const& AbsoluteWorldPosition) const
{
	return ClippingToolPtr.IsValid()
		&& ClippingToolPtr->ShouldCutOut(AbsoluteWorldPosition, ModelIdentifier, CutoutOverlayPtr.Get());
}
