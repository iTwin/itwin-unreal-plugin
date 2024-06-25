#pragma once
#include "ITwinCesiumTile.h"
#include "ITwinCesiumTileExcluder.h"
#include <Cesium3DTilesSelection/ITileExcluder.h>

class AITwinCesiumGeoreference;

namespace Cesium3DTilesSelection {
class Tile;
}

class FITwinCesiumTileExcluderAdapter : public Cesium3DTilesSelection::ITileExcluder {
  virtual bool shouldExclude(
      const Cesium3DTilesSelection::Tile& tile) const noexcept override;
  virtual void startNewFrame() noexcept override;

private:
  TWeakObjectPtr<UITwinCesiumTileExcluder> Excluder;
  UITwinCesiumTile* Tile;
  AITwinCesiumGeoreference* Georeference;
  bool IsExcluderValid;

public:
  FITwinCesiumTileExcluderAdapter(
      TWeakObjectPtr<UITwinCesiumTileExcluder> pExcluder,
      AITwinCesiumGeoreference* pGeoreference,
      UITwinCesiumTile* pTile);
};
