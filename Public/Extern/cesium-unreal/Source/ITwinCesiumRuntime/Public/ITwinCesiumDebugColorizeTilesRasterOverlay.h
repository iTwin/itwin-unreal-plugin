// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumRasterOverlay.h"
#include "CoreMinimal.h"
#include "ITwinCesiumDebugColorizeTilesRasterOverlay.generated.h"

/**
 * A raster overlay that can be used to debug tilesets by shading each tile with
 * a random color.
 */
UCLASS(
    DisplayName = "Cesium Debug Colorize Tiles Raster Overlay",
    ClassGroup = (Cesium),
    meta = (BlueprintSpawnableComponent))
class ITWINCESIUMRUNTIME_API UITwinCesiumDebugColorizeTilesRasterOverlay
    : public UITwinCesiumRasterOverlay {
  GENERATED_BODY()

public:
protected:
  virtual std::unique_ptr<CesiumRasterOverlays::RasterOverlay> CreateOverlay(
      const CesiumRasterOverlays::RasterOverlayOptions& options = {}) override;
};
