/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGltfMeshComponentWrapper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinGltfMeshComponentWrapper.h"

#include "ITwinSceneMapping.h"
#include <ITwinMetadataConstants.h>

#include <Engine/StaticMesh.h>
#include <Materials/Material.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Misc/EngineVersionComparison.h>
#include <StaticMeshResources.h>

#include <CesiumGltf/MeshPrimitive.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/ExtensionITwinMaterialID.h>
#include <Compil/AfterNonUnrealIncludes.h>

//=======================================================================================
// class FITwinGltfMeshComponentWrapper
//=======================================================================================
FITwinGltfMeshComponentWrapper::FITwinGltfMeshComponentWrapper(ICesiumLoadedTilePrimitive& TilePrim,
															   std::optional<uint32> uvIndexForFeatures)
	: gltfMeshComponent_(&TilePrim.GetMeshComponent())
	, uvIndexForFeatures_(uvIndexForFeatures)
{
	auto* pMeshPrimitive = TilePrim.GetMeshPrimitive();
	if (pMeshPrimitive)
	{
		// Test if this primitive is linked to a specific ITwin Material ID (test extension specially added
		// by our gltf tuning process.
		auto const* matIdExt = pMeshPrimitive->getExtension<BeUtils::ExtensionITwinMaterialID>();
		if (matIdExt)
		{
			iTwinMaterialID_ = matIdExt->materialId;
		}
	}
}

FITwinGltfMeshComponentWrapper::FITwinGltfMeshComponentWrapper(UStaticMeshComponent& MeshComponent, 
															   uint64_t ITwinMaterialID)
	: gltfMeshComponent_(&MeshComponent)
	, iTwinMaterialID_(ITwinMaterialID)
{
}

TObjectPtr<UStaticMesh> FITwinGltfMeshComponentWrapper::GetSourceStaticMesh() const
{
	if (!gltfMeshComponent_.IsValid()) {
		// checkf(false, TEXT("obsolete gltf mesh pointer"));
		// => this can happen, as the initial Cesium tile may have been destroyed in the interval.
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

void FITwinGltfMeshComponentWrapper::HideOriginalMeshComponent(bool bHide /*= true*/)
{
	if (gltfMeshComponent_.IsValid())
	{
		gltfMeshComponent_->SetVisibility(!bHide);
	}
}

std::optional<uint32> FITwinGltfMeshComponentWrapper::GetFeatureIDsInVertexUVs() const
{
	return uvIndexForFeatures_;
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
}
