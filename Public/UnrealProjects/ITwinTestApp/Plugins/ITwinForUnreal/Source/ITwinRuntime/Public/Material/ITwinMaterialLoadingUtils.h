/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLoadingUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

#include <memory>
#include <variant>

class AStaticMeshActor;
namespace AdvViz::SDK { struct ITwinMaterial; }
namespace BeUtils { class GltfMaterialHelper; }

namespace ITwin
{
	using FMaterialPtr = std::shared_ptr<AdvViz::SDK::ITwinMaterial>;
	// Either the path to the material (json) file, or the material definition itself.
	using FMaterialAssetInfo = std::variant<FString /*MaterialAssetPath*/, FMaterialPtr>;

	ITWINRUNTIME_API bool LoadMaterialOnGenericMesh(FMaterialAssetInfo const& MaterialAssetInfo,
		AStaticMeshActor& MeshActor,
		std::shared_ptr<BeUtils::GltfMaterialHelper> const& SrcIModelMatHelper = {});
}
