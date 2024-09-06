/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMappingBuilder.h"

#include "ITwinIModel.h"
#include "ITwinSceneMapping.h"
#include <ITwinMetadataConstants.h>
#include <ITwinGeolocation.h>
#include <Math/UEMathExts.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <StaticMeshResources.h>

#include <ITwinCesium3DTileset.h>
#include <ITwinCesiumFeatureIdSet.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumModelMetadata.h>
#include <ITwinCesiumPrimitiveFeatures.h>

#include <CesiumGltf/ExtensionITwinMaterialID.h>
#include <CesiumGltf/MeshPrimitive.h>

#include <Materials/MaterialInstanceDynamic.h>

#include <set>

// Activate this to test the retrieval of iTwin material IDs by overriding the base color
// on a per material ID basis.
#define DEBUG_ITWIN_MATERIAL_IDS() 0


#if DEBUG_ITWIN_MATERIAL_IDS()
namespace
{
    // Temporary code to test material IDs
    static std::unordered_map<UMaterialInstanceDynamic*, FLinearColor> materialColorOverrides;
}
#endif

// From FITwinVecMath::createMatrix
FMatrix CreateMatrixFromGlm(const glm::dmat4& m) noexcept {
  return FMatrix(
      FVector(m[0].x, m[0].y, m[0].z),
      FVector(m[1].x, m[1].y, m[1].z),
      FVector(m[2].x, m[2].y, m[2].z),
      FVector(m[3].x, m[3].y, m[3].z));
}

//=======================================================================================
// class FITwinSceneMappingBuilder
//=======================================================================================

void FITwinSceneMappingBuilder::OnMeshConstructed(
    const Cesium3DTilesSelection::Tile& Tile,
    const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
    const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
    const FITwinCesiumMeshData& CesiumData)
{
    auto const tileId = Tile.getTileID();
    FITwinSceneTile& sceneTile = SceneMapping.KnownTiles[tileId];
    const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT; // always look in 1st set (_FEATURE_ID_0)

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

#if DEBUG_ITWIN_MATERIAL_IDS()
    auto const itBaseColorOverride = materialColorOverrides.find(pMaterial.Get());
    if (itBaseColorOverride != materialColorOverrides.end())
    {
        pMaterial->SetVectorParameterValueByInfo(
            FMaterialParameterInfo(
                "baseColorFactor",
                EMaterialParameterAssociation::GlobalParameter,
                INDEX_NONE),
            itBaseColorOverride->second);
        pMaterial->SetVectorParameterValueByInfo(
            FMaterialParameterInfo(
                "baseColorFactor",
                EMaterialParameterAssociation::LayerParameter,
                0),
            itBaseColorOverride->second);
    }
#endif // DEBUG_ITWIN_MATERIAL_IDS

    AITwinCesium3DTileset* Tileset = nullptr;
    // MeshComponent is a UITwinCesiumGltfPrimitiveComponent, a USceneComponent which 'AttachParent' is
    // a UITwinCesiumGltfComponent, which Owner is the cesium tileset, thus:
    if (ensure(MeshComponent->GetAttachParent()))
    {
        Tileset = Cast<AITwinCesium3DTileset>(MeshComponent->GetAttachParent()->GetOwner());
    }
    if (!SceneMapping.CesiumToUnrealTransform && ensure(Tileset))
    {
        SceneMapping.CesiumToUnrealTransform.emplace(FTransform(CreateMatrixFromGlm(
            Tileset->GetCesiumTilesetToUnrealRelativeWorldTransform())));
    }
    if (!SceneMapping.ModelCenter_ITwin)
    {
        // the ModelCenter, as used in 3DFT plugin, can be retrieved by fetching the translation of the
        // root tile
        // here again, please do not change this code without testing saved views in 3DFT level...
        auto const* rootTile = &Tile;
        while (rootTile->getParent() != nullptr)
        {
            rootTile = rootTile->getParent();
        }
        auto const& tsfTranslation = rootTile->getTransform()[3];
        SceneMapping.ModelCenter_ITwin.emplace(tsfTranslation[0], tsfTranslation[1], tsfTranslation[2]);
        if (ensure(SceneMapping.CesiumToUnrealTransform && !SceneMapping.ModelCenter_UE))
        {
            SceneMapping.ModelCenter_UE.emplace(
                SceneMapping.CesiumToUnrealTransform->TransformPosition(*SceneMapping.ModelCenter_ITwin));
        }
    }
    // Note: geoloc must have been set before, MeshComponent->GetComponentTransform depends on it!
    auto MeshBox = StaticMesh->GetBoundingBox();
    auto const& Transform = MeshComponent->GetComponentTransform();
    // Update global bounding box, both in Unreal and "ITwin" coordinate system
    // Beware the code for ITwin coordinate system should not be modified without testing saved views with
    // the default level of the former 3DFT plugin!
    if (MeshBox.IsValid)
    {
        SceneMapping.IModelBBox_UE += Transform.TransformPosition(MeshBox.Min);
        SceneMapping.IModelBBox_UE += Transform.TransformPosition(MeshBox.Max);

        // For iTwin coordinate system: swap Y with Z (compensate what is done previously)
        std::swap(MeshBox.Min.Y, MeshBox.Min.Z);
        std::swap(MeshBox.Max.Y, MeshBox.Max.Z);
        SceneMapping.IModelBBox_ITwin += MeshBox.Min;
        SceneMapping.IModelBBox_ITwin += MeshBox.Max;
    }

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
    // note that this has already been checked:
    // if no featureIDSet exists in features, pElementProperty would be null...
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
                    sceneTile.Materials.push_back(pMaterial);
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
                auto& Elem = SceneMapping.ElementFor(ElementID);
                Elem.bHasMesh = true;
                Elem.BBox += Transform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(vtxIndex)));
            }
        }
    }
    if (SceneMapping.OnNewTileMeshBuilt)
        SceneMapping.OnNewTileMeshBuilt(tileId, std::move(MeshElementIDs));
}

uint32 FITwinSceneMappingBuilder::BakeFeatureIDsInVertexUVs(std::optional<uint32> featuresAccessorIndex,
	ICesiumMeshBuildCallbacks::FITwinCesiumMeshData const& CesiumMeshData,
    FStaticMeshLODResources& LODResources) const
{
    return FITwinGltfMeshComponentWrapper::BakeFeatureIDsInVertexUVs(
        featuresAccessorIndex, CesiumMeshData, LODResources);
}

FITwinSceneMappingBuilder::FITwinSceneMappingBuilder(FITwinSceneMapping& InSceneMapping,
                                                     AITwinIModel& InIModel)
    : SceneMapping(InSceneMapping)
    , IModel(InIModel)
{
}

UMaterialInstanceDynamic* FITwinSceneMappingBuilder::CreateMaterial_GameThread(
    CesiumGltf::MeshPrimitive const* pMeshPrimitive,
    UMaterialInterface*& pBaseMaterial,
    UObject* InOuter,
    FName const& Name) {
    std::optional<uint64_t> iTwinMaterialID;
    if (pMeshPrimitive)
    {
        auto const* matIdExt = pMeshPrimitive->getExtension<CesiumGltf::ExtensionITwinMaterialID>();
        if (matIdExt)
            iTwinMaterialID = matIdExt->materialId;
    }
    if (IModel.ShouldFillMaterialInfoFromTuner())
    {
        // Map of iTwin materials not yet ready: see if we have loaded the list from tileset.json
        IModel.FillMaterialInfoFromTuner();
    }
    UMaterialInterface* CustomBaseMaterial = nullptr;
    if (iTwinMaterialID)
    {
        auto itCustomMat = IModel.CustomMaterials.Find(*iTwinMaterialID);
        if (ensureMsgf(itCustomMat, TEXT("iTwin Material 0x%I64x not parsed from tileset.json"), *iTwinMaterialID))
        {
            CustomBaseMaterial = itCustomMat->Material.Get();
            if (CustomBaseMaterial)
                pBaseMaterial = CustomBaseMaterial;
        }
    }
    UMaterialInstanceDynamic* pMat = Super::CreateMaterial_GameThread(pMeshPrimitive, pBaseMaterial, InOuter, Name);
    if (iTwinMaterialID && !CustomBaseMaterial)
    {
#if DEBUG_ITWIN_MATERIAL_IDS()
        // Temporary code to visualize iTwin material IDs
        const auto MatID = *iTwinMaterialID;
        static std::unordered_map<uint64_t, FLinearColor> matIDColorMap;
        FLinearColor matColor;
        auto const itClr = matIDColorMap.find(MatID);
        if (itClr != matIDColorMap.end())
        {
            matColor = itClr->second;
        }
        else
        {
            matColor = (*iTwinMaterialID == 0) ? FLinearColor::White : FLinearColor::MakeRandomColor();
            matColor.A = 1.;
            matIDColorMap.emplace(MatID, matColor);
        }
        materialColorOverrides.emplace(pMat, matColor);
#endif // DEBUG_ITWIN_MATERIAL_IDS
    }
    return pMat;
}

void FITwinSceneMappingBuilder::BeforeTileDestruction(
    const Cesium3DTilesSelection::Tile& Tile,
    USceneComponent* TileGltfComponent)
{
    // The passed component is the scene component cerated for the given tile (UITwinCesiumGltfComponent).
    // Its children are the primitive components (UITwinCesiumGltfPrimitiveComponent), on which we point in
    // FITwinGltfMeshComponentWrapper => remove any wrapper pointing on the components about to be freed.
    // Note that they may not exist in the mapping, typically in case we have to apply some tuning, or for
    // unsupported primitives types (UITwinCesiumGltfPointsComponent.
    auto itSceneTile = SceneMapping.KnownTiles.find(Tile.getTileID());
    if (itSceneTile == SceneMapping.KnownTiles.end())
    {
        return;
    }
    auto const& PrimComponentPtrs = TileGltfComponent->GetAttachChildren();
    // To raw (const) pointers for use in Find
    TArray<USceneComponent const*> PrimComponents;
    PrimComponents.Reserve(PrimComponentPtrs.Num());
    Algo::Transform(PrimComponentPtrs, PrimComponents,
        [](TObjectPtr<USceneComponent> const& V) -> USceneComponent const*
    { return V.Get(); }
    );

    auto& GltfMeshes = itSceneTile->second.GltfMeshes;
    GltfMeshes.erase(
        std::remove_if(
            GltfMeshes.begin(), GltfMeshes.end(),
            [&](FITwinGltfMeshComponentWrapper const& itwinWrapper)
    {
        return PrimComponents.Find(itwinWrapper.GetMeshComponent()) != INDEX_NONE;
    }),
        GltfMeshes.end());
}
