/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CesiumMeshBuildCallbacks.h>

class AITwinIModel;
class FITwinSceneMapping;

class FITwinSceneMappingBuilder : public ICesiumMeshBuildCallbacks
{
	using Super = ICesiumMeshBuildCallbacks;
public:
	FITwinSceneMappingBuilder(AITwinIModel& IModel);

	virtual void OnMeshConstructed(Cesium3DTilesSelection::Tile& Tile,
		UStaticMeshComponent& MeshComponent, UMaterialInstanceDynamic& Material,
		FCesiumMeshData const& CesiumData) override;

	virtual void OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile) override;

	virtual void OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible) override;

	virtual void BeforeTileDestruction(
		const Cesium3DTilesSelection::Tile& Tile,
		USceneComponent* TileGltfComponent) override;

	enum class EPropertyType : uint8
	{
		Element,
		Category,
		Model,
		Geometry,
		Material
	};

	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(Cesium3DTilesSelection::Tile const& Tile,
		UStaticMeshComponent const& MeshComponent, CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial, FCesiumModelMetadata const& Metadata,
		FCesiumPrimitiveFeatures const& Features, UObject* InOuter, FName const& Name) override;

	virtual void TuneMaterial(
		CesiumGltf::Material const& glTFmaterial,
		CesiumGltf::MaterialPBRMetallicRoughness const& pbr,
		UMaterialInstanceDynamic* pMaterial,
		EMaterialParameterAssociation association,
		int32 index) const override;

	/// Create a dummy mapping composed of just one tile using one material. Introduced to apply materials
	/// from the Material Library to arbitrary meshes.
	static void BuildFromNonCesiumMesh(FITwinSceneMapping& SceneMapping,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		uint64_t ITwinMaterialID);

private:
	void SelectSchedulesBaseMaterial(UStaticMeshComponent const& MeshComponent,
		UMaterialInterface*& pBaseMaterial, FCesiumModelMetadata const& Metadata,
		FCesiumPrimitiveFeatures const& Features) const;

	AITwinIModel& IModel;
};
