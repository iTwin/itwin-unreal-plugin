#include "ITwinCesiumTileExcluderAdapter.h"
#include "Cesium3DTilesSelection/Tile.h"
#include "ITwinCesiumGeoreference.h"
#include "ITwinVecMath.h"

bool FITwinCesiumTileExcluderAdapter::shouldExclude(
    const Cesium3DTilesSelection::Tile& tile) const noexcept {
  if (!this->IsExcluderValid) {
    return false;
  }
  Tile->_tileBounds = tile.getBoundingVolume();
  Tile->UpdateBounds();
  return Excluder->ShouldExclude(Tile);
}

void FITwinCesiumTileExcluderAdapter::startNewFrame() noexcept {
  if (!Excluder.IsValid() || !IsValid(Tile) || !IsValid(Georeference)) {
    IsExcluderValid = false;
    return;
  }

  IsExcluderValid = true;
  Tile->_tileTransform =
      Georeference->GetGeoTransforms()
          .GetAbsoluteUnrealWorldToEllipsoidCenteredTransform();
}

FITwinCesiumTileExcluderAdapter::FITwinCesiumTileExcluderAdapter(
    TWeakObjectPtr<UITwinCesiumTileExcluder> pExcluder,
    AITwinCesiumGeoreference* pGeoreference,
    UITwinCesiumTile* pTile)
    : Excluder(pExcluder),
      Tile(pTile),
      Georeference(pGeoreference),
      IsExcluderValid(true){};
