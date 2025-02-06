/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCesiumMeshBuildCallbacks.h>

class AITwinIModel;

class FITwinSceneMappingBuilder : public ICesiumMeshBuildCallbacks
{
	using Super = ICesiumMeshBuildCallbacks;
public:
	FITwinSceneMappingBuilder(AITwinIModel& IModel);

	virtual void OnMeshConstructed(
		Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumData) override;

	virtual void OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile) override;

	virtual void OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible) override;

	virtual void BeforeTileDestruction(
		const Cesium3DTilesSelection::Tile& Tile,
		USceneComponent* TileGltfComponent) override;

	virtual std::optional<uint32> BakeFeatureIDsInVertexUVs(std::optional<uint32> featuresAccessorIndex,
		FITwinCesiumMeshData const& CesiumMeshData,
		bool duplicateVertices,
		TArray<FStaticMeshBuildVertex>& vertices,
		TArray<uint32> const& indices) const override;

	enum class EPropertyType : uint8
	{
		Element,
		Category,
		Model,
		Geometry
	};

	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(
		CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial,
		UObject* InOuter,
		FName const& Name) override;

	virtual void TuneMaterial(
		CesiumGltf::Material const& glTFmaterial,
		CesiumGltf::MaterialPBRMetallicRoughness const& pbr,
		UMaterialInstanceDynamic* pMaterial,
		EMaterialParameterAssociation association,
		int32 index) const override;

private:
	AITwinIModel& IModel;
};
