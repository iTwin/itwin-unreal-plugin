#pragma once

#include "Library.h"

#include <CesiumGeospatial/BoundingRegion.h>
#include <CesiumGeospatial/GlobeRectangle.h>

#include <glm/fwd.hpp>

#include <string_view>
#include <vector>

namespace CesiumGltf {
struct Buffer;
struct Model;
} // namespace CesiumGltf

namespace CesiumGltfContent {
/**
 * A collection of utility functions that are used to process and transform a
 * gltf model
 */
struct CESIUMGLTFCONTENT_API GltfUtilities {
  /**
   * @brief Applies the glTF's RTC_CENTER, if any, to the given transform.
   *
   * If the glTF has a `CESIUM_RTC` extension, this function will multiply the
   * given matrix with the (translation) matrix that is created from the
   * `RTC_CENTER` in the. If the given model does not have this extension, then
   * this function will return the `rootTransform` unchanged.
   *
   * @param model The glTF model
   * @param rootTransform The matrix that will be multiplied with the transform
   * @return The result of multiplying the `RTC_CENTER` with the
   * `rootTransform`.
   */
  static glm::dmat4x4 applyRtcCenter(
      const CesiumGltf::Model& gltf,
      const glm::dmat4x4& rootTransform);

  /**
   * @brief Applies the glTF's `gltfUpAxis`, if any, to the given transform.
   *
   * By default, the up-axis of a glTF model will the the Y-axis.
   *
   * If the tileset that contained the model had the `asset.gltfUpAxis` string
   * property, then the information about the up-axis has been stored in as a
   * number property called `gltfUpAxis` in the `extras` of the given model.
   *
   * Depending on whether this value is `CesiumGeometry::Axis::X`, `Y`, or `Z`,
   * the given matrix will be multiplied with a matrix that converts the
   * respective axis to be the Z-axis, as required by the 3D Tiles standard.
   *
   * @param model The glTF model
   * @param rootTransform The matrix that will be multiplied with the transform
   * @return The result of multiplying the `rootTransform` with the
   * `gltfUpAxis`.
   */
  static glm::dmat4x4 applyGltfUpAxisTransform(
      const CesiumGltf::Model& model,
      const glm::dmat4x4& rootTransform);

  /**
   * @brief Computes a bounding region from the vertex positions in a glTF
   * model.
   *
   * If the glTF model spans the anti-meridian, the west and east longitude
   * values will be in the usual -PI to PI range, but east will have a smaller
   * value than west.
   *
   * If the glTF contains no geometry, the returned region's rectangle
   * will be {@link GlobeRectangle::EMPTY}, its minimum height will be 1.0, and
   * its maximum height will be -1.0 (the minimum will be greater than the
   * maximum).
   *
   * @param gltf The model.
   * @param transform The transform from model coordinates to ECEF coordinates.
   * @return The computed bounding region.
   */
  static CesiumGeospatial::BoundingRegion computeBoundingRegion(
      const CesiumGltf::Model& gltf,
      const glm::dmat4& transform);

  /**
   * @brief Parse the copyright field of a glTF model and return the individual
   * credits.
   *
   * Credits are read from the glTF's asset.copyright field. This method assumes
   * that individual credits are separated by semicolons.
   *
   * @param gltf The model.
   * @return The credits from the glTF.
   */
  static std::vector<std::string_view>
  parseGltfCopyright(const CesiumGltf::Model& gltf);

  /**
   * @brief Parse a semicolon-separated string, such as the copyright field of a
   * glTF model, and return the individual parts (credits).
   *
   * @param s The string to parse.
   * @return The semicolon-delimited parts.
   */
  static std::vector<std::string_view>
  parseGltfCopyright(const std::string_view& s);

  /**
   * @brief Merges all of the glTF's buffers into a single buffer (the first
   * one).
   *
   * This is useful when writing the glTF as a GLB, which supports only a single
   * embedded buffer.
   *
   * @param gltf The glTF in which to merge buffers.
   */
  static void collapseToSingleBuffer(CesiumGltf::Model& gltf);

  /**
   * @brief Copies the content of one {@link Buffer} to the end of another,
   * updates all {@link BufferView} instances to refer to the destination
   * buffer, and clears the contents of the original buffer.
   *
   * The source buffer is not removed, but it has a `byteLength` of zero after
   * this function completes.
   *
   * @param gltf The glTF model to modify.
   * @param destination The destination Buffer into which to move content.
   * @param source The source Buffer from which to move content.
   */
  static void moveBufferContent(
      CesiumGltf::Model& gltf,
      CesiumGltf::Buffer& destination,
      CesiumGltf::Buffer& source);
};
} // namespace CesiumGltfContent
