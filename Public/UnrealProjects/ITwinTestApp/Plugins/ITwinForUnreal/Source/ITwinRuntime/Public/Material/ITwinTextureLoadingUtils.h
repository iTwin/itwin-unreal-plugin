/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTextureLoadingUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>
#include <Containers/Array.h>
#include <AssetRegistry/AssetData.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/TextureKey.h>
#	include <SDK/Core/Visualization/TextureUsage.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

class UTexture2D;
namespace AdvViz::SDK {
	struct ITwinMaterial;
	class MaterialPersistenceManager;
}
namespace BeUtils {
	class RWLockBase;
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
		std::map<std::string, BeUtils::GltfMaterialHelperPtr> const& imodelToMatHelper,
		bool bResolveLocalDiskTextures = false,
		BeUtils::WLock const* pLock = nullptr);

	UTexture2D* ResolveAsUnrealTexture(
		BeUtils::GltfMaterialHelper& GltfMatHelper,
		std::string const& TextureId,
		AdvViz::SDK::ETextureSource eSource);

	void ResolveITwinTextures(
		std::unordered_map<AdvViz::SDK::TextureKey, std::string> const& iTwinTextures,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		BeUtils::GltfMaterialHelperPtr GltfMatHelper,
		std::filesystem::path const& textureDir);

	//! Given a texture identifier, returns the corresponding texture buffer (read from a local file or
	//! retrieved from a server or cesium cache, depending on the source of the texture).
	ITWINRUNTIME_API bool LoadTextureBuffer(AdvViz::SDK::TextureKey const& texKey,
		BeUtils::GltfMaterialHelper const& matHelper,
		BeUtils::RWLockBase const& lock,
		TArray64<uint8>& outBuffer,
		std::string& strError);
}
