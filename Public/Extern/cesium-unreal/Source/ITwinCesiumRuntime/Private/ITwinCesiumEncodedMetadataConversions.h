// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include <gsl/span>

enum class EITwinCesiumMetadataType : uint8;
enum class EITwinCesiumEncodedMetadataType : uint8;
struct FITwinCesiumPropertyTablePropertyDescription;
struct FITwinCesiumPropertyTableProperty;
struct FITwinCesiumMetadataPropertyDetails;
struct FITwinCesiumMetadataEncodingDetails;

/**
 * @brief Gets the best-fitting encoded type for the given metadata type.
 */
EITwinCesiumEncodedMetadataType
CesiumMetadataTypeToEncodingType(EITwinCesiumMetadataType Type);

/**
 * @brief Gets the best-fitting encoded types and conversion method for a given
 * metadata type. This determines the best way (if one is possible) to transfer
 * values of the given type to the GPU, for access in Unreal materials.
 *
 * An array size can also be supplied if bIsArray is true on the given value
 * type. If bIsArray is true, but the given array size is zero, this indicates
 * the arrays of the property vary in length. Variable-length array properties
 * are unsupported.
 *
 * @param PropertyDetails The metadata property details
 */
FITwinCesiumMetadataEncodingDetails CesiumMetadataPropertyDetailsToEncodingDetails(
    FITwinCesiumMetadataPropertyDetails PropertyDetails);

/**
 * @brief Gets the number of components associated with the given encoded type.
 * @param type The encoded metadata type.
 */
size_t
CesiumGetEncodedMetadataTypeComponentCount(EITwinCesiumEncodedMetadataType Type);

/**
 * Any custom encoding behavior, e.g., special encoding of unsupported
 * properties, can go here. Use the below methods as examples.
 */

/**
 * @brief Coerces property values to the type specified by the property
 * description. The following property types are supported:
 * - scalars
 * - vecNs
 * - booleans
 * - scalar and boolean arrays (up to the first four elements)
 *
 * Additionally, if the property contains strings or string arrays, it will
 * attempt to parse numbers from each string, then coerce those numbers to the
 * desired format.
 */
struct CesiumEncodedMetadataCoerce {
  /**
   * Whether it is possible to apply the encoding method based on the property
   * description.
   *
   * @param description The property table property description.
   */
  static bool
  canEncode(const FITwinCesiumPropertyTablePropertyDescription& description);

  /**
   * Encodes the data of the property table property into the given texture data
   * pointer, as the type specified in the property description.
   *
   * @param propertyDescription The property table property description.
   * @param property The property table property itself.
   * @param pTextureData A pointer to the texture data, which will be filled
   * during encoding.
   * @param pixelSize The size of a pixel from the given texture, in bytes.
   */
  static void encode(
      const FITwinCesiumPropertyTablePropertyDescription& propertyDescription,
      const FITwinCesiumPropertyTableProperty& property,
      gsl::span<std::byte>& pTextureData,
      size_t pixelSize);
};

/**
 * @brief Attempts to parse colors from string property values and encode them
 * for access in Unreal materials. This supports the following formats:
 * - rgb(R,G,B), where R, G, and B are values in the range [0, 255]
 * - hexcode colors, e.g. #AF012B and #fff
 */
struct CesiumEncodedMetadataParseColorFromString {
  /**
   * Whether it is possible to apply the encoding method based on the property
   * description.
   *
   * @param description The property table property description.
   */
  static bool
  canEncode(const FITwinCesiumPropertyTablePropertyDescription& description);

  /**
   * Encodes the data of the property table property into the given texture data
   * pointer, as the type specified in the property description.
   *
   * @param propertyDescription The property table property description.
   * @param property The property table property itself.
   * @param pTextureData A pointer to the texture data, which will be filled
   * during encoding.
   * @param pixelSize The size of a pixel from the given texture, in bytes.
   */
  static void encode(
      const FITwinCesiumPropertyTablePropertyDescription& propertyDescription,
      const FITwinCesiumPropertyTableProperty& property,
      gsl::span<std::byte>& textureData,
      size_t pixelSize);
};
