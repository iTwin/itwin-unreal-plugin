/*--------------------------------------------------------------------------------------+
|
|     $Source: TextureLoadingUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>
#include <AssetRegistry/AssetData.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/TextureKey.h>
#	include <SDK/Core/Visualization/TextureUsage.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

class UTexture2D;
namespace AdvViz::SDK {
	struct ITwinMaterial;
	class MaterialPersistenceManager;
}
namespace BeUtils {
	class WLock;
	class GltfMaterialHelper;
	using GltfMaterialHelperPtr = std::shared_ptr<GltfMaterialHelper>;
}

namespace ITwin
{
	bool ResolveDecorationTextures(
		AdvViz::SDK::MaterialPersistenceManager& matPersistenceMngr,
		AdvViz::SDK::PerIModelTextureSet const& perModelTextures,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		std::map<std::string, BeUtils::GltfMaterialHelperPtr> const& imodelIdToMatHalper,
		bool bResolveLocalDiskTextures = false,
		BeUtils::WLock const* pLock = nullptr);

	UTexture2D* ResolveMatLibraryTexture(
		BeUtils::GltfMaterialHelper const& GltfMatHelper,
		std::string const& TextureId);

	void ResolveITwinTextures(
		std::unordered_map<AdvViz::SDK::TextureKey, std::string> const& iTwinTextures,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		BeUtils::GltfMaterialHelperPtr GltfMatHelper,
		std::filesystem::path const& textureDir);
}
