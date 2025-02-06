/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCesiumMeshBuildCallbacks.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/TileID.h>
#include <Components.h>
#include <UObject/WeakObjectPtr.h>

#include <optional>
#include <unordered_map>

class UMaterialInstanceDynamic;
class UMaterialInterface;
enum EMaterialParameterAssociation : int;
class UStaticMeshComponent;
class USceneComponent;
struct FITwinCesiumModelMetadata;
struct FITwinCesiumPrimitiveFeatures;

namespace CesiumGltf {
	struct MeshPrimitive;
	struct Material;
	struct MaterialPBRMetallicRoughness;
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
		Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumMeshData) = 0;

	/**
	* Called at the end of all static mesh components' construction for a given tile.
	*/
	virtual void OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile) = 0;

	/**
	* Called when changing the visibility of any UITwinCesiumGltfComponent, ie usually several times per
	* tile (when the tileset selection leads to showing or hiding a whole tile).
	*/
	virtual void OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible) = 0;

	/**
	* Called before a tile is destroyed (when it is unloaded, typically).
	*/
	virtual void BeforeTileDestruction(
		const Cesium3DTilesSelection::Tile& Tile,
		USceneComponent* TileGltfComponent) = 0;

	/**
	* Bakes feature IDs in next free slot of vertex UVs, if the primitive actually contains the attribute
	* '_FEATURE_ID_0' dedicated to such features. Feature IDs will be filled in the first component of those
	* UVs (ie. the 'u' component).
	* Returns the UV slot actually filled, if any.
	*/
	virtual std::optional<uint32> BakeFeatureIDsInVertexUVs(std::optional<uint32> featuresAccessorIndex,
		FITwinCesiumMeshData const& CesiumMeshData,
		bool duplicateVertices,
		TArray<FStaticMeshBuildVertex>& vertices,
		TArray<uint32> const& indices) const = 0;

	/**
	* Creates a material instance for the given primitive.
	*/
	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(
		CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial,
		UObject* InOuter,
		FName const& Name);

	/**
	* Tune the Unreal material instance, depending on the glTF material definition.
	*/
	virtual void TuneMaterial(
		const CesiumGltf::Material& glTFmaterial,
		const CesiumGltf::MaterialPBRMetallicRoughness& pbr,
		UMaterialInstanceDynamic* pMaterial,
		EMaterialParameterAssociation association,
		int32 index) const;

private:
	static TSharedPtr<ICesiumMeshBuildCallbacks> Singleton;
};

