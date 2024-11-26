/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialTuner.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "GltfMaterialHelper.h"

#include <CesiumGltf/Image.h>
#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>


namespace BeUtils
{

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

		//! Merge color (if any) and alpha textures into one single texture, and return the resulting image
		//! path (empty in case of any error).
		std::filesystem::path MergeColorAlpha(std::string const& colorTexId,
			std::string const& alphaTexId,
			bool& needTranslucentMat,
			GltfMaterialHelper::Lock const& lock);

	private:
		int32_t ConvertITwinTexture(std::string const& itwinTextureId,
			std::vector<CesiumGltf::Texture>& textures,
			std::vector<CesiumGltf::Image>& images,
			GltfMaterialHelper::Lock const& lock);

		int32_t MergeColorAlphaTextures(std::string const& colorTexId,
			std::string const& alphaTexId,
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
		std::unordered_map<std::string, GltfTextureInfo> itwinToGltTextures_; // iTwin TextureId -> glTF Texture index
	};

} // namespace BeUtils
