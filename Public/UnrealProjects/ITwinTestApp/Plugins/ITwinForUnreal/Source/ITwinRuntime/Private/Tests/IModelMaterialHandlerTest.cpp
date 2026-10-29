/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelMaterialHandlerTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Material/ITwinIModelMaterialHandler.h>
#include <Material/ITwinMaterialLibrary.h>
#include <ITwinIModel.h>
#include <ITwinSceneMapping.h>
#include "WebTestHelpers.h"

#include <Engine/World.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformFile.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>
#include <Misc/Paths.h>

// MUST be included before BeforeNonUnrealIncludes...
// problem with INT and FLOAT macros in Windows/AllowWindowsPlatformTypes.h
#include <Cesium3DTilesSelection/GltfModifier.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	pragma push_macro("FLOAT")
#	undef FLOAT
#	include <BeUtils/Gltf/GltfBuilder.h>
#	pragma pop_macro("FLOAT")
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfTuner.h>
#	include <CesiumGltf/Model.h>
#	include <Core/Tools/Tools.h>
#	include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#	include <SDK/Core/ITwinAPI/ITwinTypes.h>
#	include <SDK/Core/Visualization/MaterialPersistence.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace
{
	inline std::filesystem::path ToAbsoluteFSPath(FString const& Path)
	{
		std::error_code ec;
		return std::filesystem::absolute(TCHAR_TO_UTF8(*Path), ec);
	}

	inline FString ToAbsolute(FString const& Path)
	{
		std::filesystem::path AbsPath = ToAbsoluteFSPath(Path);
		FString StrAbsPath(AbsPath.string().c_str());
		StrAbsPath.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		return StrAbsPath;
	};

	FString SetTextureImpl(FITwinIModelMaterialHandler& Handler, uint64_t MaterialId,
		AdvViz::SDK::EChannelType ChannelType, const FString& TexturePath,
		AITwinIModel& IModel, TSceneMappingPtr& SceneMapping)
	{
		const std::filesystem::path AbsoluteTexturePath = ToAbsoluteFSPath(TexturePath);
		std::string const NewTextureId_StdStr = Handler.GetGltfMatHelper()->FindOrCreateTextureID(AbsoluteTexturePath);
		const FString NewTextureId = UTF8_TO_TCHAR(NewTextureId_StdStr.c_str());

		Handler.SetMaterialChannelTextureID(MaterialId, ChannelType, NewTextureId,
			AdvViz::SDK::ETextureSource::LocalDisk, SceneMapping, IModel.GetDefaultTexturesHolder());

		return NewTextureId;
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelMaterialHandlerTest, FAutomationTestBase, \
	"Bentley.ITwinForUnreal.ITwinRuntime.IModelMaterialHandler", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelMaterialGlassSwitchTest, FAutomationTestBase, \
	"Bentley.ITwinForUnreal.ITwinRuntime.MaterialGlassSwitch", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIModelMaterialGlassSwitchTest::RunTest(const FString& /*Parameters*/)
{
	AITwinIModel::EnableMaterialTuning(true);
	auto World = FITwinAPITestHelperBase::GetTestWorld();
	UTEST_NOT_NULL(TEXT("Test world"), World);
	auto* IModel = World->SpawnActor<AITwinIModel>();
	UTEST_NOT_NULL(TEXT("Test iModel"), IModel);
	IModel->IModelId = TEXT("imodel_glass_switch_test");
	Be::CleanUpGuard Cleanup([IModel]() { IModel->Destroy(); });
	for (double InitialOpacity : { 0.5, 0.05 })
	{
		FITwinIModelMaterialHandler Handler;
		Handler.SetSpecificPersistenceManager(std::make_shared<AdvViz::SDK::MaterialPersistenceManager>());
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);
		TSceneMappingPtr SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);
		CesiumUtility::JsonValue::Object Extras =
		{
			{ "iTwinMaterials", CesiumUtility::JsonValue::Array
				{ CesiumUtility::JsonValue::Object { { "id", "0x6789" }, { "name", "Glass switch" } } } }
		};
		Tuner->ParseExtras(Extras);
		constexpr uint64_t MaterialId = 0x6789;
		auto Helper = Handler.GetGltfMatHelper();
		{
			BeUtils::WLock Lock(Helper->GetMutex());
			Helper->CreateITwinMaterialSlot(MaterialId, "Glass switch", Lock);
			Helper->SetCurrentAlphaMode(MaterialId, "MASK", Lock);
		}
		UTEST_TRUE(TEXT("Initial kind is PBR"), Handler.GetMaterialKind(MaterialId) == AdvViz::SDK::EMaterialKind::PBR);
		bool bModified = false;
		Helper->SetChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Opacity, InitialOpacity, bModified);
		Handler.SetMaterialKind(MaterialId, AdvViz::SDK::EMaterialKind::Glass, SceneMapping);

		AdvViz::SDK::EMaterialKind EffectiveKind;
		bool bRequiresTranslucency = false;
		UTEST_TRUE(TEXT("Material has requirements"), Handler.GetMaterialCustomRequirements(MaterialId, EffectiveKind, bRequiresTranslucency));
		UTEST_TRUE(TEXT("Effective kind is Glass"), EffectiveKind == AdvViz::SDK::EMaterialKind::Glass);
		UTEST_TRUE(TEXT("Glass requires translucency"), bRequiresTranslucency);
		UTEST_EQUAL(TEXT("Opacity is clamped only above the Glass default"),
			Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Opacity), FMath::Min(InitialOpacity, 0.1));
		UTEST_EQUAL(TEXT("Glass metallic default"), Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Metallic), 0.);
		UTEST_EQUAL(TEXT("Glass specular default"), Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Specular), 0.5);
	}
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelMaterialGlassAppearanceTest, FAutomationTestBase, \
	"Bentley.ITwinForUnreal.ITwinRuntime.MaterialGlassAppearance", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIModelMaterialGlassAppearanceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace AdvViz::SDK;
	using EChannelType = AdvViz::SDK::EChannelType;
	using EMaterialKind = AdvViz::SDK::EMaterialKind;
	AITwinIModel::EnableMaterialTuning(true);
	auto World = FITwinAPITestHelperBase::GetTestWorld();
	UTEST_NOT_NULL(TEXT("Test world"), World);
	auto* IModel = World->SpawnActor<AITwinIModel>();
	UTEST_NOT_NULL(TEXT("Test iModel"), IModel);
	IModel->IModelId = TEXT("imodel_glass_appearance_test");
	Be::CleanUpGuard Cleanup([IModel]() { IModel->Destroy(); });
	for (bool bExplicitTint : { false, true })
	for (double InitialOpacity : { 0.71, 0.05, 0.1 })
	{
		FITwinIModelMaterialHandler Handler;
		Handler.SetSpecificPersistenceManager(std::make_shared<MaterialPersistenceManager>());
		auto Tuner = std::make_shared<BeUtils::GltfTuner>(true);
		Handler.Initialize(Tuner, IModel);
		TSceneMappingPtr SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);
		Tuner->ParseExtras({ { "iTwinMaterials", CesiumUtility::JsonValue::Array
			{ CesiumUtility::JsonValue::Object { { "id", "0x6789" }, { "name", "Glass appearance" } } } } });
		constexpr uint64_t MaterialId = 0x6789;
		auto Helper = Handler.GetGltfMatHelper();
		ITwinRenderMaterialProperties Source;
		Source.name = "Glass appearance";
		Source.attributes["HasBaseColor"] = true;
		Source.attributes["color"] = std::array<double, 3>{ 0.2, 0.4, 0.7 };
		{
			BeUtils::WLock Lock(Helper->GetMutex());
			Helper->CreateITwinMaterialSlot(MaterialId, Source.name, Lock);
			Helper->SetIModelRenderMaterialProperties(MaterialId, Source, Source.name, Lock);
			Helper->SetCurrentAlphaMode(MaterialId, "MASK", Lock);
		}
		bool bModified = false;
		Helper->SetChannelIntensity(MaterialId, EChannelType::Opacity, InitialOpacity, bModified);
		if (bExplicitTint)
			Helper->SetChannelColor(MaterialId, EChannelType::Color, ITwinColor{ 0.6, 0.3, 0.1, 1. }, bModified);
		auto const ExpectedTint = Helper->GetChannelColor(MaterialId, EChannelType::Color);
		auto const ExpectedTexture = Helper->GetChannelColorMap(MaterialId, EChannelType::Color);
		auto const ExpectedTextureFactor = Helper->GetChannelIntensity(MaterialId, EChannelType::Color);
		Handler.SetMaterialKind(MaterialId, EMaterialKind::PBR, SceneMapping);
		{
			BeUtils::RLock Lock(Helper->GetMutex());
			UTEST_TRUE(TEXT("PBR no-op does not introduce a tint"),
				Helper->GetCustomMaterialDefinition(MaterialId, Lock)->GetChannelColorOpt(EChannelType::Color).has_value() == bExplicitTint);
		}
		BeUtils::GltfBuilder Builder;
		Builder.AddMetadataProperty("features", "element", { 1 });
		Builder.AddMetadataProperty("materials", "material", { MaterialId }, 1);
		Builder.GetModel().meshes.emplace_back();
		auto Primitive = Builder.AddMeshPrimitive(0, Builder.AddMaterial(), CesiumGltf::MeshPrimitive::Mode::TRIANGLES);
		Primitive.SetPositions({ { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } });
		Primitive.SetIndices(std::vector<std::array<uint8_t, 1>>{ { 0 }, { 1 }, { 2 } }, false);
		Primitive.SetFeatureIds(std::vector<std::array<float, 1>>{ { 0.f }, { 0.f }, { 0.f } }, true);
		Primitive.SetITwinMaterialID(MaterialId);
		Primitive.SetColors(std::vector<std::array<uint8_t, 4>>{ { 0, 0, 0, 182 }, { 255, 64, 32, 64 }, { 25, 200, 90, 128 } });
		Tuner->SetMaterialRules({ .itwinMatIDsToSplit_ = { MaterialId } });
		CesiumGltf::Model PbrModel;
		UTEST_TRUE(TEXT("PBR tuning succeeds"), Tuner->applyForUnitTest(Builder.GetModel(), glm::dmat4(1.), PbrModel));
		UTEST_EQUAL(TEXT("PBR mesh exists"), PbrModel.meshes.size(), size_t(1));
		UTEST_EQUAL(TEXT("PBR retains vertex colors without explicit tint"),
			PbrModel.meshes[0].primitives[0].attributes.contains("COLOR_0"), !bExplicitTint);
		Handler.SetMaterialKind(MaterialId, EMaterialKind::Glass, SceneMapping);
		{
			BeUtils::RLock Lock(Helper->GetMutex());
			UTEST_TRUE(TEXT("Glass establishes explicit tint"),
				Helper->GetCustomMaterialDefinition(MaterialId, Lock)->GetChannelColorOpt(EChannelType::Color).has_value());
		}
		UTEST_TRUE(TEXT("Glass preserves resolved tint"), Helper->GetChannelColor(MaterialId, EChannelType::Color) == ExpectedTint);
		UTEST_TRUE(TEXT("Glass preserves color texture"), Helper->GetChannelColorMap(MaterialId, EChannelType::Color) == ExpectedTexture);
		UTEST_EQUAL(TEXT("Glass preserves texture factor"), Helper->GetChannelIntensity(MaterialId, EChannelType::Color), ExpectedTextureFactor);
		CesiumGltf::Model GlassModel;
		UTEST_TRUE(TEXT("Glass tuning succeeds"), Tuner->applyForUnitTest(Builder.GetModel(), glm::dmat4(1.), GlassModel));
		UTEST_EQUAL(TEXT("Glass mesh exists"), GlassModel.meshes.size(), size_t(1));
		UTEST_EQUAL(TEXT("Glass primitive exists"), GlassModel.meshes[0].primitives.size(), size_t(1));
		auto const& TunedPrimitive = GlassModel.meshes[0].primitives[0];
		UTEST_FALSE(TEXT("Glass drops baked vertex RGB and alpha"), TunedPrimitive.attributes.contains("COLOR_0"));
		auto const& Material = GlassModel.materials[TunedPrimitive.material];
		UTEST_TRUE(TEXT("Glass is translucent"), Material.alphaMode == "BLEND");
		UTEST_TRUE(TEXT("Glass has PBR factors"), Material.pbrMetallicRoughness.has_value());
		auto const& Factor = Material.pbrMetallicRoughness->baseColorFactor;
		for (size_t Channel = 0; Channel < 3; ++Channel)
			UTEST_EQUAL(TEXT("Glass factor is tint, not first vertex RGB"), Factor[Channel], ExpectedTint[Channel]);
		UTEST_EQUAL(TEXT("Glass retains custom opacity with clamp"), Factor[3], FMath::Min(InitialOpacity, 0.1));
		Handler.SetMaterialKind(MaterialId, EMaterialKind::Glass, SceneMapping);
		UTEST_TRUE(TEXT("Repeated Glass switch preserves tint"), Helper->GetChannelColor(MaterialId, EChannelType::Color) == ExpectedTint);
	}
	return true;
}

bool FIModelMaterialHandlerTest::RunTest(const FString& /*Parameters*/)
{
	AITwinIModel::EnableMaterialTuning(true);

	auto World = FITwinAPITestHelperBase::GetTestWorld();
	ensure(World);

	// Load an iModel.
	auto* const IModel = World->SpawnActor<AITwinIModel>();
	IModel->IModelId = TEXT("imodel_material_test_imodelid");

	SECTION("Constructor initializes GltfMaterialHelper")
	{
		FITwinIModelMaterialHandler Handler;
		UTEST_NOT_NULL(TEXT("GltfMatHelper should be initialized"), Handler.GetGltfMatHelper().get());
	}

	SECTION("Global persistence manager can be set and retrieved")
	{
		auto PersistenceMgr = std::make_shared<AdvViz::SDK::MaterialPersistenceManager>();

		FITwinIModelMaterialHandler::SetGlobalPersistenceManager(PersistenceMgr);

		auto Retrieved = FITwinIModelMaterialHandler::GetGlobalPersistenceManager();
		UTEST_EQUAL(TEXT("Global persistence manager should be retrievable"), Retrieved.get(), PersistenceMgr.get());
	}

	SECTION("Specific persistence manager can be set and retrieved")
	{
		FITwinIModelMaterialHandler Handler;
		auto SpecificPersistenceMgr = std::make_shared<AdvViz::SDK::MaterialPersistenceManager>();

		Handler.SetSpecificPersistenceManager(SpecificPersistenceMgr);

		auto Retrieved = Handler.GetPersistenceManager();
		UTEST_EQUAL(TEXT("Specific persistence manager should be used"), Retrieved.get(), SpecificPersistenceMgr.get());
	}

	SECTION("GetPersistenceManager falls back to global when no specific manager is set")
	{
		FITwinIModelMaterialHandler Handler;
		auto GlobalPersistenceMgr = std::make_shared<AdvViz::SDK::MaterialPersistenceManager>();

		FITwinIModelMaterialHandler::SetGlobalPersistenceManager(GlobalPersistenceMgr);

		auto Retrieved = Handler.GetPersistenceManager();
		UTEST_EQUAL(TEXT("Global persistence manager should be used as fallback"), Retrieved.get(), GlobalPersistenceMgr.get());
	}

	SECTION("GetMaterialChannelIntensity with invalid material returns default")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t InvalidMaterialId = 999999;
		double Intensity = Handler.GetMaterialChannelIntensity(InvalidMaterialId, AdvViz::SDK::EChannelType::Color);

		// Should return a sensible default (likely 0.0 or 1.0 depending on implementation)
		UTEST_TRUE(TEXT("Intensity should be a valid number"), !FMath::IsNaN(Intensity));
	}

	SECTION("GetMaterialChannelColor with invalid material returns default")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t InvalidMaterialId = 999999;
		FLinearColor Color = Handler.GetMaterialChannelColor(InvalidMaterialId, AdvViz::SDK::EChannelType::Color);

		// Should return a sensible default color
		UTEST_TRUE(TEXT("Color should be valid"), !FMath::IsNaN(Color.R) && !FMath::IsNaN(Color.G) && !FMath::IsNaN(Color.B));
	}

	SECTION("GetMaterialChannelTextureID with invalid material returns empty")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t InvalidMaterialId = 999999;
		AdvViz::SDK::ETextureSource OutSource;
		FString TextureId = Handler.GetMaterialChannelTextureID(InvalidMaterialId, AdvViz::SDK::EChannelType::Color, OutSource);

		UTEST_TRUE(TEXT("TextureId should be empty for invalid material"), TextureId.IsEmpty());
	}

	SECTION("GetMaterialKind with invalid material returns default")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t InvalidMaterialId = 999999;
		AdvViz::SDK::EMaterialKind Kind = Handler.GetMaterialKind(InvalidMaterialId);

		// Should return a valid enum value (default material kind)
		UTEST_TRUE(TEXT("Kind should be a valid enum"), static_cast<int>(Kind) >= 0);
	}

	SECTION("GetMaterialCustomRequirements with invalid material returns false")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t InvalidMaterialId = 999999;
		AdvViz::SDK::EMaterialKind OutMaterialKind;
		bool bOutRequiresTranslucency;

		bool bHasCustomDef = Handler.GetMaterialCustomRequirements(InvalidMaterialId, OutMaterialKind, bOutRequiresTranslucency);

		UTEST_FALSE(TEXT("Should return false for invalid material"), bHasCustomDef);
	}

	SECTION("SetMaterialName with empty name returns false")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		constexpr uint64_t MaterialId = 12345;
		bool bResult = Handler.SetMaterialName(MaterialId, FString());

		UTEST_FALSE(TEXT("SetMaterialName should reject empty names"), bResult);
	}

	SECTION("GetCustomMaterials returns empty map initially")
	{
		FITwinIModelMaterialHandler Handler;

		const TMap<uint64, FITwinIModelMaterialHandler::FITwinCustomMaterial>& CustomMaterials = Handler.GetCustomMaterials();

		UTEST_TRUE(TEXT("CustomMaterials should be empty initially"), CustomMaterials.Num() == 0);
	}

	// =================================================================================
	// Tests with valid materials
	// =================================================================================

	SECTION("GetMaterialChannelIntensity with valid material")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot via the material info callback mechanism
		constexpr uint64_t MaterialId = 12345;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "TestMaterial", Lock);
		}

		// Test Get methods on FITwinIModelMaterialHandler
		double Roughness = Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Roughness);
		UTEST_TRUE(TEXT("Roughness should be valid"), !FMath::IsNaN(Roughness));
		UTEST_TRUE(TEXT("Roughness should be in valid range"), Roughness >= 0.0 && Roughness <= 1.0);

		double Metallic = Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Metallic);
		UTEST_TRUE(TEXT("Metallic should be valid"), !FMath::IsNaN(Metallic));
		UTEST_TRUE(TEXT("Metallic should be in valid range"), Metallic >= 0.0 && Metallic <= 1.0);

		double Opacity = Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Opacity);
		UTEST_TRUE(TEXT("Opacity should be valid"), !FMath::IsNaN(Opacity));
		UTEST_TRUE(TEXT("Opacity should be in valid range"), Opacity >= 0.0 && Opacity <= 1.0);
	}

	SECTION("GetMaterialChannelColor with valid material")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 23456;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "ColorTestMaterial", Lock);
		}

		// Test GetMaterialChannelColor method
		FLinearColor Color = Handler.GetMaterialChannelColor(MaterialId, AdvViz::SDK::EChannelType::Color);
		UTEST_TRUE(TEXT("Color R component should be valid"), !FMath::IsNaN(Color.R));
		UTEST_TRUE(TEXT("Color G component should be valid"), !FMath::IsNaN(Color.G));
		UTEST_TRUE(TEXT("Color B component should be valid"), !FMath::IsNaN(Color.B));

		// Verify color components are in valid range [0,1]
		UTEST_TRUE(TEXT("Red component in range"), Color.R >= 0.0f && Color.R <= 1.0f);
		UTEST_TRUE(TEXT("Green component in range"), Color.G >= 0.0f && Color.G <= 1.0f);
		UTEST_TRUE(TEXT("Blue component in range"), Color.B >= 0.0f && Color.B <= 1.0f);
	}

	SECTION("SetMaterialChannelColor modifies material definition")
	{
		FITwinIModelMaterialHandler Handler;

		TSceneMappingPtr SceneMapping;
		SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);

		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		CesiumUtility::JsonValue::Object extras =
		{
			{
				"iTwinMaterials",
				CesiumUtility::JsonValue::Array
				{
					CesiumUtility::JsonValue::Object
					{
						{ "id", "0x2000000039d" },
						{ "name", "Galvanized steel 2: Metals" }
					},
					CesiumUtility::JsonValue::Object
					{
						{ "id", "0x2000000039e" },
						{ "name", "Wood #001" }
					}
				}
			}
		};
		Tuner->ParseExtras(extras);

		// Create a valid material slot and add to custom materials
		constexpr uint64_t MaterialId = 0x2000000039d;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "SetColorMaterial", Lock);
		}

		// Get initial color
		FLinearColor InitialColor = Handler.GetMaterialChannelColor(MaterialId, AdvViz::SDK::EChannelType::Color);

		// Set a new color directly via GltfMatHelper (SetMaterialChannelColor requires SceneMapping)
		// This tests that the underlying material system correctly stores the color
		FLinearColor NewColor(0.8f, 0.3f, 0.2f, 1.0f);
		Handler.SetMaterialChannelColor(MaterialId, AdvViz::SDK::EChannelType::Color, NewColor, SceneMapping);

		// Verify the color was set by retrieving it via Handler's Get method
		FLinearColor UpdatedColor = Handler.GetMaterialChannelColor(MaterialId, AdvViz::SDK::EChannelType::Color);
		UTEST_EQUAL(TEXT("Red channel should match"), UpdatedColor.R, NewColor.R);
		UTEST_EQUAL(TEXT("Green channel should match"), UpdatedColor.G, NewColor.G);
		UTEST_EQUAL(TEXT("Blue channel should match"), UpdatedColor.B, NewColor.B);

		// Similar test for an intensity channel (e.g., Normal)
		double InitialNormalIntensity = Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Normal);
		UTEST_EQUAL(TEXT("Initial normal intensity should be 1.0"), InitialNormalIntensity, 1.0);
		Handler.SetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Normal, 0.5, SceneMapping);
		double NormalIntensity = Handler.GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Normal);
		UTEST_EQUAL(TEXT("Normal intensity should match"), NormalIntensity, 0.5);

		// Test setting and getting a texture.

		const FString TexturePath = FPaths::ProjectDir() / "../../../Public/SDK/Core/Visualization/Tests/UT_TextureToUpload.png";
		UTEST_TRUE(TEXT("texture exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*TexturePath));
		const FString ColorTextureId = SetTextureImpl(Handler, MaterialId, AdvViz::SDK::EChannelType::Color, TexturePath, *IModel, SceneMapping);

		AdvViz::SDK::ETextureSource OutSource;
		OutSource = AdvViz::SDK::ETextureSource::Library; // Initialize to a different value to ensure GetMaterialChannelTextureID sets it
		FString TextureId = Handler.GetMaterialChannelTextureID(MaterialId, AdvViz::SDK::EChannelType::Color, OutSource);
		UTEST_EQUAL(TEXT("Texture ID should match"), TextureId, ColorTextureId);
		UTEST_EQUAL(TEXT("Texture source should match"), OutSource, AdvViz::SDK::ETextureSource::LocalDisk);

		const FString RoughnessTextureId = SetTextureImpl(Handler, MaterialId, AdvViz::SDK::EChannelType::Roughness, TexturePath, *IModel, SceneMapping);
		OutSource = AdvViz::SDK::ETextureSource::Library;
		TextureId = Handler.GetMaterialChannelTextureID(MaterialId, AdvViz::SDK::EChannelType::Roughness, OutSource);
		UTEST_EQUAL(TEXT("Texture ID should match"), TextureId, RoughnessTextureId);
		UTEST_EQUAL(TEXT("Texture source should match"), OutSource, AdvViz::SDK::ETextureSource::LocalDisk);
	}

	SECTION("GetMaterialChannelTextureID with valid material returns expected value")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 34567;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "TextureMaterial", Lock);
		}

		// Get texture ID (initially should be empty for a newly created material)
		AdvViz::SDK::ETextureSource OutSource;
		FString TextureId = Handler.GetMaterialChannelTextureID(MaterialId, AdvViz::SDK::EChannelType::Color, OutSource);

		// For a newly created material slot without textures, should return empty or default texture
		UTEST_TRUE(TEXT("Texture ID should be empty"), TextureId.IsEmpty());
	}

	SECTION("GetMaterialUVTransform with valid material")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 45678;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "UVMaterial", Lock);
		}

		// Get UV transform
		AdvViz::SDK::ITwinUVTransform UVTransform = Handler.GetMaterialUVTransform(MaterialId);

		// Verify UV transform has sensible default values
		UTEST_TRUE(TEXT("UV transform offset should be valid"), !FMath::IsNaN(UVTransform.offset[0]) && !FMath::IsNaN(UVTransform.offset[1]));
		UTEST_TRUE(TEXT("UV transform scale should be valid"), !FMath::IsNaN(UVTransform.scale[0]) && !FMath::IsNaN(UVTransform.scale[1]));
		UTEST_TRUE(TEXT("UV transform rotation should be valid"), !FMath::IsNaN(UVTransform.rotation));
	}

	SECTION("GetMaterialKind with valid material")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 56789;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "KindMaterial", Lock);
		}

		// Get material kind
		AdvViz::SDK::EMaterialKind Kind = Handler.GetMaterialKind(MaterialId);

		// Verify it's a valid enum value
		UTEST_TRUE(TEXT("Material kind should be PBR"), Kind == AdvViz::SDK::EMaterialKind::PBR);
	}

	SECTION("GetMaterialCustomRequirements with valid material")
	{
		FITwinIModelMaterialHandler Handler;

		TSceneMappingPtr SceneMapping;
		SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);

		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		CesiumUtility::JsonValue::Object extras =
		{
			{
				"iTwinMaterials",
				CesiumUtility::JsonValue::Array
				{
					CesiumUtility::JsonValue::Object
					{
						{ "id", "0x00000006789" },
						{ "name", "Galvanized steel" }
					}
				}
			}
		};
		Tuner->ParseExtras(extras);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 0x00000006789;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "RequirementsMaterial", Lock);
		}

		AdvViz::SDK::EMaterialKind OutMaterialKind;
		bool bOutRequiresTranslucency;

		// For a newly created material without customizations, should return false
		bool bHasCustomReq = Handler.GetMaterialCustomRequirements(MaterialId, OutMaterialKind, bOutRequiresTranslucency);

		// Default material should not have custom requirements
		UTEST_TRUE(TEXT("Existing material should have requirements"), bHasCustomReq);
		UTEST_FALSE(TEXT("Default material should not be translucent"), bOutRequiresTranslucency);

		// Adding a MASK opacity texture should not make it require translucency.
		const FString OpacityTexturePath = FPaths::ProjectDir() / TEXT("../../../Public/BeUtils/UnitTests/Data/alpha_mask_arrow.png");
		UTEST_TRUE(TEXT("texture exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*OpacityTexturePath));
		SetTextureImpl(Handler, MaterialId, AdvViz::SDK::EChannelType::Opacity, OpacityTexturePath, *IModel, SceneMapping);
		Handler.GetMaterialCustomRequirements(MaterialId, OutMaterialKind, bOutRequiresTranslucency);
		UTEST_FALSE(TEXT("Material should not require translucency after adding opacity mask texture"), bOutRequiresTranslucency);

		// Adding a generic opacity texture should make it require translucency.
		const FString OpacityTexturePath2 = FPaths::ProjectDir() / TEXT("../../../Public/BeUtils/UnitTests/Data/alpha_mask_hlfoot.png");
		UTEST_TRUE(TEXT("texture exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*OpacityTexturePath2));
		SetTextureImpl(Handler, MaterialId, AdvViz::SDK::EChannelType::Opacity, OpacityTexturePath2, *IModel, SceneMapping);
		Handler.GetMaterialCustomRequirements(MaterialId, OutMaterialKind, bOutRequiresTranslucency);
		UTEST_TRUE(TEXT("Material should require translucency after adding opacity texture"), bOutRequiresTranslucency);
	}

	SECTION("SetMaterialName with valid material and non-empty name succeeds")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create a valid material slot
		constexpr uint64_t MaterialId = 78901;
		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId, "OldName", Lock);
		}

		// Add material to custom materials map so SetMaterialName can find it
		auto& CustomMaterials = const_cast<TMap<uint64, FITwinIModelMaterialHandler::FITwinCustomMaterial>&>(Handler.GetCustomMaterials());
		FITwinIModelMaterialHandler::FITwinCustomMaterial CustomMat;
		CustomMat.Name = TEXT("OldName");
		CustomMat.DisplayName = TEXT("OldName");
		CustomMaterials.Add(MaterialId, CustomMat);

		// Try to set a new name
		bool bResult = Handler.SetMaterialName(MaterialId, TEXT("NewMaterialName"));

		UTEST_TRUE(TEXT("SetMaterialName should succeed with valid name"), bResult);
	}

	SECTION("Multiple valid materials can coexist")
	{
		FITwinIModelMaterialHandler Handler;
		auto Tuner = std::make_shared<BeUtils::GltfTuner>();
		Handler.Initialize(Tuner, IModel);

		// Create multiple valid materials
		constexpr uint64_t MaterialId1 = 10001;
		constexpr uint64_t MaterialId2 = 10002;
		constexpr uint64_t MaterialId3 = 10003;

		{
			BeUtils::WLock Lock(Handler.GetGltfMatHelper()->GetMutex());
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId1, "Material1", Lock);
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId2, "Material2", Lock);
			Handler.GetGltfMatHelper()->CreateITwinMaterialSlot(MaterialId3, "Material3", Lock);
		}

		// Verify we can query all of them
		double Roughness1 = Handler.GetMaterialChannelIntensity(MaterialId1, AdvViz::SDK::EChannelType::Roughness);
		double Roughness2 = Handler.GetMaterialChannelIntensity(MaterialId2, AdvViz::SDK::EChannelType::Roughness);
		double Roughness3 = Handler.GetMaterialChannelIntensity(MaterialId3, AdvViz::SDK::EChannelType::Roughness);

		UTEST_TRUE(TEXT("Material1 roughness valid"), !FMath::IsNaN(Roughness1));
		UTEST_TRUE(TEXT("Material2 roughness valid"), !FMath::IsNaN(Roughness2));
		UTEST_TRUE(TEXT("Material3 roughness valid"), !FMath::IsNaN(Roughness3));
	}

	SECTION("Load/save material from/to file")
	{
		IModel->InitializeMaterialTuning();
		auto Tuner = IModel->GetGltfTuner();
		auto GltfMatHelper = IModel->GetGltfMaterialHelper();

		CesiumUtility::JsonValue::Object extras =
		{
			{
				"iTwinMaterials",
				CesiumUtility::JsonValue::Array
				{
					CesiumUtility::JsonValue::Object
					{
						{ "id", "0x00000002711" },
						{ "name", "Galvanized steel 2: Metals" }
					}
				}
			}
		};
		Tuner->ParseExtras(extras);

		// Create a valid material.
		constexpr uint64_t MaterialId = 0x00000002711;
		{
			BeUtils::WLock Lock(GltfMatHelper->GetMutex());
			GltfMatHelper->CreateITwinMaterialSlot(MaterialId, "Material", Lock);
		}

		FString MatJson = FPaths::ProjectDir() / "../../../Public/SDK/Core/Visualization/Tests/Material/test_material.json";
		UTEST_TRUE(TEXT("material JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*MatJson));

		// Use an absolute path.
		MatJson = ToAbsolute(MatJson);
		UTEST_TRUE(TEXT("Absolute material JSON exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*MatJson));

		bool const bLoaded = IModel->LoadMaterialFromAssetFile(MaterialId, MatJson);
		UTEST_TRUE(TEXT("Material file was loaded successfully"), bLoaded);


		AdvViz::SDK::ETextureSource OutSource;
		OutSource = AdvViz::SDK::ETextureSource::Library; // Initialize to a different value to ensure GetMaterialChannelTextureID sets it
		FString TextureId = IModel->GetMaterialChannelTextureID(MaterialId, AdvViz::SDK::EChannelType::Color, OutSource);
		TextureId.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		const FString ColorTexturePath = FPaths::ProjectDir() / "../../../Public/SDK/Core/Visualization/Tests/Material/test_material_textures/color.jpg";
		UTEST_EQUAL(TEXT("Loaded color texture should match"), TextureId, ToAbsolute(ColorTexturePath));
		UTEST_EQUAL(TEXT("Loaded color texture source should match"), OutSource, AdvViz::SDK::ETextureSource::LocalDisk);

		const double Roughness = IModel->GetMaterialChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Roughness);
		UTEST_TRUE(TEXT("Loaded roughness should match"), std::fabs(Roughness - 0.9009018148915184) < 1e-6);

		const FString TestContentPath = ToAbsolute(FPaths::ProjectSavedDir() / TEXT("MaterialTests"));
		if (!IFileManager::Get().DirectoryExists(*TestContentPath))
		{
			UTEST_TRUE(TEXT("Create directory for written materials"), IFileManager::Get().MakeDirectory(*TestContentPath));
		}
		auto const RemoveTestDir = [this, &TestContentPath]() {
			UTEST_TRUE(TEXT("Delete directory after writing material"), IFileManager::Get().DeleteDirectory(*TestContentPath, true, true));
			return true;
		};
		Be::CleanUpGuard CleanupGuard([&RemoveTestDir]() {
			RemoveTestDir();
		});

		auto ExportResult = FITwinMaterialLibrary::ExportMaterialToDisk(*IModel, MaterialId,
			TEXT("ThisIsAMaterial"), TestContentPath,
			FITwinMaterialLibrary::ExportOptions{
				.bPromptBeforeOverwrite = false
			});
		UTEST_TRUE(TEXT("Material was exported successfully"), ExportResult.has_value());

		const FString CopiedOpacityPath = TestContentPath / TEXT("opacity.png");
		UTEST_TRUE(TEXT("Opacity texture was copied"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*CopiedOpacityPath));

		FString ExportedMatJson;
		UTEST_TRUE(TEXT("MaterialExistsInDir"),  FITwinMaterialLibrary::MaterialExistsInDir(TestContentPath, ExportedMatJson));

		bool const bLoaded2 = IModel->LoadMaterialFromAssetFile(MaterialId, ExportedMatJson);
		UTEST_TRUE(TEXT("Exported material file was reloaded successfully"), bLoaded2);
		UTEST_TRUE(TEXT("Material name"), IModel->GetMaterialName(MaterialId, true) == TEXT("ThisIsAMaterial"));

		const FString TextureId2 = IModel->GetMaterialChannelTextureID(MaterialId, AdvViz::SDK::EChannelType::Color, OutSource);
		UTEST_TRUE(TEXT("Reloaded material file has texture"), TextureId2.EndsWith(TEXT("color.jpg")));
	}

	return true;
}

#endif // WITH_TESTS
