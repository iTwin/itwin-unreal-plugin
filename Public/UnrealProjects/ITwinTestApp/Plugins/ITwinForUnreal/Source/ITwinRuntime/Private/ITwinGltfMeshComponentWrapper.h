/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGltfMeshComponentWrapper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesetLifecycleEventReceiver.h>
#include "Components.h"
#include "MaterialTypes.h"

#include <ITwinElementID.h>
#include <ITwinFeatureID.h>

#include <optional>
#include <unordered_map>

using FCesiumToUnrealTexCoordMap = std::unordered_map<int32_t, uint32_t>;

class FITwinSceneTile;
struct FITwinExtractedEntity;
struct FCesiumPropertyTableProperty;
class ICesiumLoadedTilePrimitive;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;


/// Holds a pointer to a GLTF Mesh Component created by Cesium when loading primitives.
/// It was introduced in order to be able to extract new mesh components from this mesh
/// depending on the FeatureID, and also to add per-vertex UVs if needed
class FITwinGltfMeshComponentWrapper
{
public:
	FITwinGltfMeshComponentWrapper(UStaticMeshComponent& MeshComponent, uint64_t ITwinMaterialID);
	FITwinGltfMeshComponentWrapper(ICesiumLoadedTilePrimitive& TilePrim,
								   std::optional<uint32> uvIndexForFeatures);

    /// Get the known uvIndexForFeatures_, the UV channel index where FeatureIDs have been baked.
	/// \return the UV channel index used to fill the information, or nullopt if meta-
	///	 data was not available or does not contain features.
    std::optional<uint32> GetFeatureIDsInVertexUVs() const;

	/// Returns whether the mesh component holds the feature IDs baked in its per-vertex
	/// UV coordinates.
	bool HasBakedFeatureIDsInVertexUVs() const {
		return uvIndexForFeatures_.has_value();
	}

	/// Hide the original GLTF mesh component on/off.
	/// Can be used for debugging.
	void HideOriginalMeshComponent(bool bHide = true);

	UStaticMeshComponent const* GetMeshComponent() const {
		return gltfMeshComponent_.IsValid() ? gltfMeshComponent_.Get() : nullptr;
	}
	UStaticMeshComponent* MeshComponent() {
		return gltfMeshComponent_.IsValid() ? gltfMeshComponent_.Get() : nullptr;
	}

	std::optional<uint64_t> const& GetITwinMaterialIDOpt() const { return iTwinMaterialID_; }
	bool HasITwinMaterialID(uint64_t MatID) const { return iTwinMaterialID_ && *iTwinMaterialID_ == MatID; }

	/// Apply Func to all material instances linked to this mesh.
	void ForEachMaterialInstance(std::function<void(UMaterialInstanceDynamic&)> const& Func);


private:

	static const uint32 INVALID_TRIANGLE_INDEX = static_cast<uint32>(-1);

	/// Checks source GLTF mesh component validity.
	TObjectPtr<UStaticMesh> GetSourceStaticMesh() const;


private:
	/// Original mesh component created by Cesium plugin.
	TWeakObjectPtr<UStaticMeshComponent> gltfMeshComponent_;

	/// If we bake feature IDs in per-vertex UVs, this will store the
	/// corresponding UV index.
	std::optional<uint32> uvIndexForFeatures_ = std::nullopt;
	/// Contains the iTwin material ID corresponding to the primitive, if any (ie. when some material tuning
	/// was requested, and thus the gltf tuner split the result agains this material ID).
	std::optional<uint64_t> iTwinMaterialID_ = std::nullopt;
};

