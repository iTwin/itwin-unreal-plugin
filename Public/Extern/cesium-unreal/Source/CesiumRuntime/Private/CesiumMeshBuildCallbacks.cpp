/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumMeshBuildCallbacks.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "CesiumMeshBuildCallbacks.h"

#include <Materials/MaterialInstanceDynamic.h>

ICesiumMeshBuildCallbacks::ICesiumMeshBuildCallbacks() {

}

ICesiumMeshBuildCallbacks::~ICesiumMeshBuildCallbacks() {

}

UMaterialInstanceDynamic* ICesiumMeshBuildCallbacks::CreateMaterial_GameThread(
    Cesium3DTilesSelection::Tile const& Tile, UStaticMeshComponent const& MeshComponent,
    CesiumGltf::MeshPrimitive const* pMeshPrimitive, UMaterialInterface*& pBaseMaterial,
    FCesiumModelMetadata const& Metadata, FCesiumPrimitiveFeatures const& Features,
    UObject* InOuter, FName const& Name)
{
    // Default implementation: just create a new instance
    return UMaterialInstanceDynamic::Create(pBaseMaterial, InOuter, Name);
}

void ICesiumMeshBuildCallbacks::TuneMaterial(
    CesiumGltf::Material const& /*glTFmaterial*/,
    CesiumGltf::MaterialPBRMetallicRoughness const& /*pbr*/,
    UMaterialInstanceDynamic* /*pMaterial*/,
    EMaterialParameterAssociation /*association*/,
    int32 /*index*/) const {

}
