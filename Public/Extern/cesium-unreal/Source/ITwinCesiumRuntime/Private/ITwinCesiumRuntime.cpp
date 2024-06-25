// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumRuntime.h"
#include "Cesium3DTilesContent/registerAllTileContentTypes.h"
#include "CesiumAsync/CachingAssetAccessor.h"
#include "CesiumAsync/GunzipAssetAccessor.h"
#include "CesiumAsync/SqliteCache.h"
#include "ITwinCesiumRuntimeSettings.h"
#include "CesiumUtility/Tracing.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "ITwinSpdlogUnrealLoggerSink.h"
#include "ITwinUnrealAssetAccessor.h"
#include "ITwinUnrealTaskProcessor.h"
#include <CesiumAsync/AsyncSystem.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <Modules/ModuleManager.h>
#include <spdlog/spdlog.h>

#if CESIUM_TRACING_ENABLED
#include <chrono>
#endif

#define LOCTEXT_NAMESPACE "FITwinCesiumRuntimeModule"

DEFINE_LOG_CATEGORY(LogITwinCesium);

void FITwinCesiumRuntimeModule::StartupModule() {
  Cesium3DTilesContent::registerAllTileContentTypes();

  std::shared_ptr<spdlog::logger> pLogger = spdlog::default_logger();
  pLogger->sinks() = {std::make_shared<ITwinSpdlogUnrealLoggerSink>()};

  FModuleManager::Get().LoadModuleChecked(TEXT("HTTP"));

  CESIUM_TRACE_INIT(
      "cesium-trace-" +
      std::to_string(std::chrono::time_point_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now())
                         .time_since_epoch()
                         .count()) +
      ".json");

  FString PluginShaderDir = FPaths::Combine(
      IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"))->GetBaseDir(),
      TEXT("Shaders"));
  AddShaderSourceDirectoryMapping(
      TEXT("/Plugin/ITwinForUnreal"),
      PluginShaderDir);
}

void FITwinCesiumRuntimeModule::ShutdownModule() { CESIUM_TRACE_SHUTDOWN(); }

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FITwinCesiumRuntimeModule, ITwinCesiumRuntime)

FITwinCesium3DTilesetIonTroubleshooting OnCesium3DTilesetIonTroubleshooting{};
FITwinCesiumRasterOverlayIonTroubleshooting
    OnCesiumRasterOverlayIonTroubleshooting{};

CesiumAsync::AsyncSystem& ITwinCesium::getAsyncSystem() noexcept {
  static CesiumAsync::AsyncSystem asyncSystem(
      std::make_shared<ITwinUnrealTaskProcessor>());
  return asyncSystem;
}

namespace ITwinCesium {

std::string getCacheDatabaseName() {
#if PLATFORM_ANDROID
  FString BaseDirectory = FPaths::ProjectPersistentDownloadDir();
#elif PLATFORM_IOS
  FString BaseDirectory =
      FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Cesium"));
  if (!IFileManager::Get().DirectoryExists(*BaseDirectory)) {
    IFileManager::Get().MakeDirectory(*BaseDirectory, true);
  }
#else
  FString BaseDirectory = FPaths::EngineUserDir();
#endif

  FString CesiumDBFile =
      FPaths::Combine(*BaseDirectory, TEXT("cesium-request-cache.sqlite"));
  FString PlatformAbsolutePath =
      IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(
          *CesiumDBFile);

  UE_LOG(
      LogITwinCesium,
      Display,
      TEXT("Caching Cesium requests in %s"),
      *PlatformAbsolutePath);

  return TCHAR_TO_UTF8(*PlatformAbsolutePath);
}

std::shared_ptr<CesiumAsync::ICacheDatabase>& getCacheDatabase() {
  static int MaxCacheItems =
      GetDefault<UITwinCesiumRuntimeSettings>()->MaxCacheItems;

  static std::shared_ptr<CesiumAsync::ICacheDatabase> pCacheDatabase =
      std::make_shared<CesiumAsync::SqliteCache>(
          spdlog::default_logger(),
          ITwinCesium::getCacheDatabaseName(),
          MaxCacheItems);

  return pCacheDatabase;
}

const std::shared_ptr<CesiumAsync::IAssetAccessor>& getAssetAccessor() {
  static int RequestsPerCachePrune =
      GetDefault<UITwinCesiumRuntimeSettings>()->RequestsPerCachePrune;
  static std::shared_ptr<CesiumAsync::IAssetAccessor> pAssetAccessor =
      std::make_shared<CesiumAsync::GunzipAssetAccessor>(
          std::make_shared<CesiumAsync::CachingAssetAccessor>(
              spdlog::default_logger(),
              std::make_shared<ITwinUnrealAssetAccessor>(),
              ITwinCesium::getCacheDatabase(),
              RequestsPerCachePrune));
  return pAssetAccessor;
}

} //ITwinCesium
