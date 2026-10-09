/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesetLifecycleEventReceiver.h>
#include <Tasks/Task.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "ITwinSceneMappingBuilder.generated.h"

class AITwinIModel;
class TSceneMappingPtr;
struct FCesiumModelMetadata;
struct FCesiumPrimitiveFeatures;

namespace CesiumGltf {
	struct Model;
	struct MeshPrimitive;
}

namespace LoadGltfResult {
	struct LoadedPrimitiveResult;
}

/// Pre-fetched ITwin feature data from worker thread
struct FITwinPreFetchedPrimitiveData
{
	std::unordered_set<int64> UniqueFeatureIDs;
	
	struct FFeatureData
	{
		uint64 ElementID = 0;
		uint64 CategoryID = 0;
		uint64 ModelID = 0;
		uint64 MaterialID = 0;
		uint8 GeometryID = 0;
		bool bHasGeometry = false;
	};
	std::unordered_map<int64, FFeatureData> FeatureDataMap;
	bool bHasData = false;
	
	// Store the model pointer to detect if model was replaced
	const CesiumGltf::Model* pOriginalModel = nullptr;
};

UCLASS()
class UITwinSceneMappingBuilder : public UObject, public ICesium3DTilesetLifecycleEventReceiver
{
	GENERATED_BODY()
public:
	void SetIModel(AITwinIModel& InIModel);

	void PreFetchPrimitiveData(const CesiumGltf::Model& Model, const CesiumGltf::MeshPrimitive& Primitive, int32 MeshIndex, int32 PrimitiveIndex);

	void OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim) override;
	void OnTileLoaded(ICesiumLoadedTile& Tile) override;
	void OnTileVisibilityChanged(ICesiumLoadedTile& Tile, bool visible) override;
	void OnTileUnloading(ICesiumLoadedTile& Tile) override;
	UMaterialInstanceDynamic* CreateMaterial(ICesiumLoadedTilePrimitive& TilePrim,
		UMaterialInterface* pDefaultBaseMaterial, FName const& Name) override;
	void CustomizeMaterial(ICesiumLoadedTilePrimitive& TilePrim,
		UMaterialInstanceDynamic& Material, const UCesiumMaterialUserData* pCesiumData,
		CesiumGltf::Material const& glTFmaterial) override;

	/// Create a dummy mapping composed of just one tile using one material.
	static void BuildFromNonCesiumMesh(TSceneMappingPtr& SceneMapping,
		UStaticMeshComponent& MeshComponent,
		uint64_t ITwinMaterialID);

private:
	void SelectSchedulesBaseMaterial(
		UStaticMeshComponent const& MeshComponent, UMaterialInterface*& pBaseMaterial,
		FCesiumModelMetadata const& Metadata, FCesiumPrimitiveFeatures const& Features) const;

	using FPrimitiveKey = std::pair<int32, int32>; // (MeshIndex, PrimitiveIndex)
	
	std::unordered_map<FPrimitiveKey, FITwinPreFetchedPrimitiveData,
		decltype([](const auto& x) { return std::hash<int32>{}(x.first) ^ (std::hash<int32>{}(x.second) << 1); })> PreFetchedPrimitiveData;
	std::mutex PreFetchedDataMutex;

	FITwinPreFetchedPrimitiveData TakePreFetchedData(int32 MeshIndex, int32 PrimitiveIndex);

	static void OnPrimitivePreFetchFromGltfTuner(const CesiumGltf::Model& Model, const CesiumGltf::MeshPrimitive& Primitive, int32_t MeshIndex, int32_t PrimitiveIndex, void* UserData);

	AITwinIModel* IModel = nullptr;
};
