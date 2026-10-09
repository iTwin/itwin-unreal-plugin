/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClipping3DTilesetHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClipping3DTilesetHelper.h>

#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingModelGroups.h>
#include <Clipping/ITwinClippingRenderer.h>
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
	if (InClippingTool)
	{
		ClippingRenderer = InClippingTool->GetRenderer();
	}
	else
	{
		ClippingRenderer = nullptr;
	}
}

void UITwinClipping3DTilesetHelper::SetCutoutOverlay(const UCesiumPolygonRasterOverlay* InPolygonRasterOverlay)
{
	CutoutOverlayPtr = InPolygonRasterOverlay;
}

bool UITwinClipping3DTilesetHelper::UpdateCPDFlagsFromClippingSelection(FITwinClippingModelGroups const& ModelGroups)
{
	const float NewId = static_cast<float>(ModelGroups.GetGroupId(ModelIdentifier));
	if (std::fabs(ScalarClippingModelGroupId - NewId) > 0.5f)
	{
		ScalarClippingModelGroupId = NewId;
		return true;
	}
	return false;
}

void UITwinClipping3DTilesetHelper::ApplyCPDFlagsToMeshComponent(UPrimitiveComponent& Component) const
{
	// the following index (0) is defined in ITwin/Materials/MF_GlobalClipping.uasset
	Component.SetCustomPrimitiveDataFloat(0, ScalarClippingModelGroupId);
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
			(FVector const& Position, uint32/*FaceIndex*/, uint32, uint32, uint32 /*VertexIndex A, B and C*/)
			{
				return !ShouldCutOut(MeshComponent.GetComponentTransform().TransformPosition(Position));
			});
	}
#endif

}

bool UITwinClipping3DTilesetHelper::ShouldCutOut(FVector const& AbsoluteWorldPosition) const
{
	return ClippingRenderer.IsValid()
		&& ClippingRenderer->ShouldCutOut(AbsoluteWorldPosition, ModelIdentifier, CutoutOverlayPtr.Get());
}
