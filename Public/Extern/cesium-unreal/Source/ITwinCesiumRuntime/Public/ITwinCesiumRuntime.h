// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include <memory>

class AITwinCesium3DTileset;
class UITwinCesiumRasterOverlay;

namespace CesiumAsync {
class AsyncSystem;
class IAssetAccessor;
class ICacheDatabase;
} // namespace CesiumAsync

DECLARE_LOG_CATEGORY_EXTERN(LogITwinCesium, Log, All);

class FITwinCesiumRuntimeModule : public IModuleInterface {
public:
  /** IModuleInterface implementation */
  virtual void StartupModule() override;
  virtual void ShutdownModule() override;
};

/**
 * The delegate for the OnCesium3DTilesetIonTroubleshooting, which is triggered
 * when the tileset encounters a load error.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
    FITwinCesium3DTilesetIonTroubleshooting,
    AITwinCesium3DTileset*);

ITWINCESIUMRUNTIME_API extern FITwinCesium3DTilesetIonTroubleshooting
    OnCesium3DTilesetIonTroubleshooting;

/**
 * The delegate for the OnCesiumRasterOverlayIonTroubleshooting, which is
 * triggered when the tileset encounters a load error.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
    FITwinCesiumRasterOverlayIonTroubleshooting,
    UITwinCesiumRasterOverlay*);

ITWINCESIUMRUNTIME_API extern FITwinCesiumRasterOverlayIonTroubleshooting
    OnCesiumRasterOverlayIonTroubleshooting;

namespace ITwinCesium
{
    ITWINCESIUMRUNTIME_API CesiumAsync::AsyncSystem& getAsyncSystem() noexcept;
    ITWINCESIUMRUNTIME_API const std::shared_ptr<CesiumAsync::IAssetAccessor>& getAssetAccessor();

    ITWINCESIUMRUNTIME_API std::shared_ptr<CesiumAsync::ICacheDatabase>& getCacheDatabase();
}
