/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCesiumMeshBuildCallbacks.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/TileID.h>
#include <UObject/WeakObjectPtr.h>

#include <optional>
#include <unordered_map>

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class USceneComponent;
struct FITwinCesiumModelMetadata;
struct FITwinCesiumPrimitiveFeatures;
struct FStaticMeshLODResources;

namespace CesiumGltf {
	struct MeshPrimitive;
} // namespace CesiumGltf
namespace Cesium3DTilesSelection {
	class Tile;
}
using FITwinCesiumToUnrealTexCoordMap = std::unordered_map<int32_t, uint32_t>;


class ITWINCESIUMRUNTIME_API ICesiumMeshBuildCallbacks
{
public:
	ICesiumMeshBuildCallbacks();
	virtual ~ICesiumMeshBuildCallbacks();

	struct FITwinCesiumMeshData
	{
		const CesiumGltf::MeshPrimitive* pMeshPrimitive;
		const FITwinCesiumModelMetadata& Metadata;
		const FITwinCesiumPrimitiveFeatures& Features;
		FITwinCesiumToUnrealTexCoordMap& GltfToUnrealTexCoordMap;
	};

	/**
	* Called at the end of the static mesh component construction.
	*/
	virtual void OnMeshConstructed(
		const Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumMeshData) = 0;

	/**
	* Called before a tile is destroyed (when it is unloaded, typically).
	*/
	virtual void BeforeTileDestruction(
		const Cesium3DTilesSelection::Tile& Tile,
		USceneComponent* TileGltfComponent) = 0;

	virtual uint32 BakeFeatureIDsInVertexUVs(std::optional<uint32> featuresAccessorIndex,
		FITwinCesiumMeshData const& CesiumMeshData, FStaticMeshLODResources& LODResources) const = 0;

	/**
	* Creates a material instance for the given primitive.
	*/
	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(
		CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial,
		UObject* InOuter,
		FName const& Name);

private:
	static TSharedPtr<ICesiumMeshBuildCallbacks> Singleton;
};

