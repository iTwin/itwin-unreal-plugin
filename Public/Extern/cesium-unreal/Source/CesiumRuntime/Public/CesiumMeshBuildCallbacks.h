/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumMeshBuildCallbacks.cpp $
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
struct FCesiumModelMetadata;
struct FCesiumPrimitiveFeatures;

namespace CesiumGltf {
	struct MeshPrimitive;
	struct Material;
	struct MaterialPBRMetallicRoughness;
} // namespace CesiumGltf
namespace Cesium3DTilesSelection {
	class Tile;
}
using FCesiumToUnrealTexCoordMap = std::unordered_map<int32_t, uint32_t>;


class CESIUMRUNTIME_API ICesiumMeshBuildCallbacks
{
public:
	ICesiumMeshBuildCallbacks();
	virtual ~ICesiumMeshBuildCallbacks();

	/// TODO_GCO: All could be accessed from the UITwinCesiumGltfPrimitiveComponent (or its Outer
	/// UCesiumGltfComponent, in the case of Metadata), except that both classes are Module-private.
	struct FCesiumMeshData
	{
		const CesiumGltf::MeshPrimitive* pMeshPrimitive;
		const FCesiumModelMetadata& Metadata;
		const FCesiumPrimitiveFeatures& Features;
		FCesiumToUnrealTexCoordMap& GltfToUnrealTexCoordMap;
	};

	/**
	* Called at the end of the static mesh component construction.
	*/
	virtual void OnMeshConstructed(
		Cesium3DTilesSelection::Tile& Tile,
		UStaticMeshComponent& MeshComponent,
		UMaterialInstanceDynamic& pMaterial,
		FCesiumMeshData const& CesiumMeshData) = 0;

	/**
	* Called at the end of all static mesh components' construction for a given tile.
	*/
	virtual void OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile) = 0;

	/**
	* Called when changing the visibility of any UCesiumGltfComponent, ie usually several times per
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
	* Creates a material instance for the given primitive.
	* pMeshPrimitive, Metadata and Features could be found inside MeshComponent if it could be passed as
	*	UITwinCesiumGltfPrimitiveComponent, but the class is Module-private (see similar situation with
	*	FITwinCesiumMeshData above).
	*/
	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(Cesium3DTilesSelection::Tile const& Tile,
		UStaticMeshComponent const& MeshComponent, CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial, FCesiumModelMetadata const& Metadata,
		FCesiumPrimitiveFeatures const& Features, UObject* InOuter, FName const& Name);

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

