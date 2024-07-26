/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGltfMeshComponentWrapper.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCesiumMeshBuildCallbacks.h>
#include "Components.h"
#include "MaterialTypes.h"

#include <ITwinElementID.h>
#include <ITwinFeatureID.h>

#include <optional>
#include <unordered_map>


class FITwinSceneTile;
struct FITwinExtractedEntity;
struct FITwinCesiumPropertyTableProperty;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;


struct FITwinMeshExtractionOptions
{
    /// Whether the extracted component should use a distinct material instance
    bool bCreateNewMaterialInstance = false;
    bool bSetupMatForTileTexturesNow = false;
    /// When a new material instance is to be created, we may supply the base material
    /// to use for this creation. If none is provided, we will use the base material of
    /// the material instance used by the original mesh.
    UMaterialInterface* BaseMaterialForNewInstance = nullptr;
    std::optional<std::pair<FMaterialParameterInfo, float>> ScalarParameterToSet;

    /// Mostly for debugging purpose: assign a random color depending on the Element ID.
    bool bPerElementColorationMode = false;
};


/// Holds a pointer to a GLTF Mesh Component created by Cesium when loading primitives.
/// It was introduced in order to be able to extract new mesh components from this mesh
/// depending on the FeatureID, and also to add per-vertex UVs if needed
class FITwinGltfMeshComponentWrapper
{
public:
    FITwinGltfMeshComponentWrapper(
        const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
        const ICesiumMeshBuildCallbacks::FITwinCesiumMeshData& CesiumData);


    /// Extract faces matching the given element, if any, as a new Unreal mesh.
    /// \return true if a sub-mesh was actually extracted.
    bool ExtractElement(
        ITwinElementID const Element,
        FITwinExtractedEntity& ExtractedEntity,
        FITwinMeshExtractionOptions const& Options = {});

    /// Returns whether meta-data has been parsed for this primitive.
    bool HasParsedMetaData() const {
        return metadataStatus_.has_value();
    }

    /// Returns whether the given element ID has been detected while parsing meta-data.
    /// \remark if meta-data has never been parsed (see HasParsedMetaData), this will
    /// will never been the case.
    bool HasDetectedElementID(ITwinElementID const ElementID) const;

    bool CanExtractElement(ITwinElementID const ElementID);

    /// Bake FeatureID in a vertex UV channel
    /// \return the UV channel index used to fill the information, or nullopt if meta-
    /// data was not available or does not contain features.
    std::optional<uint32> BakeFeatureIDsInVertexUVs();

    /// Returns whether the mesh component holds the feature IDs baked in its per-vertex
    /// UV coordinates.
    bool HasBakedFeatureIDsInVertexUVs() const {
        return uvIndexForFeatures_.has_value();
    }

    /// Hide the original GLTF mesh component on/off.
    /// Can be used for debugging.
    void HideOriginalMeshComponent(bool bHide = true);

    /// Extract a given percentage of elements (for debugging).
    /// \return Number of elements newly extracted.
    uint32 ExtractSomeElements(
        FITwinSceneTile& SceneTile,
        float Percentage,
        FITwinMeshExtractionOptions const& Options = {});

    UStaticMeshComponent const* GetMeshComponent() const {
        return gltfMeshComponent_.IsValid() ? gltfMeshComponent_.Get() : nullptr;
    }

private:

    static const uint32 INVALID_TRIANGLE_INDEX = static_cast<uint32>(-1);

    /// Very simplified version of FStaticMeshSection
    struct FSimpleStaticMeshSection
    {
        uint32 FirstIndex = 0; // start index (in the index buffer)
        uint32 NumTriangles = 0;
        /// If all polygons in the section share the same FeatureID, this will hold it
        std::optional<ITwinFeatureID> CommonFeatureID;

        void Invalidate() {
            FirstIndex = INVALID_TRIANGLE_INDEX;
        }
        bool IsValid() const {
            return FirstIndex != INVALID_TRIANGLE_INDEX;
        }
    };

    struct FPropertyTableAccess
    {
        const FITwinCesiumPropertyTableProperty* Prop = nullptr;
        int64 FeatureIDSetIndex = 0;
    };


    enum class EMetadataStatus
    {
        InvalidMesh,
        MissingMetadata,
        UnsortedMetadata,
        SortedByElement,
    };

    /// Checks source GLTF mesh component validity.
    TObjectPtr<UStaticMesh> GetSourceStaticMesh() const;
    /// Retrieve the table property corresponding to ITwin Element ID.
    const FITwinCesiumPropertyTableProperty* FetchElementProperty(int64& FeatureIDSetIndex) const;
    bool GetElementPropertyAccess(FPropertyTableAccess& Access) const;

    /// Depending on the state of meta-data/features, the original mesh can be split in
    /// sections depending on the ITwin ElementID (optimized case) or not.
    EMetadataStatus ComputeMetadataStatus();

    void InitExtractedMeshComponent(
        FITwinExtractedEntity& ExtractedEntity,
        FName const& MeshName) const;
    bool FinalizeExtractedEntity(
        FITwinExtractedEntity& ExtractedEntity,
        FName const& MeshName,
        ITwinElementID const EltID,
        TArray<FStaticMeshBuildVertex> const& BuildVertices,
        TArray<uint32> const& Indices,
        TObjectPtr<UStaticMesh> const& SrcStaticMesh,
        FITwinMeshExtractionOptions const& Options,
        std::optional<uint32> const& UVIndexForFeatures) const;

    inline void CheckFeatureIDUniqueness(
        FSimpleStaticMeshSection& curSection,
        const ITwinFeatureID& FeatureID) const;

    /// Extract faces matching the given element, using a section already computed
    /// before (optimized case)
    bool ExtractMeshSectionElement(
        ITwinElementID const Element,
        FSimpleStaticMeshSection const& MeshSection,
        FITwinExtractedEntity& ExtractedEntity,
        FITwinMeshExtractionOptions const& Options) const;

    /// Extract faces matching the given element, using the non-optimized mode.
    bool ExtractElement_SLOW(
        ITwinElementID const Element,
        uint32 const EvalNumTriangles,
        FITwinExtractedEntity& ExtractedEntity,
        FITwinMeshExtractionOptions const& Options) const;

private:
    /// Original mesh component created by Cesium plugin.
    TWeakObjectPtr<UStaticMeshComponent> gltfMeshComponent_;

    /// raw pointers owned by the UCesiumGltfComponent
    /// => never access them without testing the validity of MeshGltfComponent
    FITwinCesiumModelMetadata const* pMetadata_ = nullptr;
    FITwinCesiumPrimitiveFeatures const* pFeatures_ = nullptr;
    FITwinCesiumToUnrealTexCoordMap* pGltfToUnrealTexCoordMap_ = nullptr;

    /// Result of splitting the mesh by element
    /// If faces are sorted by ElementID (which is the case in the first examples we had,
    /// but it probably not guaranteed, we can cache the result of the splitting in the
    /// form of sections, making it faster to extract a given element without having to
    /// access features & meta-data again and again).
    std::optional<EMetadataStatus> metadataStatus_ = std::nullopt;

    using ElementSectionMap = std::unordered_map<ITwinElementID, FSimpleStaticMeshSection>;
    ElementSectionMap elementSections_;

    /// Contains the accessor index matching features, if any.
    /// (see CesiumGltf::MeshPrimitive::attributes member documentation)
    std::optional<uint32> featuresAccessorIndex_ = std::nullopt;
    /// If we bake feature IDs in per-vertex UVs, this will store the
    /// corresponding UV index.
    std::optional<uint32> uvIndexForFeatures_ = std::nullopt;
};

