/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCesiumMeshBuildCallbacks.h>

class AITwinIModel;
class FITwinSceneMapping;

// In initial tests, we granted the possibility to override the base material completely, in order to let us
// pick a replacement material from Unreal content browser. This will not be the final workflow (as it would
// be incompatible with Synchro4D or even selection highlights, and would also raise issues if the picked
// material is not double-sided...)
// Keep code just for debugging purpose for now.
#define ITWIN_ALLOW_REPLACE_BASE_MATERIAL() 0

class FITwinSceneMappingBuilder : public ICesiumMeshBuildCallbacks
{
	using Super = ICesiumMeshBuildCallbacks;
public:
	FITwinSceneMappingBuilder(FITwinSceneMapping& SceneMapping, AITwinIModel& IModel);

	virtual void OnMeshConstructed(
		const Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumData) override;

	virtual void BeforeTileDestruction(
		const Cesium3DTilesSelection::Tile& Tile,
		USceneComponent* TileGltfComponent) override;

	virtual uint32 BakeFeatureIDsInVertexUVs(std::optional<uint32> featuresAccessorIndex,
		FITwinCesiumMeshData const& CesiumMeshData, FStaticMeshLODResources& LODResources) const override;

#if ITWIN_ALLOW_REPLACE_BASE_MATERIAL()
	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(
		CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial,
		UObject* InOuter,
		FName const& Name) override;
#endif

private:
	FITwinSceneMapping& SceneMapping;
	AITwinIModel& IModel;
};
