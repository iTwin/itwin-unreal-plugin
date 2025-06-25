/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinExtractedMeshComponent.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CesiumCustomVisibilitiesMeshComponent.h" // Original class moved there

/// Mesh component created when extracting a specific iTwin element from a Cesium primitive.
/// The visibility of those meshes has to be overridden in some cases, to match the animation defined in
/// Synchro4D schedules.
using UITwinExtractedMeshComponent = UCesiumCustomVisibilitiesMeshComponent;

class UMaterialInterface;
class UMaterialInstanceDynamic;
namespace ITwin
{
	UMaterialInstanceDynamic* ChangeBaseMaterialInUEMesh(UStaticMeshComponent& MeshComponent,
		UMaterialInterface* BaseMaterial,
		TWeakObjectPtr<UMaterialInstanceDynamic> const* SupposedPreviousMaterial = nullptr);
}
