// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumWebMapServiceRasterOverlay.h"
#include "Algo/Transform.h"
#include "CesiumRasterOverlays/WebMapServiceRasterOverlay.h"
#include "ITwinCesiumRuntime.h"

std::unique_ptr<CesiumRasterOverlays::RasterOverlay>
UITwinCesiumWebMapServiceRasterOverlay::CreateOverlay(
    const CesiumRasterOverlays::RasterOverlayOptions& options) {

  CesiumRasterOverlays::WebMapServiceRasterOverlayOptions wmsOptions;
  if (MaximumLevel > MinimumLevel) {
    wmsOptions.minimumLevel = MinimumLevel;
    wmsOptions.maximumLevel = MaximumLevel;
  }
  wmsOptions.layers = TCHAR_TO_UTF8(*Layers);
  wmsOptions.tileWidth = TileWidth;
  wmsOptions.tileHeight = TileHeight;
  return std::make_unique<CesiumRasterOverlays::WebMapServiceRasterOverlay>(
      TCHAR_TO_UTF8(*this->MaterialLayerKey),
      TCHAR_TO_UTF8(*this->BaseUrl),
      std::vector<CesiumAsync::IAssetAccessor::THeader>(),
      wmsOptions,
      options);
}
