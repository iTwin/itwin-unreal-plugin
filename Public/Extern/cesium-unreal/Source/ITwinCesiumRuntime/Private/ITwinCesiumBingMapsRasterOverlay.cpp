// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumBingMapsRasterOverlay.h"
#include "Cesium3DTilesSelection/Tileset.h"
#include "CesiumRasterOverlays/BingMapsRasterOverlay.h"

std::unique_ptr<CesiumRasterOverlays::RasterOverlay>
UITwinCesiumBingMapsRasterOverlay::CreateOverlay(
    const CesiumRasterOverlays::RasterOverlayOptions& options) {
  std::string mapStyle;

  switch (this->MapStyle) {
  case EITwinBingMapsStyle::Aerial:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::AERIAL;
    break;
  case EITwinBingMapsStyle::AerialWithLabelsOnDemand:
    mapStyle =
        CesiumRasterOverlays::BingMapsStyle::AERIAL_WITH_LABELS_ON_DEMAND;
    break;
  case EITwinBingMapsStyle::RoadOnDemand:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::ROAD_ON_DEMAND;
    break;
  case EITwinBingMapsStyle::CanvasDark:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::CANVAS_DARK;
    break;
  case EITwinBingMapsStyle::CanvasLight:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::CANVAS_LIGHT;
    break;
  case EITwinBingMapsStyle::CanvasGray:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::CANVAS_GRAY;
    break;
  case EITwinBingMapsStyle::OrdnanceSurvey:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::ORDNANCE_SURVEY;
    break;
  case EITwinBingMapsStyle::CollinsBart:
    mapStyle = CesiumRasterOverlays::BingMapsStyle::COLLINS_BART;
    break;
  }

  return std::make_unique<CesiumRasterOverlays::BingMapsRasterOverlay>(
      TCHAR_TO_UTF8(*this->MaterialLayerKey),
      "https://dev.virtualearth.net",
      TCHAR_TO_UTF8(*this->BingMapsKey),
      mapStyle,
      "",
      CesiumGeospatial::Ellipsoid::WGS84,
      options);
}
