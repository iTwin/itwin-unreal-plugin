// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#if WITH_EDITOR

#include "ITwinCesiumSceneGeneration.h"

#include "Tests/AutomationEditorCommon.h"

#include "GameFramework/PlayerStart.h"
#include "LevelEditorViewport.h"

#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumGeoreference.h"
#include "ITwinCesiumSunSky.h"
#include "ITwinGlobeAwareDefaultPawn.h"

#include "ITwinCesiumTestHelpers.h"

namespace Cesium {

FString SceneGenerationContext::testIonToken(
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiIyOGUxNjFmMy1mY2ZhLTQwMmEtYTNkYy1kZmExMGJjNjdlNTkiLCJpZCI6MjU5LCJpYXQiOjE2OTYxOTg1MTl9.QN5_xydinXOHF0xqy2zwQ5Hh4I5pVcLeMaqiJ9ZEsD4");

void SceneGenerationContext::setCommonProperties(
    const FVector& origin,
    const FVector& position,
    const FRotator& rotation,
    float fieldOfView) {
  startPosition = position;
  startRotation = rotation;
  startFieldOfView = fieldOfView;

  georeference->SetOriginLongitudeLatitudeHeight(origin);

  pawn->SetActorLocation(startPosition);
  pawn->SetActorRotation(startRotation);

  TInlineComponentArray<UCameraComponent*> cameras;
  pawn->GetComponents<UCameraComponent>(cameras);
  for (UCameraComponent* cameraComponent : cameras)
    cameraComponent->SetFieldOfView(startFieldOfView);
}

void SceneGenerationContext::refreshTilesets() {
  std::vector<AITwinCesium3DTileset*>::iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it)
    (*it)->RefreshTileset();
}

void SceneGenerationContext::setSuspendUpdate(bool suspend) {
  std::vector<AITwinCesium3DTileset*>::iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it)
    (*it)->SuspendUpdate = suspend;
}

void SceneGenerationContext::setMaximumSimultaneousTileLoads(int value) {
  std::vector<AITwinCesium3DTileset*>::iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it)
    (*it)->MaximumSimultaneousTileLoads = value;
}

bool SceneGenerationContext::areTilesetsDoneLoading() {
  if (tilesets.empty())
    return false;

  std::vector<AITwinCesium3DTileset*>::const_iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it) {
    AITwinCesium3DTileset* tileset = *it;

    int progress = (int)tileset->GetLoadProgress();
    if (progress != 100) {
      // We aren't done
      return false;
    }
  }
  return true;
}

void SceneGenerationContext::trackForPlay() {
  ITwinCesiumTestHelpers::trackForPlay(sunSky);
  ITwinCesiumTestHelpers::trackForPlay(georeference);
  ITwinCesiumTestHelpers::trackForPlay(pawn);

  std::vector<AITwinCesium3DTileset*>::iterator it;
  for (it = tilesets.begin(); it != tilesets.end(); ++it) {
    AITwinCesium3DTileset* tileset = *it;
    ITwinCesiumTestHelpers::trackForPlay(tileset);
  }
}

void SceneGenerationContext::initForPlay(
    SceneGenerationContext& creationContext) {
  world = GEditor->PlayWorld;
  sunSky = ITwinCesiumTestHelpers::findInPlay(creationContext.sunSky);
  georeference = ITwinCesiumTestHelpers::findInPlay(creationContext.georeference);
  pawn = ITwinCesiumTestHelpers::findInPlay(creationContext.pawn);

  startPosition = creationContext.startPosition;
  startRotation = creationContext.startRotation;
  startFieldOfView = creationContext.startFieldOfView;

  tilesets.clear();

  std::vector<AITwinCesium3DTileset*>& creationTilesets = creationContext.tilesets;
  std::vector<AITwinCesium3DTileset*>::iterator it;
  for (it = creationTilesets.begin(); it != creationTilesets.end(); ++it) {
    AITwinCesium3DTileset* creationTileset = *it;
    AITwinCesium3DTileset* tileset = ITwinCesiumTestHelpers::findInPlay(creationTileset);
    tilesets.push_back(tileset);
  }
}

void SceneGenerationContext::syncWorldCamera() {
  assert(GEditor);

  if (GEditor->IsPlayingSessionInEditor()) {
    // If in PIE, set the player
    assert(world->GetNumPlayerControllers() == 1);

    APlayerController* controller = world->GetFirstPlayerController();
    assert(controller);

    controller->ClientSetLocation(startPosition, startRotation);

    APlayerCameraManager* cameraManager = controller->PlayerCameraManager;
    assert(cameraManager);

    cameraManager->SetFOV(startFieldOfView);
  } else {
    // If editing, set any viewports
    for (FLevelEditorViewportClient* ViewportClient :
         GEditor->GetLevelViewportClients()) {
      if (ViewportClient == NULL)
        continue;
      ViewportClient->SetViewLocation(startPosition);
      ViewportClient->SetViewRotation(startRotation);
      if (ViewportClient->ViewportType == LVT_Perspective)
        ViewportClient->ViewFOV = startFieldOfView;
      ViewportClient->Invalidate();
    }
  }
}

void createCommonWorldObjects(SceneGenerationContext& context) {

  context.world = FAutomationEditorCommonUtils::CreateNewMap();

  context.sunSky = context.world->SpawnActor<AITwinCesiumSunSky>();

  APlayerStart* playerStart = context.world->SpawnActor<APlayerStart>();

  FSoftObjectPath objectPath(
      TEXT("Class'/ITwinForUnreal/ITwinDynamicPawn.ITwinDynamicPawn_C'"));
  TSoftObjectPtr<UObject> DynamicPawn = TSoftObjectPtr<UObject>(objectPath);

  context.georeference =
      AITwinCesiumGeoreference::GetDefaultGeoreference(context.world);
  context.pawn = context.world->SpawnActor<AITwinGlobeAwareDefaultPawn>(
      Cast<UClass>(DynamicPawn.LoadSynchronous()));

  context.pawn->AutoPossessPlayer = EAutoReceiveInput::Player0;

  AWorldSettings* pWorldSettings = context.world->GetWorldSettings();
  if (pWorldSettings)
    pWorldSettings->bEnableWorldBoundsChecks = false;
}

} // namespace Cesium

#endif // #if WITH_EDITOR
