#pragma once

#include "Library.h"
#include "TileOcclusionRendererProxy.h"
#include "spdlog-cesium.h"
#include <rapidjson/fwd.h>

#include <CesiumAsync/AsyncSystem.h>
#include <CesiumAsync/HttpHeaders.h>

#include <glm/fwd.hpp>

#include <memory>

namespace CesiumAsync {
class IAssetAccessor;
class ITaskProcessor;
} // namespace CesiumAsync

namespace CesiumUtility {
class CreditSystem;
}

namespace Cesium3DTilesSelection {
class IPrepareRendererResources;

//! Abstract class that allows tuning a glTF model.
//! "Tuning" means reorganizing the primitives, eg. merging or splitting them.
//! Merging primitives can lead to improved rendering performance.
//! Splitting primitives allows to assign different materials to parts that were initially in the same primitive.
//! Tuning is done in 2 phases: first phase in worker thread, then second phase in main thread.
//! Tuning can occur several times during the lifetime of the model, depending on current needs.
//! For example, if the user wants to assign a specific material on some part of the model,
//! we can trigger a new tuning process.
//! Hence the use of a "tune version" which allows to know if the mesh is up-to-date, or must be re-processed.
class GltfTuner
{
public:
	//! The current version of the tuner, which should be incremented by client code whenever
	//! models needs to be re-tuned.
	int currentVersion = 0;
	virtual ~GltfTuner() = default;
  virtual CesiumGltf::Model Tune(const CesiumGltf::Model& model,
    const glm::dmat4& tileTransform, const glm::dvec4& rootTranslation) = 0;
	virtual void ParseTilesetJson(const rapidjson::Document& tilesetJson) = 0;
  //! The tuning may require some additional external data such as textures, typically in case of material
  //! customizations. In such case, we may need to use custom headers (holding an iTwin access token for
  //! example...)
  virtual CesiumAsync::HttpHeaders GetHeadersForExternalData() const { return {}; }
};

/**
 * @brief External interfaces used by a {@link Tileset}.
 *
 * Not supposed to be used by clients.
 */
class CESIUM3DTILESSELECTION_API TilesetExternals final {
public:
  /**
   * @brief An external {@link CesiumAsync::IAssetAccessor}.
   */
  std::shared_ptr<CesiumAsync::IAssetAccessor> pAssetAccessor;

  /**
   * @brief An external {@link IPrepareRendererResources}.
   */
  std::shared_ptr<IPrepareRendererResources> pPrepareRendererResources;

  /**
   * @brief The async system to use to do work in threads.
   *
   * The tileset will automatically call
   * {@link CesiumAsync::AsyncSystem::dispatchMainThreadTasks} from
   * {@link Tileset::updateView}.
   */
  CesiumAsync::AsyncSystem asyncSystem;

  /**
   * @brief An external {@link CreditSystem} that can be used to manage credit
   * strings and track which which credits to show and remove from the screen
   * each frame.
   */
  std::shared_ptr<CesiumUtility::CreditSystem> pCreditSystem;

  /**
   * @brief A spdlog logger that will receive log messages.
   *
   * If not specified, defaults to `spdlog::default_logger()`.
   */
  std::shared_ptr<spdlog::logger> pLogger = spdlog::default_logger();

  /**
   * @brief A pool of renderer proxies to determine the occlusion state of
   * tile bounding volumes.
   *
   * If not specified, the traversal will not attempt to leverage occlusion
   * information.
   */
  std::shared_ptr<TileOcclusionRendererProxyPool> pTileOcclusionProxyPool =
      nullptr;

  std::shared_ptr<GltfTuner> gltfTuner = nullptr;
};

} // namespace Cesium3DTilesSelection
