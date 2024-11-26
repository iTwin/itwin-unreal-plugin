/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialTuner.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <BeUtils/Gltf/GltfMaterialTuner.h>

#include <CesiumGltfContent/ImageManipulation.h>
#include <CesiumGltfReader/GltfReader.h>

#include <algorithm>
#include <fstream>
#include <spdlog/fmt/fmt.h>
#include <SDK/Core/Tools/Assert.h>

namespace BeUtils
{

	namespace
	{

		/// Return true if this material holds a definition for the given channel.
		inline bool DefinesChannel(SDK::Core::ITwinMaterial const& mat, SDK::Core::EChannelType channel)
		{
			return mat.channels[(size_t)channel].has_value();
		}

		inline bool HasDefinedChannels(SDK::Core::ITwinMaterial const& mat)
		{
			return std::find_if(mat.channels.begin(), mat.channels.end(),
				[](auto&& chan) { return chan.has_value(); }) != mat.channels.end();
		}

		//inline size_t CountDefinedChannels(SDK::Core::ITwinMaterial const& mat)
		//{
		//	return std::count_if(mat.channels.begin(), mat.channels.end(),
		//		[](auto&& chan) { return chan.has_value(); });
		//}

		/// Append a new glTF texture pointing at the given file path (converted to a local URI)
		inline int32_t CreateGltfTextureFromLocalPath(std::filesystem::path const& texturePath,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images)
		{
			int32_t gltfTexId = -1;
			if (!texturePath.empty())
			{
				// Create one gltf image and one gltf texture
				gltfTexId = static_cast<int32_t>(textures.size());
				CesiumGltf::Texture& gltfTexture = textures.emplace_back();
				gltfTexture.source = static_cast<int32_t>(images.size());
				CesiumGltf::Image& gltfImage = images.emplace_back();

				// since c++20, this uses now char8_t...
				auto const tex_u8string = texturePath.generic_u8string();
				gltfImage.uri = "file:///" + std::string(tex_u8string.begin(), tex_u8string.end());
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

		static std::vector<std::byte> ReadPngFile(const std::filesystem::path& fileName)
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

		static bool WritePngFile(std::vector<std::byte> const& pngBuffer, const std::filesystem::path& fileName)
		{
			std::ofstream file(fileName, std::ios::binary | std::ios::out);
			if (!file)
			{
				return {};
			}
			file.write(reinterpret_cast<char const*>(pngBuffer.data()), pngBuffer.size());
			return !file.fail();
		}

	}


#define RED_TO_BW_FACTOR	0.35f
#define GREEN_TO_BW_FACTOR	0.50f
#define BLUE_TO_BW_FACTOR	0.15f

	static std::vector<std::byte> MergeColorAlphaFilesImpl(
		std::filesystem::path const& colorTexturePath,
		std::filesystem::path const& alphaTexturePath,
		bool& isTranslucencyNeeded)
	{
		using namespace CesiumGltfReader;
		using namespace CesiumGltfContent;

		isTranslucencyNeeded = false;

		bool const hasColorTexture = !colorTexturePath.empty();
		ImageReaderResult colorImageResult;
		if (hasColorTexture)
		{
			std::vector<std::byte> const colorPngData = ReadPngFile(colorTexturePath);
			colorImageResult = GltfReader::readImage(colorPngData, CesiumGltf::Ktx2TranscodeTargets{});
			if (!colorImageResult.image)
				return {};
		}
		std::vector<std::byte> const alphaPngData = ReadPngFile(alphaTexturePath);
		ImageReaderResult alphaImageResult =
			GltfReader::readImage(alphaPngData, CesiumGltf::Ktx2TranscodeTargets{});
		if (!alphaImageResult.image)
			return {};

		CesiumGltf::ImageCesium const& alphaImg(*alphaImageResult.image);

		// When this code was written, only 8-bit was supported by Cesium image reader, and both PNG and JPG
		// images were created as RGBA (see GltfReader.cpp)
		BE_ASSERT(alphaImg.bytesPerChannel == 1);
		BE_ASSERT(alphaImg.channels == 4);

		CesiumGltf::ImageCesium targetImg;
		targetImg.channels = 4;
		if (hasColorTexture)
		{
			CesiumGltf::ImageCesium const& colorImg(*colorImageResult.image);
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
			CesiumGltf::ImageCesium const& colorImg(*colorImageResult.image);
			if (!ImageManipulation::blitImage(targetImg,
				{ 0, 0, targetImg.width, targetImg.height },
				colorImg,
				{ 0, 0, colorImg.width, colorImg.height }))
			{
				return {};
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
			return {};
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
		return ImageManipulation::savePng(targetImg);
	}


	GltfMaterialTuner::GltfMaterialTuner(std::shared_ptr<GltfMaterialHelper> const& materialHelper)
		: materialHelper_(materialHelper)
	{}


	int32_t GltfMaterialTuner::ConvertITwinTexture(std::string const& itwinTextureId,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		auto it = itwinToGltTextures_.find(itwinTextureId);
		if (it != itwinToGltTextures_.end())
		{
			// This iTwin texture has already been converted.
			return it->second.gltfTextureIndex_;
		}
		// Test if the corresponding texture is available locally.
		auto const& texturePath = materialHelper_->GetITwinTextureLocalPath(itwinTextureId, lock);
		int32_t const gltfTexId = CreateGltfTextureFromLocalPath(texturePath, textures, images);
		itwinToGltTextures_.emplace(itwinTextureId, GltfTextureInfo{ .gltfTextureIndex_ = gltfTexId });
		return gltfTexId;
	}


	int32_t GltfMaterialTuner::MergeColorAlphaTextures(std::string const& colorTexId,
		std::string const& alphaTexId,
		bool& needTranslucentMat,
		std::vector<CesiumGltf::Texture>& textures,
		std::vector<CesiumGltf::Image>& images,
		GltfMaterialHelper::Lock const& lock)
	{
		std::string const mergedTexId = colorTexId + "-A-" + alphaTexId;
		auto it = itwinToGltTextures_.find(mergedTexId);
		if (it != itwinToGltTextures_.end())
		{
			// This combination color+alpha has already been computed
			needTranslucentMat = it->second.needTranslucentMat_;
			return it->second.gltfTextureIndex_;
		}

		std::filesystem::path const mergedTexturePath = MergeColorAlpha(colorTexId, alphaTexId, needTranslucentMat, lock);
		if (mergedTexturePath.empty())
		{
			return -1;
		}

		int32_t const gltfTexId = CreateGltfTextureFromLocalPath(mergedTexturePath, textures, images);
		itwinToGltTextures_.emplace(mergedTexId,
			GltfTextureInfo{
				.gltfTextureIndex_ = gltfTexId,
				.needTranslucentMat_ = needTranslucentMat
			});
		return gltfTexId;
	}


	std::filesystem::path GltfMaterialTuner::MergeColorAlpha(std::string const& colorTexId,
		std::string const& alphaTexId,
		bool& needTranslucentMat,
		GltfMaterialHelper::Lock const& lock)
	{
		bool const hasColorTexture = !colorTexId.empty();
		std::filesystem::path const emptyPath;
		std::filesystem::path const& colorTexturePath = hasColorTexture
			? materialHelper_->GetITwinTextureLocalPath(colorTexId, lock)
			: emptyPath;
		std::filesystem::path const& alphaTexturePath = materialHelper_->GetITwinTextureLocalPath(alphaTexId, lock);
		if (alphaTexturePath.empty())
		{
			BE_ISSUE("no alpha texture to merge");
			return {};
		}
		// Test if the merged texture already exists locally. If not, create it now.
		std::size_t const hashTex1 = hasColorTexture ? std::hash<std::string>()(colorTexId) : 0;
		std::size_t const hashTex2 = std::hash<std::string>()(alphaTexId);
		std::string const basename_MergedTex = fmt::format("c_{0:#x}-a_{1:#x}", hashTex1, hashTex2);
		std::string const basename_MergedTex_masked = basename_MergedTex + "_masked.png";
		std::string const basename_MergedTex_blend = basename_MergedTex + "_blend.png";

		auto const textureDir = materialHelper_->GetTextureDirectory(lock); // per model texture cache
		std::filesystem::path const mergedTexturePath_masked = textureDir / basename_MergedTex_masked;
		std::filesystem::path const mergedTexturePath_blend = textureDir / basename_MergedTex_blend;
		std::error_code ec;
		bool const hasCachedMergedTex_masked = std::filesystem::exists(mergedTexturePath_masked, ec);
		bool const hasCachedMergedTex_blend = std::filesystem::exists(mergedTexturePath_blend, ec);
		// Normally, the alpha texture should be either masked or translucent, but since we use a hash of the
		// filename, the case could happen... Also, if the user edits the alpha texture file without renaming
		// it, we could also switch from one type to the other, and thus get the 2 versions here.
		std::filesystem::path mergedTexturePath;
		BE_ASSERT(!(hasCachedMergedTex_masked && hasCachedMergedTex_blend), "2 alpha names with same hash?");
		if (hasCachedMergedTex_masked)
		{
			mergedTexturePath = mergedTexturePath_masked;
			needTranslucentMat = false;
		}
		else if (hasCachedMergedTex_blend)
		{
			mergedTexturePath = mergedTexturePath_blend;
			needTranslucentMat = true;
		}
		else
		{
			// Actually create a new texture now, merging color and alpha
			auto const pngOutData = MergeColorAlphaFilesImpl(colorTexturePath, alphaTexturePath, needTranslucentMat);
			if (pngOutData.empty())
			{
				return {};
			}
			mergedTexturePath = needTranslucentMat ? mergedTexturePath_blend : mergedTexturePath_masked;
			if (!WritePngFile(pngOutData, mergedTexturePath))
			{
				return {};
			}
		}
		BE_ASSERT(!mergedTexturePath.empty() && std::filesystem::exists(mergedTexturePath, ec));
		return mergedTexturePath;
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

		if (pItwinMatDef && HasDefinedChannels(*pItwinMatDef))
		{
			// Initial glTF material produced by the Mesh Export Service
			CesiumGltf::Material const& orgMaterial(materials[gltfMatId]);

			// Material customized from our custom definition, initialized with the initial glTF material
			// produced by the Mesh Export Service.
			CesiumGltf::Material customMaterial(orgMaterial);

			auto const& itwinMatDef(*pItwinMatDef);
			bool const hasCustomRoughness = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Roughness);
			bool const hasCustomMetallic = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Metallic);
			bool const hasCustomTransparency = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Transparency);
			bool const hasCustomAlpha = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Alpha);
			// Color edition (added after the YII)
			bool const hasCustomColor = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Color);
			bool const hasCustomNormal = DefinesChannel(itwinMatDef, SDK::Core::EChannelType::Normal);

			if (!customMaterial.pbrMetallicRoughness)
			{
				customMaterial.pbrMetallicRoughness.emplace();
			}

			if (hasCustomRoughness)
			{
				customMaterial.pbrMetallicRoughness->roughnessFactor =
					itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Roughness]->intensity;
			}
			if (hasCustomMetallic)
			{
				customMaterial.pbrMetallicRoughness->metallicFactor =
					itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Metallic]->intensity;
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

			double const dTransparency =
				materialHelper_->GetChannelIntensity(itwinMatId, SDK::Core::EChannelType::Transparency, lock);

			if (hasCustomColor)
			{
				auto const& baseColor =
					itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Color]->color;
				customMaterial.pbrMetallicRoughness->baseColorFactor =
				{
					baseColor[0],
					baseColor[1],
					baseColor[2],
					(1. - dTransparency) /*baseColor[3]*/
				};
				bOverrideColor = true;
			}

			// In GLTF, the base color texture is used for both color and opacity channels
			int32_t colorTexIndex = -1;

			SDK::Core::ITwinChannelMap const colorMap =
				materialHelper_->GetChannelColorMap(itwinMatId, SDK::Core::EChannelType::Color, lock);
			SDK::Core::ITwinChannelMap const alphaMap =
				materialHelper_->GetChannelIntensityMap(itwinMatId, SDK::Core::EChannelType::Alpha, lock);
			bool const hasColorTexture = !colorMap.IsEmpty();
			bool const hasAlphaTexture = !alphaMap.IsEmpty();
			if (hasAlphaTexture)
			{
				// Merge color and alpha. Beware the color map can be empty here! In such case, we just
				// fill the R,G,B values with 1.
				bool needTranslucentMat(false);
				colorTexIndex = MergeColorAlphaTextures(colorMap.texture, alphaMap.texture,
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
				colorTexIndex = ConvertITwinTexture(colorMap.texture, textures, images, lock);
			}
			if (colorTexIndex >= 0)
			{
				customMaterial.pbrMetallicRoughness->baseColorTexture.emplace();
				customMaterial.pbrMetallicRoughness->baseColorTexture->index = colorTexIndex;
			}
			if (hasAlphaTexture)
			{
				// Override the global opacity component (often 0 when the initial material uses a color
				// texture).
				SetBaseColorOpacity(*customMaterial.pbrMetallicRoughness, 1.);
			}

			if (hasCustomNormal)
			{
				auto const& normalMap =
					itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Normal]->colorMap;
				int32_t normTexIndex = ConvertITwinTexture(normalMap.texture, textures, images, lock);
				if (normTexIndex >= 0)
				{
					customMaterial.normalTexture.emplace();
					customMaterial.normalTexture->index = normTexIndex;
					customMaterial.normalTexture->scale =
						itwinMatDef.channels[(size_t)SDK::Core::EChannelType::Normal]->intensity;
				}
			}

			// Store the final alpha mode, to handle translucent switch when changing the alpha texture
			if (hasSetAlphaMode)
			{
				materialHelper_->SetCurrentAlphaMode(itwinMatId, customMaterial.alphaMode, lock);
			}

			gltfMatId = static_cast<int32_t>(materials.size());
			// Append the new material - beware it may invalidate orgMaterial!
			materials.emplace_back(std::move(customMaterial));
		}

		itwinToGltfMaterial_.emplace(itwinMatId, GltfMaterialInfo{ gltfMatId, bOverrideColor });
		return gltfMatId;
	}


} // namespace BeUtils
