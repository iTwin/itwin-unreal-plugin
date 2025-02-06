/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialTuner.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <BeUtils/Gltf/GltfMaterialTuner.h>

#include <CesiumGltf/ExtensionITwinMaterial.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltfContent/ImageManipulation.h>
#include <CesiumGltfReader/GltfReader.h>

#include <fstream>
#include <spdlog/fmt/fmt.h>

#include <SDK/Core/ITwinAPI/ITwinMaterial.inl>
#include <SDK/Core/Tools/Assert.h>
#include <SDK/Core/Tools/Log.h>

#include <boost/container/small_vector.hpp>

namespace BeUtils
{

	namespace
	{

		/// Return true if this material holds a definition for the given channel.
		inline bool DefinesChannel(SDK::Core::ITwinMaterial const& mat, SDK::Core::EChannelType channel)
		{
			return mat.channels[(size_t)channel].has_value();
		}

		//inline size_t CountDefinedChannels(SDK::Core::ITwinMaterial const& mat)
		//{
		//	return std::count_if(mat.channels.begin(), mat.channels.end(),
		//		[](auto&& chan) { return chan.has_value(); });
		//}


		/// Append a new glTF texture pointing at the given texture (which can come from the decoration
		/// service or a local file).
		inline int32_t CreateGltfTextureFromTextureAccess(
			GltfMaterialHelper::TextureAccess const& texAccess,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images)
		{
			int32_t gltfTexId = -1;
			if (texAccess.IsValid())
			{
				// Create one glTF image and one glTF texture
				gltfTexId = static_cast<int32_t>(textures.size());
				CesiumGltf::Texture& gltfTexture = textures.emplace_back();
				gltfTexture.source = static_cast<int32_t>(images.size());
				CesiumGltf::Image& gltfImage = images.emplace_back();
				if (texAccess.cesiumImage)
				{
					// Reuse already loaded image (much faster).
					gltfImage = *texAccess.cesiumImage;
				}
				else
				{
					// This texture is using a local path.

					// since c++20, this uses now char8_t...
					auto const tex_u8string = texAccess.filePath.generic_u8string();
					gltfImage.uri = "file:///" + std::string(tex_u8string.begin(), tex_u8string.end());
				}
			}
			return gltfTexId;
		}

		static inline void SetBaseColorOpacity(CesiumGltf::MaterialPBRMetallicRoughness& pbr, double opacityValue)
		{
			auto& baseColorFactor = pbr.baseColorFactor;
			if (baseColorFactor.size() < 4)
			{
				// Just in case...
				baseColorFactor.resize(4, 1.);
			}
			baseColorFactor[3] = opacityValue;
		}

		static std::vector<std::byte> ReadFileContent(const std::filesystem::path& fileName)
		{
			std::ifstream file(fileName, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return {};
			}

			std::streamsize size = file.tellg();
			file.seekg(0, std::ios::beg);

			std::vector<std::byte> buffer(static_cast<size_t>(size));
			file.read(reinterpret_cast<char*>(buffer.data()), size);

			return buffer;
		}

		static bool WriteFileContent(std::vector<std::byte> const& pngBuffer, const std::filesystem::path& fileName)
		{
			std::ofstream file(fileName, std::ios::binary | std::ios::out);
			if (!file)
			{
				return false;
			}
			file.write(reinterpret_cast<char const*>(pngBuffer.data()), pngBuffer.size());
			return !file.fail();
		}

	}


#define RED_TO_BW_FACTOR	0.35f
#define GREEN_TO_BW_FACTOR	0.50f
#define BLUE_TO_BW_FACTOR	0.15f


	using ReadImageResult = SDK::expected<
		CesiumGltf::ImageCesium,
		GenericFailureDetails>;

	static ReadImageResult ReadImageCesium(std::filesystem::path const& texPath, std::string_view const& channelName)
	{
		using namespace CesiumGltfReader;

		std::vector<std::byte> const imageData = ReadFileContent(texPath);
		if (imageData.empty())
		{
			return SDK::make_unexpected(GenericFailureDetails{
				fmt::format("failed reading {} image content from '{}'",
					channelName, texPath.generic_string()) });
		}
		ImageReaderResult imgReadResult = GltfReader::readImage(imageData, CesiumGltf::Ktx2TranscodeTargets{});
		if (!imgReadResult.image)
		{
			// propagate errors
			std::string errorDetails = fmt::format("failed reading {} image from '{}'",
				channelName, texPath.generic_string());
			if (!imgReadResult.errors.empty())
			{
				errorDetails += " ; additional details:";
				for (auto const& err : imgReadResult.errors)
				{
					errorDetails += fmt::format("\n - {}", err);
				}
			}
			return SDK::make_unexpected(GenericFailureDetails{ errorDetails });
		}
		return std::move(*imgReadResult.image);
	}

	static ReadImageResult GetImageCesium(GltfMaterialHelper::TextureAccess const& texAccess,
		std::string_view const& channelName,
		GltfMaterialHelper::Lock const& lock)
	{
		if (texAccess.cesiumImage)
		{
			// We already have a Cesium image
			return texAccess.cesiumImage->cesium;
		}
		else
		{
			return ReadImageCesium(texAccess.filePath, channelName);
		}
	}


	static GltfMaterialTuner::SaveCesiumImageResult SaveImageCesiumIfNeeded(
		CesiumGltf::Image const& targetImg,
		std::filesystem::path const& outputTexPath,
		bool bOverwriteExisting = false)
	{
		using namespace CesiumGltfContent;
		using ESaveImageAction = GltfMaterialTuner::ESaveImageAction;

		// Save the texture to PNG if needed.
		std::error_code ec;
		if (!bOverwriteExisting && std::filesystem::exists(outputTexPath, ec))
		{
			return ESaveImageAction::None;
		}

		auto const pngOutData = ImageManipulation::savePng(targetImg.cesium);
		if (pngOutData.empty())
		{
			return SDK::make_unexpected(GenericFailureDetails{ "failed formatting PNG image" });
		}
		if (!WriteFileContent(pngOutData, outputTexPath))
		{
			return SDK::make_unexpected(GenericFailureDetails{
				fmt::format("failed writing image content to '{}'", outputTexPath.generic_string()) });
		}
		return ESaveImageAction::Saved;
	}

	/*static*/
	GltfMaterialTuner::SaveCesiumImageResult GltfMaterialTuner::SaveImageCesium(
		CesiumGltf::Image const& image, std::filesystem::path const& outputTexPath)
	{
		return SaveImageCesiumIfNeeded(image, outputTexPath, true /*bOverwriteExisting*/);
	}

	static GltfMaterialTuner::FormatTextureResult MergeColorAlphaFilesImpl(
		GltfMaterialHelper::TextureAccess const& colorTexture,
		GltfMaterialHelper::TextureAccess const& alphaTexture,
		bool& isTranslucencyNeeded,
		GltfMaterialHelper::Lock const& lock)
	{
		using namespace CesiumGltfReader;
		using namespace CesiumGltfContent;

		isTranslucencyNeeded = false;

		bool const hasColorTexture = colorTexture.IsValid();
		ReadImageResult colorImageResult;
		if (hasColorTexture)
		{
			colorImageResult = GetImageCesium(colorTexture, "color", lock);
			if (!colorImageResult)
			{
				return SDK::make_unexpected(colorImageResult.error());
			}
		}
		ReadImageResult alphaImageResult = GetImageCesium(alphaTexture, "opacity", lock);
		if (!alphaImageResult)
		{
			return SDK::make_unexpected(alphaImageResult.error());
		}

		CesiumGltf::ImageCesium const& alphaImg(*alphaImageResult);

		// When this code was written, only 8-bit was supported by Cesium image reader, and both PNG and JPG
		// images were created as RGBA (see GltfReader.cpp)
		BE_ASSERT(alphaImg.bytesPerChannel == 1);
		BE_ASSERT(alphaImg.channels == 4);

		CesiumGltf::Image outImage;
		CesiumGltf::ImageCesium& targetImg(outImage.cesium);
		targetImg.channels = 4;
		if (hasColorTexture)
		{
			CesiumGltf::ImageCesium const& colorImg(*colorImageResult);
			targetImg.width = std::max(colorImg.width, alphaImg.width);
			targetImg.height = std::max(colorImg.height, alphaImg.height);

			BE_ASSERT(colorImg.bytesPerChannel == 1);
			targetImg.bytesPerChannel = std::max(colorImg.bytesPerChannel, alphaImg.bytesPerChannel);

			// PNG and JPG readers create RGBA images (see GltfReader.cpp)
			BE_ASSERT(colorImg.channels == 4);
		}
		else
		{
			// Special "merge" case where we just transfer R,G,B channels into A.
			targetImg.width = alphaImg.width;
			targetImg.height = alphaImg.height;
			targetImg.bytesPerChannel = alphaImg.bytesPerChannel;
		}

		int32_t const nbPixels = targetImg.width * targetImg.height;

		// If no color texture is provided, fill the output buffer with white pixels (alpha will be computed
		// afterwards, in the same loop as in the normal merge case).
		std::byte const defaultPixComponent = hasColorTexture ? std::byte(0) : std::byte(255);

		targetImg.pixelData.resize(static_cast<size_t>(
			nbPixels * targetImg.channels * targetImg.bytesPerChannel), defaultPixComponent);

		CesiumGltf::ImageCesium targetAlphaImg = targetImg;

		// Resize color and alpha to the final size
		if (hasColorTexture)
		{
			CesiumGltf::ImageCesium const& colorImg(*colorImageResult);
			if (!ImageManipulation::blitImage(targetImg,
				{ 0, 0, targetImg.width, targetImg.height },
				colorImg,
				{ 0, 0, colorImg.width, colorImg.height }))
			{
				return SDK::make_unexpected(GenericFailureDetails{ "could not blit source color image" });
			}
		}
		else
		{
			// targetImg has already the good size, and was filled with white pixels => nothing more to do
		}

		if (!ImageManipulation::blitImage(targetAlphaImg,
			{ 0, 0, targetImg.width, targetImg.height },
			alphaImg,
			{ 0, 0, alphaImg.width, alphaImg.height }))
		{
			return SDK::make_unexpected(GenericFailureDetails{ "could not blit source alpha image" });
		}

		// Only 8-bit supported by Cesium image reader (if an update allows 16-bit in the future, then we
		// need to adapt the code in the loop on pixels, below)
		BE_ASSERT(targetImg.bytesPerChannel == 1);

		// Then just copy alpha
		std::byte* pTargetAlphaByte = targetImg.pixelData.data() + 3;
		std::byte const* pSrcAlphaBytes = targetAlphaImg.pixelData.data();
		int32_t const offsetPerPix = targetImg.channels * targetImg.bytesPerChannel;
		for (int32_t pix(0); pix < nbPixels; ++pix)
		{
			float const fAlphaByte = RED_TO_BW_FACTOR * static_cast<float>(pSrcAlphaBytes[0])
				+ GREEN_TO_BW_FACTOR * static_cast<float>(pSrcAlphaBytes[1])
				+ BLUE_TO_BW_FACTOR * static_cast<float>(pSrcAlphaBytes[2]);
			*pTargetAlphaByte = static_cast<std::byte>(std::clamp(fAlphaByte, 0.f, 255.f));

			// Translucency will be required in Unreal material as soon as we have not a pure mask.
			isTranslucencyNeeded = isTranslucencyNeeded || (fAlphaByte > 0.5f && fAlphaByte < 254.5f);

			pTargetAlphaByte += offsetPerPix;
			pSrcAlphaBytes += offsetPerPix;
		}
		return GltfMaterialTuner::FormatTextureResultData{
			std::filesystem::path{},
			std::move(outImage)
		};
	}

	struct MergeImageInput
	{
		SDK::Core::EChannelType materialChannel = SDK::Core::EChannelType::ENUM_END;
		std::string matChannelShortPrefix;
		SDK::Core::ETextureChannel rgbaChan = SDK::Core::ETextureChannel::A;
	};

	class MergeImageInputArray
	{
	public:
		struct Entry
		{
			GltfMaterialHelper::TextureAccess srcTexAccess = {};
			MergeImageInput const chanInfo = {};
		};
		// We are limited by R,G,B,A channels in target texture, so we will never have more than 4 entries
		using EntryVec = boost::container::small_vector<Entry, 4>;

		EntryVec const& GetData() const { return entries_; }

		bool HasRGBAChannel(SDK::Core::ETextureChannel rgbaChan) const
		{
			return std::find_if(entries_.begin(), entries_.end(),
				[rgbaChan](auto const& e) { return e.chanInfo.rgbaChan == rgbaChan; }) != entries_.end();
		}

		inline bool AddSourceTexture(SDK::Core::ITwinChannelMap const& itwinTexture,
			MergeImageInput const& chanInfo,
			GltfMaterialHelper const& materialHelper,
			GltfMaterialHelper::Lock const& lock)
		{
			if (!itwinTexture.HasTexture())
			{
				return false;
			}
			if (HasRGBAChannel(chanInfo.rgbaChan))
			{
				BE_ISSUE("cannot extract several source textures to the same R/G/B/A channel in target!");
				return false;
			}
			auto const texAccess = materialHelper.GetTextureAccess(itwinTexture, lock);
			if (!texAccess.IsValid())
			{
				return false;
			}
			entries_.push_back({ texAccess, chanInfo });
			return true;
		}

	private:
		EntryVec entries_;
	};

	static GltfMaterialTuner::FormatTextureResult MergeIntensityChannelsImpl(
		MergeImageInputArray const& srcTextures,
		std::filesystem::path const& outputTexPath,
		GltfMaterialHelper::Lock const& lock)
	{
		using namespace CesiumGltfReader;
		using namespace CesiumGltfContent;

		if (srcTextures.GetData().empty())
		{
			return SDK::make_unexpected(GenericFailureDetails{ "no textures to merge" });
		}
		BE_ASSERT(srcTextures.GetData().size() != 4); // This is an invariant, due to the test in AddSourceTexture...

		struct ImageWithInfo
		{
			CesiumGltf::ImageCesium img;
			MergeImageInput chanInfo;
		};
		using ImageWithInfoVec = boost::container::small_vector<ImageWithInfo, 4>;
		ImageWithInfoVec imagesWithInfo;
		for (auto const& inputData : srcTextures.GetData())
		{
			auto imgResult = GetImageCesium(inputData.srcTexAccess,
				SDK::Core::GetChannelName(inputData.chanInfo.materialChannel),
				lock);
			if (!imgResult)
			{
				return SDK::make_unexpected(imgResult.error());
			}
			imagesWithInfo.push_back(
				{ std::move(*imgResult), inputData.chanInfo });
		}

		CesiumGltf::Image outImage;
		CesiumGltf::ImageCesium& targetImg(outImage.cesium);
		targetImg.bytesPerChannel = 1;
		targetImg.channels = 4;
		targetImg.width = 0;
		targetImg.height = 0;
		for (auto const& imgWithInfo : imagesWithInfo)
		{
			CesiumGltf::ImageCesium const& img(imgWithInfo.img);
			BE_ASSERT(img.bytesPerChannel == 1 && img.channels == 4);
			targetImg.width = std::max(targetImg.width, img.width);
			targetImg.height = std::max(targetImg.height, img.height);
		}

		int32_t const nbPixels = targetImg.width * targetImg.height;

		// Initialize all pixel components with 255: this is particularly important for metallic/roughness,
		// when only the roughness texture is provided by the user: indeed, when metallic is zero everywhere,
		// roughness has absolutely no effect (that's why the default metallicRoughness texture in Cesium is
		// a 100% cyan texture (G=B=255)
		targetImg.pixelData.resize(static_cast<size_t>(
			nbPixels * targetImg.channels * targetImg.bytesPerChannel), std::byte(255));

		auto const resizeAndExtractChannel = [&](CesiumGltf::ImageCesium const& srcImage, MergeImageInput const& chanInfo)
			-> SDK::expected<bool, GenericFailureDetails>
		{
			// Resize source image if needed
			CesiumGltf::ImageCesium resizedSrcImage;
			bool const needResizing = srcImage.width != targetImg.width
				|| srcImage.height != targetImg.height;
			if (needResizing)
			{
				resizedSrcImage = targetImg;
				if (!ImageManipulation::blitImage(resizedSrcImage,
					{ 0, 0, resizedSrcImage.width, resizedSrcImage.height },
					srcImage,
					{ 0, 0, srcImage.width, srcImage.height }))
				{
					return SDK::make_unexpected(GenericFailureDetails{
						fmt::format("could not blit source {} image",
							SDK::Core::GetChannelName(chanInfo.materialChannel)) });
				}
			}
			CesiumGltf::ImageCesium const& actualSrcImage = needResizing ? resizedSrcImage : srcImage;

			// Only 8-bit supported by Cesium image reader (if an update allows 16-bit in the future, then we
			// should adapt the code in the loop on pixels, below)
			BE_ASSERT(targetImg.bytesPerChannel == 1);

			// Then extract intensity and copy it to the appropriate pixel component
			// ImageCesium uses the order R,G,B,A, which matches our enum SDK::Core::ETextureChannel and thus
			// allows to write this:
			std::byte* pTargetByte = targetImg.pixelData.data() + static_cast<size_t>(chanInfo.rgbaChan);
			std::byte const* pSrcBytes = actualSrcImage.pixelData.data();
			int32_t const offsetPerPix = targetImg.channels * targetImg.bytesPerChannel;
			for (int32_t pix(0); pix < nbPixels; ++pix)
			{
				float const fIntensByte = RED_TO_BW_FACTOR * static_cast<float>(pSrcBytes[0])
					+ GREEN_TO_BW_FACTOR * static_cast<float>(pSrcBytes[1])
					+ BLUE_TO_BW_FACTOR * static_cast<float>(pSrcBytes[2]);
				*pTargetByte = static_cast<std::byte>(std::clamp(fIntensByte, 0.f, 255.f));

				pTargetByte += offsetPerPix;
				pSrcBytes += offsetPerPix;
			}
			return true;
		};

		for (auto const& imgWithInfo : imagesWithInfo)
		{
			auto extractionResult = resizeAndExtractChannel(imgWithInfo.img, imgWithInfo.chanInfo);
			if (!extractionResult)
			{
				return SDK::make_unexpected(extractionResult.error());
			}
		}

		// Save the merged texture to PNG if needed.
		auto const saveImgOpt = SaveImageCesiumIfNeeded(outImage, outputTexPath);
		if (!saveImgOpt)
		{
			return SDK::make_unexpected(saveImgOpt.error());
		}

		return GltfMaterialTuner::FormatTextureResultData{
			outputTexPath,
			std::move(outImage)
		};
	}


	GltfMaterialTuner::GltfMaterialTuner(std::shared_ptr<GltfMaterialHelper> const& materialHelper)
		: materialHelper_(materialHelper)
	{}


	int32_t GltfMaterialTuner::ConvertTexture(SDK::Core::ITwinChannelMap const& textureMap,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		TextureKey const textureKey = { textureMap.texture, textureMap.eSource };
		auto it = itwinToGltTextures_.find(textureKey);
		if (it != itwinToGltTextures_.end())
		{
			// This iTwin texture has already been converted.
			return it->second.gltfTextureIndex_;
		}
		int32_t const gltfTexId = CreateGltfTextureFromTextureAccess(
			materialHelper_->GetTextureAccess(textureMap, lock),
			textures, images);
		itwinToGltTextures_.emplace(textureKey, GltfTextureInfo{ .gltfTextureIndex_ = gltfTexId });
		return gltfTexId;
	}


	template <typename MergeFunc>
	int32_t GltfMaterialTuner::TMergeTexturesOrFindInCache(std::string const mergedTexId,
		MergeFunc&& mergeFunc,
		bool& needTranslucentMat,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		// For now, merged textures are only stored locally. We may upload them to the decoration service if
		// we need to optimize tuning.
		TextureKey const mergedTexKey = { mergedTexId, SDK::Core::ETextureSource::LocalDisk };
		auto it = itwinToGltTextures_.find(mergedTexKey);
		if (it != itwinToGltTextures_.end())
		{
			// This combination has already been computed
			needTranslucentMat = it->second.needTranslucentMat_;
			return it->second.gltfTextureIndex_;
		}
		// Try to find it from the material helper (computed for another tile, typically).
		auto texAccess = materialHelper_->GetTextureAccess(mergedTexKey.id, mergedTexKey.eSource, lock);
		if (!texAccess.IsValid())
		{
			// First time we request this combination: compute it now, and store the resulting Cesium image.
			auto mergeResult = mergeFunc(needTranslucentMat);
			if (!mergeResult)
			{
				// An error occurred. For now, just log it.
				BE_LOGE("ITwinMaterial", "texture merge error: " << mergeResult.error().message);
				return -1;
			}
			texAccess = materialHelper_->StoreCesiumImage(mergedTexKey,
				std::move(mergeResult->cesiumImage),
				lock);
		}

		int32_t const gltfTexId = CreateGltfTextureFromTextureAccess(texAccess, textures, images);
		itwinToGltTextures_.emplace(mergedTexKey,
			GltfTextureInfo{
				.gltfTextureIndex_ = gltfTexId,
				.needTranslucentMat_ = needTranslucentMat
			});
		return gltfTexId;
	}

	int32_t GltfMaterialTuner::MergeColorAlphaTextures(
		SDK::Core::ITwinChannelMap const& colorTex,
		SDK::Core::ITwinChannelMap const& alphaTex,
		bool& needTranslucentMat,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		return TMergeTexturesOrFindInCache(colorTex.texture + "-A-" + alphaTex.texture,
			[&](bool& bTranslucent) ->FormatTextureResult
		{
			return MergeColorAlpha(colorTex, alphaTex, bTranslucent, lock);
		},
			needTranslucentMat, textures, images, lock);
	}


	GltfMaterialTuner::FormatTextureResult GltfMaterialTuner::MergeColorAlpha(
		SDK::Core::ITwinChannelMap const& colorTex,
		SDK::Core::ITwinChannelMap const& alphaTex,
		bool& needTranslucentMat,
		GltfMaterialHelper::Lock const& lock)
	{
		bool const hasColorTexture = colorTex.HasTexture();

		// Test if the merged texture already exists locally. If not, create it now.
		std::size_t const hashTex1 = hasColorTexture ? std::hash<std::string>()(colorTex.texture) : 0;
		std::size_t const hashTex2 = std::hash<std::string>()(alphaTex.texture);
		std::string const basename_MergedTex = fmt::format("c_{0:#x}-a_{1:#x}", hashTex1, hashTex2);
		std::string const basename_MergedTex_masked = basename_MergedTex + "_masked.png";
		std::string const basename_MergedTex_blend = basename_MergedTex + "_blend.png";

		auto const textureDir = materialHelper_->GetTextureDirectory(lock); // per model texture cache
		std::filesystem::path const mergedTexturePath_masked = textureDir / basename_MergedTex_masked;
		std::filesystem::path const mergedTexturePath_blend = textureDir / basename_MergedTex_blend;

#if 0
		std::error_code ec;
		bool const hasCachedMergedTex_masked = std::filesystem::exists(mergedTexturePath_masked, ec);
		bool const hasCachedMergedTex_blend = std::filesystem::exists(mergedTexturePath_blend, ec);
		// Normally, the alpha texture should be either masked or translucent, but since we use a hash of the
		// filename, the case could happen... Also, if the user edits the alpha texture file without renaming
		// it, we could also switch from one type to the other, and thus get the 2 versions here.
		BE_ASSERT(!(hasCachedMergedTex_masked && hasCachedMergedTex_blend), "2 alpha names with same hash?");
		if (hasCachedMergedTex_masked)
		{
			needTranslucentMat = false;
			return mergedTexturePath_masked;
		}
		else if (hasCachedMergedTex_blend)
		{
			needTranslucentMat = true;
			return mergedTexturePath_blend;
		}
#endif

		// Actually create a new texture now, merging color (if any) and alpha
		GltfMaterialHelper::TextureAccess const emptyAccess;
		GltfMaterialHelper::TextureAccess const colorTexAccess = hasColorTexture
			? materialHelper_->GetTextureAccess(colorTex, lock)
			: emptyAccess;
		GltfMaterialHelper::TextureAccess const alphaTexAccess = materialHelper_->GetTextureAccess(alphaTex, lock);
		if (!alphaTexAccess.IsValid())
		{
			return SDK::make_unexpected(GenericFailureDetails{ "no alpha texture to merge" });
		}
		auto const mergeRes = MergeColorAlphaFilesImpl(colorTexAccess, alphaTexAccess, needTranslucentMat, lock);
		if (!mergeRes)
		{
			return SDK::make_unexpected(mergeRes.error());
		}
		// The output file path will depend on the alpha mode (this is done like this to avoid having to
		// store this information (which depends only on the source alpha map content) elsewhere.
		std::filesystem::path const mergedTexturePath =
			needTranslucentMat ? mergedTexturePath_blend : mergedTexturePath_masked;

		auto const saveImgOpt = SaveImageCesiumIfNeeded(mergeRes->cesiumImage, mergedTexturePath);
		if (!saveImgOpt)
		{
			return SDK::make_unexpected(saveImgOpt.error());
		}
		return GltfMaterialTuner::FormatTextureResultData{
			mergedTexturePath,
			std::move(mergeRes->cesiumImage)
		};
	}


	int32_t GltfMaterialTuner::MergeMetallicRoughnessTextures(
		SDK::Core::ITwinChannelMap const& metallicTex,
		SDK::Core::ITwinChannelMap const& roughnessTex,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		bool needTranslucentMat(false); // not used here
		return TMergeTexturesOrFindInCache(metallicTex.texture + "-R-" + roughnessTex.texture,
			[&](bool& /*bTranslucent*/) ->FormatTextureResult
		{
			return MergeMetallicRoughness(metallicTex, roughnessTex, lock);
		},
			needTranslucentMat, textures, images, lock);
	}

	GltfMaterialTuner::FormatTextureResult GltfMaterialTuner::MergeIntensityChannels(
		SDK::Core::ITwinChannelMap const& tex1, MergeImageInput const& chanInfo1,
		SDK::Core::ITwinChannelMap const& tex2, MergeImageInput const& chanInfo2,
		GltfMaterialHelper::Lock const& lock)
	{
		bool const hasTexture1 = tex1.HasTexture();
		bool const hasTexture2 = tex2.HasTexture();

		// Test if the merged texture already exists locally. If not, create it now.
		std::size_t const tex1_hash = hasTexture1 ? std::hash<std::string>()(tex1.texture) : 0;
		std::size_t const tex2_hash = hasTexture2 ? std::hash<std::string>()(tex2.texture) : 0;
		std::string const basename_MergedTex = fmt::format("{0}_{1:#x}-{2}_{3:#x}.png",
			chanInfo1.matChannelShortPrefix, tex1_hash,
			chanInfo2.matChannelShortPrefix, tex2_hash);

		auto const textureDir = materialHelper_->GetTextureDirectory(lock); // per model texture cache
		std::filesystem::path const mergedTexturePath = textureDir / basename_MergedTex;

		//std::error_code ec;
		//if (std::filesystem::exists(mergedTexturePath, ec))
		//{
		//	return mergedTexturePath;
		//}

		// Actually create a new texture now, merging the one or two channels.
		MergeImageInputArray srcTextures;
		srcTextures.AddSourceTexture(tex1, chanInfo1, *materialHelper_, lock);
		srcTextures.AddSourceTexture(tex2, chanInfo2, *materialHelper_, lock);
		return MergeIntensityChannelsImpl(srcTextures, mergedTexturePath, lock);
	}

	GltfMaterialTuner::FormatTextureResult GltfMaterialTuner::MergeMetallicRoughness(
		SDK::Core::ITwinChannelMap const& metallicTex,
		SDK::Core::ITwinChannelMap const& roughnessTex,
		GltfMaterialHelper::Lock const& lock)
	{
		// Metallic -> Blue component
		// Roughness -> Green component
		return MergeIntensityChannels(
			metallicTex, MergeImageInput{ SDK::Core::EChannelType::Metallic, "metal", SDK::Core::ETextureChannel::B },
			roughnessTex, MergeImageInput{ SDK::Core::EChannelType::Roughness, "rough", SDK::Core::ETextureChannel::G },
			lock);
	}

	int32_t GltfMaterialTuner::FormatAOTexture(SDK::Core::ITwinChannelMap const& occlusionTex,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		bool needTranslucentMat(false); // not used here
		return TMergeTexturesOrFindInCache(occlusionTex.texture + "-AO",
			[&](bool& /*bTranslucent*/) ->FormatTextureResult
		{
			return FormatAO(occlusionTex, lock);
		},
			needTranslucentMat, textures, images, lock);
	}

	GltfMaterialTuner::FormatTextureResult GltfMaterialTuner::FormatAO(SDK::Core::ITwinChannelMap const& occlusionTex,
		GltfMaterialHelper::Lock const& lock)
	{
		// Roughness -> formatted alone in output texture, using Red component
		return MergeIntensityChannels(
			occlusionTex, MergeImageInput{ SDK::Core::EChannelType::AmbientOcclusion, "AO", SDK::Core::ETextureChannel::R },
			{}, MergeImageInput{ SDK::Core::EChannelType::ENUM_END, "", SDK::Core::ETextureChannel::A },
			lock);
	}

	GltfMaterialTuner::FormatTextureResult GltfMaterialTuner::ConvertChannelTextureToGltf(
		int64_t itwinMaterialId,
		SDK::Core::EChannelType const channelJustEdited,
		bool& needTranslucentMat)
	{
		GltfMaterialHelper::Lock lock(materialHelper_->GetMutex());
		// Some channels require to be merged together (color+alpha), (metallic+roughness) or
		// formatted to use a given R,G,B,A component
		if (channelJustEdited == SDK::Core::EChannelType::Alpha
			|| (channelJustEdited == SDK::Core::EChannelType::Color
				&& materialHelper_->HasChannelMap(itwinMaterialId, SDK::Core::EChannelType::Alpha, lock)))
		{
			return MergeColorAlpha(
				materialHelper_->GetChannelColorMap(itwinMaterialId, SDK::Core::EChannelType::Color, lock),
				materialHelper_->GetChannelIntensityMap(itwinMaterialId, SDK::Core::EChannelType::Alpha, lock),
				needTranslucentMat,
				lock);
		}
		if (channelJustEdited == SDK::Core::EChannelType::Metallic
			|| channelJustEdited == SDK::Core::EChannelType::Roughness)
		{
			return MergeMetallicRoughness(
				materialHelper_->GetChannelIntensityMap(itwinMaterialId, SDK::Core::EChannelType::Metallic, lock),
				materialHelper_->GetChannelIntensityMap(itwinMaterialId, SDK::Core::EChannelType::Roughness, lock),
				lock);
		}
		if (channelJustEdited == SDK::Core::EChannelType::AmbientOcclusion)
		{
			return FormatAO(
				materialHelper_->GetChannelIntensityMap(itwinMaterialId, SDK::Core::EChannelType::AmbientOcclusion, lock),
				lock);
		}
		return FormatTextureResult{};
	}

	int32_t GltfMaterialTuner::ConvertITwinMaterial(uint64_t itwinMatId,
		int32_t gltfMatId,
		std::vector<CesiumGltf::Material>& materials,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		bool& bOverrideColor,
		std::vector<std::array<uint8_t, 4>> const& meshColors)
	{
		auto it = itwinToGltfMaterial_.find(itwinMatId);
		if (it != itwinToGltfMaterial_.end())
		{
			// This iTwin material has already been converted.
			bOverrideColor = it->second.overrideColor_;
			return it->second.gltfMaterialIndex_;
		}
		BE_ASSERT(materialHelper_.get() != nullptr);
		GltfMaterialHelper::Lock lock(materialHelper_->GetMutex());
		auto const itwinMatInfo = materialHelper_->GetITwinMaterialInfo(itwinMatId, lock);
		auto const* pItwinMatDef = itwinMatInfo.second;

		if (pItwinMatDef && SDK::Core::HasCustomSettings(*pItwinMatDef))
		{
			// Initial glTF material produced by the Mesh Export Service
			CesiumGltf::Material const& orgMaterial(materials[gltfMatId]);

			// Material customized from our custom definition, initialized with the initial glTF material
			// produced by the Mesh Export Service.
			CesiumGltf::Material customMaterial(orgMaterial);

			auto const& itwinMatDef(*pItwinMatDef);
			bool const hasCustomTransparency = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Transparency);
			bool const hasCustomAlpha = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Alpha);
			bool const hasCustomAO = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::AmbientOcclusion);
			bool const hasCustomColor = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Color);
			bool const hasCustomNormal = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Normal);

			if (!customMaterial.pbrMetallicRoughness)
			{
				customMaterial.pbrMetallicRoughness.emplace();
			}

			customMaterial.pbrMetallicRoughness->roughnessFactor =
				materialHelper_->GetChannelIntensity(itwinMatId, SDK::Core::EChannelType::Roughness, lock);
			customMaterial.pbrMetallicRoughness->metallicFactor =
				materialHelper_->GetChannelIntensity(itwinMatId, SDK::Core::EChannelType::Metallic, lock);

			int32_t metallicRoughnessTexIndex = -1;
			SDK::Core::ITwinChannelMap const metallicMap =
				materialHelper_->GetChannelIntensityMap(itwinMatId, SDK::Core::EChannelType::Metallic, lock);
			SDK::Core::ITwinChannelMap const roughnessMap =
				materialHelper_->GetChannelIntensityMap(itwinMatId, SDK::Core::EChannelType::Roughness, lock);
			if (metallicMap.HasTexture() || roughnessMap.HasTexture())
			{
				metallicRoughnessTexIndex = MergeMetallicRoughnessTextures(
					metallicMap, roughnessMap, textures, images, lock);
			}
			if (metallicRoughnessTexIndex >= 0)
			{
				customMaterial.pbrMetallicRoughness->metallicRoughnessTexture.emplace();
				customMaterial.pbrMetallicRoughness->metallicRoughnessTexture->index = metallicRoughnessTexIndex;
			}
			else if (customMaterial.pbrMetallicRoughness->metallicRoughnessTexture)
			{
				// Discard obsolete metallic-roughness texture.
				// Note that following https://github.com/iTwin/imodel-native-internal/pull/698, the Mesh
				// Export Service does export a texture when the model uses several materials. This texture
				// can be discarded as we compute the same roughness/metallic values as the M.E.S in
				// GltfMaterialHelper::#GetChannelDefaultIntensity.
				customMaterial.pbrMetallicRoughness->metallicRoughnessTexture.reset();
			}

			bool hasSetAlphaMode = false;
			auto const setAlphaMode = [&](std::string const& newAlphaMode)
			{
				customMaterial.alphaMode = newAlphaMode;
				hasSetAlphaMode = true;
			};

			if (hasCustomTransparency || hasCustomAlpha)
			{
				// If this is the first time we change the transparency of this material, store its
				// initial alpha mode (could be blend, opaque or masked).
				std::string initialAlphaMode;
				if (!materialHelper_->GetInitialAlphaMode(itwinMatId, initialAlphaMode, lock))
				{
					materialHelper_->StoreInitialAlphaMode(itwinMatId, orgMaterial.alphaMode, lock);
					initialAlphaMode = orgMaterial.alphaMode;
				}
				double const alpha = (hasCustomAlpha
					? itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Alpha]->intensity
					: (1. - itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Transparency]->intensity));
				bool bEnforceOpaque = false;
				if (alpha < 1.)
				{
					// Enforce the use of the translucent base material.
					setAlphaMode(CesiumGltf::Material::AlphaMode::BLEND);
				}
				else
				{
					setAlphaMode(initialAlphaMode);
					if (initialAlphaMode == CesiumGltf::Material::AlphaMode::BLEND)
					{
						// if the model was transparent in the model (glass) and we turn it opaque, we
						// need to enforce the alpha mode as well:
						setAlphaMode(CesiumGltf::Material::AlphaMode::OPAQUE);
						bEnforceOpaque = true;
					}
				}

				SetBaseColorOpacity(*customMaterial.pbrMetallicRoughness, alpha);

				if (bEnforceOpaque && !meshColors.empty())
				{
					// In such case, we need to override the per-vertex colors, as they contain the
					// baked value of alpha, so with an opaque material, this would activate some Cesium
					// alpha dithering, which is not what the user wants if he is turning some glasses
					// totally opaque...
					// Just keep the RGB component of the original color, if any.
					auto const& baseColorU8 = meshColors[0];
					auto& baseColorFactor = customMaterial.pbrMetallicRoughness->baseColorFactor;
					baseColorFactor[0] = static_cast<double>(baseColorU8[0]) / 255.0;
					baseColorFactor[1] = static_cast<double>(baseColorU8[1]) / 255.0;
					baseColorFactor[2] = static_cast<double>(baseColorU8[2]) / 255.0;
					bOverrideColor = true;
				}
			}

			double const alpha =
				materialHelper_->GetChannelIntensity(itwinMatId, SDK::Core::EChannelType::Alpha, lock);

			if (hasCustomColor)
			{
				auto const& baseColor =
					itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Color]->color;
				customMaterial.pbrMetallicRoughness->baseColorFactor =
				{
					baseColor[0],
					baseColor[1],
					baseColor[2],
					alpha /*baseColor[3]*/
				};
				bOverrideColor = true;
			}

			// In GLTF, the base color texture is used for both color and opacity channels
			int32_t colorTexIndex = -1;

			SDK::Core::ITwinChannelMap const colorMap =
				materialHelper_->GetChannelColorMap(itwinMatId, SDK::Core::EChannelType::Color, lock);
			SDK::Core::ITwinChannelMap const alphaMap =
				materialHelper_->GetChannelIntensityMap(itwinMatId, SDK::Core::EChannelType::Alpha, lock);
			bool const hasColorTexture = colorMap.HasTexture();
			bool const hasAlphaTexture = alphaMap.HasTexture();
			if (hasAlphaTexture)
			{
				// Merge color and alpha. Beware the color map can be empty here! In such case, we just
				// fill the R,G,B values with 1.
				bool needTranslucentMat(false);
				colorTexIndex = MergeColorAlphaTextures(colorMap, alphaMap,
					needTranslucentMat, textures, images, lock);
				if (needTranslucentMat)
				{
					setAlphaMode(CesiumGltf::Material::AlphaMode::BLEND);
				}
			}
			else if (hasColorTexture && hasCustomColor)
			{
				// Custom color, without alpha
				// (Note that the original glTF already holds the initial color texture if any, so we only
				// handle the conversion here if the color channel was customized).
				colorTexIndex = ConvertTexture(colorMap, textures, images, lock);
			}
			if (colorTexIndex >= 0)
			{
				customMaterial.pbrMetallicRoughness->baseColorTexture.emplace();
				customMaterial.pbrMetallicRoughness->baseColorTexture->index = colorTexIndex;
			}
			else if (customMaterial.pbrMetallicRoughness->baseColorTexture
				&& colorMap.IsDiscarded()
				&& !hasAlphaTexture)
			{
				// Discard the existing color texture, as it was discarded by the user.
				customMaterial.pbrMetallicRoughness->baseColorTexture.reset();
			}
			if (hasAlphaTexture)
			{
				// Override the global opacity component (often 0 when the initial material uses a color
				// texture).
				SetBaseColorOpacity(*customMaterial.pbrMetallicRoughness, 1.);
			}

			SDK::Core::ITwinChannelMap const occlusionMap =
				materialHelper_->GetChannelIntensityMap(itwinMatId, SDK::Core::EChannelType::AmbientOcclusion, lock);
			if (occlusionMap.HasTexture())
			{
				int32_t const occlusionTexIndex = FormatAOTexture(occlusionMap, textures, images, lock);
				if (occlusionTexIndex >= 0)
				{
					customMaterial.occlusionTexture.emplace();
					customMaterial.occlusionTexture->index = occlusionTexIndex;
				}
				if (customMaterial.occlusionTexture && hasCustomAO)
				{
					customMaterial.occlusionTexture->strength =
						itwinMatDef.channels[(size_t)SDK::Core::EChannelType::AmbientOcclusion]->intensity;
				}
			}
			else if (customMaterial.occlusionTexture)
			{
				// Discard obsolete AO texture.
				customMaterial.occlusionTexture.reset();
			}

			SDK::Core::ITwinChannelMap const normalMap =
				materialHelper_->GetChannelColorMap(itwinMatId, SDK::Core::EChannelType::Normal, lock);
			if (normalMap.HasTexture())
			{
				int32_t const normTexIndex = ConvertTexture(normalMap, textures, images, lock);
				if (normTexIndex >= 0)
				{
					customMaterial.normalTexture.emplace();
					customMaterial.normalTexture->index = normTexIndex;
				}
				if (customMaterial.normalTexture && hasCustomNormal)
				{
					customMaterial.normalTexture->scale =
						itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Normal]->intensity;
				}
			}
			else if (customMaterial.normalTexture)
			{
				// Discard obsolete normal map.
				customMaterial.normalTexture.reset();
			}

			// Store the final alpha mode, to handle translucent switch when changing the alpha texture
			if (hasSetAlphaMode)
			{
				materialHelper_->SetCurrentAlphaMode(itwinMatId, customMaterial.alphaMode, lock);
			}

			// For now, we only support specular as a scalar value, and not the full PBR-Specular workflow.
			double const specular =
				materialHelper_->GetChannelIntensity(itwinMatId, SDK::Core::EChannelType::Specular, lock);
			if (specular > 0.)
			{
				auto& iTwinMaterialExt = customMaterial.addExtension<CesiumGltf::ExtensionITwinMaterial>();
				iTwinMaterialExt.specularFactor = specular;
			}

			// In ITwin material's shader we handle one global UV transformation for all textures.
			if (itwinMatDef.HasUVTransform())
			{
				auto const& iTwinUVTsf = itwinMatDef.uvTransform;
				auto& cesiumUVTsf = customMaterial.addExtension<CesiumGltf::ExtensionKhrTextureTransform>();
				cesiumUVTsf.scale = { iTwinUVTsf.scale[0], iTwinUVTsf.scale[1] };
				cesiumUVTsf.offset = { iTwinUVTsf.offset[0], iTwinUVTsf.offset[1] };
				cesiumUVTsf.rotation = iTwinUVTsf.rotation;
			}

			gltfMatId = static_cast<int32_t>(materials.size());
			// Append the new material - beware it may invalidate orgMaterial!
			materials.emplace_back(std::move(customMaterial));
		}

		itwinToGltfMaterial_.emplace(itwinMatId, GltfMaterialInfo{ gltfMatId, bOverrideColor });
		return gltfMatId;
	}


} // namespace BeUtils
