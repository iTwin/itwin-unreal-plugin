// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#if WITH_EDITOR

#include <vector>

class UWorld;
class AITwinCesiumSunSky;
class AITwinCesiumGeoreference;
class AITwinGlobeAwareDefaultPawn;
class AITwinCesium3DTileset;
class AITwinCesiumCameraManager;

namespace Cesium {

struct SceneGenerationContext {
  UWorld* world;
  AITwinCesiumSunSky* sunSky;
  AITwinCesiumGeoreference* georeference;
  AITwinGlobeAwareDefaultPawn* pawn;
  std::vector<AITwinCesium3DTileset*> tilesets;

  FVector startPosition;
  FRotator startRotation;
  float startFieldOfView;

  void setCommonProperties(
      const FVector& origin,
      const FVector& position,
      const FRotator& rotation,
      float fieldOfView);

  void refreshTilesets();
  void setSuspendUpdate(bool suspend);
  void setMaximumSimultaneousTileLoads(int32 value);
  bool areTilesetsDoneLoading();

  void trackForPlay();
  void initForPlay(SceneGenerationContext& creationContext);
  void syncWorldCamera();

  static FString testIonToken;
};

void createCommonWorldObjects(SceneGenerationContext& context);

}; // namespace Cesium

#endif // #if WITH_EDITOR
