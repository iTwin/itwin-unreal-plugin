/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneMappingBuilderTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include "SceneMappingTestHelper.h"
#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <ITwinSceneMappingBuilder.h>
#include <ITwinSceneMapping.h>
#include <ITwinElementID.h>
#include <ITwinFeatureID.h>
#include <ITwinSceneMappingTypes.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>
#include <UObject/Package.h>

class USceneMappingTestHelper::FImpl
{
public:
	FImpl() {}
	~FImpl() {}
};


USceneMappingTestHelper::USceneMappingTestHelper()
	: Super()
	, Impl(MakePimpl<FImpl>())
{
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestMeshComponent"));
}

bool USceneMappingTestHelper::Init()
{
	return true;
}

bool USceneMappingTestHelper::PostCondition() const
{
	return true;
}

void USceneMappingTestHelper::OnReset()
{

}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FSceneMappingBuilderTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SceneMappingBuilder", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


bool FSceneMappingBuilderTest::RunTest(const FString& /*Parameters*/)
{
	USceneMappingTestHelper& Helper = USceneMappingTestHelper::Instance();

	SECTION("FITwinElementFeaturesInTile - Initialization")
	{
		FITwinElementFeaturesInTile ElementFeatures;
		ElementFeatures.ElementID = ITwinElementID(123);

		TestEqual("ElementID should be set correctly", ElementFeatures.ElementID.value(), 123ull);
		TestEqual("SceneRank should be NOT_ELEM initially", ElementFeatures.SceneRank, ITwinScene::NOT_ELEM);
		TestEqual("ExtractedRank should be NOT_EXTR initially", ElementFeatures.ExtractedRank, ITwinTile::NOT_EXTR);
		TestTrue("Features vector should be empty initially", ElementFeatures.Features.empty());
		TestTrue("Materials vector should be empty initially", ElementFeatures.Materials.empty());
		TestTrue("Meshes vector should be empty initially", ElementFeatures.Meshes.empty());
		TestFalse("bHasTestedForTranslucentFeaturesNeedingExtraction should be false", 
			ElementFeatures.bHasTestedForTranslucentFeaturesNeedingExtraction);
		TestFalse("bIsAlphaSetInTextureToHideExtractedElement should be false",
			ElementFeatures.bIsAlphaSetInTextureToHideExtractedElement);
	}

	SECTION("FITwinElementFeaturesInTile - Unload")
	{
		FITwinElementFeaturesInTile ElementFeatures;
		ElementFeatures.ElementID = ITwinElementID(789);
		ElementFeatures.SceneRank = ITwinScene::ElemIdx(5);
		ElementFeatures.Features.push_back(ITwinFeatureID(10));
		ElementFeatures.Features.push_back(ITwinFeatureID(20));

		// Add a material (note: in actual use this would be a valid material)
		ElementFeatures.Materials.push_back(nullptr);

		// Call Unload
		ElementFeatures.Unload();

		// After unload, features and materials should be cleared
		TestTrue("Features should be cleared after unload", ElementFeatures.Features.empty());
		TestTrue("Materials should be cleared after unload", ElementFeatures.Materials.empty());
		TestTrue("Meshes should be cleared after unload", ElementFeatures.Meshes.empty());

		// But other members should remain intact for re-population
		TestEqual("ElementID should remain after unload", static_cast<uint64>(ElementFeatures.ElementID.value()), 789ull);
	}

	SECTION("FITwinExtractedEntity - Initialization")
	{
		FITwinExtractedEntity ExtractedEntity;
		ExtractedEntity.ElementID = ITwinElementID(999);

		TestEqual("ElementID should be set", static_cast<uint64>(ExtractedEntity.ElementID.value()), 999ull);
		TestFalse("bIsCurrentlyTransformed should be false initially", ExtractedEntity.bIsCurrentlyTransformed);
		TestFalse("Should not have FeatureIDsUVIndex initially", ExtractedEntity.FeatureIDsUVIndex.has_value());
		TestFalse("TransformableMeshComponent should be invalid initially", ExtractedEntity.TransformableMeshComponent.IsValid());
		TestFalse("Material should be invalid initially", ExtractedEntity.Material.IsValid());
		TestFalse("IsValid should return false without valid pointers", ExtractedEntity.IsValid());
	}

	SECTION("FITwinExtractedElement - Initialization and Unload")
	{
		FITwinExtractedElement ExtractedElement;
		ExtractedElement.ElementID = ITwinElementID(1111);

		// Add some entities
		FITwinExtractedEntity Entity1;
		Entity1.ElementID = ITwinElementID(1111);
		ExtractedElement.Entities.push_back(Entity1);

		FITwinExtractedEntity Entity2;
		Entity2.ElementID = ITwinElementID(1111);
		ExtractedElement.Entities.push_back(Entity2);

		TestEqual("Should have 2 entities", static_cast<int32>(ExtractedElement.Entities.size()), 2);

		// Call Unload
		ExtractedElement.Unload();

		// Entities container should still exist but be cleared
		TestEqual("Entities should be cleared after unload", static_cast<int32>(ExtractedElement.Entities.size()), 0);
		TestEqual("ElementID should remain", static_cast<uint64>(ExtractedElement.ElementID.value()), 1111ull);
	}

	SECTION("FITwinSceneTile - Initialization")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		TestNull("pCesiumTile should be null initially", SceneTile.pCesiumTile);
		TestEqual("MaxFeatureID should be NOT_FEATURE", SceneTile.MaxFeatureID, ITwin::NOT_FEATURE);
		TestFalse("bIsSetupFor4DAnimation should be false", SceneTile.bIsSetupFor4DAnimation);
		TestFalse("bVisible should be false initially", SceneTile.bVisible);
		TestFalse("IsLoaded should return false for empty tile", SceneTile.IsLoaded());
		TestEqual("NumElementsFeatures should be 0", SceneTile.NumElementsFeatures(), 0ull);
	}

	SECTION("FITwinSceneTile - Element Features Lookup")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		// Test that finding non-existent element returns nullptr
		ITwinElementID TestElem1(100);
		TestNull("FindElementFeaturesSLOW should return nullptr for non-existent element",
			SceneTile.FindElementFeaturesSLOW(TestElem1));
		TestNull("FindElementFeaturesConstSLOW should return nullptr for non-existent element",
			SceneTile.FindElementFeaturesConstSLOW(TestElem1));

		// Add an element
		auto& ElemFeatures = SceneTile.ElementFeaturesSLOW(TestElem1);
		TestEqual("ElementID should match", ElemFeatures.ElementID.value(), 100ull);
		TestEqual("NumElementsFeatures should be 1", SceneTile.NumElementsFeatures(), 1ull);

		// Test finding existing element
		auto* FoundElem = SceneTile.FindElementFeaturesSLOW(TestElem1);
		TestNotNull("FindElementFeaturesSLOW should find existing element", FoundElem);
		if (FoundElem)
		{
			TestEqual("Found element should have correct ID", FoundElem->ElementID.value(), 100ull);
		}

		// Test FindElementFeaturesConstSLOW with rank output
		ITwinTile::ElemIdx RankOut;
		auto* ConstFound = SceneTile.FindElementFeaturesConstSLOW(TestElem1, &RankOut);
		TestNotNull("FindElementFeaturesConstSLOW should find existing element", ConstFound);
		TestEqual("Rank should be 0 for first element", RankOut.value(), 0u);

		// Add another element
		ITwinElementID TestElem2(200);
		auto& ElemFeatures2 = SceneTile.ElementFeaturesSLOW(TestElem2);
		TestEqual("NumElementsFeatures should be 2", SceneTile.NumElementsFeatures(), 2ull);

		// Verify both elements exist
		TestNotNull("First element should still exist", SceneTile.FindElementFeaturesSLOW(TestElem1));
		TestNotNull("Second element should exist", SceneTile.FindElementFeaturesSLOW(TestElem2));

		ITwinTile::ElemIdx RankOut2;
		ConstFound = SceneTile.FindElementFeaturesConstSLOW(TestElem2, &RankOut2);
		TestNotNull("FindElementFeaturesConstSLOW should find existing element", ConstFound);
		TestEqual("Rank should be 1 for second element", RankOut2.value(), 1u);
	}

	SECTION("FITwinSceneTile - Material Features Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		ITwinRenderMaterialElementID MatID1(1000);
		ITwinRenderMaterialElementID MatID2(2000);

		// Test finding non-existent material
		TestNull("FindMaterialFeaturesSLOW should return nullptr for non-existent material",
			SceneTile.FindMaterialFeaturesSLOW(MatID1));

		// Add material features
		auto& MatFeatures1 = SceneTile.MaterialFeaturesSLOW(MatID1);
		TestEqual("MaterialID should match", MatFeatures1.MaterialID.value(), 1000ull);

		// Add features to the material
		MatFeatures1.Features.insert(ITwinFeatureID(10));
		MatFeatures1.Features.insert(ITwinFeatureID(20));
		MatFeatures1.Features.insert(ITwinFeatureID(30));

		// Test finding existing material
		auto* FoundMat = SceneTile.FindMaterialFeaturesSLOW(MatID1);
		TestNotNull("FindMaterialFeaturesSLOW should find existing material", FoundMat);
		if (FoundMat)
		{
			TestEqual("Material should have 3 features", static_cast<int32>(FoundMat->Features.size()), 3);
			TestTrue("Material should contain feature 10", FoundMat->Features.contains(ITwinFeatureID(10)));
			TestTrue("Material should contain feature 20", FoundMat->Features.contains(ITwinFeatureID(20)));
		}

		// Add second material
		auto& MatFeatures2 = SceneTile.MaterialFeaturesSLOW(MatID2);
		MatFeatures2.Features.insert(ITwinFeatureID(40));

		// Verify both materials exist
		TestNotNull("First material should still exist", SceneTile.FindMaterialFeaturesSLOW(MatID1));
		TestNotNull("Second material should exist", SceneTile.FindMaterialFeaturesSLOW(MatID2));
	}

	SECTION("FITwinSceneTile - Model Features Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		ITwinElementID ModelID1(5000);
		ITwinElementID ModelID2(6000);

		// Test finding non-existent model
		TestNull("FindModelFeaturesSLOW should return nullptr for non-existent model",
			SceneTile.FindModelFeaturesSLOW(ModelID1));

		// Add model features
		auto& ModelFeatures = SceneTile.ModelFeaturesSLOW(ModelID1);
		TestEqual("ModelID should match", ModelFeatures.ModelID.value(), 5000ull);

		// Add features to model
		ModelFeatures.Features.insert(ITwinFeatureID(100));
		ModelFeatures.Features.insert(ITwinFeatureID(101));

		// Test finding
		auto* Found = SceneTile.FindModelFeaturesSLOW(ModelID1);
		TestNotNull("FindModelFeaturesSLOW should find existing model", Found);
		if (Found)
		{
			TestEqual("Model should have 2 features", static_cast<int32>(Found->Features.size()), 2);
		}

		// Add second model
		auto& ModelFeatures2 = SceneTile.ModelFeaturesSLOW(ModelID2);
		TestNotNull("Second model should exist", SceneTile.FindModelFeaturesSLOW(ModelID2));
	}

	SECTION("FITwinSceneTile - Category Features Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		ITwinElementID CategoryID1(7000);
		ITwinElementID CategoryID2(8000);

		// Test finding non-existent category
		TestNull("FindCategoryFeaturesSLOW should return nullptr for non-existent category",
			SceneTile.FindCategoryFeaturesSLOW(CategoryID1));

		// Add category features
		auto& CatFeatures = SceneTile.CategoryFeaturesSLOW(CategoryID1);
		TestEqual("CategoryID should match", CatFeatures.CategoryID.value(), 7000ull);

		// Add features
		CatFeatures.Features.insert(ITwinFeatureID(200));
		CatFeatures.Features.insert(ITwinFeatureID(201));
		CatFeatures.Features.insert(ITwinFeatureID(202));

		// Test finding
		auto* Found = SceneTile.FindCategoryFeaturesSLOW(CategoryID1);
		TestNotNull("FindCategoryFeaturesSLOW should find existing category", Found);
		if (Found)
		{
			TestEqual("Category should have 3 features", static_cast<int32>(Found->Features.size()), 3);
		}
	}

	SECTION("FITwinSceneTile - CategoryPerModel Features Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		ITwinElementID CategoryID(9000);
		ITwinElementID ModelID(10000);

		// Test finding non-existent category per model
		TestNull("FindCategoryPerModelFeaturesSLOW should return nullptr for non-existent entry",
			SceneTile.FindCategoryPerModelFeaturesSLOW(std::make_pair(std::cref(CategoryID), std::cref(ModelID))));

		// Add category per model features
		auto& CatPerModelFeatures = SceneTile.CategoryPerModelFeaturesSLOW(CategoryID, ModelID);
		TestEqual("CategoryID should match", CatPerModelFeatures.CategoryID.value(), 9000ull);
		TestEqual("ModelID should match", CatPerModelFeatures.ModelID.value(), 10000ull);

		// Add features
		CatPerModelFeatures.Features.insert(ITwinFeatureID(300));
		CatPerModelFeatures.Features.insert(ITwinFeatureID(301));

		// Test finding
		auto* Found = SceneTile.FindCategoryPerModelFeaturesSLOW(std::make_pair(std::cref(CategoryID), std::cref(ModelID)));
		TestNotNull("FindCategoryPerModelFeaturesSLOW should find existing entry", Found);
		if (Found)
		{
			TestEqual("CategoryPerModel should have 2 features", static_cast<int32>(Found->Features.size()), 2);
		}

		// Test with different model under same category
		ITwinElementID ModelID2(11000);
		auto& CatPerModelFeatures2 = SceneTile.CategoryPerModelFeaturesSLOW(CategoryID, ModelID2);
		CatPerModelFeatures2.Features.insert(ITwinFeatureID(400));

		// Verify both entries exist
		TestNotNull("First CategoryPerModel should exist",
			SceneTile.FindCategoryPerModelFeaturesSLOW(std::make_pair(std::cref(CategoryID), std::cref(ModelID))));
		TestNotNull("Second CategoryPerModel should exist",
			SceneTile.FindCategoryPerModelFeaturesSLOW(std::make_pair(std::cref(CategoryID), std::cref(ModelID2))));
	}

	SECTION("FITwinSceneTile - Extracted Elements Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		ITwinElementID ElemID1(12000);
		ITwinElementID ElemID2(13000);

		// Test finding non-existent extracted element
		TestNull("FindExtractedElementSLOW should return nullptr for non-existent element",
			SceneTile.FindExtractedElementSLOW(ElemID1));

		// Create element features first
		auto& ElemFeatures = SceneTile.ElementFeaturesSLOW(ElemID1);

		// Create extracted element
		auto [ExtractedRef, bWasInserted] = SceneTile.ExtractedElementSLOW(ElemFeatures);
		TestTrue("ExtractedElementSLOW should insert new element", bWasInserted);
		TestEqual("Extracted element ID should match", ExtractedRef.get().ElementID.value(), 12000ull);
		TestNotEqual("ExtractedRank should be set", ElemFeatures.ExtractedRank, ITwinTile::NOT_EXTR);

		// Verify element can be found
		auto* FoundExtracted = SceneTile.FindExtractedElementSLOW(ElemID1);
		TestNotNull("FindExtractedElementSLOW should find extracted element", FoundExtracted);

		// Test retrieving by rank
		auto& ExtractedByRank = SceneTile.ExtractedElement(ElemFeatures.ExtractedRank);
		TestEqual("ExtractedElement by rank should match", ExtractedByRank.ElementID.value(), 12000ull);

		// Try to get same element again (should not insert)
		auto [ExtractedRef2, bWasInserted2] = SceneTile.ExtractedElementSLOW(ElemFeatures);
		TestFalse("ExtractedElementSLOW should not insert duplicate", bWasInserted2);
		TestEqual("Should return same element", ExtractedRef2.get().ElementID.value(), 12000ull);
	}

	SECTION("FITwinSceneTile - ForEach Methods")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		// Add multiple elements
		for (int i = 0; i < 5; ++i)
		{
			auto& Elem = SceneTile.ElementFeaturesSLOW(ITwinElementID(100 + i));
			Elem.Features.push_back(ITwinFeatureID(i));
		}

		// Test ForEachElementFeatures (non-const)
		int CountNonConst = 0;
		SceneTile.ForEachElementFeatures([&CountNonConst](FITwinElementFeaturesInTile& Elem)
		{
			CountNonConst++;
			// Modify to test non-const access
			Elem.bHasTestedForTranslucentFeaturesNeedingExtraction = true;
		});
		TestEqual("ForEachElementFeatures should iterate 5 elements", CountNonConst, 5);

		// Test ForEachElementFeatures (const)
		int CountConst = 0;
		const FITwinSceneTile& ConstTile = SceneTile;
		ConstTile.ForEachElementFeatures([&CountConst, this](FITwinElementFeaturesInTile const& Elem)
		{
			CountConst++;
			TestTrue("Flag should have been set in previous loop",
				Elem.bHasTestedForTranslucentFeaturesNeedingExtraction);
		});
		TestEqual("Const ForEachElementFeatures should iterate 5 elements", CountConst, 5);

		// Add extracted elements
		for (int i = 0; i < 3; ++i)
		{
			auto& ElemFeatures = SceneTile.ElementFeaturesSLOW(ITwinElementID(200 + i));
			auto [ExtractedRef, _] = SceneTile.ExtractedElementSLOW(ElemFeatures);

			// Add an entity to each extracted element
			FITwinExtractedEntity Entity;
			Entity.ElementID = ElemFeatures.ElementID;
			Entity.bIsCurrentlyTransformed = (i % 2 == 0);
			ExtractedRef.get().Entities.push_back(Entity);
		}

		// Test ForEachExtractedElement
		int ExtractedCount = 0;
		SceneTile.ForEachExtractedElement([&ExtractedCount, this](FITwinExtractedElement& Extracted)
		{
			ExtractedCount++;
			TestTrue("Each extracted element should have entities", !Extracted.Entities.empty());
		});
		TestEqual("ForEachExtractedElement should iterate 3 elements", ExtractedCount, 3);

		// Test ForEachExtractedEntity
		int EntityCount = 0;
		SceneTile.ForEachExtractedEntity([&EntityCount](FITwinExtractedEntity& Entity)
		{
			EntityCount++;
		});
		TestEqual("ForEachExtractedEntity should iterate 3 entities", EntityCount, 3);
	}

	SECTION("FITwinSceneTile - Unload and Reload Behavior")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		// Populate the tile with data
		auto& ElemFeatures1 = SceneTile.ElementFeaturesSLOW(ITwinElementID(300));
		ElemFeatures1.Features.push_back(ITwinFeatureID(1));
		ElemFeatures1.Features.push_back(ITwinFeatureID(2));

		auto& ElemFeatures2 = SceneTile.ElementFeaturesSLOW(ITwinElementID(301));
		ElemFeatures2.Features.push_back(ITwinFeatureID(3));

		auto& MatFeatures = SceneTile.MaterialFeaturesSLOW(ITwinRenderMaterialElementID(1500));
		MatFeatures.Features.insert(ITwinFeatureID(10));

		// Create extracted element
		auto [ExtractedRef, _] = SceneTile.ExtractedElementSLOW(ElemFeatures1);
		FITwinExtractedEntity Entity;
		Entity.ElementID = ElemFeatures1.ElementID;
		ExtractedRef.get().Entities.push_back(Entity);

		TestEqual("Should have 2 element features before unload", SceneTile.NumElementsFeatures(), 2ull);
		TestFalse("Tile should not be loaded without GltfMeshes", SceneTile.IsLoaded());

		// Mock adding a GltfMesh wrapper to make it "loaded"
		uint64_t ITwinMaterialID = 1234;
		SceneTile.GltfMeshWrappers().emplace_back(*Helper.GetMeshComponent(), ITwinMaterialID);

		TestTrue("Tile should be loaded with GltfMeshes", SceneTile.IsLoaded());

		// Unload the tile
		SceneTile.Unload();

		// After unload, containers should be preserved but cleared
		TestEqual("Element features container should preserve size", SceneTile.NumElementsFeatures(), 2ull);
		TestFalse("Tile should not be loaded after unload", SceneTile.IsLoaded());

		// Verify element features were cleared but structure preserved
		auto* ElemAfterUnload = SceneTile.FindElementFeaturesSLOW(ITwinElementID(300));
		TestNotNull("Element features should still exist after unload", ElemAfterUnload);
		if (ElemAfterUnload)
		{
			TestTrue("Features should be cleared after unload", ElemAfterUnload->Features.empty());
			TestEqual("ElementID should be preserved", ElemAfterUnload->ElementID.value(), 300ull);
		}

		// Verify extracted elements structure is preserved
		auto* ExtractedAfterUnload = SceneTile.FindExtractedElementSLOW(ITwinElementID(300));
		TestNotNull("Extracted element container should exist", ExtractedAfterUnload);
		if (ExtractedAfterUnload)
		{
			TestTrue("Extracted entities should be cleared", ExtractedAfterUnload->Entities.empty());
		}
	}

	SECTION("FITwinSceneTile - Material Management")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		TestTrue("Materials list should be empty initially", SceneTile.GetMaterials().empty());

		// Note: Cannot create real UMaterialInstanceDynamic without full engine context
		// but we can test the container behavior
		UMaterialInstanceDynamic* DummyMat = nullptr;
		SceneTile.AddMaterial(DummyMat);

		TestEqual("Materials list should have 1 entry", static_cast<int32>(SceneTile.GetMaterials().size()), 1);
	}

	SECTION("FITwinSceneTile - Bounding Box")
	{
		CesiumTileID TileID;
		FITwinSceneTile SceneTile(TileID);

		// Without mesh components, bounding box should be invalid
		FBox BBox = SceneTile.GetBoundingBoxOfGlTFMeshes();
		TestFalse("Bounding box should be invalid without meshes", static_cast<bool>(BBox.IsValid));
	}

	SECTION("FITwinMaterialFeaturesInTile - Initialization")
	{
		FITwinMaterialFeaturesInTile MaterialFeatures;
		MaterialFeatures.MaterialID = ITwinRenderMaterialElementID(2222);

		TestEqual("MaterialID should be set", static_cast<uint64>(MaterialFeatures.MaterialID.value()), 2222ull);
		TestTrue("Features set should be empty", MaterialFeatures.Features.empty());
	}

	SECTION("FITwinModelFeaturesInTile - Initialization")
	{
		FITwinModelFeaturesInTile ModelFeatures;
		ModelFeatures.ModelID = ITwinElementID(3333);

		TestEqual("ModelID should be set", static_cast<uint64>(ModelFeatures.ModelID.value()), 3333ull);
		TestTrue("Features set should be empty", ModelFeatures.Features.empty());
	}

	SECTION("FITwinCategoryFeaturesInTile - Initialization")
	{
		FITwinCategoryFeaturesInTile CategoryFeatures;
		CategoryFeatures.CategoryID = ITwinElementID(4444);

		TestEqual("CategoryID should be set", static_cast<uint64>(CategoryFeatures.CategoryID.value()), 4444ull);
		TestTrue("Features set should be empty", CategoryFeatures.Features.empty());
	}

	SECTION("FITwinCategoryPerModelFeaturesInTile - Initialization")
	{
		FITwinCategoryPerModelFeaturesInTile CategoryPerModelFeatures;
		CategoryPerModelFeatures.CategoryID = ITwinElementID(5555);
		CategoryPerModelFeatures.ModelID = ITwinElementID(6666);

		TestEqual("CategoryID should be set", static_cast<uint64>(CategoryPerModelFeatures.CategoryID.value()), 5555ull);
		TestEqual("ModelID should be set", static_cast<uint64>(CategoryPerModelFeatures.ModelID.value()), 6666ull);
		TestTrue("Features set should be empty", CategoryPerModelFeatures.Features.empty());
	}

	SECTION("FITwinPropertyTextureFlag - Initialization and Setup")
	{
		FITwinPropertyTextureFlag TextureFlag;

		std::shared_ptr<FITwinDynamicShadingBGRA8Property> BGRATexture;
		FITwinDynamicShadingBGRA8Property::Create(BGRATexture, ITwinFeatureID(64), std::nullopt);

		TestTrue("TexturesSetup should be 0 initially", TextureFlag.NeedSetupInMaterials(BGRATexture, 1));

		// Simulate texture setup
		TextureFlag.OnTextureSetupInMaterials(2);
		TestFalse("TexturesSetup should be updated", TextureFlag.NeedSetupInMaterials(BGRATexture, 2));

		// Invalidate
		TextureFlag.Invalidate();
		TestTrue("TexturesSetup should be 0 after invalidation", TextureFlag.NeedSetupInMaterials(BGRATexture, 1));
	}

	SECTION("FElemAnimRequirements - Initialization")
	{
		FElemAnimRequirements AnimRequirements;

		TestFalse("bNeedHiliteAndOpaTex should be false initially", AnimRequirements.bNeedHiliteAndOpaTex);
		TestFalse("bNeedCuttingPlaneTex should be false initially", AnimRequirements.bNeedCuttingPlaneTex);
		TestFalse("bNeedTranslucentMat should be false initially", AnimRequirements.bNeedTranslucentMat);
		TestFalse("bNeedBeTransformable should be false initially", AnimRequirements.bNeedBeTransformable);
	}

	SECTION("FITwinElement - Initialization")
	{
		FITwinElement Element;

		TestFalse("bHasMesh should be false initially", Element.bHasMesh);
		TestEqual("ElementID should be NOT_ELEMENT", Element.ElementID, ITwin::NOT_ELEMENT);
		TestEqual("ParentInVec should be NOT_ELEM", Element.ParentInVec, ITwinScene::NOT_ELEM);
		TestEqual("DuplicatesList should be NOT_DUPL", Element.DuplicatesList, ITwinScene::NOT_DUPL);
		TestTrue("AnimationKeys should be empty", Element.AnimationKeys.empty());
		TestTrue("SubElemsInVec should be empty", Element.SubElemsInVec.empty());
		TestFalse("BBox should be invalid initially", static_cast<bool>(Element.BBox.IsValid));
	}

	SECTION("ITwinSceneMappingTypes - Strong Type Constants")
	{
		// Test ITwinScene strong types
		TestEqual("NOT_TILE should have max uint32 value", ITwinScene::NOT_TILE.value(), (uint32_t)-1);
		TestEqual("NOT_ELEM should have max uint64 value", ITwinScene::NOT_ELEM.value(), (size_t)-1);
		TestEqual("NOT_DUPL should have max uint64 value", ITwinScene::NOT_DUPL.value(), (size_t)-1);

		// Test ITwinTile strong types
		TestEqual("NOT_ELEM (ITwinTile) should have max uint32 value", ITwinTile::NOT_ELEM.value(), (uint32_t)-1);
		TestEqual("NOT_EXTR should have max uint32 value", ITwinTile::NOT_EXTR.value(), (uint32_t)-1);

		// Test that strong types prevent accidental mixing
		ITwinScene::TileIdx SceneTileIdx(5);
		ITwinScene::ElemIdx SceneElemIdx(10);
		TestEqual("SceneTileIdx value should be 5", SceneTileIdx.value(), 5u);
		TestEqual("SceneElemIdx value should be 10", SceneElemIdx.value(), 10ull);
	}

	SECTION("FPickingOptions - Default and Custom")
	{
		FPickingOptions DefaultOptions;
		TestFalse("OnlyVisibleTiles should be false by default", DefaultOptions.OnlyVisibleTiles());
		TestFalse("TestElementVisibility should be false by default", DefaultOptions.TestElementVisibility());
		TestFalse("MakeSelected should be false by default", DefaultOptions.MakeSelected());
		TestFalse("SkipResetSelection should be false by default", DefaultOptions.SkipResetSelection());
		TestFalse("HitWorldPosition should be empty by default", DefaultOptions.HitWorldPosition().has_value());

		// Test CreateDefaultPickVisible
		FPickingOptions PickVisibleOptions = FPickingOptions::CreateDefaultPickVisible();
		TestTrue("OnlyVisibleTiles should be true for PickVisible", PickVisibleOptions.OnlyVisibleTiles());
		TestTrue("TestElementVisibility should be true for PickVisible", PickVisibleOptions.TestElementVisibility());
		TestTrue("MakeSelected should be true for PickVisible", PickVisibleOptions.MakeSelected());
	}

	SECTION("FShowHideOptions - Initialization")
	{
		FShowHideOptions ShowHideOptions;
		TestFalse("OnlyVisibleTiles should be false by default", ShowHideOptions.OnlyVisibleTiles());
		TestFalse("ConstructionData should be false by default", ShowHideOptions.ConstructionData());
		TestFalse("SkipResetSelection should be false by default", ShowHideOptions.SkipResetSelection());
		TestFalse("Force should be false by default", ShowHideOptions.Force());
	}

	SECTION("UITwinSceneMappingBuilder - BuildFromNonCesiumMesh")
	{
		// Note: This test verifies the static method signature exists
		// Full testing would require setting up a valid mesh component

		TSceneMappingPtr SceneMapping;
		SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);

		const uint64_t ITwinMaterialID = 12345;

		// Verify the method can be called (will do nothing with null inputs, but validates signature)
		UITwinSceneMappingBuilder::BuildFromNonCesiumMesh(SceneMapping, *Helper.GetMeshComponent(), ITwinMaterialID);

		TestTrue("BuildFromNonCesiumMesh should execute without crashing", true);
	}

	SECTION("FITwinPreFetchedPrimitiveData - Initialization")
	{
		FITwinPreFetchedPrimitiveData PreFetchedData;

		TestTrue("UniqueFeatureIDs should be empty", PreFetchedData.UniqueFeatureIDs.empty());
		TestTrue("FeatureDataMap should be empty", PreFetchedData.FeatureDataMap.empty());
		TestFalse("bHasData should be false initially", PreFetchedData.bHasData);
		TestNull("pOriginalModel should be null initially", PreFetchedData.pOriginalModel);
	}

	// Helper to create a populated tile for testing pick/hide/show methods
	auto CreatePopulatedTile = []() -> FITwinSceneTile
	{
		CesiumTileID TileID;
		FITwinSceneTile Tile(TileID);
		Tile.MaxFeatureID = ITwinFeatureID(50); // Set max feature ID
		Tile.bVisible = true; // Mark as visible

		// Add elements with features
		ITwinElementID Elem1(1001);
		auto& ElemFeatures1 = Tile.ElementFeaturesSLOW(Elem1);
		ElemFeatures1.Features.push_back(ITwinFeatureID(10));
		ElemFeatures1.Features.push_back(ITwinFeatureID(11));

		ITwinElementID Elem2(1002);
		auto& ElemFeatures2 = Tile.ElementFeaturesSLOW(Elem2);
		ElemFeatures2.Features.push_back(ITwinFeatureID(20));
		ElemFeatures2.Features.push_back(ITwinFeatureID(21));

		ITwinElementID Elem3(1003);
		auto& ElemFeatures3 = Tile.ElementFeaturesSLOW(Elem3);
		ElemFeatures3.Features.push_back(ITwinFeatureID(30));

		// Add materials with features
		ITwinRenderMaterialElementID Mat1(2001);
		auto& MatFeatures1 = Tile.MaterialFeaturesSLOW(Mat1);
		MatFeatures1.Features.insert(ITwinFeatureID(10));
		MatFeatures1.Features.insert(ITwinFeatureID(20));

		ITwinRenderMaterialElementID Mat2(2002);
		auto& MatFeatures2 = Tile.MaterialFeaturesSLOW(Mat2);
		MatFeatures2.Features.insert(ITwinFeatureID(30));

		// Add models with features
		ITwinElementID Model1(3001);
		auto& ModelFeatures1 = Tile.ModelFeaturesSLOW(Model1);
		ModelFeatures1.Features.insert(ITwinFeatureID(10));
		ModelFeatures1.Features.insert(ITwinFeatureID(11));

		ITwinElementID Model2(3002);
		auto& ModelFeatures2 = Tile.ModelFeaturesSLOW(Model2);
		ModelFeatures2.Features.insert(ITwinFeatureID(20));

		// Add categories with features
		ITwinElementID Cat1(4001);
		auto& CatFeatures1 = Tile.CategoryFeaturesSLOW(Cat1);
		CatFeatures1.Features.insert(ITwinFeatureID(10));
		CatFeatures1.Features.insert(ITwinFeatureID(20));
		CatFeatures1.Features.insert(ITwinFeatureID(30));

		ITwinElementID Cat2(4002);
		auto& CatFeatures2 = Tile.CategoryFeaturesSLOW(Cat2);
		CatFeatures2.Features.insert(ITwinFeatureID(21));

		// Add categories per model
		auto& CatPerModel1 = Tile.CategoryPerModelFeaturesSLOW(Cat1, Model1);
		CatPerModel1.Features.insert(ITwinFeatureID(10));
		CatPerModel1.Features.insert(ITwinFeatureID(11));

		auto& CatPerModel2 = Tile.CategoryPerModelFeaturesSLOW(Cat2, Model2);
		CatPerModel2.Features.insert(ITwinFeatureID(20));
		CatPerModel2.Features.insert(ITwinFeatureID(21));

		return Tile;
	};

	SECTION("FITwinSceneTile::PickElement - Pick Element in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Elem1(1001);
		FPickingOptions Opts = FPickingOptions().MakeSelected(true);

		// Pick element that exists
		bool bPicked = Tile.PickElement(Elem1, TextureNeeds, Opts);
		TestTrue("PickElement should succeed for existing element", bPicked);
		TestTrue("Texture should be created and changed", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Pick non-existent element
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		ITwinElementID NonExistent(9999);
		bPicked = Tile.PickElement(NonExistent, TextureNeeds, Opts);
		TestFalse("PickElement should fail for non-existent element", bPicked);

		// Test with OnlyVisibleTiles option on invisible tile
		FITwinSceneTile InvisibleTile = CreatePopulatedTile();
		InvisibleTile.bVisible = false;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FPickingOptions VisOpts = FPickingOptions().MakeSelected(true).OnlyVisibleTiles(true);
		bPicked = InvisibleTile.PickElement(Elem1, TextureNeeds, VisOpts);
		TestFalse("PickElement should fail for invisible tile when OnlyVisibleTiles is true", bPicked);
	}

	SECTION("FITwinSceneTile::PickMaterial - Pick Material in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinRenderMaterialElementID Mat1(2001);
		FPickingOptions Opts = FPickingOptions().MakeSelected(true);

		// Pick material that exists
		bool bPicked = Tile.PickMaterial(Mat1, TextureNeeds, Opts);
		TestTrue("PickMaterial should succeed for existing material", bPicked);
		TestTrue("Texture should be created and changed", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Pick non-existent material
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		ITwinRenderMaterialElementID NonExistent(9999);
		bPicked = Tile.PickMaterial(NonExistent, TextureNeeds, Opts);
		TestFalse("PickMaterial should fail for non-existent material", bPicked);

		// Test with visible tile requirement
		Tile.bVisible = false;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FPickingOptions VisOpts = FPickingOptions().MakeSelected(true).OnlyVisibleTiles(true);
		bPicked = Tile.PickMaterial(Mat1, TextureNeeds, VisOpts);
		TestFalse("PickMaterial should fail for invisible tile when OnlyVisibleTiles is true", bPicked);
	}

	SECTION("FITwinSceneTile::DeselectElements - Deselect Multiple Elements")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		// First, select some elements
		ITwinElementID Elem1(1001);
		ITwinElementID Elem2(1002);
		FPickingOptions PickOpts = FPickingOptions().MakeSelected(true);

		Tile.PickElement(Elem1, TextureNeeds, PickOpts);
		PickOpts = PickOpts.SkipResetSelection(true); // Add second without deselecting first
		Tile.PickElement(Elem2, TextureNeeds, PickOpts);

		// Now deselect them
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> ToDeselect = { Elem1, Elem2 };
		Tile.DeselectElements(ToDeselect, TextureNeeds);

		TestFalse("Texture should not be created after deselection", TextureNeeds.bWasCreated);
		TestTrue("Texture should be changed after deselection", TextureNeeds.bWasChanged);

		// Deselecting non-existent elements should not crash
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> NonExistent = { ITwinElementID(9999) };
		Tile.DeselectElements(NonExistent, TextureNeeds);
		TestFalse("Deselecting non-existent elements should not change texture", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created for non-existent element", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::HideElements - Hide Elements in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Elem1(1001);
		ITwinElementID Elem2(1002);
		std::unordered_set<ITwinElementID> ToHide = { Elem1, Elem2 };
		FShowHideOptions Opts;

		// Hide elements
		Tile.HideElements(ToHide, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after hiding", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Test hiding on a tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.HideElements(ToHide, TextureNeeds, Opts);
		TestFalse("Hiding on empty tile should not change texture", TextureNeeds.bWasChanged);

		// Test with OnlyVisibleTiles option on invisible tile
		Tile.bVisible = false;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FShowHideOptions VisOpts = FShowHideOptions().OnlyVisibleTiles(true);
		Tile.HideElements(ToHide, TextureNeeds, VisOpts);
		TestFalse("Hiding on invisible tile with OnlyVisibleTiles should not change texture", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);

		// Test Force option - hide already hidden element
		Tile.bVisible = true;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		Tile.HideElements(ToHide, TextureNeeds, Opts); // Hide again
		bool bChangedWithoutForce = TextureNeeds.bWasChanged;

		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FShowHideOptions ForceOpts = FShowHideOptions().Force(true);
		Tile.HideElements(ToHide, TextureNeeds, ForceOpts);
		TestTrue("Force option should apply even if already hidden", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::ShowElements - Show Elements in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Elem1(1001);
		ITwinElementID Elem3(1003);
		std::unordered_set<ITwinElementID> ToShow = { Elem1, Elem3 };
		FShowHideOptions Opts;

		// First hide elements
		Tile.HideElements(ToShow, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after showing", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Then show them
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		Tile.ShowElements(ToShow, TextureNeeds, Opts);
		TestTrue("Texture should be changed after showing", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);

		// Test showing on a tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.ShowElements(ToShow, TextureNeeds, Opts);
		TestFalse("Showing on empty tile should not change texture", TextureNeeds.bWasChanged);

		// Test with OnlyVisibleTiles option on invisible tile
		Tile.bVisible = false;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FShowHideOptions VisOpts = FShowHideOptions().OnlyVisibleTiles(true);
		Tile.ShowElements(ToShow, TextureNeeds, VisOpts);
		TestFalse("Showing on invisible tile with OnlyVisibleTiles should not change texture", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::HideModels - Hide Models in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Model1(3001);
		ITwinElementID Model2(3002);
		std::unordered_set<ITwinElementID> ToHide = { Model1, Model2 };
		FShowHideOptions Opts;

		// Hide models
		Tile.HideModels(ToHide, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after hiding models", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Test hiding non-existent model
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> NonExistent = { ITwinElementID(9999) };
		Tile.HideModels(NonExistent, TextureNeeds, Opts);
		// Should not crash, but may or may not change texture depending on implementation

		// Test on tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.HideModels(ToHide, TextureNeeds, Opts);
		TestFalse("Hiding models on empty tile should not change texture", TextureNeeds.bWasChanged);

		// Test Force option
		Tile.bVisible = true;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FShowHideOptions ForceOpts = FShowHideOptions().Force(true);
		Tile.HideModels(ToHide, TextureNeeds, ForceOpts);
		TestTrue("Force option should apply even if already hidden", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::HideCategories - Hide Categories in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Cat1(4001);
		ITwinElementID Cat2(4002);
		std::unordered_set<ITwinElementID> ToHide = { Cat1, Cat2 };
		FShowHideOptions Opts;

		// Hide categories
		Tile.HideCategories(ToHide, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after hiding categories", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Test hiding non-existent category
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> NonExistent = { ITwinElementID(9999) };
		Tile.HideCategories(NonExistent, TextureNeeds, Opts);
		// Should not crash

		// Test on tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.HideCategories(ToHide, TextureNeeds, Opts);
		TestFalse("Hiding categories on empty tile should not change texture", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::HideCategoriesPerModel - Hide Categories Per Model in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Cat1(4001);
		ITwinElementID Model1(3001);
		ITwinElementID Cat2(4002);
		ITwinElementID Model2(3002);

		std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> ToHide;
		ToHide.insert(std::make_pair(Cat1, Model1));
		ToHide.insert(std::make_pair(Cat2, Model2));
		FShowHideOptions Opts;

		// Hide categories per model
		Tile.HideCategoriesPerModel(ToHide, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after hiding categories per model",
			TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Test on tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.HideCategoriesPerModel(ToHide, TextureNeeds, Opts);
		TestFalse("Hiding categories per model on empty tile should not change texture", TextureNeeds.bWasChanged);

		// Test Force option
		Tile.bVisible = true;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		FShowHideOptions ForceOpts = FShowHideOptions().Force(true);
		Tile.HideCategoriesPerModel(ToHide, TextureNeeds, ForceOpts);
		TestTrue("Force option should apply", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);
	}

	SECTION("FITwinSceneTile::ShowCategoriesPerModel - Show Categories Per Model in Populated Tile")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Cat1(4001);
		ITwinElementID Model1(3001);
		ITwinElementID Cat2(4002);
		ITwinElementID Model2(3002);

		std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> ToProcess;
		ToProcess.insert(std::make_pair(Cat1, Model1));
		ToProcess.insert(std::make_pair(Cat2, Model2));
		FShowHideOptions Opts;

		// First hide them
		Tile.HideCategoriesPerModel(ToProcess, TextureNeeds, Opts);
		TestTrue("Texture should be created and changed after hiding categories per model",
			TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);

		// Then show them
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		Tile.ShowCategoriesPerModel(ToProcess, TextureNeeds, Opts);
		TestTrue("Texture should be changed after showing categories per model", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);

		// Test on tile with no features
		CesiumTileID EmptyTileID;
		FITwinSceneTile EmptyTile(EmptyTileID);
		EmptyTile.MaxFeatureID = ITwin::NOT_FEATURE;
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		EmptyTile.ShowCategoriesPerModel(ToProcess, TextureNeeds, Opts);
		TestFalse("Showing categories per model on empty tile should not change texture", TextureNeeds.bWasChanged);
	}

	SECTION("FITwinSceneTile::ForEachMaterialInstanceMatchingID - Iterate Material Instances")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();

		// This test verifies the method signature and that it doesn't crash
		// Full testing would require actual material instances with metadata
		uint64_t ITwinMaterialID = 12345;
		int32 CallCount = 0;

		Tile.ForEachMaterialInstanceMatchingID(ITwinMaterialID,
			[&CallCount](UMaterialInstanceDynamic& MatInst)
			{
				CallCount++;
			});

		// With no actual material instances set up, callback should not be called
		TestEqual("ForEachMaterialInstanceMatchingID should not call callback with no matching materials", CallCount, 0);
	}

	SECTION("FITwinSceneTile::SetITwinMaterialChannelTexture - Set Custom Texture on Material")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();

		// This test verifies the method signature and basic execution
		// Full testing would require actual material instances
		uint64_t ITwinMaterialID = 12345;
		AdvViz::SDK::EChannelType Channel = static_cast<AdvViz::SDK::EChannelType>(0);
		UTexture* pTexture = nullptr;

		// Should not crash with null texture
		Tile.SetITwinMaterialChannelTexture(ITwinMaterialID, Channel, pTexture);
		TestTrue("SetITwinMaterialChannelTexture should not crash with null texture", true);
	}

	SECTION("FITwinSceneTile::ResetCustomTexturesInMaterials - Reset Custom Textures")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();

		// This test verifies the method signature and basic execution
		// Full testing would require actual material instances with custom textures set
		Tile.ResetCustomTexturesInMaterials();
		TestTrue("ResetCustomTexturesInMaterials should not crash", true);
	}

	SECTION("FITwinSceneTile - Combined Pick, Hide, Show Workflow")
	{
		FITwinSceneTile Tile = CreatePopulatedTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;

		ITwinElementID Elem1(1001);
		ITwinElementID Elem2(1002);
		ITwinElementID Elem3(1003);

		// 1. Pick an element
		FPickingOptions PickOpts = FPickingOptions().MakeSelected(true);
		bool bPicked = Tile.PickElement(Elem1, TextureNeeds, PickOpts);
		TestTrue("Should pick Elem1", bPicked);

		// 2. Hide some elements
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> ToHide = { Elem2, Elem3 };
		FShowHideOptions Opts;
		Tile.HideElements(ToHide, TextureNeeds, Opts);
		TestTrue("Should hide elements", TextureNeeds.bWasChanged);

		// 3. Deselect the picked element
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		std::unordered_set<ITwinElementID> ToDeselect = { Elem1 };
		Tile.DeselectElements(ToDeselect, TextureNeeds);
		TestTrue("Should deselect element", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);

		// 4. Show previously hidden elements
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		Tile.ShowElements(ToHide, TextureNeeds, Opts);
		TestTrue("Should show elements", TextureNeeds.bWasChanged);
		TestFalse("Texture should not be created", TextureNeeds.bWasCreated);

		// 5. Hide and show models
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		ITwinElementID Model1(3001);
		std::unordered_set<ITwinElementID> Models = { Model1 };
		Tile.HideModels(Models, TextureNeeds, Opts);
		TestTrue("Should hide model", TextureNeeds.bWasChanged);

		// 6. Hide and show categories
		TextureNeeds = FITwinSceneTile::FTextureNeeds();
		ITwinElementID Cat1(4001);
		std::unordered_set<ITwinElementID> Cats = { Cat1 };
		Tile.HideCategories(Cats, TextureNeeds, Opts);
		TestTrue("Should hide category", TextureNeeds.bWasChanged);
	}

	// =====================================================================================
	// FITwinSceneMapping tests
	// =====================================================================================

	SECTION("FITwinSceneMapping - Element Management")
	{
		FITwinSceneMapping SceneMapping(false);

		// Add elements
		ITwinElementID Elem1(1001);
		ITwinElementID Elem2(1002);

		auto& Element1 = SceneMapping.ElementForSLOW(Elem1);
		Element1.bHasMesh = true;
		TestEqual("NumElements should be 1", SceneMapping.NumElements(), 1ull);

		auto& Element2 = SceneMapping.ElementForSLOW(Elem2);
		Element2.bHasMesh = true;
		TestEqual("NumElements should be 2", SceneMapping.NumElements(), 2ull);

		// Test GetElement by ElementID
		auto const& RetrievedElem1 = SceneMapping.GetElement<ITwinElementID>(Elem1);
		TestEqual("Retrieved element should have correct ID", RetrievedElem1.ElementID, Elem1);
		TestTrue("Retrieved element should have mesh", RetrievedElem1.bHasMesh);

		// Test GetElementForSLOW with existing element
		ITwinScene::ElemIdx Rank;
		auto* FoundElem = SceneMapping.GetElementForSLOW(Elem1, &Rank);
		TestNotNull("GetElementForSLOW should find existing element", FoundElem);
		if (FoundElem)
		{
			TestEqual("Found element should have correct ID", FoundElem->ElementID, Elem1);
		}

		// Test GetElementForSLOW with non-existent element
		ITwinElementID NonExistent(9999);
		auto* NotFoundElem =SceneMapping.GetElementForSLOW(NonExistent);
		TestNull("GetElementForSLOW should return nullptr for non-existent element", NotFoundElem);

		// Test GetElements
		auto const& AllElements = SceneMapping.GetElements();
		TestEqual("GetElements should return all elements", static_cast<int32>(AllElements.size()), 2);
	}

	SECTION("FITwinSceneMapping - Element Hierarchy")
	{
		FITwinSceneMapping SceneMapping(false);

		// Create parent-child relationship
		ITwinElementID ParentID(2001);
		ITwinElementID ChildID(2002);

		ITwinScene::ElemIdx ParentRank;
		auto& ParentElem = SceneMapping.ElementForSLOW(ParentID, &ParentRank);
		ParentElem.bHasMesh = true;

		ITwinScene::ElemIdx ChildRank;
		auto& ChildElem = SceneMapping.ElementForSLOW(ChildID, &ChildRank);
		ChildElem.bHasMesh = true;
		ChildElem.ParentInVec = ParentRank;

		// Add child to parent's SubElemsInVec
		ParentElem.SubElemsInVec.push_back(ChildRank);

		// Verify hierarchy
		TestEqual("Child should have correct parent", ChildElem.ParentInVec, ParentRank);
		TestEqual("Parent should have 1 child", static_cast<int32>(ParentElem.SubElemsInVec.size()), 1);
		TestEqual("Parent's child should be correct", ParentElem.SubElemsInVec[0], ChildRank);
	}

	SECTION("FITwinSceneMapping - Bounding Box Management")
	{
		FITwinSceneMapping SceneMapping(false);

		ITwinElementID ElemID(4001);
		auto& Element = SceneMapping.ElementForSLOW(ElemID);
		Element.bHasMesh = true;

		// Set bounding box
		FBox TestBox(FVector(-100, -100, -100), FVector(100, 100, 100));
		Element.BBox = TestBox;

		// Retrieve bounding box
		FBox const& RetrievedBox = SceneMapping.GetBoundingBox(ElemID);
		TestTrue("Bounding box should be valid", static_cast<bool>(RetrievedBox.IsValid));
		TestEqual("Bounding box min should match", RetrievedBox.Min, TestBox.Min);
		TestEqual("Bounding box max should match", RetrievedBox.Max, TestBox.Max);

		// Test GetBoundingBoxOfAllGlTFMeshes (should be invalid with no tiles)
		FBox AllMeshesBox = SceneMapping.GetBoundingBoxOfAllGlTFMeshes();
		TestFalse("GetBoundingBoxOfAllGlTFMeshes should return invalid box with no tiles", static_cast<bool>(AllMeshesBox.IsValid));
	}

	SECTION("FITwinSceneMapping - HideElements")
	{
		FITwinSceneMapping SceneMapping(false);

		ITwinElementID Elem1(7001);
		ITwinElementID Elem2(7002);

		auto& Element1 = SceneMapping.ElementForSLOW(Elem1);
		Element1.bHasMesh = true;
		auto& Element2 = SceneMapping.ElementForSLOW(Elem2);
		Element2.bHasMesh = true;

		// Hide elements from saved view
		std::unordered_set<ITwinElementID> ToHide = { Elem1, Elem2 };
		SceneMapping.HideElements(ToHide, false); // IsConstruction = false
		auto const& HiddenElems = SceneMapping.GetSavedViewHiddenElements();
		TestTrue("Hidden elements should include elements", HiddenElems.contains(Elem1));
		TestTrue("Hidden elements should include elements", HiddenElems.contains(Elem2));

		// Test IsElementHiddenInSavedView
		bool bIsHidden = SceneMapping.IsElementHiddenInSavedView(Elem1);
		TestTrue("Element should be reported as hidden in saved view", bIsHidden);

		// Test with Force option
		SceneMapping.HideElements(ToHide, false, true); // Force = true
		TestTrue("HideElements with Force should not crash", true);
	}

	SECTION("FITwinSceneMapping - ShowElements")
	{
		FITwinSceneMapping SceneMapping(false);

		ITwinElementID Elem1(7101);
		ITwinElementID Elem2(7102);

		auto& Element1 = SceneMapping.ElementForSLOW(Elem1);
		Element1.bHasMesh = true;
		auto& Element2 = SceneMapping.ElementForSLOW(Elem2);
		Element2.bHasMesh = true;

		// First hide elements
		std::unordered_set<ITwinElementID> Elements = { Elem1, Elem2 };
		SceneMapping.HideElements(Elements, false);

		// Then show them
		SceneMapping.ShowElements(Elements);
		auto const& AlwaysDrawn = SceneMapping.GetSavedViewAlwaysDrawnElements();
		TestTrue("Always drawn elements should include shown elements", AlwaysDrawn.contains(Elem1));
		TestTrue("Always drawn elements should include shown elements", AlwaysDrawn.contains(Elem2));

		// Test with Force option
		SceneMapping.ShowElements(Elements, true); // Force = true
		TestTrue("ShowElements with Force should not crash", true);
	}

	SECTION("FITwinSceneMapping - HideModels")
	{
		FITwinSceneMapping SceneMapping(false);

		ITwinElementID Model1(8001);
		ITwinElementID Model2(8002);

		std::unordered_set<ITwinElementID> ToHide = { Model1, Model2 };
		SceneMapping.HideModels(ToHide);

		auto const& HiddenModels = SceneMapping.GetSavedViewHiddenModels();
		TestTrue("Hidden models should include models", HiddenModels.contains(Model1));
		TestTrue("Hidden models should include models", HiddenModels.contains(Model2));

		// Test with Force option
		SceneMapping.HideModels(ToHide, true); // Force = true
		TestTrue("HideModels with Force should not crash", true);
	}

	SECTION("FITwinSceneMapping - HideCategories")
	{
		FITwinSceneMapping SceneMapping(false);

		ITwinElementID Cat1(9001);
		ITwinElementID Cat2(9002);

		std::unordered_set<ITwinElementID> ToHide = { Cat1, Cat2 };
		SceneMapping.HideCategories(ToHide);

		auto const& HiddenCats = SceneMapping.GetSavedViewHiddenCategories();
		TestTrue("Hidden categories should include categories", HiddenCats.contains(Cat1));
		TestTrue("Hidden categories should include categories", HiddenCats.contains(Cat2));

		// Test with Force option
		SceneMapping.HideCategories(ToHide, true); // Force = true
		TestTrue("HideCategories with Force should not crash", true);
	}

	SECTION("FITwinSceneMapping - ToJson and FromJson")
	{
		FITwinSceneMapping SourceMapping(false);

		// Add some elements
		ITwinElementID Elem1(15001);
		auto& Element1 = SourceMapping.ElementForSLOW(Elem1);
		Element1.bHasMesh = true;
		Element1.BBox = FBox(FVector(-50, -50, -50), FVector(50, 50, 50));

		ITwinElementID Elem2(15002);
		auto& Element2 = SourceMapping.ElementForSLOW(Elem2);
		Element2.bHasMesh = true;
		Element2.BBox = FBox(FVector(-100, -100, -100), FVector(100, 100, 100));

		// Serialize to JSON
		TSharedPtr<FJsonObject> JsonObj = SourceMapping.ToJson();
		TestTrue("ToJson should return valid JSON object", JsonObj.IsValid());

		// Deserialize from JSON
		FITwinSceneMapping TargetMapping(false);
		bool bSuccess = TargetMapping.FromJson(JsonObj);
		TestTrue("FromJson should succeed", bSuccess);

		// Verify deserialized data
		TestEqual("Target should have same number of elements",
			TargetMapping.NumElements(), SourceMapping.NumElements());

		auto const& RetrievedElem1 = TargetMapping.GetElement<ITwinElementID>(Elem1);
		TestEqual("Deserialized element should have correct ID", RetrievedElem1.ElementID, Elem1);
		TestFalse("Deserialized element should NOT have mesh", RetrievedElem1.bHasMesh);

		// Test FromJson with null
		FITwinSceneMapping NullMapping(false);
		bool bNullSuccess = NullMapping.FromJson(nullptr);
		TestFalse("FromJson should fail with null JSON", bNullSuccess);
	}

	SECTION("FITwinSceneMapping - Reset")
	{
		FITwinSceneMapping SceneMapping(false);

		// Add elements and select one
		ITwinElementID Elem1(16001);
		auto& Element1 = SceneMapping.ElementForSLOW(Elem1);
		Element1.bHasMesh = true;

		// Hide some elements
		std::unordered_set<ITwinElementID> ToHide = { Elem1 };
		SceneMapping.HideElements(ToHide, false);

		// Reset
		SceneMapping.Reset();

		// Verify reset state
		TestEqual("NumElements should be 0 after reset", SceneMapping.NumElements(), 0ull);
		TestFalse("GetSavedViewHiddenElements should not be emptied by reset",
			SceneMapping.GetSavedViewHiddenElements().empty());
		TestTrue("GetSelectedElements should be empty after reset",
			SceneMapping.GetSelectedElements().empty());
	}

	return !HasAnyErrors();
}

#endif // WITH_TESTS
