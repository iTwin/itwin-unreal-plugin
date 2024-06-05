/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMappingBuilder.h"
#include "ITwinSceneMapping.h"
#include <ITwinMetadataConstants.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <StaticMeshResources.h>

#include <ITwinCesiumFeatureIdSet.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumModelMetadata.h>
#include <ITwinCesiumPrimitiveFeatures.h>

#include <CesiumGltf/MeshPrimitive.h>

#include <set>


//=======================================================================================
// class FITwinSceneMappingBuilder
//=======================================================================================

void FITwinSceneMappingBuilder::OnMeshConstructed(
    const Cesium3DTilesSelection::TileID& tileId,
    const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
    const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
    const FITwinCesiumMeshData& CesiumData)
{
    FITwinSceneTile& sceneTile = sceneMapping_.KnownTiles[tileId];
    const int64 FeatureIDSetIndex = 0; // always look in 1st set (_FEATURE_ID_0)

    const TObjectPtr<UStaticMesh> StaticMesh = MeshComponent->GetStaticMesh();
    if (!StaticMesh
        || !StaticMesh->GetRenderData()
        || !StaticMesh->GetRenderData()->LODResources.IsValidIndex(0))
    {
        checkf(false, TEXT("incomplete mesh"));
        // should not happen with the version of cesium-unreal we initially
        // used - if you get there, it's probably that we upgraded the module
        // cesium-unreal, and that there are some substantial changes in the
        // way meshes are created for Unreal!
        return;
    }

    auto MeshBox = StaticMesh->GetBoundingBox();
    auto const& Transform = MeshComponent->GetComponentTransform();
    // Update global bounding box
    if (MeshBox.IsValid)
    {
        sceneMapping_.IModelBBox += Transform.TransformPosition(MeshBox.Min);
        sceneMapping_.IModelBBox += Transform.TransformPosition(MeshBox.Max);
    }
    sceneMapping_.ModelCenter = Transform.GetTranslation();

    const FITwinCesiumPrimitiveFeatures& Features(CesiumData.Features);
    const FITwinCesiumPropertyTableProperty* pElementProperty =
        UITwinCesiumMetadataPickingBlueprintLibrary::FindValidProperty(
            Features,
            CesiumData.Metadata,
            ITwinCesium::Metada::ELEMENT_NAME,
            FeatureIDSetIndex);
    if (pElementProperty == nullptr)
    {
        return;
    }

    // Add a wrapper for this GLTF mesh: used in case we need to extract sub-parts
    // matching a given ElementID (for Synchro4D animation), or if we need to bake
    // feature IDs in its vertex UVs.
    sceneTile.GltfMeshes.emplace_back(MeshComponent, CesiumData);

    std::unordered_map<ITwinElementID, FBox>& KnownBBoxes = sceneMapping_.KnownBBoxes;

    // note that this has already been checked:
    // if no featureIDSet exists in features, pElementProperty would be
    // null...
    const FITwinCesiumFeatureIdSet& FeatureIdSet =
        UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features)[FeatureIDSetIndex];

    const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer& PositionBuffer = LODResources.VertexBuffers.PositionVertexBuffer;

    // For now, we fill the mapping for all elements found in the primitive
    // At the end, we should probably restrict this to a given set of
    // ElementIDs, depending on what we can "animate" in Synchro (?)
    // TODO_GCO/TODO_JDE

    std::set<ITwinFeatureID> recordedFeatureIDs;
    std::unordered_map<ITwinFeatureID, ITwinElementID> featureIDToElementID;
    std::set<ITwinElementID> MeshElementIDs;
    const uint32 NumVertices = PositionBuffer.GetNumVertices();
    for (uint32 vtxIndex(0); vtxIndex < NumVertices; vtxIndex++)
    {
        const int64 FeatureID =
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                FeatureIdSet,
                static_cast<int64>(vtxIndex));
        ITwinElementID ElementID = ITwin::NOT_ELEMENT;
        if (FeatureID >= 0)
        {
            // only record a given feature once (obviously, many vertices
            // belong to a same feature...)
            const ITwinFeatureID ITwinFeatID = ITwinFeatureID(FeatureID);
            if (recordedFeatureIDs.insert(ITwinFeatID).second)
            {
                // update max feature ID for tile
                if (sceneTile.MaxFeatureID == ITwin::NOT_FEATURE
                    || ITwinFeatID > sceneTile.MaxFeatureID)
                {
                    sceneTile.MaxFeatureID = ITwinFeatID;
                }
                // fetch the ElementID corresponding to this feature
                ElementID = ITwinElementID(
                    UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                        UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
                            *pElementProperty,
                            FeatureID),
                        ITwin::NOT_ELEMENT.value()));
                featureIDToElementID[ITwinFeatID] = ElementID;
                if (ElementID != ITwin::NOT_ELEMENT)
                {
                    FITwinElementFeaturesInTile& TileData = sceneTile.ElementFeatures(ElementID)->second;
                    MeshElementIDs.insert(ElementID);
                    // There can be duplicates here (as several primitives can have the
                    // same features in a given tile) so we filter them here.
                    //
                    // TODO_JDE:
                    // We should profile a bit, and see if we can use a set or flat_set
                    // instead.
                    if (std::find(
                        TileData.Features.begin(),
                        TileData.Features.end(),
                        ITwinFeatID) == TileData.Features.end())
                    {
                        TileData.Features.push_back(ITwinFeatID);
                    }
                    // The material is always different (each primitive uses its own
                    // material instance).
                    TileData.Materials.push_back(pMaterial);
                }
            }
            else
            {
                check(featureIDToElementID.find(ITwinFeatID) != featureIDToElementID.end());
                ElementID = featureIDToElementID[ITwinFeatID];
            }

            if (ElementID != ITwin::NOT_ELEMENT)
            {
                // update BBox
                FBox& ElementBBox = KnownBBoxes[ElementID];
                ElementBBox += Transform.TransformPosition(
                    FVector3d(PositionBuffer.VertexPosition(vtxIndex)));
            }
        }
    }
    sceneMapping_.OnNewTileMeshFromBuilder(tileId, sceneTile, MeshElementIDs);
}

bool FITwinSceneMappingBuilder::ShouldAllocateUVForFeatures() const
{
    // For now, we always allocate this extra layer, used in BakeFeatureIDsInVertexUVs.
    // We could condition it on the presence of Synchro4D schedules, *but* its seems that
    // it has no impact at all on the buffers actually allocated by Unreal, which always
    // allocates spaces for MAX_STATIC_TEXCOORDS (VtxBuffer.GetNumTexCoords() always
    // returns MAX_STATIC_TEXCOORDS...)
    return true;
}


FITwinSceneMappingBuilder::FITwinSceneMappingBuilder(FITwinSceneMapping& sceneMapping)
    : sceneMapping_(sceneMapping) {

}

