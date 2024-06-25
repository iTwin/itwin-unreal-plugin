// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#if WITH_EDITOR

#include "ITwinCesiumLoadTestCore.h"

#include "Misc/AutomationTest.h"

#include "CesiumAsync/ICacheDatabase.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumIonRasterOverlay.h"
#include "ITwinCesiumRuntime.h"
#include "ITwinCesiumSunSky.h"
#include "ITwinGlobeAwareDefaultPawn.h"

using namespace Cesium;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FITwinCesiumSampleDenver,
    "Cesium.Performance.SampleLocaleDenver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FITwinCesiumSampleMelbourne,
    "Cesium.Performance.SampleLocaleMelbourne",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FITwinCesiumSampleMontrealPointCloud,
    "Cesium.Performance.SampleTestPointCloud",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSampleMaxTileLoads,
    "Cesium.Performance.SampleVaryMaxTileLoads",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)

void refreshSampleTilesets(
    SceneGenerationContext& context,
    TestPass::TestingParameter parameter) {
  context.refreshTilesets();
}

void setupForDenver(SceneGenerationContext& context) {
  context.setCommonProperties(
      FVector(-104.988892, 39.743462, 1798.679443),
      FVector(0, 0, 0),
      FRotator(-5.2, -149.4, 0),
      90.0f);

  // Add Cesium World Terrain
  AITwinCesium3DTileset* worldTerrainTileset =
      context.world->SpawnActor<AITwinCesium3DTileset>();
  worldTerrainTileset->SetTilesetSource(EITwinTilesetSource::FromCesiumIon);
  worldTerrainTileset->SetIonAssetID(1);
  worldTerrainTileset->SetIonAccessToken(SceneGenerationContext::testIonToken);
  worldTerrainTileset->SetActorLabel(TEXT("Cesium World Terrain"));

  // Bing Maps Aerial overlay
  UITwinCesiumIonRasterOverlay* pOverlay = NewObject<UITwinCesiumIonRasterOverlay>(
      worldTerrainTileset,
      FName("Bing Maps Aerial"),
      RF_Transactional);
  pOverlay->MaterialLayerKey = TEXT("Overlay0");
  pOverlay->IonAssetID = 2;
  pOverlay->SetActive(true);
  pOverlay->OnComponentCreated();
  worldTerrainTileset->AddInstanceComponent(pOverlay);

  // Aerometrex Denver
  AITwinCesium3DTileset* aerometrexTileset =
      context.world->SpawnActor<AITwinCesium3DTileset>();
  aerometrexTileset->SetTilesetSource(EITwinTilesetSource::FromCesiumIon);
  aerometrexTileset->SetIonAssetID(354307);
  aerometrexTileset->SetIonAccessToken(SceneGenerationContext::testIonToken);
  aerometrexTileset->SetMaximumScreenSpaceError(2.0);
  aerometrexTileset->SetActorLabel(TEXT("Aerometrex Denver"));

  context.tilesets.push_back(worldTerrainTileset);
  context.tilesets.push_back(aerometrexTileset);
}

void setupForMelbourne(SceneGenerationContext& context) {
  context.setCommonProperties(
      FVector(144.951538, -37.809871, 140.334974),
      FVector(1052, 506, 23651),
      FRotator(-32, 20, 0),
      90.0f);

  context.sunSky->SolarTime = 16.8;
  context.sunSky->UpdateSun();

  // Add Cesium World Terrain
  AITwinCesium3DTileset* worldTerrainTileset =
      context.world->SpawnActor<AITwinCesium3DTileset>();
  worldTerrainTileset->SetTilesetSource(EITwinTilesetSource::FromCesiumIon);
  worldTerrainTileset->SetIonAssetID(1);
  worldTerrainTileset->SetIonAccessToken(SceneGenerationContext::testIonToken);
  worldTerrainTileset->SetActorLabel(TEXT("Cesium World Terrain"));

  // Bing Maps Aerial overlay
  UITwinCesiumIonRasterOverlay* pOverlay = NewObject<UITwinCesiumIonRasterOverlay>(
      worldTerrainTileset,
      FName("Bing Maps Aerial"),
      RF_Transactional);
  pOverlay->MaterialLayerKey = TEXT("Overlay0");
  pOverlay->IonAssetID = 2;
  pOverlay->SetActive(true);
  pOverlay->OnComponentCreated();
  worldTerrainTileset->AddInstanceComponent(pOverlay);

  AITwinCesium3DTileset* melbourneTileset =
      context.world->SpawnActor<AITwinCesium3DTileset>();
  melbourneTileset->SetTilesetSource(EITwinTilesetSource::FromCesiumIon);
  melbourneTileset->SetIonAssetID(69380);
  melbourneTileset->SetIonAccessToken(SceneGenerationContext::testIonToken);
  melbourneTileset->SetMaximumScreenSpaceError(6.0);
  melbourneTileset->SetActorLabel(TEXT("Melbourne Photogrammetry"));
  melbourneTileset->SetActorLocation(FVector(0, 0, 900));

  context.tilesets.push_back(worldTerrainTileset);
  context.tilesets.push_back(melbourneTileset);
}

void setupForMontrealPointCloud(SceneGenerationContext& context) {
  context.setCommonProperties(
      FVector(-73.616526, 45.57335, 95.048859),
      FVector(0, 0, 0),
      FRotator(-90.0, 0.0, 0.0),
      90.0f);

  AITwinCesium3DTileset* montrealTileset =
      context.world->SpawnActor<AITwinCesium3DTileset>();
  montrealTileset->SetTilesetSource(EITwinTilesetSource::FromCesiumIon);
  montrealTileset->SetIonAssetID(28945);
  montrealTileset->SetIonAccessToken(SceneGenerationContext::testIonToken);
  montrealTileset->SetMaximumScreenSpaceError(16.0);
  montrealTileset->SetActorLabel(TEXT("Montreal Point Cloud"));

  context.tilesets.push_back(montrealTileset);
}

bool FITwinCesiumSampleDenver::RunTest(const FString& Parameters) {
  std::vector<TestPass> testPasses;
  testPasses.push_back(TestPass{"Cold Cache", nullptr, nullptr});
  testPasses.push_back(TestPass{"Warm Cache", refreshSampleTilesets, nullptr});

  return RunLoadTest(
      GetBeautifiedTestName(),
      setupForDenver,
      testPasses,
      1024,
      768);
}

bool FITwinCesiumSampleMelbourne::RunTest(const FString& Parameters) {
  std::vector<TestPass> testPasses;
  testPasses.push_back(TestPass{"Cold Cache", nullptr, nullptr});
  testPasses.push_back(TestPass{"Warm Cache", refreshSampleTilesets, nullptr});

  return RunLoadTest(
      GetBeautifiedTestName(),
      setupForMelbourne,
      testPasses,
      1024,
      768);
}

bool FITwinCesiumSampleMontrealPointCloud::RunTest(const FString& Parameters) {
  auto adjustCamera = [this](
                          SceneGenerationContext& context,
                          TestPass::TestingParameter parameter) {
    // Zoom way out
    context.startPosition = FVector(0, 0, 7240000.0);
    context.startRotation = FRotator(-90.0, 0.0, 0.0);
    context.syncWorldCamera();

    context.pawn->SetActorLocation(context.startPosition);
  };

  auto verifyVisibleTiles = [this](
                                SceneGenerationContext& context,
                                TestPass::TestingParameter parameter) {
    Cesium3DTilesSelection::Tileset* pTileset =
        context.tilesets[0]->GetTileset();
    if (TestNotNull("Tileset", pTileset)) {
      int visibleTiles = 0;
      pTileset->forEachLoadedTile([&](Cesium3DTilesSelection::Tile& tile) {
        if (tile.getState() != Cesium3DTilesSelection::TileLoadState::Done)
          return;
        const Cesium3DTilesSelection::TileContent& content = tile.getContent();
        const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
            content.getRenderContent();
        if (!pRenderContent) {
          return;
        }

        UITwinCesiumGltfComponent* Gltf = static_cast<UITwinCesiumGltfComponent*>(
            pRenderContent->getRenderResources());
        if (Gltf && Gltf->IsVisible()) {
          ++visibleTiles;
        }
      });

      TestEqual("visibleTiles", visibleTiles, 1);
    }
  };

  std::vector<TestPass> testPasses;
  testPasses.push_back(TestPass{"Cold Cache", nullptr, nullptr});
  testPasses.push_back(TestPass{"Adjust", adjustCamera, verifyVisibleTiles});

  return RunLoadTest(
      GetBeautifiedTestName(),
      setupForMontrealPointCloud,
      testPasses,
      512,
      512);
}

bool FSampleMaxTileLoads::RunTest(const FString& Parameters) {

  auto setupPass = [this](
                       SceneGenerationContext& context,
                       TestPass::TestingParameter parameter) {
    std::shared_ptr<CesiumAsync::ICacheDatabase> pCacheDatabase =
        ITwinCesium::getCacheDatabase();
    pCacheDatabase->clearAll();

    int maxLoadsTarget = std::get<int>(parameter);
    context.setMaximumSimultaneousTileLoads(maxLoadsTarget);

    context.refreshTilesets();
  };

  auto reportStep = [this](const std::vector<TestPass>& testPasses) {
    FString reportStr;
    reportStr += "\n\nTest Results\n";
    reportStr += "------------------------------------------------------\n";
    reportStr += "(measured time) - (MaximumSimultaneousTileLoads value)\n";
    reportStr += "------------------------------------------------------\n";
    std::vector<TestPass>::const_iterator it;
    for (it = testPasses.begin(); it != testPasses.end(); ++it) {
      const TestPass& pass = *it;
      reportStr +=
          FString::Printf(TEXT("%.2f secs - %s"), pass.elapsedTime, *pass.name);
      if (pass.isFastest)
        reportStr += " <-- fastest";
      reportStr += "\n";
    }
    reportStr += "------------------------------------------------------\n";
    UE_LOG(LogITwinCesium, Display, TEXT("%s"), *reportStr);
  };

  std::vector<TestPass> testPasses;
  testPasses.push_back(TestPass{"Default", NULL, NULL});
  testPasses.push_back(TestPass{"12", setupPass, NULL, 12});
  testPasses.push_back(TestPass{"16", setupPass, NULL, 16});
  testPasses.push_back(TestPass{"20", setupPass, NULL, 20});
  testPasses.push_back(TestPass{"24", setupPass, NULL, 24});
  testPasses.push_back(TestPass{"28", setupPass, NULL, 28});

  return RunLoadTest(
      GetBeautifiedTestName(),
      setupForMelbourne,
      testPasses,
      1024,
      768,
      reportStep);
}
#endif
