/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialTuner.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "GltfMaterialHelper.h"

#include <CesiumGltf/Image.h>
#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>

#include <SDK/Core/Tools/Error.h>


namespace BeUtils
{

	struct GenericFailureDetails
	{
		/**
		 * @brief A human-readable explanation of what failed.
		 */
		std::string message = "";
	};

	struct MergeImageInput;


	//! Class used during the tuning a glTF model.
	//! Responsible for the conversion of iTwin material definitions into valid glTF materials.
	class GltfMaterialTuner
	{
	public:
		GltfMaterialTuner(std::shared_ptr<GltfMaterialHelper> const& materialHelper);

		bool CanConvertITwinMaterials() const { return (bool)materialHelper_; }

		//! Converts the given iTwin material (referenced by its unique ID itwinMatId) into a glTF material
		//! if it holds some customizations compared to the original model exported by the Mesh Export
		//! Service. In such case, a new glTF material may be created and appended to the array of glTF
		//! materials. New glTF textures and images may be created during this process.
		int32_t ConvertITwinMaterial(uint64_t itwinMatId,
			int32_t gltfMatId,
			std::vector<CesiumGltf::Material>& materials,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			bool& bOverrideColor,
			std::vector<std::array<uint8_t, 4>> const& meshColors);

		struct FormatTextureResultData
		{
			std::filesystem::path const filePath = {};
			CesiumGltf::Image cesiumImage;
		};

		//! Format the newly added texture if needed, and return the resulting image path (empty if no
		//! formatting was required).
		//! Examples of such formatting: merge of opacity and color in a single texture, idem for metallic
		//! and roughness in dedicated channels...)

		using FormatTextureResult = SDK::expected<
			FormatTextureResultData,
			GenericFailureDetails>;
		FormatTextureResult ConvertChannelTextureToGltf(
			int64_t itwinMaterialId,
			SDK::Core::EChannelType const channelJustEdited,
			bool& needTranslucentMat);

		enum ESaveImageAction
		{
			None,
			Saved
		};
		using SaveCesiumImageResult = SDK::expected<
			ESaveImageAction,
			GenericFailureDetails>;
		static SaveCesiumImageResult SaveImageCesium(
			CesiumGltf::Image const& image, std::filesystem::path const& outputTexPath);

	private:
		int32_t ConvertTexture(SDK::Core::ITwinChannelMap const& textureMap,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);

		int32_t MergeColorAlphaTextures(SDK::Core::ITwinChannelMap const& colorTex,
			SDK::Core::ITwinChannelMap const& alphaTex,
			bool& needTranslucentMat,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);

		//! Merge color (if any) and alpha textures into one single texture, and return the resulting image
		//! path (or an error).
		FormatTextureResult MergeColorAlpha(SDK::Core::ITwinChannelMap const& colorTex,
			SDK::Core::ITwinChannelMap const& alphaTex,
			bool& needTranslucentMat,
			GltfMaterialHelper::Lock const& lock);


		//! Generic merge method between one or two intensity channels.
		FormatTextureResult MergeIntensityChannels(
			SDK::Core::ITwinChannelMap const& tex1, MergeImageInput const& chanInfo1,
			SDK::Core::ITwinChannelMap const& tex2, MergeImageInput const& chanInfo2,
			GltfMaterialHelper::Lock const& lock);

		int32_t MergeMetallicRoughnessTextures(SDK::Core::ITwinChannelMap const& metallicTex,
			SDK::Core::ITwinChannelMap const& roughnessTex,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);

		FormatTextureResult MergeMetallicRoughness(SDK::Core::ITwinChannelMap const& metallicTex,
			SDK::Core::ITwinChannelMap const& roughnessTex, GltfMaterialHelper::Lock const& lock);


		int32_t FormatAOTexture(SDK::Core::ITwinChannelMap const& occlusionTex,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);

		FormatTextureResult FormatAO(SDK::Core::ITwinChannelMap const& occlusionTex, GltfMaterialHelper::Lock const& lock);


		template <typename MergeFunc>
		int32_t TMergeTexturesOrFindInCache(std::string const mergedTexId,
			MergeFunc&& mergeFunc,
			bool& needTranslucentMat,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);


	private:
		std::shared_ptr<GltfMaterialHelper> const& materialHelper_;

		struct GltfMaterialInfo
		{
			int32_t gltfMaterialIndex_ = 0; // index in the array of glTF materials
			bool overrideColor_ = false; // whether we should override the colors baked in mesh vertices
		};
		std::unordered_map<uint64_t, GltfMaterialInfo> itwinToGltfMaterial_; // iTwin MaterialId -> glTF Material index

		struct GltfTextureInfo
		{
			int32_t gltfTextureIndex_ = 0; // index in the array of glTF textures
			bool needTranslucentMat_ = false; // whether we need to activate translucency (for alpha/color textures)
		};
		using TextureKey = SDK::Core::TextureKey;
		std::unordered_map<TextureKey, GltfTextureInfo> itwinToGltTextures_; // iTwin TextureId -> glTF Texture index
	};

} // namespace BeUtils
