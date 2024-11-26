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

#include <ITwinCesium3DTileset.h>
#include <ITwinCesiumFeatureIdSet.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumModelMetadata.h>
#include <ITwinCesiumPrimitiveFeatures.h>

#include <CesiumGltf/ExtensionITwinMaterialID.h>
#include <CesiumGltf/MeshPrimitive.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <StaticMeshResources.h>

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

namespace
{
    ITwinElementID FeatureIDToITwinID(const FITwinCesiumPropertyTableProperty* pProperty, const int64 FeatureID)
    {
        return ITwinElementID(
            UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
                    *pProperty,
                    FeatureID),
                ITwin::NOT_ELEMENT.value()));
    }

    template <typename E>
    constexpr typename std::underlying_type<E>::type to_underlying(E e) {
        return static_cast<typename std::underlying_type<E>::type>(e);
    }
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
    auto KnownT = SceneMapping.KnownTiles.try_emplace(tileId, FITwinSceneTile{});
    FITwinSceneTile& sceneTile = KnownT.first->second;
    sceneTile.bNewMeshesToAnimate = true;//irrelevant if !s_bMaskTilesUntilFullyAnimated

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
    // Note: geoloc must have been set before, MeshComponent->GetComponentTransform depends on it!
    auto const& Transform = MeshComponent->GetComponentTransform();

    // always look in 1st set (_FEATURE_ID_0)
    const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
    const FITwinCesiumPrimitiveFeatures& Features(CesiumData.Features);
    constexpr int MetaDataNamesCount = 3;
    std::array<FString, MetaDataNamesCount> MetaDataNames = { 
        ITwinCesium::Metada::ELEMENT_NAME, 
        ITwinCesium::Metada::SUBCATEGORY_NAME, 
        ITwinCesium::Metada::MODEL_NAME 
    };
    std::array<const FITwinCesiumPropertyTableProperty*, MetaDataNamesCount> pProperties;
    for (int i = 0; i < MetaDataNamesCount; i++)
    {
        pProperties[i] = 
            UITwinCesiumMetadataPickingBlueprintLibrary::FindValidProperty(
                Features,
                CesiumData.Metadata,
                MetaDataNames[i],
                FeatureIDSetIndex);
    }
    if (pProperties[to_underlying(EPropertyType::Element)] == nullptr)
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

    bool bHasAddedMaterialToSceneTile = false;
    std::unordered_map<ITwinFeatureID, ITwinElementID> FeatureToElemID;
    const uint32 NumVertices = PositionBuffer.GetNumVertices();
    FITwinElement Dummy;
    FITwinElement* pElemStruct = &Dummy;
    ITwinElementID LastElem = ITwin::NOT_ELEMENT;
    ITwinFeatureID LastFeature = ITwin::NOT_FEATURE;
    for (uint32 vtxIndex(0); vtxIndex < NumVertices; vtxIndex++)
    {
        const int64 FeatureID =
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                FeatureIdSet,
                static_cast<int64>(vtxIndex));
        if (FeatureID < 0)
            continue;

        const ITwinFeatureID ITwinFeatID = ITwinFeatureID(FeatureID);
        if (ITwinFeatID != LastFeature) // almost always the same => optimize
        {
            auto Known = FeatureToElemID.try_emplace(ITwinFeatID, ITwinElementID{});
            ITwinElementID& ElementID = Known.first->second;
            LastFeature = ITwinFeatID;
            // only record a given feature once (obviously, many vertices belong to a same feature...)
            if (Known.second) // was inserted -> first time encountered
            {
                if (ITwinFeatID > sceneTile.MaxFeatureID || sceneTile.MaxFeatureID == ITwin::NOT_FEATURE)
                {
                    sceneTile.MaxFeatureID = ITwinFeatID;
                }
                // fetch the ElementID corresponding to this feature
                ElementID = FeatureIDToITwinID(pProperties[to_underlying(EPropertyType::Element)], FeatureID);
                if (ElementID != ITwin::NOT_ELEMENT)
                {
                    if (ElementID != LastElem) // actually the case 99% of the time
                    {
                        // fetch the CategoryID and ModelID corresponding to this feature
                        ITwinElementID CategoryID =
                            FeatureIDToITwinID(pProperties[to_underlying(EPropertyType::Category)], FeatureID);
                        CategoryID--;
                        ITwinElementID const ModelID =
                            FeatureIDToITwinID(pProperties[to_underlying(EPropertyType::Model)], FeatureID);
                        SceneMapping.CategoryIDToElementIDs[CategoryID].insert(ElementID);
                        SceneMapping.ModelIDToElementIDs[ModelID].insert(ElementID);
                    }
                    // There can be duplicates here (as several primitives can have the
                    // same features in a given tile) so we filter them here.
                    // TODO_JDE: We should profile a bit, and see if we can use a set optimized for small
                    // sizes? (Note: flat_set is based on std::vector by default, and its ordering requirement
                    // probably makes it slower than a mere vector for our use case)
                    FITwinElementFeaturesInTile& TileData = sceneTile.ElementFeatures(ElementID)->second;
                    if (std::find(TileData.Features.begin(), TileData.Features.end(), ITwinFeatID)
                        == TileData.Features.end())
                    {
                        TileData.Features.push_back(ITwinFeatID);
                    }
                    // The material is always different (each primitive uses its own material instance).
                    TileData.Materials.push_back(pMaterial);

                    if (!bHasAddedMaterialToSceneTile)
                    {
                        sceneTile.Materials.push_back(pMaterial);
                        bHasAddedMaterialToSceneTile = true;
                    }
                }
            }
            LastElem = ElementID;
        }
        // update BBox with each new vertex position
        if (LastElem != ITwin::NOT_ELEMENT)
        {
            // Almost always the same => optimize
            if (LastElem != pElemStruct->Id)
                pElemStruct = &SceneMapping.ElementFor(LastElem);
            pElemStruct->bHasMesh = true;
            pElemStruct->BBox +=
                Transform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(vtxIndex)));
        }
    }
    if (SceneMapping.OnNewTileMeshBuilt)
    {
        std::set<ITwinElementID> MeshElementIDs;
        for (auto&& [_, ElemID] : FeatureToElemID)
            if (ITwin::NOT_ELEMENT != ElemID)
                MeshElementIDs.insert(ElemID);
        SceneMapping.OnNewTileMeshBuilt(tileId, std::move(MeshElementIDs), pMaterial, KnownT.second, sceneTile);
    }
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

#if ITWIN_ALLOW_REPLACE_BASE_MATERIAL()

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

#endif // ITWIN_ALLOW_REPLACE_BASE_MATERIAL


void FITwinSceneMappingBuilder::BeforeTileDestruction(
    const Cesium3DTilesSelection::Tile& Tile,
    USceneComponent* TileGltfComponent)
{
    // The passed component is the scene component created for the given tile (UITwinCesiumGltfComponent).
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
