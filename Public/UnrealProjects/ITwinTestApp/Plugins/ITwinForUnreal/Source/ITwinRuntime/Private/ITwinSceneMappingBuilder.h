/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesetLifecycleEventReceiver.h>

#include "ITwinSceneMappingBuilder.generated.h"

class AITwinIModel;
class FITwinSceneMapping;

UCLASS()
class UITwinSceneMappingBuilder : public UObject, public ICesium3DTilesetLifecycleEventReceiver
{
	GENERATED_BODY()
public:
	void SetIModel(AITwinIModel& InIModel);

	void OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim) override;
	void OnTileLoaded(ICesiumLoadedTile& Tile) override;
	void OnTileVisibilityChanged(ICesiumLoadedTile& Tile, bool visible) override;
	void OnTileUnloading(ICesiumLoadedTile& Tile) override;
	UMaterialInstanceDynamic* CreateMaterial(ICesiumLoadedTilePrimitive& TilePrim,
		UMaterialInterface* pDefaultBaseMaterial, FName const& Name) override;
	void CustomizeMaterial(ICesiumLoadedTilePrimitive& TilePrim,
		UMaterialInstanceDynamic& Material, const UCesiumMaterialUserData* pCesiumData,
		CesiumGltf::Material const& glTFmaterial) override;

	/// Create a dummy mapping composed of just one tile using one material. Introduced to apply materials
	/// from the Material Library to arbitrary meshes.
	static void BuildFromNonCesiumMesh(FITwinSceneMapping& SceneMapping,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		uint64_t ITwinMaterialID);

private:
	void SelectSchedulesBaseMaterial(UStaticMeshComponent const& MeshComponent,
		UMaterialInterface*& pBaseMaterial, FCesiumModelMetadata const& Metadata,
		FCesiumPrimitiveFeatures const& Features) const;

	AITwinIModel* IModel = nullptr;
};
