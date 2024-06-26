// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumMetadataValueType.h"
#include <cstdlib>
#include <type_traits>

#include "ITwinCesiumMetadataEncodingDetails.generated.h"

/**
 * @brief The component type that a metadata property's values will be encoded
 * as. These correspond to the pixel component types that are supported in
 * Unreal textures.
 */
UENUM()
enum class EITwinCesiumEncodedMetadataComponentType : uint8 { None, Uint8, Float };

/**
 * @brief The type that a metadata property's values will be encoded as.
 */
UENUM()
enum class EITwinCesiumEncodedMetadataType : uint8 {
  None,
  Scalar,
  Vec2,
  Vec3,
  Vec4
};

/**
 * @brief Indicates how a property value from EXT_structural_metadata should be
 * converted to a GPU-accessible type, if possible.
 */
UENUM()
enum class EITwinCesiumEncodedMetadataConversion : uint8 {
  /**
   * Do nothing. This is typically used for property types that are
   * completely unable to be coerced.
   */
  None,
  /**
   * Coerce the components of a property value to the specified component type.
   * If the property contains string values, this attempts to parse numbers from
   * the strings as uint8s.
   */
  Coerce,
  /**
   * Attempt to parse a color from a string property value. This supports
   * the following formats:
   * - rgb(R, G, B), where R, G, and B are values in the range [0, 255]
   * - hexcode colors, e.g. #ff0000
   */
  ParseColorFromString
};

/**
 * Describes how a property from EXT_structural_metadata will be encoded for
 * access in Unreal materials.
 */
USTRUCT()
struct FITwinCesiumMetadataEncodingDetails {
  GENERATED_USTRUCT_BODY()

  FITwinCesiumMetadataEncodingDetails()
      : Type(EITwinCesiumEncodedMetadataType::None),
        ComponentType(EITwinCesiumEncodedMetadataComponentType::None),
        Conversion(EITwinCesiumEncodedMetadataConversion::None) {}

  FITwinCesiumMetadataEncodingDetails(
      EITwinCesiumEncodedMetadataType InType,
      EITwinCesiumEncodedMetadataComponentType InComponentType,
      EITwinCesiumEncodedMetadataConversion InConversion)
      : Type(InType),
        ComponentType(InComponentType),
        Conversion(InConversion) {}

  /**
   * The GPU-compatible type that this property's values will be encoded as.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium")
  EITwinCesiumEncodedMetadataType Type;

  /**
   * The GPU-compatible component type that this property's values will be
   * encoded as. These correspond to the pixel component types that are
   * supported in Unreal textures.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium")
  EITwinCesiumEncodedMetadataComponentType ComponentType;

  /**
   * The method of conversion used for this property. This describes how the
   * values will be converted for access in Unreal materials. Note that not all
   * property types are compatible with the methods of conversion.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium")
  EITwinCesiumEncodedMetadataConversion Conversion;

  inline bool operator==(const FITwinCesiumMetadataEncodingDetails& Info) const {
    return Type == Info.Type && ComponentType == Info.ComponentType &&
           Conversion == Info.Conversion;
  }

  inline bool operator!=(const FITwinCesiumMetadataEncodingDetails& Info) const {
    return Type != Info.Type || ComponentType != Info.ComponentType ||
           Conversion != Info.Conversion;
  }

  bool HasValidType() const {
    return Type != EITwinCesiumEncodedMetadataType::None &&
           ComponentType != EITwinCesiumEncodedMetadataComponentType::None;
  }
};
