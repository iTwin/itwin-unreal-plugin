/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGltfMeshComponentWrapper.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinGltfMeshComponentWrapper.h"

#include "ITwinSceneMapping.h"
#include <ITwinMetadataConstants.h>

#include <ITwinExtractedMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/Material.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <StaticMeshResources.h>

#include <ITwinCesiumFeatureIdSet.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumModelMetadata.h>
#include <ITwinCesiumPrimitiveFeatures.h>

#include <CesiumGltf/ExtensionITwinMaterialID.h>
#include <CesiumGltf/MeshPrimitive.h>


//=======================================================================================
// class FITwinGltfMeshComponentWrapper
//=======================================================================================
FITwinGltfMeshComponentWrapper::FITwinGltfMeshComponentWrapper(
    const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
    const ICesiumMeshBuildCallbacks::FITwinCesiumMeshData& CesiumData)
    : gltfMeshComponent_(MeshComponent)
    , pMetadata_(&CesiumData.Metadata)
    , pFeatures_(&CesiumData.Features)
    , pGltfToUnrealTexCoordMap_(&CesiumData.GltfToUnrealTexCoordMap)
{
    if (CesiumData.pMeshPrimitive)
    {
        // Store the uvAccessor index for features, in case we need to bake them later
        // (see #BakeFeatureIDsInVertexUVs)
        auto featAccessorIt = CesiumData.pMeshPrimitive->attributes.find("_FEATURE_ID_0");
        if (featAccessorIt != CesiumData.pMeshPrimitive->attributes.end()) {
            featuresAccessorIndex_ = featAccessorIt->second;
            // test if we have already some UVs storing features per vertex:
            auto uvIndexIt = CesiumData.GltfToUnrealTexCoordMap.find(*featuresAccessorIndex_);
            if (uvIndexIt != CesiumData.GltfToUnrealTexCoordMap.cend()) {
                uvIndexForFeatures_ = uvIndexIt->second;
            }
        }

        // Test if this primitive is linked to a specific ITwin Material ID (test extension specially added
        // by our gltf tuning process.
        auto const* matIdExt = CesiumData.pMeshPrimitive->getExtension<CesiumGltf::ExtensionITwinMaterialID>();
        if (matIdExt)
        {
            iTwinMaterialID_ = matIdExt->materialId;
        }
    }
}

TObjectPtr<UStaticMesh> FITwinGltfMeshComponentWrapper::GetSourceStaticMesh() const
{
    if (!gltfMeshComponent_.IsValid()) {
        // checkf(false, TEXT("obsolete gltf mesh pointer"));
        // => this can happen, since we can now request extraction at any time, and the
        // initial Cesium tile may have been destroyed in the interval.
        return nullptr;
    }
    const TObjectPtr<UStaticMesh> StaticMesh = gltfMeshComponent_->GetStaticMesh();
    if (!StaticMesh
        || !StaticMesh->GetRenderData()
        || !StaticMesh->GetRenderData()->LODResources.IsValidIndex(0))
    {
        checkf(false, TEXT("incomplete mesh"));
        // should not happen with the version of cesium-unreal we initially
        // used - if you get there, it's probably that we upgraded the module
        // cesium-unreal, and that there are some substantial changes in the
        // way meshes are created for Unreal!
        return nullptr;
    }
    return StaticMesh;
}

const FITwinCesiumPropertyTableProperty* FITwinGltfMeshComponentWrapper::FetchElementProperty(
    int64& FeatureIDSetIndex) const
{
    if (pFeatures_ == nullptr || pMetadata_ == nullptr) {
        checkf(false, TEXT("no gltf meta-data/features"));
        return nullptr;
    }
    FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT; // always look in 1st set (_FEATURE_ID_0)

    const FITwinCesiumPropertyTableProperty* pElementProperty =
        UITwinCesiumMetadataPickingBlueprintLibrary::FindValidProperty(
            *pFeatures_,
            *pMetadata_,
            ITwinCesium::Metada::ELEMENT_NAME,
            FeatureIDSetIndex);
    if (pElementProperty == nullptr) {
        // this should not happen, only because the primitives not having any ITwin
        // ElementID should have been filtered before...
        checkf(false, TEXT("'%s' property not found in metadata"),
            *ITwinCesium::Metada::ELEMENT_NAME);
    }
    return pElementProperty;
}

bool FITwinGltfMeshComponentWrapper::GetElementPropertyAccess(FPropertyTableAccess& Access) const
{
    Access.Prop = FetchElementProperty(Access.FeatureIDSetIndex);
    return Access.Prop != nullptr;
}

inline void FITwinGltfMeshComponentWrapper::CheckFeatureIDUniqueness(
    FSimpleStaticMeshSection& curSection,
    const ITwinFeatureID& FeatureID) const
{
    checkSlow(FeatureID < std::numeric_limits<ITwinFeatureID>::max());
    if (curSection.CommonFeatureID && (*curSection.CommonFeatureID != FeatureID))
    {
        curSection.CommonFeatureID.reset();
    }
}

FITwinGltfMeshComponentWrapper::EMetadataStatus FITwinGltfMeshComponentWrapper::ComputeMetadataStatus()
{
    // Check that the pointed mesh and meta-data is still valid
    const TObjectPtr<UStaticMesh> StaticMesh = GetSourceStaticMesh();
    if (!StaticMesh) {
        return EMetadataStatus::InvalidMesh;
    }

    int64 FeatureIDSetIndex = 0;
    const FITwinCesiumPropertyTableProperty* pElementProperty =
        FetchElementProperty(FeatureIDSetIndex);
    if (pElementProperty == nullptr) {
        return EMetadataStatus::MissingMetadata;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(ITwin::Extract::ParseMetaData)


    const FITwinCesiumPropertyTableProperty& ITWIN_ElementProperty(*pElementProperty);

    // note that this has already been checked:
    // if no featureIDSet exists in features, pITWIN_ElementProperty would be
    // null...
    const FITwinCesiumFeatureIdSet& FeatureIdSet =
        UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
            *pFeatures_)[FeatureIDSetIndex];

    const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
    const FRawStaticIndexBuffer& IndexBuffer = LODResources.IndexBuffer;

    const FIndexArrayView Indices = IndexBuffer.GetArrayView();
    const int64 NumTriangles = static_cast<int64>(Indices.Num() / 3);
    ITwinElementID curEltID = ITwin::NOT_ELEMENT;
    int64 NumTrianglesInCurElement = 0;

    // If triangles are sorted by element ID, we can fasten the extraction by storing the
    // different sections once for all, which will avoid having to parse meta-data again
    // and again
    bool bUseMeshSections = true;
    FSimpleStaticMeshSection curSection;

    uint64 AccumFaces = 0; // for debugging

    auto const CommitSection = [&]()
    {
        // commit current section if it is not empty
        if (curSection.NumTriangles > 0)
        {
            if (curEltID != ITwin::NOT_ELEMENT)
            {
                // Only consider *true* Itwin Elements
                // sub-parts of the mesh without any ElementID will never be
                // extracted, obviously.
                const ITwinElementID ITwinEltID = curEltID;

                auto InsertedSection = elementSections_.try_emplace(ITwinEltID, curSection);
                if (!InsertedSection.second) // was already present
                {
                    // Not contiguous... We will not use any mesh section for this
                    // element during extraction. 
                    // Just increment the number of triangles matching current element
                    bUseMeshSections = false;
                    InsertedSection.first->second.Invalidate();
                    InsertedSection.first->second.NumTriangles += curSection.NumTriangles;
                }
            }

            AccumFaces += curSection.NumTriangles;
        }
    };

    for (int64 FaceIndex(0); FaceIndex < NumTriangles; FaceIndex++)
    {
        const uint32 vtxId0 = Indices[3 * FaceIndex];
        const int64 FeatureID = // not yet an ITwinFeatureID, which is unsigned and 32bits!
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                FeatureIdSet,
                vtxId0);
        const ITwinElementID elementID = (FeatureID < 0)
            ? ITwin::NOT_ELEMENT
            : ITwinElementID(
                UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                    UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
                        ITWIN_ElementProperty,
                        FeatureID),
                    ITwin::NOT_ELEMENT.value()));

        if (elementID == curEltID) // this most likely means FeatureID is valid
        {
            curSection.NumTriangles++;

            CheckFeatureIDUniqueness(curSection, ITwinFeatureID(FeatureID));
        }
        else
        {
            // commit current section
            CommitSection();

            // start a new section
            curSection.NumTriangles = 1;
            curSection.FirstIndex = 3 * FaceIndex; // *not* FaceIndex!
            checkSlow(FeatureID < std::numeric_limits<ITwinFeatureID>::max());
            curSection.CommonFeatureID.emplace(FeatureID);
            curEltID = elementID;
        }
    }

    // do not forget the last section!
    CommitSection();

    if (bUseMeshSections)
    {
        // all faces should have been added to sections
        checkSlow(static_cast<int64>(AccumFaces) == NumTriangles);

        return EMetadataStatus::SortedByElement;
    }
    else
    {
        return EMetadataStatus::UnsortedMetadata;
    }
}

bool FITwinGltfMeshComponentWrapper::ExtractElement(
    ITwinElementID const Element,
    FITwinExtractedEntity& ExtractedEntity,
    FITwinMeshExtractionOptions const& Options /*= {}*/)
{
    checkfSlow(Element != ITwinElementID(-1), TEXT("trying to extract invalid ElementID"));

    if (!metadataStatus_)
    {
        // first time we try to extract something => see if we can benefit from mesh
        // sections to optimize next extractions.
        metadataStatus_ = ComputeMetadataStatus();
    }

    switch (*metadataStatus_)
    {
    case EMetadataStatus::SortedByElement:
    case EMetadataStatus::UnsortedMetadata:
    {
        auto itSection = elementSections_.find(Element);
        if (itSection == elementSections_.cend())
        {
            // Nothing to do.
            return false;
        }
        if (itSection->second.IsValid())
        {
            // valid mesh section => optimized case
            return ExtractMeshSectionElement(
                Element,
                itSection->second,
                ExtractedEntity,
                Options);
        }
        else
        {
            // slower mode (parse meta-data again)
            checkSlow(*metadataStatus_ == EMetadataStatus::UnsortedMetadata);
            return ExtractElement_SLOW(
                Element,
                itSection->second.NumTriangles,
                ExtractedEntity,
                Options);
        }
    }

    case EMetadataStatus::InvalidMesh:
    case EMetadataStatus::MissingMetadata:
        return false;

    default:
        ensureMsgf(false, TEXT("unhandled case: %d"), static_cast<int>(*metadataStatus_));
        return false;
    }
}

void FITwinGltfMeshComponentWrapper::InitExtractedMeshComponent(
    FITwinExtractedEntity& ExtractedEntity,
    FName const& MeshName) const
{
    UITwinExtractedMeshComponent* pMesh = NewObject<UITwinExtractedMeshComponent>(
        gltfMeshComponent_.Get(),
        MeshName);

    // Copy some settings from the source gltf primitive mesh.
    // Note that the things we copy here are deduced from the code in
    // CesiumGltfComponent.cpp (see #loadPrimitiveGameThreadPart)
    // Ideally, I would have preferred using a method CopyAllButMeshData,
    // but I did not find it... The drawback here is that if we upgrade cesium-unreal,
    // we will probably have to report things here and in #FinalizeExtractedEntity

    pMesh->SetRelativeTransform(gltfMeshComponent_->GetRelativeTransform());

    pMesh->bUseDefaultCollision = gltfMeshComponent_->bUseDefaultCollision;
    pMesh->SetCollisionObjectType(gltfMeshComponent_->GetCollisionObjectType());
    pMesh->SetFlags(gltfMeshComponent_->GetFlags());
    pMesh->SetRenderCustomDepth(gltfMeshComponent_->bRenderCustomDepth);
    pMesh->SetCustomDepthStencilValue(gltfMeshComponent_->CustomDepthStencilValue);
    pMesh->bCastDynamicShadow = gltfMeshComponent_->bCastDynamicShadow;


    ExtractedEntity.MeshComponent = pMesh;
    ExtractedEntity.OriginalTransform = gltfMeshComponent_->GetComponentTransform();
}

bool FITwinGltfMeshComponentWrapper::FinalizeExtractedEntity(
    FITwinExtractedEntity& ExtractedEntity,
    FName const& MeshName,
    ITwinElementID const EltID,
    TArray<FStaticMeshBuildVertex> const& StaticMeshBuildVertices,
    TArray<uint32> const& Indices,
    TObjectPtr<UStaticMesh> const& SrcStaticMesh,
    FITwinMeshExtractionOptions const& Options,
    std::optional<uint32> const& UVIndexForFeatures) const
{
    if (!ExtractedEntity.MeshComponent.IsValid() || !SrcStaticMesh)
    {
        ensureMsgf(false, TEXT("mesh destroyed before finalization!"));
        return false;
    }
    if (StaticMeshBuildVertices.IsEmpty() || Indices.IsEmpty())
    {
        ensureMsgf(false, TEXT("nothing to extract"));
        return false;
    }
    const FStaticMeshLODResources& SrcLODResources = SrcStaticMesh->GetRenderData()->LODResources[0];
    const FStaticMeshVertexBuffers& SrcVertexBuffers = SrcLODResources.VertexBuffers;

    UStaticMesh* pStaticMesh = NewObject<UStaticMesh>(
        ExtractedEntity.MeshComponent.Get(), MeshName);
    ExtractedEntity.MeshComponent->SetStaticMesh(pStaticMesh);

    pStaticMesh->SetFlags(SrcStaticMesh->GetFlags());
    pStaticMesh->NeverStream = SrcStaticMesh->NeverStream;

    TUniquePtr<FStaticMeshRenderData> RenderData =
        MakeUnique<FStaticMeshRenderData>();

    {
        TRACE_CPUPROFILER_EVENT_SCOPE(ITwin::Extract::ComputeAABB)

        FBox aaBox;
        for (FStaticMeshBuildVertex const& vtx : StaticMeshBuildVertices)
        {
            aaBox += FVector3d(vtx.Position);
        }
        aaBox.GetCenterAndExtents(
            RenderData->Bounds.Origin,
            RenderData->Bounds.BoxExtent);
        RenderData->Bounds.SphereRadius = 0.0f;
    }

    // Fill mesh data
    RenderData->AllocateLODResources(1);
    FStaticMeshLODResources& LODResources = RenderData->LODResources[0];

    // Same comment as in #loadPrimitive in ITwinCesiumGltfComponent.cpp.
    // For extracted pieces, the need for mesh data on the CPU is less obvious, but we get warnings in
    // Unreal's logs if we do not activate this flag, which may cause troubles in packaged mode...
    const bool bNeedsCPUAccess = true;

    const bool hasVertexColors = SrcVertexBuffers.ColorVertexBuffer.GetNumVertices() > 0;
    LODResources.bHasColorVertexData = hasVertexColors;

    {
        LODResources.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(
            SrcVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs());

        LODResources.VertexBuffers.PositionVertexBuffer.Init(
            StaticMeshBuildVertices,
            bNeedsCPUAccess);

        FColorVertexBuffer& ColorVertexBuffer =
            LODResources.VertexBuffers.ColorVertexBuffer;
        if (hasVertexColors) {
            ColorVertexBuffer.Init(StaticMeshBuildVertices, bNeedsCPUAccess);
        }
        const uint32 NumTexCoords = UVIndexForFeatures.has_value()
            ? (UVIndexForFeatures.value() + 1)
            : SrcVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
        FStaticMeshVertexBufferFlags VtxBufferFlags;
        VtxBufferFlags.bNeedsCPUAccess = bNeedsCPUAccess;
        LODResources.VertexBuffers.StaticMeshVertexBuffer.Init(
            StaticMeshBuildVertices,
            NumTexCoords,
            VtxBufferFlags);
    }

    FStaticMeshSectionArray& Sections = LODResources.Sections;
    FStaticMeshSection& section = Sections.AddDefaulted_GetRef();
    // This will be ignored if the primitive contains points.
    section.NumTriangles = Indices.Num() / 3;
    section.FirstIndex = 0;
    section.MinVertexIndex = 0;
    section.MaxVertexIndex = StaticMeshBuildVertices.Num() - 1;
    section.bEnableCollision = true;
    section.bCastShadow = true;
    section.MaterialIndex = 0;

    LODResources.IndexBuffer.TrySetAllowCPUAccess(bNeedsCPUAccess);
    LODResources.IndexBuffer.SetIndices(
        Indices,
        StaticMeshBuildVertices.Num() >= std::numeric_limits<uint16>::max()
        ? EIndexBufferStride::Type::Force32Bit
        : EIndexBufferStride::Type::Force16Bit);

    LODResources.bHasDepthOnlyIndices = SrcLODResources.bHasDepthOnlyIndices;
    LODResources.bHasReversedIndices = SrcLODResources.bHasReversedIndices;
    LODResources.bHasReversedDepthOnlyIndices = SrcLODResources.bHasReversedDepthOnlyIndices;

    RenderData->ScreenSize[0].Default = 1.0f;

    pStaticMesh->SetRenderData(std::move(RenderData));

    // assign material
    UMaterialInstanceDynamic* SrcMaterialInstance =
        Cast<UMaterialInstanceDynamic>(
            SrcStaticMesh->GetMaterial(0));
    if (SrcMaterialInstance != nullptr)
    {
        UMaterialInstanceDynamic* MaterialToUse = SrcMaterialInstance;

        if (Options.bCreateNewMaterialInstance || Options.bPerElementColorationMode)
        {
            static uint32_t nextMaterialId = 0;
            const FName ImportedSlotName(
                *(TEXT("ITwinExtractedMaterial_") + FString::FromInt(nextMaterialId++)));

            // We may enforce the base material (for translucency, typically).
            UMaterialInterface* BaseMatForNewInstance =
                (Options.BaseMaterialForNewInstance != nullptr)
                ? Options.BaseMaterialForNewInstance
                : SrcMaterialInstance->GetBaseMaterial();
            UMaterialInstanceDynamic* pNewMaterial =
                UMaterialInstanceDynamic::Create(
                    BaseMatForNewInstance,
                    nullptr,
                    ImportedSlotName);
            pNewMaterial->CopyParameterOverrides(SrcMaterialInstance);
            if (Options.ScalarParameterToSet)
            {
                pNewMaterial->SetScalarParameterValueByInfo(Options.ScalarParameterToSet->first,
                                                            Options.ScalarParameterToSet->second);
            }
            pNewMaterial->TwoSided = true;

            if (Options.bPerElementColorationMode)
            {
                static std::unordered_map<ITwinElementID, FLinearColor> eltColorMap;
                FLinearColor matColor;
                auto InsertedClr = eltColorMap.try_emplace(EltID);
                if (InsertedClr.second) // new clr was inserted
                {
                    InsertedClr.first->second = FLinearColor::MakeRandomColor();
                    InsertedClr.first->second.A = 1.;
                }
                pNewMaterial->SetVectorParameterValueByInfo(
                    FMaterialParameterInfo(
                        "baseColorFactor",
                        EMaterialParameterAssociation::GlobalParameter,
                        INDEX_NONE),
                    InsertedClr.first->second);
                pNewMaterial->SetVectorParameterValueByInfo(
                    FMaterialParameterInfo(
                        "baseColorFactor",
                        EMaterialParameterAssociation::LayerParameter,
                        0),
                    InsertedClr.first->second);
            }
            MaterialToUse = pNewMaterial;
            if (ensure(Options.SceneTile))
                Options.SceneTile->AddMaterial(MaterialToUse);

            // The texture in the newly created material instance will have to be setup afterwards
            ExtractedEntity.TextureFlags.HighlightsAndOpacitiesFlags.Invalidate();
        }

        ExtractedEntity.Material = MaterialToUse;
        pStaticMesh->AddMaterial(MaterialToUse);
    }

    pStaticMesh->SetLightingGuid();
    pStaticMesh->InitResources();

    // Set up RenderData bounds and LOD data
    pStaticMesh->CalculateExtendedBounds();

    pStaticMesh->CreateBodySetup();

    // TODO_GCO: try to avoid crashes I had with heavy scenes only - should MeshComponent be a strong ptr??
    if (!ensure(ExtractedEntity.MeshComponent.IsValid()))
        return false;
    ExtractedEntity.MeshComponent->SetMobility(gltfMeshComponent_->Mobility);

    ExtractedEntity.MeshComponent->AttachToComponent(gltfMeshComponent_.Get(), FAttachmentTransformRules::KeepWorldTransform);
    ExtractedEntity.MeshComponent->RegisterComponent();

    // Extracted entities should *always* have their Feature IDs baked in UVs
    if (UVIndexForFeatures)
    {
        ExtractedEntity.FeatureIDsUVIndex = UVIndexForFeatures;
    }
    else if (ensure(HasBakedFeatureIDsInVertexUVs()))
    {
        ExtractedEntity.FeatureIDsUVIndex = uvIndexForFeatures_;
    }
    // Do not show the extracted entity if the source mesh is currently invisible
    if (!gltfMeshComponent_->IsVisible())
    {
        ExtractedEntity.MeshComponent->SetVisibility(false, true);
    }
    // Keep a link to the source mesh, in order to adjust the entity's visibility
    // - avoid showing the entity in the future if its source is hidden due to the 3D tileset criteria
    ExtractedEntity.SourceMeshComponent = gltfMeshComponent_;

    return true;
}

namespace
{
    class StaticMeshExtractionHelper
    {
    public:
        StaticMeshExtractionHelper(
            FStaticMeshLODResources const& SrcLODResources,
            std::optional<ITwinFeatureID> const& CommonFeatureToBakeInUVs = std::nullopt,
            std::optional<uint32> const& uvIndexForFeatures = std::nullopt);

        void ReserveArraysForNumTriangles(uint32 NumTriangles);

        inline void AddVertex(uint32 const SrcVtxId);

        void FinalizeArrays();

        TArray<FStaticMeshBuildVertex> const& GetBuildVertices() const {
            return BuildVertices;
        }
        TArray<uint32> const& GetIndices() const {
            return Indices;
        }

        inline void SetFeatureForNextVertices(ITwinFeatureID const FeatID);
        inline void SetFeatureForSourceVertex(
            uint32 const SrcVtxId,
            uint32 const UVIndex,
            int64 const FeatID64);

    private:
        // source buffers & properties
        FPositionVertexBuffer const& SrcPositions;
        FStaticMeshVertexBuffer const& SrcVertexBuffer;
        FColorVertexBuffer const& SrcVtxColors;
        uint32 SrcNumTexCoords = 0;
        bool bHasVtxData = false;
        bool bHasVertexColors = false;

        // in some cases, we also fill an extra UV layer containing the FeatureID common
        // to all triangles in the extracted mesh
        std::optional<FVector2f> FeatureToBakeInUVs = std::nullopt;
        std::optional<uint32> const UVIndexForFeature = std::nullopt;

        // result of the extraction
        TArray<FStaticMeshBuildVertex> BuildVertices;
        TArray<uint32> Indices;

        // internal data for incremental construction
        uint32 NextNewVertexIndex = 0;
        std::unordered_map<uint32, uint32> VtxIndicesMap;
    };


    StaticMeshExtractionHelper::StaticMeshExtractionHelper(
        FStaticMeshLODResources const& SrcLODResources,
        std::optional<ITwinFeatureID> const& CommonFeatureToBakeInUVs /*= std::nullopt*/,
        std::optional<uint32> const& uvIndexForFeatures /*= std::nullopt*/)
        : SrcPositions(SrcLODResources.VertexBuffers.PositionVertexBuffer)
        , SrcVertexBuffer(SrcLODResources.VertexBuffers.StaticMeshVertexBuffer)
        , SrcVtxColors(SrcLODResources.VertexBuffers.ColorVertexBuffer)
        , UVIndexForFeature(uvIndexForFeatures)
    {
        SrcNumTexCoords = SrcVertexBuffer.GetNumTexCoords(); // always MAX_STATIC_TEXCOORDS?
        bHasVtxData = SrcVertexBuffer.GetNumVertices() > 0;
        bHasVertexColors = SrcVtxColors.GetNumVertices() > 0;

        if (CommonFeatureToBakeInUVs && UVIndexForFeature)
        {
            // This is an optimization for a quite frequent case: when the extracted part
            // is assigned a same FeatureID for all vertices, we can directly bake the
            // latter in the output mesh's UVs (this will avoid having to parse meta-data
            // again if we need those UVs later, *after* extraction).
            const float fFeatureId = static_cast<float>(CommonFeatureToBakeInUVs->value());
            FeatureToBakeInUVs = FVector2f(fFeatureId, 0.0f);
        }
    }

    void StaticMeshExtractionHelper::ReserveArraysForNumTriangles(uint32 NumTriangles)
    {
        BuildVertices.Reserve(3 * NumTriangles);
        Indices.Reserve(3 * NumTriangles);
    }

    void StaticMeshExtractionHelper::FinalizeArrays()
    {
        BuildVertices.Shrink();

        // normally, the number of extracted faces is predictable
        checkSlow(Indices.GetSlack() == 0);
        Indices.Shrink();
    }

    inline void StaticMeshExtractionHelper::AddVertex(uint32 const SrcVtxId)
    {
        auto DstVtxId = VtxIndicesMap.try_emplace(SrcVtxId, NextNewVertexIndex);
        if (DstVtxId.second) // was inserted
        {
            checkfSlow(NextNewVertexIndex == BuildVertices.Num(), TEXT("bad invariant!"));

            // copy vertex now
            FStaticMeshBuildVertex& DstBuildVertex = BuildVertices.Add_GetRef(FStaticMeshBuildVertex{});
            DstBuildVertex.Position = SrcPositions.VertexPosition(SrcVtxId);
            if (bHasVtxData)
            {
                DstBuildVertex.TangentX = SrcVertexBuffer.VertexTangentX(SrcVtxId);
                DstBuildVertex.TangentY = SrcVertexBuffer.VertexTangentY(SrcVtxId);
                DstBuildVertex.TangentZ = SrcVertexBuffer.VertexTangentZ(SrcVtxId);
            }
            for (uint32 tex(0); tex < SrcNumTexCoords; ++tex)
            {
                DstBuildVertex.UVs[tex] = SrcVertexBuffer.GetVertexUV(SrcVtxId, tex);
            }
            if (FeatureToBakeInUVs)
            {
                DstBuildVertex.UVs[*UVIndexForFeature] = *FeatureToBakeInUVs;
            }
            if (bHasVertexColors)
            {
                DstBuildVertex.Color = SrcVtxColors.VertexColor(SrcVtxId);
            }

            Indices.Add(NextNewVertexIndex);
            NextNewVertexIndex++;
        }
        else
        {
            // not inserted -> already copied before
            Indices.Add(DstVtxId.first->second);
        }
    }

    inline void StaticMeshExtractionHelper::SetFeatureForNextVertices(
        ITwinFeatureID const FeatID)
    {
        FeatureToBakeInUVs = FVector2f(static_cast<float>(FeatID.value()), 0.0f);
    }

    inline void StaticMeshExtractionHelper::SetFeatureForSourceVertex(
        uint32 const SrcVtxId,
        uint32 const UVIndex,
        int64 const FeatID64)
    {
        auto itDstVtxId = VtxIndicesMap.find(SrcVtxId);
        if (itDstVtxId != VtxIndicesMap.end())
        {
            ITwinFeatureID const FeatID = (FeatID64 < 0)
                ? ITwin::NOT_FEATURE
                : ITwinFeatureID(FeatID64);

            BuildVertices[itDstVtxId->second].UVs[UVIndex] =
                FVector2f(static_cast<float>(FeatID.value()), 0.0f);
        }
        else
        {
            // already copied before
            checkfSlow(false, TEXT("unknown vertex %u!"), SrcVtxId);
        }
    }
}

bool FITwinGltfMeshComponentWrapper::ExtractMeshSectionElement(
    ITwinElementID const Element,
    FSimpleStaticMeshSection const& MeshSection,    
    FITwinExtractedEntity& ExtractedEntity,
    FITwinMeshExtractionOptions const& Options) const
{
    const TObjectPtr<UStaticMesh> SrcStaticMesh = GetSourceStaticMesh();
    if (!SrcStaticMesh) {
        return false;
    }
    checkfSlow(MeshSection.IsValid(), TEXT("invalid section!"));

    const FString MeshEltName = gltfMeshComponent_->GetName()
        + FString::Printf(TEXT("_ELT_%llu_SECTION"), Element.value());
    const FName ExtractedMeshName(*MeshEltName);

    InitExtractedMeshComponent(ExtractedEntity, ExtractedMeshName);

    const FStaticMeshLODResources& SrcLODResources = SrcStaticMesh->GetRenderData()->LODResources[0];
    const FRawStaticIndexBuffer& SrcIndexBuffer = SrcLODResources.IndexBuffer;
    const FIndexArrayView SrcIndices = SrcIndexBuffer.GetArrayView();

    std::optional<uint32> UVIndexForFeatures;
    // If we have not yet baked the features in UVs, *and* the mesh section has a unique
    // FeatureID, we can bake it very easily:
    if (MeshSection.CommonFeatureID
        && !HasBakedFeatureIDsInVertexUVs()
        && pGltfToUnrealTexCoordMap_
        && pGltfToUnrealTexCoordMap_->size() < MAX_STATIC_TEXCOORDS)
    {
        UVIndexForFeatures = (uint32)pGltfToUnrealTexCoordMap_->size();
    }
    StaticMeshExtractionHelper meshExtractor(
        SrcLODResources,
        MeshSection.CommonFeatureID,
        UVIndexForFeatures);

    // The advantage here is that we don't have to parse metadata again:
    // just populate the sub-mesh from the section
    meshExtractor.ReserveArraysForNumTriangles(MeshSection.NumTriangles);

    // Copy only the section
    uint32 SrcVertexIndex = MeshSection.FirstIndex;
    const uint32 SrcVtxIndexEnd = SrcVertexIndex + 3 * MeshSection.NumTriangles;
    for (; SrcVertexIndex < SrcVtxIndexEnd; SrcVertexIndex++)
    {
        const uint32 SrcVtxId = SrcIndices[SrcVertexIndex];
        meshExtractor.AddVertex(SrcVtxId);
    }

    // If we have not yet baked features in the master mesh's UVs, and if the section has
    // several features, compute and bake them now (which required to access meta-data).
    FPropertyTableAccess ElementAcc;
    if (!MeshSection.CommonFeatureID
        && !HasBakedFeatureIDsInVertexUVs()
        && pGltfToUnrealTexCoordMap_
        && pGltfToUnrealTexCoordMap_->size() < MAX_STATIC_TEXCOORDS
        && GetElementPropertyAccess(ElementAcc))
    {
        // Parse all extracted vertices again, and fill the appropriate UV slot with the
        // Feature ID
        uint32 const UVForFeat = (uint32)pGltfToUnrealTexCoordMap_->size();

        const FITwinCesiumPropertyTableProperty& ITWIN_ElementProperty(*ElementAcc.Prop);
        const FITwinCesiumFeatureIdSet& FeatureIdSet =
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
                *pFeatures_)[ElementAcc.FeatureIDSetIndex];

        SrcVertexIndex = MeshSection.FirstIndex;
        for (; SrcVertexIndex < SrcVtxIndexEnd; SrcVertexIndex++)
        {
            const uint32 SrcVtxId = SrcIndices[SrcVertexIndex];
            meshExtractor.SetFeatureForSourceVertex(
                SrcVtxId,
                UVForFeat,
                UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                    FeatureIdSet,
                    SrcVtxId));
        }
        UVIndexForFeatures = UVForFeat;
    }

    meshExtractor.FinalizeArrays();

    return FinalizeExtractedEntity(
        ExtractedEntity,
        ExtractedMeshName,
        Element,
        meshExtractor.GetBuildVertices(),
        meshExtractor.GetIndices(),
        SrcStaticMesh,
        Options,
        UVIndexForFeatures);
}


bool FITwinGltfMeshComponentWrapper::ExtractElement_SLOW(
    ITwinElementID const Element,
    uint32 const EvalNumTriangles,
    FITwinExtractedEntity& ExtractedEntity,
    FITwinMeshExtractionOptions const& Options) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ITwin::Extract::SlowMode)

    const TObjectPtr<UStaticMesh> SrcStaticMesh = GetSourceStaticMesh();
    if (!SrcStaticMesh) {
        return false;
    }
    int64 FeatureIDSetIndex = 0;
    const FITwinCesiumPropertyTableProperty* pElementProperty =
        FetchElementProperty(FeatureIDSetIndex);
    if (pElementProperty == nullptr) {
        return false;
    }

    const FITwinCesiumPropertyTableProperty& ITWIN_ElementProperty(*pElementProperty);
    const FITwinCesiumFeatureIdSet& FeatureIdSet =
        UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
            *pFeatures_)[FeatureIDSetIndex];

    const FString MeshEltName = gltfMeshComponent_->GetName()
        + FString::Printf(TEXT("_ELT_%llu"), Element.value());
    const FName ExtractedMeshName(*MeshEltName);

    InitExtractedMeshComponent(ExtractedEntity, ExtractedMeshName);

    
    const FStaticMeshLODResources& SrcLODResources = SrcStaticMesh->GetRenderData()->LODResources[0];
    const FRawStaticIndexBuffer& SrcIndexBuffer = SrcLODResources.IndexBuffer;
    const FIndexArrayView SrcIndices = SrcIndexBuffer.GetArrayView();
    const int64 SrcTriangles = static_cast<int64>(SrcIndices.Num() / 3);

    std::optional<uint32> UVIndexForFeatures;
    // If we have not yet baked the features in UVs, do it now
    if (!HasBakedFeatureIDsInVertexUVs()
        && pGltfToUnrealTexCoordMap_
        && pGltfToUnrealTexCoordMap_->size() < MAX_STATIC_TEXCOORDS)
    {
        UVIndexForFeatures = (uint32)pGltfToUnrealTexCoordMap_->size();
    }

    StaticMeshExtractionHelper meshExtractor(
        SrcLODResources,
        std::nullopt,
        UVIndexForFeatures);
    meshExtractor.ReserveArraysForNumTriangles(EvalNumTriangles);

    // slower: we have to parse meta-data again...
    for (int64 FaceIndex(0); FaceIndex < SrcTriangles; FaceIndex++)
    {
        const uint32 vtxId0 = SrcIndices[3 * FaceIndex];
        const int64 vtxFeatureID = // not yet an ITwinFeatureID, which is unsigned and 32bits!
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                FeatureIdSet,
                vtxId0);
        const ITwinElementID vtxElementID = (vtxFeatureID < 0)
            ? ITwin::NOT_ELEMENT
            : ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
                    ITWIN_ElementProperty,
                    vtxFeatureID),
                ITwin::NOT_ELEMENT.value()));

        if (vtxElementID == Element) // this most likely means FeatureID is valid
        {
            if (UVIndexForFeatures)
            {
                // Bake the feature ID in UVs
                meshExtractor.SetFeatureForNextVertices(ITwinFeatureID(vtxFeatureID));
            }
            // append the triangle
            for (int i(0); i < 3; i++)
            {
                meshExtractor.AddVertex(SrcIndices[3 * FaceIndex + i]);
            }
        }
    }

    meshExtractor.FinalizeArrays();

    return FinalizeExtractedEntity(
        ExtractedEntity,
        ExtractedMeshName,
        Element,
        meshExtractor.GetBuildVertices(),
        meshExtractor.GetIndices(),
        SrcStaticMesh,
        Options,
        UVIndexForFeatures);
}

bool FITwinGltfMeshComponentWrapper::HasDetectedElementID(ITwinElementID const Element) const
{
    if (!HasParsedMetaData())
    {
        return false;
    }
    switch (*metadataStatus_)
    {
    case EMetadataStatus::SortedByElement:
    case EMetadataStatus::UnsortedMetadata:
        return (elementSections_.find(Element) != elementSections_.end());

    case EMetadataStatus::InvalidMesh:
    case EMetadataStatus::MissingMetadata:
        return false;

    default:
        checkf(false, TEXT("unhandled case: %d"), static_cast<int>(*metadataStatus_));
        return false;
    }
}

bool FITwinGltfMeshComponentWrapper::CanExtractElement(ITwinElementID const Element)
{
    if (!gltfMeshComponent_.IsValid())
    {
        // The Cesium tile may have been destroyed in the interval.
        return false;
    }
    if (!metadataStatus_)
    {
        metadataStatus_ = ComputeMetadataStatus();
    }
    return HasDetectedElementID(Element);
}

void FITwinGltfMeshComponentWrapper::HideOriginalMeshComponent(bool bHide /*= true*/)
{
    if (gltfMeshComponent_.IsValid())
    {
        gltfMeshComponent_->SetVisibility(!bHide);
    }
}

uint32 FITwinGltfMeshComponentWrapper::ExtractSomeElements(
    FITwinSceneTile& SceneTile,
    float Percentage,
    FITwinMeshExtractionOptions const& InOptions /*= {}*/)
{
    uint32 nExtracted = 0;
    FITwinMeshExtractionOptions Options = InOptions;
    Options.SceneTile = &SceneTile;

#if ENABLE_DRAW_DEBUG
    if (!metadataStatus_)
    {
        // first time we try to extract something => see if we can benefit from mesh
        // sections to optimize next extractions.
        metadataStatus_ = ComputeMetadataStatus();
    }
    if (*metadataStatus_ == EMetadataStatus::InvalidMesh
        || *metadataStatus_ == EMetadataStatus::MissingMetadata)
    {
        return 0;
    }
    // only extract some elements that were not yet extracted
    const uint32 nbEltsToExtract = static_cast<uint32>(
        std::ceil(elementSections_.size() * Percentage));
    for (auto const& [EltID, MeshSection] : elementSections_)
    {
        auto&& ExtractionEntry = SceneTile.ExtractedElement(EltID);
        if (ExtractionEntry.second) // was inserted, ie not extracted previously
        {
            // extract it now
            auto& ExtractedEntity =
                ExtractionEntry.first->second.emplace_back(FITwinExtractedEntity{ EltID });
            if (ExtractElement(EltID, ExtractedEntity, Options))
            {
                nExtracted++;
                if (nExtracted >= nbEltsToExtract)
                    break;
            }
            else
            {
                ExtractionEntry.first->second.pop_back();
            }
        }
    }
#endif // ENABLE_DRAW_DEBUG

    return nExtracted;
}

std::optional<uint32> FITwinGltfMeshComponentWrapper::BakeFeatureIDsInVertexUVs()
{
    if (uvIndexForFeatures_) {
        // already baked before
        return uvIndexForFeatures_;
    }
    if (!featuresAccessorIndex_) {
        // the input primitive has no features
        return std::nullopt;
    }

    TObjectPtr<UStaticMesh> StaticMesh = GetSourceStaticMesh();
    if (!StaticMesh) {
        return std::nullopt;
    }
    if (pGltfToUnrealTexCoordMap_ == nullptr) {
        checkf(false, TEXT("need to maintain TexCoordMap of GLTF mesh"));
        return std::nullopt;
    }

    auto AlreadyInMap = pGltfToUnrealTexCoordMap_->find(*featuresAccessorIndex_);
    if (AlreadyInMap != pGltfToUnrealTexCoordMap_->end())
    {
        uvIndexForFeatures_ = AlreadyInMap->second;
        return uvIndexForFeatures_;
    }

    // use next free slot (accordingly to GltfToUnrealTexCoordMap)
    if (pGltfToUnrealTexCoordMap_->size() >= MAX_STATIC_TEXCOORDS) {
        checkf(false, TEXT("no space left for any extra UV layer"));
        return std::nullopt;
    }

    // ==============
    // TODO: remove?
    //
    // Leaving the following on-demand baking for the moment in case it can be fixed later, but it seems
    // to work only in the Editor! For example, it leads to disappearing meshes in a packaged app (eg.
    // "ITwinTestApp_Game" build), or even crashes in RT render!? So I have made it so that it is done
    // directly in ITwinCesiumGltfComponent.cpp's loadPrimitive (look for
    // MeshBuildCallbacks.Pin()->BakeFeatureIDsInVertexUVs)
    // Maybe related to cesium-unreal's commit 17d5f9c1b5bda0e653984436145a7566d3300b2c by Julot?
    //
    // ==============

    TUniquePtr< FStaticMeshComponentRecreateRenderStateContext > RecreateRenderStateContext
        = MakeUnique<FStaticMeshComponentRecreateRenderStateContext>(StaticMesh);

    // Note: validity of RenderData and LODResources[0] already checked in GetSourceStaticMesh
    FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
    const uint32 NumVertices = LODResources.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();

    // Dirty the mesh
    StaticMesh->Modify();

    // Release the static mesh's resources.
    StaticMesh->ReleaseResources();
    StaticMesh->ReleaseResourcesFence.Wait();

    // Fill the extra UV layer
    uvIndexForFeatures_ = BakeFeatureIDsInVertexUVs(
        featuresAccessorIndex_,
        ICesiumMeshBuildCallbacks::FITwinCesiumMeshData{
            (const CesiumGltf::MeshPrimitive*)nullptr/*because we provide featuresAccessorIndex_*/,
            *pMetadata_, *pFeatures_, *pGltfToUnrealTexCoordMap_
        },
        LODResources);

    StaticMesh->InitResources();
    gltfMeshComponent_->MarkRenderStateDirty();

    return uvIndexForFeatures_;
}

/*static*/
uint32 FITwinGltfMeshComponentWrapper::BakeFeatureIDsInVertexUVs(
    std::optional<uint32> featuresAccessorIndex,
    ICesiumMeshBuildCallbacks::FITwinCesiumMeshData const& CesiumData,
    FStaticMeshLODResources& LODResources)
{
    if (!featuresAccessorIndex)
    {
        check(CesiumData.pMeshPrimitive);
        auto featAccessorIt = CesiumData.pMeshPrimitive->attributes.find("_FEATURE_ID_0");
        if (featAccessorIt == CesiumData.pMeshPrimitive->attributes.end())
            return (uint32)-1;
        featuresAccessorIndex = featAccessorIt->second;
    }

    static const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
    const FITwinCesiumFeatureIdSet& FeatureIdSet = UITwinCesiumPrimitiveFeaturesBlueprintLibrary
        ::GetFeatureIDSets(CesiumData.Features)[FeatureIDSetIndex];
    const uint32 NumVertices = LODResources.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
    FStaticMeshVertexBuffer& VtxBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;

    check(CesiumData.GltfToUnrealTexCoordMap.size() < VtxBuffer.GetNumTexCoords());
    uint32 const uvIndex = (uint32)CesiumData.GltfToUnrealTexCoordMap.size();
    CesiumData.GltfToUnrealTexCoordMap[*featuresAccessorIndex] = uvIndex;
    for (uint32 vtxIndex(0); vtxIndex < NumVertices; vtxIndex++)
    {
        int64 const FeatID = UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
            FeatureIdSet,
            static_cast<int64>(vtxIndex));
        const float fFeatureId = static_cast<float>(
            (FeatID < 0) ? ITwin::NOT_FEATURE.value() : FeatID);
        VtxBuffer.SetVertexUV(vtxIndex, uvIndex, FVector2f(fFeatureId, 0.0f));
    }
    return uvIndex;
}


void FITwinGltfMeshComponentWrapper::ForEachMaterialInstance(std::function<void(UMaterialInstanceDynamic&)> const& Func)
{
    const TObjectPtr<UStaticMesh> SrcStaticMesh = GetSourceStaticMesh();
    if (!SrcStaticMesh) {
        return;
    }
    UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(SrcStaticMesh->GetMaterial(0));
    if (Mat)
    {
        Func(*Mat);
    }

    // By construction (see #InitExtractedMeshComponent), extracted entities are children of gltfMeshComponent_
    TArray<USceneComponent*> Children;
    gltfMeshComponent_->GetChildrenComponents(false /*bIncludeAllDescendants*/, Children);
    for (auto& Child : Children)
    {
        if (auto* ExtractedMeshComp = Cast<UStaticMeshComponent>(Child))
        {
            const TObjectPtr<UStaticMesh> ExtractedStaticMesh = ExtractedMeshComp->GetStaticMesh();
            if (ExtractedStaticMesh)
            {
                Mat = Cast<UMaterialInstanceDynamic>(ExtractedStaticMesh->GetMaterial(0));
                if (Mat)
                {
                    Func(*Mat);
                }
            }
        }
    }
}
