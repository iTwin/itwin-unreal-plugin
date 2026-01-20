/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialTuner.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "GltfMaterialHelper.h"

#include <CesiumGltf/Image.h>
#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>

#include <SDK/Core/Tools/Error.h>

#include <functional>


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
		struct GltfMaterialInfo
		{
			int32_t gltfMaterialIndex_ = 0; // index in the array of glTF materials
			bool hasCustomDefinition_ = false;
			bool overrideColor_ = false; // whether we should override the colors baked in mesh vertices
		};

		GltfMaterialTuner(std::shared_ptr<GltfMaterialHelper> const& materialHelper);

		bool CanConvertITwinMaterials() const { return (bool)materialHelper_; }

		//! Converts the given iTwin material (referenced by its unique ID itwinMatId) into a glTF material
		//! if it holds some customizations compared to the original model exported by the Mesh Export
		//! Service. In such case, a new glTF material may be created and appended to the array of glTF
		//! materials. New glTF textures and images may be created during this process.
		void ConvertITwinMaterial(uint64_t itwinMatId,
			int32_t gltfMatId,
			std::vector<CesiumGltf::Material>& materials,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialInfo& matInfo,
			std::vector<std::array<uint8_t, 4>> const& meshColors);


		struct FormatTextureResultData
		{
			std::filesystem::path const filePath = {};
			CesiumGltf::Image cesiumImage;
		};

		using FormatTextureResult = AdvViz::expected<
			FormatTextureResultData,
			GenericFailureDetails>;


		enum ESaveImageAction
		{
			None,
			Saved
		};
		using SaveCesiumImageResult = AdvViz::expected<
			ESaveImageAction,
			GenericFailureDetails>;
		static SaveCesiumImageResult SaveImageCesium(
			CesiumGltf::Image const& image, std::filesystem::path const& outputTexPath);

		using LoadCesiumImageResult = AdvViz::expected<
			bool,
			GenericFailureDetails>;
		static LoadCesiumImageResult LoadImageCesium(
			CesiumGltf::Image& image, std::vector<std::byte> const& buffer,
			std::string const& contextInfo);

		using LoadTextureBufferFunc = std::function<bool(	AdvViz::SDK::TextureKey const&,
															GltfMaterialHelper const&,
															RWLockBase const&,
															std::vector<std::byte>& outBuffer,
															std::string& strError)>;

		static void ConnectLoadTextureBufferFunc(LoadTextureBufferFunc&& func);

		static bool LoadTextureBuffer(AdvViz::SDK::TextureKey const& texKey,
			GltfMaterialHelper const& matHelper,
			RWLockBase const& lock,
			std::vector<std::byte>& cesiumBuffer,
			std::string& strError);

		static LoadCesiumImageResult ResampleTextureBuffer(
			std::vector<std::byte> const& fullSizeCesiumBuffer,
			std::vector<std::byte>& thumbnailCesiumBuffer,
			uint32_t desiredSize,
			std::string const& contextInfo);

	protected:
		//! Merge color (if any) and alpha textures into one single texture, and return the resulting image
		//! path (or an error).
		FormatTextureResult MergeColorAlpha(AdvViz::SDK::ITwinChannelMap const& colorTex,
			AdvViz::SDK::ITwinChannelMap const& alphaTex,
			bool& needTranslucentMat,
			WLock const& lock);

		FormatTextureResult MergeMetallicRoughness(AdvViz::SDK::ITwinChannelMap const& metallicTex,
			AdvViz::SDK::ITwinChannelMap const& roughnessTex,
			WLock const& lock);

		FormatTextureResult FormatAO(AdvViz::SDK::ITwinChannelMap const& occlusionTex,
			WLock const& lock);

		/// To improve logging, typically.
		std::string GetMaterialContextInfo(RWLockBase const& lock) const;

	private:
		inline int32_t CreateGltfTextureFromTextureAccess(
			GltfMaterialHelper::TextureAccess const& texAccess,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock) const;

		int32_t ConvertTexture(AdvViz::SDK::ITwinChannelMap const& textureMap,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock);

		int32_t MergeColorAlphaTextures(AdvViz::SDK::ITwinChannelMap const& colorTex,
			AdvViz::SDK::ITwinChannelMap const& alphaTex,
			bool& needTranslucentMat,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock);

		FormatTextureResult MergeColorAlphaFilesImpl(
			GltfMaterialHelper::TextureAccess const& colorTexture,
			GltfMaterialHelper::TextureAccess const& alphaTexture,
			bool& isTranslucencyNeeded,
			WLock const& lock) const;

		//! Generic merge method between one or two intensity channels.
		FormatTextureResult MergeIntensityChannels(
			AdvViz::SDK::ITwinChannelMap const& tex1, MergeImageInput const& chanInfo1,
			AdvViz::SDK::ITwinChannelMap const& tex2, MergeImageInput const& chanInfo2,
			WLock const& lock);

		class MergeImageInputArray;
		FormatTextureResult MergeIntensityChannelsImpl(
			MergeImageInputArray const& srcTextures,
			std::filesystem::path const& outputTexPath,
			WLock const& lock) const;

		int32_t MergeMetallicRoughnessTextures(AdvViz::SDK::ITwinChannelMap const& metallicTex,
			AdvViz::SDK::ITwinChannelMap const& roughnessTex,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock);


		int32_t FormatAOTexture(AdvViz::SDK::ITwinChannelMap const& occlusionTex,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock);


		int32_t CreateGltfTextureFromMerged(std::string const mergedTexId,
			bool& needTranslucentMat,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			RLock const& lock);


	protected:
		struct [[nodiscard]] ScopedMaterialId
		{
			ScopedMaterialId(GltfMaterialTuner& owner, uint64_t matId)
				: owner_(owner)
				, prevId_(owner.currentItwinMatId_) {
				owner_.currentItwinMatId_ = matId;
			}
			~ScopedMaterialId() {
				owner_.currentItwinMatId_ = prevId_;
			}
			GltfMaterialTuner& owner_;
			std::optional<uint64_t> const prevId_;
		};

		std::shared_ptr<GltfMaterialHelper> const& materialHelper_;

	private:
		std::unordered_map<uint64_t, GltfMaterialInfo> itwinToGltfMaterial_; // iTwin MaterialId -> glTF Material index

		struct GltfTextureInfo
		{
			int32_t gltfTextureIndex_ = 0; // index in the array of glTF textures
			bool needTranslucentMat_ = false; // whether we need to activate translucency (for alpha/color textures)
		};
		using TextureKey = AdvViz::SDK::TextureKey;
		std::unordered_map<TextureKey, GltfTextureInfo> itwinToGltTextures_; // iTwin TextureId -> glTF Texture index

		std::optional<uint64_t> currentItwinMatId_; // Additional information, to improve logs

		// External function to load texture bytes (not in BeUtils as it may need to rely on a specific file
		// system like in Unreal...)
		static LoadTextureBufferFunc loadTextureBufferFunc_;
	};


	/// Responsible for the preparation of textures (merging the different channels sharing a same texture in
	/// glTF format. It should be done before any tuning occurs.
	class ITwinToGltfTextureConverter : public GltfMaterialTuner
	{
		using Super = GltfMaterialTuner;

	public:
		ITwinToGltfTextureConverter(std::shared_ptr<GltfMaterialHelper> const& materialHelper);

		//! Format the newly added texture if needed, and return the resulting image path (empty if no
		//! formatting was required).
		//! Examples of such formatting: merge of opacity and color in a single texture, idem for metallic
		//! and roughness in dedicated channels...)
		GltfMaterialHelper::TextureAccess ConvertChannelTextureToGltf(
			uint64_t itwinMaterialId,
			AdvViz::SDK::EChannelType const channelJustEdited,
			bool& needTranslucentMat,
			WLock const& lock);
		GltfMaterialHelper::TextureAccess ConvertChannelTextureToGltf(
			uint64_t itwinMaterialId,
			AdvViz::SDK::EChannelType const channelJustEdited,
			bool& needTranslucentMat);

		//! Format all textures before any tuning occurs.
		//! Should be called in game thread as soon as the material definitions are loaded.
		void ConvertTexturesToGltf(uint64_t itwinMatId, WLock const& lock);

	private:
		template <typename MergeFunc>
		GltfMaterialHelper::TextureAccess TMergeTexturesInHelper(
			std::string const mergedTexId,
			MergeFunc&& mergeFunc,
			bool& needTranslucentMat,
			WLock const& lock);
	};

} // namespace BeUtils
