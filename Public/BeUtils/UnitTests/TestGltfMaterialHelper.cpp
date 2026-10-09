/*--------------------------------------------------------------------------------------+
|
|     $Source: TestGltfMaterialHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>
#include <BeUtils/Gltf/GltfMaterialHelper.h>

#include <BeHeaders/Util/CleanUpGuard.h>
#include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <SDK/Core/Tools/Tools.h>
#include <SDK/Core/Visualization/TextureKey.h>

using namespace BeUtils;
using namespace AdvViz::SDK;

namespace
{
	constexpr uint64_t TEST_MATERIAL_ID = 12345;
	constexpr uint64_t TEST_MATERIAL_ID_2 = 67890;
}

TEST_CASE("GltfMaterialHelper - Basic Construction", "[GltfMaterialHelper]")
{
	Tools::CreateAdvVizLogChannels();
	GltfMaterialHelper helper;
	REQUIRE_NOTHROW(helper.GetMutex());
}

TEST_CASE("GltfMaterialHelper - Default Channel Intensities", "[GltfMaterialHelper]")
{
	ITwinRenderMaterialProperties emptyProps;

	SECTION("Metallic defaults to 0.0 without specular")
	{
		double metallic = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Metallic, emptyProps);
		CHECK(metallic == 0.0);
	}

	SECTION("Metallic defaults based on specular property")
	{
		ITwinRenderMaterialProperties customProps;
		{
			customProps.attributes["specular"] = 0.24;
			double metallic = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Metallic, customProps);
			CHECK(metallic == 0.0);
		}
		{
			customProps.attributes["specular"] = 0.26;
			double metallic = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Metallic, customProps);
			CHECK(metallic == 1.0);
		}
	}

	SECTION("Roughness defaults based on finish property")
	{
		ITwinRenderMaterialProperties customProps;
		customProps.attributes["HasFinish"] = true;
		{
			double roughness = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Roughness, customProps);
			// Default specular exponent is 13.5, so roughness = sqrt(2 / (13.5 + 2)) = sqrt(2/15.5)
			CHECK(roughness == Catch::Approx(sqrt(2.0 / 15.5)).epsilon(0.001));
		}

		customProps.attributes["finish"] = 2.0;
		{
			double roughness = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Roughness, customProps);
			CHECK(roughness == Catch::Approx(sqrt(2.0 / 4.0)).epsilon(0.001));
		}
	}

	SECTION("Roughness defaults to 1.0")
	{
		double roughness = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Roughness, emptyProps);
		CHECK(roughness == 1.0);
	}

	SECTION("Normal defaults to 1.0")
	{
		double normal = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Normal, emptyProps);
		CHECK(normal == 1.0);
	}

	SECTION("Alpha defaults to 1.0 (opaque)")
	{
		double alpha = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Alpha, emptyProps);
		CHECK(alpha == 1.0);
	}

	SECTION("Transparency defaults to 0.0 (opaque)")
	{
		double transparency = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Transparency, emptyProps);
		CHECK(transparency == 0.0);
	}

	SECTION("Alpha defaults based on transmit property")
	{
		ITwinRenderMaterialProperties customProps;
		customProps.attributes["transmit"] = 0.65;
		double alpha = GltfMaterialHelper::GetChannelDefaultIntensity(EChannelType::Alpha, customProps);
		CHECK(alpha == Catch::Approx(0.35).epsilon(0.001));
	}
}

TEST_CASE("GltfMaterialHelper - Default Channel Colors", "[GltfMaterialHelper]")
{
	ITwinRenderMaterialProperties emptyProps;

	SECTION("Default color is white (1,1,1,1)")
	{
		ITwinColor color = GltfMaterialHelper::GetChannelDefaultColor(EChannelType::Color, emptyProps);
		CHECK(color[0] == 1.0);
		CHECK(color[1] == 1.0);
		CHECK(color[2] == 1.0);
		CHECK(color[3] == 1.0);
	}

	SECTION("No custom color channel in iTwinMaterial by default")
	{
		ITwinMaterial mat;
		CHECK(!mat.GetChannelColorOpt(EChannelType::Color).has_value());
	}

	SECTION("No custom color when setting only a color texture")
	{
		ITwinMaterial mat;
		ITwinChannelMap newColorMap;
		newColorMap.texture = "test_texture_id";
		newColorMap.eSource = ETextureSource::Decoration;
		mat.SetChannelColorMap(EChannelType::Color, newColorMap);
		CHECK(!mat.GetChannelColorOpt(EChannelType::Color).has_value());
		CHECK(!mat.channels[(size_t)EChannelType::Color]->HasColor());
	}
}

TEST_CASE("GltfMaterialHelper - Material Creation and Retrieval", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	WLock lock(helper.GetMutex());

	SECTION("Create material slot")
	{
		const ITwinMaterial* mat = helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", lock);
		REQUIRE(mat != nullptr);
	}

	SECTION("Get custom material definition")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", lock);
		const ITwinMaterial* mat = helper.GetCustomMaterialDefinition(TEST_MATERIAL_ID, lock);
		REQUIRE(mat != nullptr);
	}

	SECTION("GetCustomMaterialDefinition returns nullptr for non-existent material")
	{
		const ITwinMaterial* mat = helper.GetCustomMaterialDefinition(999999, lock);
		CHECK(mat == nullptr);
	}
}

TEST_CASE("GltfMaterialHelper - Channel Intensity Get/Set", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Set and get intensity")
	{
		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.7, modified);
		CHECK(modified == true);

		double intensity = helper.GetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic);
		CHECK(intensity == Catch::Approx(0.7).epsilon(0.0001));
	}

	SECTION("Setting same value doesn't mark as modified")
	{
		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Roughness, 0.5, modified);
		CHECK(modified == true);

		// Set same value again
		modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Roughness, 0.5, modified);
		CHECK(modified == false);
	}
}

TEST_CASE("GltfMaterialHelper - Channel Color Get/Set", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get default color")
	{
		ITwinColor color = helper.GetChannelColor(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(color[0] == 1.0);
		CHECK(color[1] == 1.0);
		CHECK(color[2] == 1.0);
	}

	SECTION("Set and get color")
	{
		ITwinColor newColor = { 0.5, 0.3, 0.8, 1.0 };
		bool modified = false;
		helper.SetChannelColor(TEST_MATERIAL_ID, EChannelType::Color, newColor, modified);
		CHECK(modified == true);

		ITwinColor retrieved = helper.GetChannelColor(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(retrieved[0] == Catch::Approx(0.5).epsilon(0.0001));
		CHECK(retrieved[1] == Catch::Approx(0.3).epsilon(0.0001));
		CHECK(retrieved[2] == Catch::Approx(0.8).epsilon(0.0001));
	}
}

TEST_CASE("GltfMaterialHelper - Alpha Mode Management", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	{
		WLock lock(helper.GetMutex());
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", lock);
	}

	SECTION("Set and get alpha mode")
	{
		WLock lock(helper.GetMutex());
		bool success = helper.SetCurrentAlphaMode(TEST_MATERIAL_ID, "BLEND", lock);
		CHECK(success == true);

		lock.unlock();

		std::string alphaMode;
		bool retrieved = helper.GetCurrentAlphaMode(TEST_MATERIAL_ID, alphaMode, RLock(helper.GetMutex()));
		CHECK(retrieved == true);
		CHECK(alphaMode == "BLEND");
	}

	SECTION("Get alpha mode for non-existent material returns false")
	{
		std::string alphaMode;
		bool retrieved = helper.GetCurrentAlphaMode(999999, alphaMode, RLock(helper.GetMutex()));
		CHECK(retrieved == false);
	}

	SECTION("Set alpha mode for non-existent material returns false")
	{
		WLock lock(helper.GetMutex());
		bool success = helper.SetCurrentAlphaMode(999999, "OPAQUE", lock);
		CHECK(success == false);
	}
}

TEST_CASE("GltfMaterialHelper - Material Kind", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get default material kind")
	{
		EMaterialKind kind = helper.GetMaterialKind(TEST_MATERIAL_ID);
		// Default should be PBR
		CHECK(kind == EMaterialKind::PBR);
	}

	SECTION("Set and get material kind")
	{
		bool modified = false;
		helper.SetMaterialKind(TEST_MATERIAL_ID, EMaterialKind::Glass, modified);
		CHECK(modified == true);

		EMaterialKind kind = helper.GetMaterialKind(TEST_MATERIAL_ID);
		CHECK(kind == EMaterialKind::Glass);
	}
}

TEST_CASE("GltfMaterialHelper - Material Name", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get material name")
	{
		auto matDefAccess = helper.GetMaterialDefinitionAccess(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		REQUIRE(matDefAccess.iTwinRenderMaterial != nullptr);
		std::string name = matDefAccess.iTwinRenderMaterial->displayName;
		CHECK(name == "TestMaterial");
	}

	SECTION("Set and get material name")
	{
		bool success = helper.SetMaterialName(TEST_MATERIAL_ID, "NewMaterialName");
		CHECK(success == true);

		std::string name = helper.GetMaterialName(TEST_MATERIAL_ID);
		CHECK(name == "NewMaterialName");
	}
}

TEST_CASE("GltfMaterialHelper - UV Transform", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get default UV transform")
	{
		ITwinUVTransform uvTransform = helper.GetUVTransform(TEST_MATERIAL_ID);
		CHECK(uvTransform.offset[0] == 0.0);
		CHECK(uvTransform.offset[1] == 0.0);
		CHECK(uvTransform.scale[0] == 1.0);
		CHECK(uvTransform.scale[1] == 1.0);
		CHECK(uvTransform.rotation == 0.0);
	}

	SECTION("Set and get UV transform")
	{
		ITwinUVTransform newTransform;
		newTransform.offset[0] = 0.5;
		newTransform.offset[1] = 0.3;
		newTransform.scale[0] = 2.0;
		newTransform.scale[1] = 1.5;
		newTransform.rotation = 45.0;

		bool modified = false;
		helper.SetUVTransform(TEST_MATERIAL_ID, newTransform, modified);
		CHECK(modified == true);

		ITwinUVTransform retrieved = helper.GetUVTransform(TEST_MATERIAL_ID);
		CHECK(retrieved.offset[0] == Catch::Approx(0.5).epsilon(0.0001));
		CHECK(retrieved.offset[1] == Catch::Approx(0.3).epsilon(0.0001));
		CHECK(retrieved.scale[0] == Catch::Approx(2.0).epsilon(0.0001));
		CHECK(retrieved.scale[1] == Catch::Approx(1.5).epsilon(0.0001));
		CHECK(retrieved.rotation == Catch::Approx(45.0).epsilon(0.0001));
	}
}

TEST_CASE("GltfMaterialHelper - Material Has Custom Definition", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;

	SECTION("Material without customizations returns false")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));
		bool hasCustom = helper.HasCustomDefinition(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		CHECK(hasCustom == false);
	}

	SECTION("Material with modified property returns true")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));
		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.8, modified);

		bool hasCustom = helper.HasCustomDefinition(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		CHECK(hasCustom == true);
	}
}

TEST_CASE("GltfMaterialHelper - Material Full Definition", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get full material definition")
	{
		ITwinMaterial matDef;
		bool success = helper.GetMaterialFullDefinition(TEST_MATERIAL_ID, matDef);
		CHECK(success == true);

		auto roughnessOpt = matDef.GetChannelIntensityOpt(EChannelType::Roughness);
		REQUIRE(roughnessOpt.has_value());
		CHECK(*roughnessOpt == 1.0);
	}

	SECTION("Set full material definition")
	{
		ITwinMaterial matDef;
		matDef.displayName = "CustomMaterial";
		matDef.kind = EMaterialKind::Glass;
		matDef.SetChannelIntensity(EChannelType::Metallic, 0.9);
		matDef.SetChannelColor(EChannelType::Color, ITwinColor{ 1.0, 0.0, 0.0, 1.0 });

		helper.SetMaterialFullDefinition(TEST_MATERIAL_ID, matDef);

		ITwinMaterial retrieved;
		helper.GetMaterialFullDefinition(TEST_MATERIAL_ID, retrieved);
		CHECK(retrieved.displayName == "CustomMaterial");
		CHECK(retrieved.kind == EMaterialKind::Glass);
		CHECK(retrieved.channels[(size_t)EChannelType::Color]->HasColor());

		auto metallicOpt = retrieved.GetChannelIntensityOpt(EChannelType::Metallic);
		REQUIRE(metallicOpt.has_value());
		CHECK(*metallicOpt == Catch::Approx(0.9).epsilon(0.0001));
	}

	SECTION("SetIModelRenderMaterialProperties")
	{
		ITwinRenderMaterialProperties renderMaterialProps;
		renderMaterialProps.name = "Beautiful Material";
		renderMaterialProps.id = "material_002";
		renderMaterialProps.attributes["transmit"] = 0.4;
		renderMaterialProps.attributes["HasFinish"] = true;
		renderMaterialProps.attributes["finish"] = 3.0;
		helper.SetIModelRenderMaterialProperties(TEST_MATERIAL_ID_2,
			renderMaterialProps,
			"Beautiful Material",
			WLock(helper.GetMutex()));

		double opacity = helper.GetChannelIntensity(TEST_MATERIAL_ID_2, EChannelType::Opacity);
		CHECK(opacity == Catch::Approx(0.6).epsilon(0.0001));

		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID_2, EChannelType::Opacity, 0.8, modified);
		CHECK(modified);

		// Custom setting should win over the default derived from the iModel render material properties.
		opacity = helper.GetChannelIntensity(TEST_MATERIAL_ID_2, EChannelType::Opacity);
		CHECK(opacity == Catch::Approx(0.8).epsilon(0.0001));

		ITwinMaterial retrieved;
		helper.GetMaterialFullDefinition(TEST_MATERIAL_ID_2, retrieved);
		auto opacityOpt = retrieved.GetChannelIntensityOpt(EChannelType::Opacity);
		REQUIRE(opacityOpt.has_value());
		CHECK(*opacityOpt == opacityOpt);
	}
}

TEST_CASE("GltfMaterialHelper - MaterialDefinitionAccess", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get material definition access")
	{
		RLock lock(helper.GetMutex());
		MaterialDefinitionAccess access = helper.GetMaterialDefinitionAccess(TEST_MATERIAL_ID, lock);
		REQUIRE(access.customMaterial != nullptr);
	}

	SECTION("MaterialDefinitionAccess - GetIntensity")
	{
		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.75, modified);

		RLock lock(helper.GetMutex());
		MaterialDefinitionAccess access = helper.GetMaterialDefinitionAccess(TEST_MATERIAL_ID, lock);
		double intensity = access.GetIntensity(EChannelType::Metallic);
		CHECK(intensity == Catch::Approx(0.75).epsilon(0.0001));
	}

	SECTION("MaterialDefinitionAccess - GetColor")
	{
		ITwinColor newColor = { 0.2, 0.4, 0.6, 1.0 };
		bool modified = false;
		helper.SetChannelColor(TEST_MATERIAL_ID, EChannelType::Color, newColor, modified);

		RLock lock(helper.GetMutex());
		MaterialDefinitionAccess access = helper.GetMaterialDefinitionAccess(TEST_MATERIAL_ID, lock);
		ITwinColor color = access.GetColor(EChannelType::Color);
		CHECK(color[0] == Catch::Approx(0.2).epsilon(0.0001));
		CHECK(color[1] == Catch::Approx(0.4).epsilon(0.0001));
		CHECK(color[2] == Catch::Approx(0.6).epsilon(0.0001));
	}
}

TEST_CASE("GltfMaterialHelper - Multiple Materials", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;

	SECTION("Create and manage multiple materials")
	{
		WLock lock(helper.GetMutex());
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "Material1", lock);
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID_2, "Material2", lock);
	}

	SECTION("Materials maintain independent properties")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "Material1", WLock(helper.GetMutex()));
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID_2, "Material2", WLock(helper.GetMutex()));

		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.8, modified);
		helper.SetChannelIntensity(TEST_MATERIAL_ID_2, EChannelType::Metallic, 0.3, modified);

		double intensity1 = helper.GetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic);
		double intensity2 = helper.GetChannelIntensity(TEST_MATERIAL_ID_2, EChannelType::Metallic);

		CHECK(intensity1 == Catch::Approx(0.8).epsilon(0.0001));
		CHECK(intensity2 == Catch::Approx(0.3).epsilon(0.0001));
	}
}

TEST_CASE("GltfMaterialHelper - Thread Safety", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;

	SECTION("Operations with explicit locks")
	{
		{
			WLock lock(helper.GetMutex());
			helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", lock);
		}

		{
			RLock lock(helper.GetMutex());
			const ITwinMaterial* mat = helper.GetCustomMaterialDefinition(TEST_MATERIAL_ID, lock);
			CHECK(mat != nullptr);
		}
	}
}

TEST_CASE("GltfMaterialHelper - GetChannelMap", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Get empty channel map by default")
	{
		ITwinChannelMap channelMap = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(channelMap.texture.empty());
		CHECK_FALSE(channelMap.HasTexture());
	}

	SECTION("Get channel map distinguishes color vs intensity channels")
	{
		// Color channel should use GetChannelColorMap
		ITwinChannelMap colorMap = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK_FALSE(colorMap.HasTexture());

		// Metallic channel should use GetChannelIntensityMap
		ITwinChannelMap metallicMap = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Metallic);
		CHECK_FALSE(metallicMap.HasTexture());
	}

	SECTION("Get channel map after setting color map")
	{
		ITwinChannelMap newColorMap;
		newColorMap.texture = "test_texture_id";
		newColorMap.eSource = ETextureSource::ITwin;

		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, newColorMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(retrieved.texture == "test_texture_id");
		CHECK(retrieved.eSource == ETextureSource::ITwin);
		CHECK(retrieved.HasTexture());
	}

	SECTION("Get channel map with locking variant")
	{
		RLock lock(helper.GetMutex());
		ITwinChannelMap channelMap = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Color, lock);
		CHECK_FALSE(channelMap.HasTexture());
	}
}

TEST_CASE("GltfMaterialHelper - HasChannelMap", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Material without texture returns false")
	{
		bool hasMap = helper.HasChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK_FALSE(hasMap);
	}

	SECTION("Material with texture returns true")
	{
		ITwinChannelMap newColorMap;
		newColorMap.texture = "texture_with_map";
		newColorMap.eSource = ETextureSource::LocalDisk;

		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, newColorMap, modified);

		bool hasMap = helper.HasChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(hasMap == true);
	}

	SECTION("HasChannelMap with explicit lock")
	{
		RLock lock(helper.GetMutex());
		bool hasMap = helper.HasChannelMap(TEST_MATERIAL_ID, EChannelType::Metallic, lock);
		CHECK_FALSE(hasMap);
	}
}

TEST_CASE("GltfMaterialHelper - SetChannelColorMap", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Set color map with iTwin texture")
	{
		ITwinChannelMap colorMap;
		colorMap.texture = "itwin_texture_123";
		colorMap.eSource = ETextureSource::ITwin;

		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(retrieved.texture == "itwin_texture_123");
		CHECK(retrieved.eSource == ETextureSource::ITwin);
	}

	SECTION("Set color map with local disk texture")
	{
		ITwinChannelMap colorMap;
		colorMap.texture = "/path/to/local/texture.png";
		colorMap.eSource = ETextureSource::LocalDisk;

		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(retrieved.texture == "/path/to/local/texture.png");
		CHECK(retrieved.eSource == ETextureSource::LocalDisk);
	}

	SECTION("Setting same color map doesn't mark as modified")
	{
		ITwinChannelMap colorMap;
		colorMap.texture = "same_texture";
		colorMap.eSource = ETextureSource::ITwin;

		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);
		CHECK(modified == true);

		// Set the same map again
		modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);
		CHECK(modified == false);
	}

	SECTION("Replace existing color map")
	{
		// Set initial map
		ITwinChannelMap firstMap;
		firstMap.texture = "first_texture";
		firstMap.eSource = ETextureSource::ITwin;
		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, firstMap, modified);

		// Replace with different map
		ITwinChannelMap secondMap;
		secondMap.texture = "second_texture";
		secondMap.eSource = ETextureSource::LocalDisk;
		modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, secondMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK(retrieved.texture == "second_texture");
		CHECK(retrieved.eSource == ETextureSource::LocalDisk);
	}

	SECTION("Clear color map by setting empty texture")
	{
		// Set initial map
		ITwinChannelMap initialMap;
		initialMap.texture = "some_texture";
		initialMap.eSource = ETextureSource::ITwin;
		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, initialMap, modified);

		// Clear by setting empty
		ITwinChannelMap emptyMap;
		modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, emptyMap, modified);
		CHECK(modified == true);

		bool hasMap = helper.HasChannelMap(TEST_MATERIAL_ID, EChannelType::Color);
		CHECK_FALSE(hasMap);
	}
}

TEST_CASE("GltfMaterialHelper - SetChannelIntensityMap", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Set intensity map for metallic channel")
	{
		ITwinChannelMap intensityMap;
		intensityMap.texture = "metallic_texture";
		intensityMap.eSource = ETextureSource::LocalDisk;

		bool modified = false;
		helper.SetChannelIntensityMap(TEST_MATERIAL_ID, EChannelType::Metallic, intensityMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelIntensityMap(TEST_MATERIAL_ID, EChannelType::Metallic);
		CHECK(retrieved.texture == "metallic_texture");
		CHECK(retrieved.eSource == ETextureSource::LocalDisk);
	}

	SECTION("Set intensity map for roughness channel")
	{
		ITwinChannelMap intensityMap;
		intensityMap.texture = "roughness_texture";
		intensityMap.eSource = ETextureSource::ITwin;

		bool modified = false;
		helper.SetChannelIntensityMap(TEST_MATERIAL_ID, EChannelType::Roughness, intensityMap, modified);
		CHECK(modified == true);

		ITwinChannelMap retrieved = helper.GetChannelMap(TEST_MATERIAL_ID, EChannelType::Roughness);
		CHECK(retrieved.texture == "roughness_texture");
	}
}

TEST_CASE("GltfMaterialHelper - ListITwinTexturesToDownload", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;

	// Create temporary texture directory
	std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "gltf_test_textures";
	{
		WLock lock(helper.GetMutex());
		helper.SetTextureDirectory(tempDir, lock, true);
	}

	// Cleanup on exit
	Be::CleanUpGuard cleanupGuard([&tempDir]() {
		std::error_code ec;
		std::filesystem::remove_all(tempDir, ec);
	});

	SECTION("Empty list when no materials with textures")
	{
		std::vector<std::string> missingTextures;
		helper.ListITwinTexturesToDownload(missingTextures, WLock(helper.GetMutex()));
		CHECK(missingTextures.empty());
	}

	SECTION("LocalDisk textures are not listed for download")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

		// Set a color map referencing a local disk texture
		ITwinChannelMap colorMap;
		colorMap.texture = "/local/path/texture.png";
		colorMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);

		std::vector<std::string> missingTextures;
		helper.ListITwinTexturesToDownload(missingTextures, WLock(helper.GetMutex()));

		// Local disk textures should not be in download list
		bool found = std::find(missingTextures.begin(), missingTextures.end(), "/local/path/texture.png") != missingTextures.end();
		CHECK_FALSE(found);
	}
}

TEST_CASE("GltfMaterialHelper - ListITwinTexturesToDownload, ListITwinTexturesToResolve", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;

	// Create temporary texture directory
	std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "gltf_test_resolve";
	{
		WLock lock(helper.GetMutex());
		helper.SetTextureDirectory(tempDir, lock, true);
	}

	Be::CleanUpGuard cleanupGuard([&tempDir]() {
		std::error_code ec;
		std::filesystem::remove_all(tempDir, ec);
	});

	SECTION("Empty list when no customized materials")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

		std::unordered_map<TextureKey, std::string> texturesToResolve;
		std::vector<uint64_t> ownerMaterialIds;
		helper.ListITwinTexturesToResolve(texturesToResolve, ownerMaterialIds, RLock(helper.GetMutex()));

		// No customized materials, so nothing to resolve
		CHECK(texturesToResolve.empty());
		CHECK(ownerMaterialIds.empty());
	}

	SECTION("List textures from customized materials")
	{
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

		// Customize material with a texture
		ITwinRenderMaterialProperties renderMaterialProps;
		renderMaterialProps.name = "TestMaterial";
		renderMaterialProps.id = "TEST_MATERIAL_ID";
		ITwinRenderMaterialAttributeMap iTwinColorTexture;
		iTwinColorTexture["TextureId"] = "itwin_texture_007";
		renderMaterialProps.maps["Pattern"] = iTwinColorTexture;
		helper.SetIModelRenderMaterialProperties(TEST_MATERIAL_ID, renderMaterialProps, renderMaterialProps.name, WLock(helper.GetMutex()));

		// List missing textures
		std::vector<std::string> missingTextures;
		helper.ListITwinTexturesToDownload(missingTextures, WLock(helper.GetMutex()));
		REQUIRE_FALSE(missingTextures.empty());
		CHECK(missingTextures[0] == "itwin_texture_007");

		// Simulate texture being downloaded (mark as available)
		ITwinTextureData mockTextureData;
		mockTextureData.bytes = {0xFF, 0x00, 0xFF}; // Dummy data
		mockTextureData.width = 1;
		mockTextureData.height = 1;
		mockTextureData.format = ImageSourceFormat::Png;
		std::filesystem::path outPath;
		helper.SetITwinTextureData("itwin_texture_007", mockTextureData, outPath);

		// Now list textures to resolve - should still be empty as material was not customized!
		std::unordered_map<TextureKey, std::string> texturesToResolve;
		std::vector<uint64_t> ownerMaterialIds;
		helper.ListITwinTexturesToResolve(texturesToResolve, ownerMaterialIds, RLock(helper.GetMutex()));

		// Only customized materials are considered, so the list should be empty
		CHECK(texturesToResolve.empty());
		CHECK(ownerMaterialIds.empty());

		// Customize it
		bool modified = false;
		helper.SetChannelIntensity(TEST_MATERIAL_ID, EChannelType::Metallic, 0.9, modified);
		CHECK(modified == true);

		// Add another material slot, using a local texture (should *not* be listed as needing
		// resolution).
		helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID_2, "TestMaterial", WLock(helper.GetMutex()));
		ITwinChannelMap roughnessMap;
		roughnessMap.texture = "roughness_texture";
		roughnessMap.eSource = ETextureSource::LocalDisk;
		helper.SetChannelIntensityMap(TEST_MATERIAL_ID_2, EChannelType::Roughness, roughnessMap, modified);
		CHECK(modified == true);


		helper.ListITwinTexturesToResolve(texturesToResolve, ownerMaterialIds, RLock(helper.GetMutex()));

		// Should find the texture that needs resolving
		REQUIRE(texturesToResolve.size() == 1);
		REQUIRE(ownerMaterialIds.size() == 1);
		CHECK(ownerMaterialIds[0] == TEST_MATERIAL_ID);
		auto const itTex = texturesToResolve.begin();
		CHECK(itTex->first.eSource == ETextureSource::ITwin);
		CHECK(itTex->first.id == "itwin_texture_007");
	}
}

TEST_CASE("GltfMaterialHelper - Material Using Textures", "[GltfMaterialHelper]")
{
	GltfMaterialHelper helper;
	helper.CreateITwinMaterialSlot(TEST_MATERIAL_ID, "TestMaterial", WLock(helper.GetMutex()));

	SECTION("Material without textures returns false")
	{
		bool hasTextures = helper.MaterialUsingTextures(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		CHECK_FALSE(hasTextures);
	}

	SECTION("Material with color map returns true")
	{
		ITwinChannelMap colorMap;
		colorMap.texture = "texture_id";
		colorMap.eSource = ETextureSource::ITwin;
		bool modified = false;
		helper.SetChannelColorMap(TEST_MATERIAL_ID, EChannelType::Color, colorMap, modified);

		bool hasTextures = helper.MaterialUsingTextures(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		CHECK(hasTextures == true);
	}

	SECTION("Material with intensity map returns true")
	{
		ITwinChannelMap intensityMap;
		intensityMap.texture = "metallic_texture";
		intensityMap.eSource = ETextureSource::LocalDisk;
		bool modified = false;
		helper.SetChannelIntensityMap(TEST_MATERIAL_ID, EChannelType::Metallic, intensityMap, modified);

		bool hasTextures = helper.MaterialUsingTextures(TEST_MATERIAL_ID, RLock(helper.GetMutex()));
		CHECK(hasTextures == true);
	}
}
