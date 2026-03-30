/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTextureHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <SDK/Core/Visualization/TextureKey.h>
#include <SDK/Core/Visualization/TextureUsage.h>

namespace CesiumAsync
{
	class AsyncSystem;
	class IAssetAccessor;
}
namespace AdvViz::SDK
{
	enum class ETextureSource : uint8_t;
	class MaterialPersistenceManager;
}

namespace BeUtils
{
	bool DownloadTexture(std::string const& textureURI,
		std::shared_ptr<std::string const> const& accessToken,
		std::shared_ptr<CesiumAsync::IAssetAccessor> const& pAssetAccessor,
		CesiumAsync::AsyncSystem const& asyncSystem,
		std::function<bool(std::vector<uint8_t> const&)>&& inCallback);


	class GltfMaterialHelper;
	using GltfMaterialHelperPtr = std::shared_ptr<GltfMaterialHelper>;
	class WLock;

	void ResolveTexturesMatchingSource(
		AdvViz::SDK::ETextureSource TexSource,
		AdvViz::SDK::MaterialPersistenceManager& matPersistenceMngr,
		AdvViz::SDK::PerIModelTextureSet const& perModelTextures,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		std::map<std::string, GltfMaterialHelperPtr> const& imodelToMatHelper,
		std::shared_ptr<std::string const> const& accessToken,
		std::shared_ptr<CesiumAsync::IAssetAccessor> const& pAssetAccessor,
		CesiumAsync::AsyncSystem const& asyncSystem,
		WLock const* pLock = nullptr);

} // namespace BeUtils
