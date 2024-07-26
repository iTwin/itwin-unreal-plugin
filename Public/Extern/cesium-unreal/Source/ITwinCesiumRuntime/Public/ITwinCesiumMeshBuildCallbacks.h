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
#include <glm/mat4x4.hpp>
#include <unordered_map>

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
struct FITwinCesiumModelMetadata;
struct FITwinCesiumPrimitiveFeatures;

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
	* Whether an extra UV layer should be allocated for feature IDs.
	*/
	virtual bool ShouldAllocateUVForFeatures() const = 0;

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

