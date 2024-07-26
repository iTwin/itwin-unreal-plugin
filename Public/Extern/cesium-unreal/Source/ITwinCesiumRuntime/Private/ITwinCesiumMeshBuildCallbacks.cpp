/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCesiumMeshBuildCallbacks.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinCesiumMeshBuildCallbacks.h"

#include <Materials/MaterialInstanceDynamic.h>

ICesiumMeshBuildCallbacks::ICesiumMeshBuildCallbacks() {

}

ICesiumMeshBuildCallbacks::~ICesiumMeshBuildCallbacks() {

}

UMaterialInstanceDynamic* ICesiumMeshBuildCallbacks::CreateMaterial_GameThread(
    CesiumGltf::MeshPrimitive const* /*pMeshPrimitive*/,
    UMaterialInterface*& pBaseMaterial,
    UObject* InOuter,
    FName const& Name) {
    // Default implementation: just create a new instance
    return UMaterialInstanceDynamic::Create(pBaseMaterial, InOuter, Name);
}
