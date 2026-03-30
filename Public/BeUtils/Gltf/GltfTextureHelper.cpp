/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTextureHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "GltfTextureHelper.h"
#include <BeUtils/Gltf/GltfMaterialHelper.h>
#include <BeUtils/Misc/RWLock.h>

#include <SDK/Core/Tools/Assert.h>
#include <SDK/Core/Tools/Log.h>
#include <SDK/Core/Visualization/MaterialPersistence.h>

#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumGltfReader/GltfReader.h>

namespace BeUtils
{


	bool DownloadTexture(std::string const& textureURI,
		std::shared_ptr<std::string const> const& accessToken,
		std::shared_ptr<CesiumAsync::IAssetAccessor> const& pAssetAccessor,
		CesiumAsync::AsyncSystem const& asyncSystem,
		std::function<bool(std::vector<uint8_t> const&)>&& inCallback)
	{
		if (textureURI.empty())
		{
			return false;
		}
		if (!accessToken || accessToken->empty())
		{
			return false;
		}

		std::vector<CesiumAsync::IAssetAccessor::THeader> const tHeaders =
		{
			{
				"Authorization",
				std::string("Bearer ") + *accessToken
			}
		};

		bool bSuccess = false;
		std::string strError;
		// This call should be very fast, as the image, if available, is already in Cesium cache.
		// And since we test TexAccess.cesiumImage before coming here, we *know* that the image is indeed
		// available.
		pAssetAccessor
			->get(asyncSystem, textureURI, tHeaders)
			.thenImmediately([&bSuccess, callback = std::move(inCallback)](
				std::shared_ptr<CesiumAsync::IAssetRequest>&& pRequest) {
			const CesiumAsync::IAssetResponse* pResponse = pRequest->response();
			if (pResponse) {
				const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(pResponse->data().data());
				std::vector<uint8_t> buffer;
				buffer.assign(dataPtr, dataPtr + pResponse->data().size());
				bSuccess = callback(buffer);
			}
		}).catchImmediately([&strError](std::exception&& e) {
			strError = e.what();
		}).wait();

		if (!strError.empty())
		{
			BE_LOGE("ITwinDecoration", "Exception while retrieving texture from '"
				<< textureURI << "': " << strError);
		}
		return bSuccess;

	}


	namespace
	{

		inline CesiumAsync::HttpHeaders GetHeadersForSource(AdvViz::SDK::ETextureSource TexSource,
															std::string const& accessToken)
		{
			if (TexSource == AdvViz::SDK::ETextureSource::Decoration)
			{
				return {
					{
						"Authorization",
						std::string("Bearer ") + accessToken
					}
				};
			}
			else
			{
				// No extra headers required for local textures
				return {};
			}
		}

		// Scoped W-Lock doing nothing if a lock is provided from outside
		struct [[nodiscard]] OptionalWLock
		{
			OptionalWLock(GltfMaterialHelper& MatHelper, WLock const* pLock = nullptr)
				: ExternalLock(pLock)
			{
				if (ExternalLock)
				{
					BE_ASSERT(ExternalLock->mutex() == &MatHelper.GetMutex());
				}
				else
				{
					LocalLock.emplace(MatHelper.GetMutex());
				}
			}

			WLock const& GetLock() const {
				BE_ASSERT(ExternalLock || LocalLock);
				return ExternalLock ? *ExternalLock : *LocalLock;
			}

			WLock const* const ExternalLock = nullptr;
			std::optional<WLock> LocalLock;
		};

	}

	void ResolveTexturesMatchingSource(
		AdvViz::SDK::ETextureSource TexSource,
		AdvViz::SDK::MaterialPersistenceManager& matPersistenceMngr,
		AdvViz::SDK::PerIModelTextureSet const& perModelTextures,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		std::map<std::string, GltfMaterialHelperPtr> const& imodelToMatHelper,
		std::shared_ptr<std::string const> const& accessToken,
		std::shared_ptr<CesiumAsync::IAssetAccessor> const& pAssetAccessor,
		CesiumAsync::AsyncSystem const& asyncSystem,
		WLock const* pLock /*= nullptr*/)
	{
		BE_ASSERT(TexSource == AdvViz::SDK::ETextureSource::Decoration
			|| TexSource == AdvViz::SDK::ETextureSource::Library);

		// Download decoration textures if needed.

		CesiumGltfReader::GltfReaderResult gltfResult;
		auto& model = gltfResult.model.emplace();
		auto& images = model.images;
		images.reserve(perModelTextures.size() * 5);

		struct LoadedImageInfo
		{
			size_t imgIndex = 0;
			AdvViz::SDK::TextureKey texKey;
		};
		struct IModelImageVec
		{
			std::shared_ptr<BeUtils::GltfMaterialHelper> matHelper;
			std::vector<LoadedImageInfo> imageInfos;
		};
		std::vector<IModelImageVec> imageCorresp;
		imageCorresp.reserve(perModelTextures.size());

		size_t gltfImageIndex = 0;
		for (auto const& [imodelid, textureSet] : perModelTextures)
		{
			auto itMatHelper = imodelToMatHelper.find(imodelid);
			if (itMatHelper == imodelToMatHelper.end())
				continue;
			auto glTFMatHelper = itMatHelper->second;
			if (!glTFMatHelper)
				continue;

			IModelImageVec& imodelImgs = imageCorresp.emplace_back();
			imodelImgs.matHelper = glTFMatHelper;
			imodelImgs.imageInfos.reserve(textureSet.size());

			// Download (or read from sqlite cache) all decoration textures used by this model
			for (auto const& texKey : textureSet)
			{
				if (texKey.eSource == TexSource)
				{
					imodelImgs.imageInfos.push_back({ gltfImageIndex, texKey });
					auto& gltfImage = images.emplace_back();
					gltfImageIndex++;
					gltfImage.uri = matPersistenceMngr.GetRelativeURL(texKey);
				}
			}
		}

		if (gltfImageIndex == 0)
		{
			// Nothing to do.
			return;
		}

		// Actually download textures. Note that we use Cesium's sqlite caching system, so this should be fast
		// except for the very first time).
		std::string const baseUrl = matPersistenceMngr.GetBaseURL(TexSource);

		std::string strError;

		// We restrict the formats to JPG and PNG, so we can leave the default options (no need to setup
		// Ktx2TranscodeTargets...)
		CesiumGltfReader::GltfReaderOptions gltfOptions;
		CesiumGltfReader::GltfReader::resolveExternalData(
			asyncSystem,
			baseUrl,
			GetHeadersForSource(TexSource, *accessToken),
			pAssetAccessor,
			gltfOptions,
			std::move(gltfResult))
			.thenImmediately([imageCorresp, pLock, &textureUsageMap](CesiumGltfReader::GltfReaderResult&& result)
		{
			auto& cesiumImages = result.model->images;
			// Dispatch the downloaded images to the appropriate material helper
			for (IModelImageVec const& imodelImgs : imageCorresp)
			{
				OptionalWLock OptLock(*imodelImgs.matHelper, pLock);
				auto const& lock = OptLock.GetLock();
				for (LoadedImageInfo const& info : imodelImgs.imageInfos)
				{
					imodelImgs.matHelper->StoreCesiumImage(info.texKey,
						std::move(cesiumImages[info.imgIndex]),
						textureUsageMap,
						lock);
				}
			}
		}).catchImmediately([&strError](std::exception&& e) {
			strError = e.what();
		}).wait();

		if (!strError.empty())
		{
			BE_LOGE("ITwinDecoration", "Exception while retrieving textures from '"
				<< baseUrl << "': " << strError);
		}
	}

} // namespace BeUtils
