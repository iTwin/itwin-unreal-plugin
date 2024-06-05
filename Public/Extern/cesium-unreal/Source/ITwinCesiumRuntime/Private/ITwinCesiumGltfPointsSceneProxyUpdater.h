#pragma once

#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumGltfPointsComponent.h"
#include "ITwinCesiumGltfPointsSceneProxy.h"

/**
 * This is used by Cesium3DTilesets to propagate their settings to any glTF
 * points components it parents.
 */
class FITwinCesiumGltfPointsSceneProxyUpdater {
public:
  /** Updates proxies with new tileset settings. Must be called from a game
   * thread. */
  static void UpdateSettingsInProxies(AITwinCesium3DTileset* Tileset) {
    if (!IsValid(Tileset) || !IsInGameThread()) {
      return;
    }

    TInlineComponentArray<UITwinCesiumGltfPointsComponent*> ComponentArray;
    Tileset->GetComponents<UITwinCesiumGltfPointsComponent>(ComponentArray);

    // Used to pass tileset data updates to render thread
    TArray<FITwinCesiumGltfPointsSceneProxy*> SceneProxies;
    TArray<FITwinCesiumGltfPointsSceneProxyTilesetData> ProxyTilesetData;

    for (UITwinCesiumGltfPointsComponent* PointsComponent : ComponentArray) {
      FITwinCesiumGltfPointsSceneProxy* PointsProxy =
          static_cast<FITwinCesiumGltfPointsSceneProxy*>(
              PointsComponent->SceneProxy);
      if (PointsProxy) {
        SceneProxies.Add(PointsProxy);
      }

      FITwinCesiumGltfPointsSceneProxyTilesetData TilesetData;
      TilesetData.UpdateFromComponent(PointsComponent);
      ProxyTilesetData.Add(TilesetData);
    }

    // Update tileset data
    ENQUEUE_RENDER_COMMAND(TransferCesium3DTilesetSettingsToPointsProxies)
    ([SceneProxies,
      ProxyTilesetData](FRHICommandListImmediate& RHICmdList) mutable {
      // Iterate over proxies and update their data
      for (int32 i = 0; i < SceneProxies.Num(); i++) {
        SceneProxies[i]->UpdateTilesetData(ProxyTilesetData[i]);
      }
    });
  }
};
