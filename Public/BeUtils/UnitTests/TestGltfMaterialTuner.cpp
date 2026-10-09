/*--------------------------------------------------------------------------------------+
|
|     $Source: TestGltfMaterialTuner.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>
#include <BeUtils/Gltf/GltfMaterialTuner.h>

#include <BeUtils/Gltf/GltfMaterialHelper.h>
#include <BeHeaders/Util/CleanUpGuard.h>
#include <SDK/Core/Tools/Tools.h>
#include <SDK/Core/Visualization/TextureKey.h>

#include <CesiumGltf/Material.h>
#include <CesiumGltf/Texture.h>
#include <CesiumGltf/Image.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace BeUtils;
using namespace AdvViz::SDK;

namespace
{
	constexpr uint64_t TEST_MATERIAL_ID = 12345;
	constexpr uint64_t TEST_MATERIAL_ID_2 = 678910;
	constexpr uint64_t TEST_MATERIAL_ID_3 = 111213;

	// Helper to create test texture files
	struct TestTextureFiles
	{
		std::filesystem::path colorTexture;
		std::filesystem::path alphaTexture_mask;
		std::filesystem::path alphaTexture_blend;
		std::filesystem::path normalTexture;
		std::filesystem::path testDir;

		TestTextureFiles()
		{
			testDir = std::filesystem::path(BEUTILS_TEST_DATA_DIR);
			colorTexture = testDir / "color.png";
			alphaTexture_mask = testDir / "alpha_mask_arrow.png";
			alphaTexture_blend = testDir / "alpha_mask_hlfoot.png";
			normalTexture = testDir / "normal.png";
		}

		bool HasRequiredTextures() const
		{
			std::error_code ec;
			return std::filesystem::exists(colorTexture, ec)
				&& std::filesystem::exists(alphaTexture_mask, ec)
				&& std::filesystem::exists(alphaTexture_blend, ec)
				&& std::filesystem::exists(normalTexture, ec);
		}
	};

	struct GltfMaterialStructures
	{
		std::vector<CesiumGltf::Material> materials;
		std::vector<CesiumGltf::Texture> textures;
		std::vector<CesiumGltf::Image> images;
		std::vector<std::array<uint8_t, 4>> meshColors;

		GltfMaterialStructures(size_t numBaseMaterials)
		{
			// Create base glTF materials.
			for (size_t i = 0; i < numBaseMaterials; ++i)
			{
				CesiumGltf::Material baseMaterial;
				baseMaterial.name = "BaseMaterial_" + std::to_string(i);
				materials.push_back(baseMaterial);
			}
		}
	};

	/*static*/
	std::vector<std::byte> ReadFile(const std::filesystem::path& fileName)
	{
		std::ifstream file(fileName, std::ios::binary | std::ios::ate);
		REQUIRE(file);

		std::streamoff size = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<std::byte> buffer{ size_t(size) };
		file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(size));

		return buffer;
	}
}

TEST_CASE("GltfMaterialTuner - Basic Construction", "[GltfMaterialTuner]")
{
	Tools::CreateAdvVizLogChannels();

	auto materialHelper = std::make_shared<GltfMaterialHelper>();
	GltfMaterialTuner tuner(materialHelper);

	SECTION("Can convert iTwin materials when helper is provided")
	{
		CHECK(tuner.CanConvertITwinMaterials() == true);
	}

	SECTION("Cannot convert without material helper")
	{
		GltfMaterialTuner emptyTuner(nullptr);
		CHECK(emptyTuner.CanConvertITwinMaterials() == false);
	}
}

TEST_CASE("GltfMaterialTuner - ConvertITwinMaterial with Color and Alpha Textures", "[GltfMaterialTuner]")
{
	// Setup test texture files
	TestTextureFiles testTextures;
	REQUIRE(testTextures.HasRequiredTextures());

	// Create material helper and tuner
	auto materialHelper = std::make_shared<GltfMaterialHelper>();
	GltfMaterialTuner tuner(materialHelper);


	// Create temporary texture directory
	std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "gltf_material_tuner_test";
	{
		WLock lock(materialHelper->GetMutex());
		materialHelper->SetTextureDirectory(tempDir, lock, true);
	}

	Be::CleanUpGuard cleanupGuard([&tempDir]() {
		std::error_code ec;
		std::filesystem::remove_all(tempDir, ec);
	});

	SECTION("Convert material with both color and alpha textures")
	{
		// Create 2 materials with both color and alpha textures:
		// - one with a masked alpha texture (fully opaque or fully transparent)
		// - one with a translucent alpha texture (semi-transparent)
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterialWithTextures-masked", 
												WLock(materialHelper->GetMutex()));
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID_2, "TestMaterialWithTextures-translucent",
			WLock(materialHelper->GetMutex()));

		// Register the texture files with the material helper
		std::string colorTextureId = materialHelper->FindOrCreateTextureID(testTextures.colorTexture);
		std::string alphaTextureId_mask = materialHelper->FindOrCreateTextureID(testTextures.alphaTexture_mask);
		std::string alphaTextureId_blend = materialHelper->FindOrCreateTextureID(testTextures.alphaTexture_blend);

		// Set color map
		ITwinChannelMap colorMap;
		colorMap.texture = colorTextureId;
		colorMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		materialHelper->SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);
		REQUIRE(modified == true);

		modified = false;
		materialHelper->SetChannelColorMap(TEST_MATERIAL_ID_2, EChannelType::Color, colorMap, modified);
		REQUIRE(modified == true);

		// Set alpha/opacity map
		ITwinChannelMap alphaMap;
		alphaMap.texture = alphaTextureId_mask;
		alphaMap.eSource = ETextureSource::LocalDisk;
		modified = false;
		materialHelper->SetChannelIntensityMap(TEST_MATERIAL_ID, EChannelType::Opacity, alphaMap, modified);
		REQUIRE(modified == true);

		ITwinChannelMap alphaMap_2;
		alphaMap_2.texture = alphaTextureId_blend;
		alphaMap_2.eSource = ETextureSource::LocalDisk;
		modified = false;
		materialHelper->SetChannelIntensityMap(TEST_MATERIAL_ID_2, EChannelType::Opacity, alphaMap_2, modified);
		REQUIRE(modified == true);

		// Verify material has textures
		{
			RLock lock(materialHelper->GetMutex());
			CHECK(materialHelper->HasChannelMap(TEST_MATERIAL_ID, EChannelType::Color, lock));
			CHECK(materialHelper->HasChannelMap(TEST_MATERIAL_ID, EChannelType::Opacity, lock));
			CHECK(materialHelper->HasChannelMap(TEST_MATERIAL_ID_2, EChannelType::Color, lock));
			CHECK(materialHelper->HasChannelMap(TEST_MATERIAL_ID_2, EChannelType::Opacity, lock));
		}

		// Convert the textures to glTF format.
		{
			WLock lock(materialHelper->GetMutex());
			ITwinToGltfTextureConverter texConverter(materialHelper);
			texConverter.ConvertTexturesToGltf(TEST_MATERIAL_ID, lock);
			texConverter.ConvertTexturesToGltf(TEST_MATERIAL_ID_2, lock);
		}

		// Prepare glTF structures
		GltfMaterialStructures gltfStructures(2); // 2 base materials

		// Convert the two iTwin materials.
		int32_t gltfMatId = 0;
		GltfMaterialTuner::GltfMaterialInfo matInfo;
		REQUIRE_NOTHROW(tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID,
			gltfMatId,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo,
			gltfStructures.meshColors
		));

		int32_t gltfMatId_2 = 1;
		GltfMaterialTuner::GltfMaterialInfo matInfo_2;
		REQUIRE_NOTHROW(tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID_2,
			gltfMatId_2,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo_2,
			gltfStructures.meshColors
		));

		// Verify conversion results
		SECTION("Material was customized")
		{
			CHECK(matInfo.hasCustomDefinition_ == true);
			CHECK(matInfo_2.hasCustomDefinition_ == true);
			CHECK(gltfStructures.materials.size() == 4);
		}

		SECTION("Textures were created")
		{
			// The tuner should merge color and alpha textures
			// This creates new texture(s) and image(s)
			CHECK(gltfStructures.textures.size() == 2);
			CHECK(gltfStructures.images.size() == 2);
		}

		SECTION("Material references created textures")
		{
			// Get the converted material (may be the original or a new one)
			REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
			REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));

			REQUIRE(matInfo_2.gltfMaterialIndex_ >= 0);
			REQUIRE(matInfo_2.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
			REQUIRE(matInfo_2.gltfMaterialIndex_ != matInfo.gltfMaterialIndex_);

			auto const& convertedMaterial = gltfStructures.materials[matInfo.gltfMaterialIndex_];
			auto const& convertedMaterial_2 = gltfStructures.materials[matInfo_2.gltfMaterialIndex_];

			// Check that PBR metallic roughness has a base color texture
			REQUIRE(convertedMaterial.pbrMetallicRoughness.has_value());
			auto const& pbr = convertedMaterial.pbrMetallicRoughness.value();
			REQUIRE(pbr.baseColorTexture.has_value());

			int32_t textureIndex = pbr.baseColorTexture.value().index;
			CHECK(textureIndex >= 0);
			CHECK(textureIndex < static_cast<int32_t>(gltfStructures.textures.size()));

			// Verify texture points to a valid image
			auto const& texture = gltfStructures.textures[textureIndex];
			CHECK(texture.source >= 0);
			CHECK(texture.source < static_cast<int32_t>(gltfStructures.images.size()));

			REQUIRE(convertedMaterial_2.pbrMetallicRoughness.has_value());
			auto const& pbr_2 = convertedMaterial_2.pbrMetallicRoughness.value();
			REQUIRE(pbr_2.baseColorTexture.has_value());
			CHECK(pbr_2.baseColorTexture.value().index >= 0);
			CHECK(pbr_2.baseColorTexture.value().index < static_cast<int32_t>(gltfStructures.textures.size()));
			CHECK(pbr_2.baseColorTexture.value().index != pbr.baseColorTexture.value().index);

			// Test save & load image.
			auto const& cesiumImage = gltfStructures.images[texture.source];
			REQUIRE(cesiumImage.pAsset != nullptr);
			auto const& cesiumImageAsset(*cesiumImage.pAsset);

			auto const texSavePath = tempDir / "resaved.png";
			auto saved = GltfMaterialTuner::SaveImageCesium(cesiumImage, texSavePath);
			REQUIRE(saved);

			CesiumGltf::Image reloadedImg;
			const std::vector<std::byte> cesiumBuffer = ReadFile(texSavePath);
			auto reloaded = GltfMaterialTuner::LoadImageCesium(reloadedImg, cesiumBuffer, "test context");
			CHECK(reloaded);

			REQUIRE(reloadedImg.pAsset != nullptr);
			auto const& reloadedImgAsset(*reloadedImg.pAsset);
			CHECK(reloadedImgAsset.width == cesiumImageAsset.width);
			CHECK(reloadedImgAsset.height == cesiumImageAsset.height);
			const uint32_t srcImageSize = (uint32_t)std::max(reloadedImgAsset.width, reloadedImgAsset.height);

			// Resample it (used for cross-lang transfer, typically).
			std::vector<std::byte> thumbnailCesiumBuffer;
			auto resampled = GltfMaterialTuner::ResampleTextureBuffer(
				cesiumBuffer,
				thumbnailCesiumBuffer,
				srcImageSize / 2,
				"test context"
			);
			CHECK(resampled);
			CHECK(thumbnailCesiumBuffer.size() < cesiumBuffer.size());
		}

		SECTION("Alpha mode is set correctly for translucent or masked textures")
		{
			REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
			REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
			auto const& convertedMaterial = gltfStructures.materials[matInfo.gltfMaterialIndex_];

			// The first material should use MASK mode.
			CHECK(convertedMaterial.alphaMode == CesiumGltf::Material::AlphaMode::MASK);

			// The second material should use BLEND mode due to semi-transparent alpha texture.
			REQUIRE(matInfo_2.gltfMaterialIndex_ >= 0);
			REQUIRE(matInfo_2.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
			auto const& convertedMaterial_2 = gltfStructures.materials[matInfo_2.gltfMaterialIndex_];
			CHECK(convertedMaterial_2.alphaMode == CesiumGltf::Material::AlphaMode::BLEND);
		}
	}

	SECTION("Convert material with color texture only")
	{
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID_3, "ColorOnlyMaterial",
												WLock(materialHelper->GetMutex()));

		// Set only color map (no alpha).
		std::string colorTextureId = materialHelper->FindOrCreateTextureID(testTextures.colorTexture);
		ITwinChannelMap colorMap;
		colorMap.texture = colorTextureId;
		colorMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		materialHelper->SetChannelColorMap(TEST_MATERIAL_ID_3, EChannelType::Color, colorMap, modified);

		// Convert the textures to glTF format.
		{
			WLock lock(materialHelper->GetMutex());
			ITwinToGltfTextureConverter texConverter(materialHelper);
			texConverter.ConvertTexturesToGltf(TEST_MATERIAL_ID_3, lock);
		}

		// Convert the material to glTF.
		GltfMaterialStructures gltfStructures(1); // 1 base material

		GltfMaterialTuner::GltfMaterialInfo matInfo;
		tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID_3,
			0,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo,
			gltfStructures.meshColors
		);

		CHECK(matInfo.hasCustomDefinition_ == true);
		CHECK(gltfStructures.textures.size() == 1);
		CHECK(gltfStructures.images.size() == 1);
		CHECK(gltfStructures.materials.size() == 2);
	}

	SECTION("Material without textures doesn't create new glTF assets")
	{
		// Create a material without any textures
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID, "NoTextureMaterial",
												WLock(materialHelper->GetMutex()));

		// Just set a color value (no texture)
		ITwinColor redColor = { 1.0, 0.0, 0.0, 1.0 };
		bool modified = false;
		materialHelper->SetChannelColor(TEST_MATERIAL_ID, EChannelType::Color, redColor, modified);

		// Convert the material to glTF.
		GltfMaterialStructures gltfStructures(1); // 1 base material
		GltfMaterialTuner::GltfMaterialInfo matInfo;
		tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID,
			0,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo,
			gltfStructures.meshColors
		);

		// Material was customized but no textures were created
		CHECK(matInfo.hasCustomDefinition_ == true);
		CHECK(gltfStructures.textures.empty());
		CHECK(gltfStructures.images.empty());

		// The material should have the custom color applied
		REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
		REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
		auto const& convertedMaterial = gltfStructures.materials[matInfo.gltfMaterialIndex_];
		REQUIRE(convertedMaterial.pbrMetallicRoughness.has_value());
		auto const& pbr = convertedMaterial.pbrMetallicRoughness.value();
		REQUIRE(pbr.baseColorFactor.size() >= 3);
		CHECK(pbr.baseColorFactor[0] == Catch::Approx(1.0).epsilon(0.01));
		CHECK(pbr.baseColorFactor[1] == Catch::Approx(0.0).epsilon(0.01));
		CHECK(pbr.baseColorFactor[2] == Catch::Approx(0.0).epsilon(0.01));
	}

}

TEST_CASE("GltfMaterialTuner - Metallic, Roughness, AO", "[GltfMaterialTuner]")
{
	TestTextureFiles testTextures;
	REQUIRE(testTextures.HasRequiredTextures());

	auto materialHelper = std::make_shared<GltfMaterialHelper>();
	GltfMaterialTuner tuner(materialHelper);

	// Create temporary texture directory
	std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "gltf_material_tuner_props_test";
	{
		WLock lock(materialHelper->GetMutex());
		materialHelper->SetTextureDirectory(tempDir, lock, true);
	}

	Be::CleanUpGuard cleanupGuard([&tempDir]() {
		std::error_code ec;
		std::filesystem::remove_all(tempDir, ec);
	});

	SECTION("Metallic and roughness values are transferred")
	{
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID, "MetallicMaterial",
												WLock(materialHelper->GetMutex()));

		bool modified = false;
		materialHelper->SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.9, modified);
		materialHelper->SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Roughness, 0.2, modified);

		GltfMaterialStructures gltfStructures(1); // 1 base material
		GltfMaterialTuner::GltfMaterialInfo matInfo;
		tuner.ConvertITwinMaterial(TEST_MATERIAL_ID, 0,
			gltfStructures.materials, gltfStructures.textures, gltfStructures.images, matInfo, gltfStructures.meshColors);

		REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
		REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
		auto const& mat = gltfStructures.materials[matInfo.gltfMaterialIndex_];
		REQUIRE(mat.pbrMetallicRoughness.has_value());
		auto const& pbr = mat.pbrMetallicRoughness.value();
		CHECK(pbr.metallicFactor == Catch::Approx(0.9).epsilon(0.01));
		CHECK(pbr.roughnessFactor == Catch::Approx(0.2).epsilon(0.01));
	}

	SECTION("Metallic and roughness textures")
	{
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID_2, "MetallicRougness-Tex",
			WLock(materialHelper->GetMutex()));

		std::string metallicTexture = materialHelper->FindOrCreateTextureID(testTextures.alphaTexture_mask);
		std::string roughnessTexture = materialHelper->FindOrCreateTextureID(testTextures.alphaTexture_blend);

		ITwinChannelMap metallicMap;
		metallicMap.texture = metallicTexture;
		metallicMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		materialHelper->SetChannelIntensityMap(TEST_MATERIAL_ID_2, EChannelType::Metallic, metallicMap, modified);

		ITwinChannelMap roughnessMap;
		roughnessMap.texture = roughnessTexture;
		roughnessMap.eSource = ETextureSource::LocalDisk;
		modified = false;
		materialHelper->SetChannelIntensityMap(TEST_MATERIAL_ID_2, EChannelType::Roughness, roughnessMap, modified);

		// Convert the textures to glTF format.
		{
			WLock lock(materialHelper->GetMutex());
			ITwinToGltfTextureConverter texConverter(materialHelper);
			texConverter.ConvertTexturesToGltf(TEST_MATERIAL_ID_2, lock);
		}

		// Convert the material to glTF.
		GltfMaterialStructures gltfStructures(1); // 1 base material
		GltfMaterialTuner::GltfMaterialInfo matInfo;
		tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID_2,
			0,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo,
			gltfStructures.meshColors
		);

		CHECK(matInfo.hasCustomDefinition_ == true);
		CHECK(gltfStructures.textures.size() == 1);
		CHECK(gltfStructures.images.size() == 1);
		CHECK(gltfStructures.materials.size() == 2);

		REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
		REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
		auto const& mat = gltfStructures.materials[matInfo.gltfMaterialIndex_];
		REQUIRE(mat.pbrMetallicRoughness.has_value());
		auto const& pbr = mat.pbrMetallicRoughness.value();
		REQUIRE(pbr.metallicRoughnessTexture.has_value());
		auto const& metallicRoughnessTexture = pbr.metallicRoughnessTexture.value();
		CHECK(metallicRoughnessTexture.index >= 0);
		CHECK(metallicRoughnessTexture.index < static_cast<int32_t>(gltfStructures.textures.size()));
	}

	SECTION("Normal and Ambient Occlusion texturex")
	{
		materialHelper->CreateITwinMaterialSlot(TEST_MATERIAL_ID_3, "NormalAO-Tex",
			WLock(materialHelper->GetMutex()));

		std::string normalTexture = materialHelper->FindOrCreateTextureID(testTextures.normalTexture);
		std::string AOTexture = materialHelper->FindOrCreateTextureID(testTextures.alphaTexture_blend);

		ITwinChannelMap normalMap;
		normalMap.texture = normalTexture;
		normalMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		materialHelper->SetChannelColorMap(TEST_MATERIAL_ID_3, EChannelType::Normal, normalMap, modified);

		ITwinChannelMap AOMap;
		AOMap.texture = AOTexture;
		AOMap.eSource = ETextureSource::LocalDisk;
		modified = false;
		materialHelper->SetChannelIntensityMap(TEST_MATERIAL_ID_3, EChannelType::AmbientOcclusion, AOMap, modified);

		// Convert the textures to glTF format.
		{
			WLock lock(materialHelper->GetMutex());
			ITwinToGltfTextureConverter texConverter(materialHelper);
			texConverter.ConvertTexturesToGltf(TEST_MATERIAL_ID_3, lock);
		}

		// Convert the material to glTF.
		GltfMaterialStructures gltfStructures(1); // 1 base material
		GltfMaterialTuner::GltfMaterialInfo matInfo;
		tuner.ConvertITwinMaterial(
			TEST_MATERIAL_ID_3,
			0,
			gltfStructures.materials,
			gltfStructures.textures,
			gltfStructures.images,
			matInfo,
			gltfStructures.meshColors
		);

		CHECK(matInfo.hasCustomDefinition_ == true);
		CHECK(gltfStructures.textures.size() == 2);
		CHECK(gltfStructures.images.size() == 2);
		CHECK(gltfStructures.materials.size() == 2);

		REQUIRE(matInfo.gltfMaterialIndex_ >= 0);
		REQUIRE(matInfo.gltfMaterialIndex_ < static_cast<int32_t>(gltfStructures.materials.size()));
		auto const& mat = gltfStructures.materials[matInfo.gltfMaterialIndex_];
		REQUIRE(mat.normalTexture.has_value());
		auto const& gltfNormalTexture = mat.normalTexture.value();
		CHECK(gltfNormalTexture.index >= 0);
		CHECK(gltfNormalTexture.index < static_cast<int32_t>(gltfStructures.textures.size()));
		REQUIRE(mat.occlusionTexture.has_value());
		auto const& gltfOcclusionTexture = mat.occlusionTexture.value();
		CHECK(gltfOcclusionTexture.index >= 0);
		CHECK(gltfOcclusionTexture.index < static_cast<int32_t>(gltfStructures.textures.size()));
	}
}
