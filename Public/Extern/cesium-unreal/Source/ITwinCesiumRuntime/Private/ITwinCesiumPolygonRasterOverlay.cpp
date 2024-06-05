// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPolygonRasterOverlay.h"
#include "Cesium3DTilesSelection/RasterizedPolygonsTileExcluder.h"
#include "Cesium3DTilesSelection/Tileset.h"
#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumBingMapsRasterOverlay.h"
#include "ITwinCesiumCartographicPolygon.h"
#include "CesiumRasterOverlays/RasterizedPolygonsOverlay.h"

using namespace Cesium3DTilesSelection;
using namespace CesiumGeospatial;
using namespace CesiumRasterOverlays;

UITwinCesiumPolygonRasterOverlay::UITwinCesiumPolygonRasterOverlay()
    : UITwinCesiumRasterOverlay() {
  this->MaterialLayerKey = TEXT("Clipping");
}

std::unique_ptr<CesiumRasterOverlays::RasterOverlay>
UITwinCesiumPolygonRasterOverlay::CreateOverlay(
    const CesiumRasterOverlays::RasterOverlayOptions& options) {
  AITwinCesium3DTileset* pTileset = this->GetOwner<AITwinCesium3DTileset>();

  FTransform worldToTileset =
      pTileset ? pTileset->GetActorTransform().Inverse() : FTransform::Identity;

  std::vector<CartographicPolygon> polygons;
  polygons.reserve(this->Polygons.Num());

  for (AITwinCesiumCartographicPolygon* pPolygon : this->Polygons) {
    if (!pPolygon) {
      continue;
    }

    CartographicPolygon polygon =
        pPolygon->CreateCartographicPolygon(worldToTileset);
    polygons.emplace_back(std::move(polygon));
  }

  return std::make_unique<CesiumRasterOverlays::RasterizedPolygonsOverlay>(
      TCHAR_TO_UTF8(*this->MaterialLayerKey),
      polygons,
      this->InvertSelection,
      CesiumGeospatial::Ellipsoid::WGS84,
      CesiumGeospatial::GeographicProjection(),
      options);
}

void UITwinCesiumPolygonRasterOverlay::OnAdd(
    Tileset* pTileset,
    RasterOverlay* pOverlay) {
  // If this overlay is used for culling, add it as an excluder too for
  // efficiency.
  if (pTileset && this->ExcludeSelectedTiles) {
    RasterizedPolygonsOverlay* pPolygons =
        static_cast<RasterizedPolygonsOverlay*>(pOverlay);
    assert(this->_pExcluder == nullptr);
    this->_pExcluder =
        std::make_shared<RasterizedPolygonsTileExcluder>(pPolygons);
    pTileset->getOptions().excluders.push_back(this->_pExcluder);
  }
}

void UITwinCesiumPolygonRasterOverlay::OnRemove(
    Tileset* pTileset,
    RasterOverlay* pOverlay) {
  auto& excluders = pTileset->getOptions().excluders;
  if (this->_pExcluder) {
    auto it = std::find(excluders.begin(), excluders.end(), this->_pExcluder);
    if (it != excluders.end()) {
      excluders.erase(it);
    }

    this->_pExcluder.reset();
  }
}
