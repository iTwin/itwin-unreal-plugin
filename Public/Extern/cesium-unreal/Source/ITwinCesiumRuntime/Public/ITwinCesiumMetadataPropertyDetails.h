// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumMetadataValueType.h"
#include "UObject/ObjectMacros.h"

#include "ITwinCesiumMetadataPropertyDetails.generated.h"

/**
 * Represents information about a metadata property according to how the
 * property is defined in EXT_structural_metadata.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumMetadataPropertyDetails {
  GENERATED_USTRUCT_BODY()

  FITwinCesiumMetadataPropertyDetails()
      : Type(EITwinCesiumMetadataType::Invalid),
        ComponentType(EITwinCesiumMetadataComponentType::None),
        bIsArray(false) {}

  FITwinCesiumMetadataPropertyDetails(
      EITwinCesiumMetadataType InType,
      EITwinCesiumMetadataComponentType InComponentType,
      bool IsArray)
      : Type(InType), ComponentType(InComponentType), bIsArray(IsArray) {}

  /**
   * The type of the metadata property.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium")
  EITwinCesiumMetadataType Type = EITwinCesiumMetadataType::Invalid;

  /**
   * The component of the metadata property. Only applies when the type is a
   * Scalar, VecN, or MatN type.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Boolean && Type != EITwinCesiumMetadataType::Enum && Type != EITwinCesiumMetadataType::String"))
  EITwinCesiumMetadataComponentType ComponentType =
      EITwinCesiumMetadataComponentType::None;

  /**
   * Whether or not this represents an array containing elements of the
   * specified types.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium")
  bool bIsArray = false;

  /**
   * The size of the arrays in the metadata property. If the property contains
   * arrays of varying length, this will be zero even though bIsArray will be
   * true> If this property does not contain arrays, this is set to zero.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta = (EditCondition = "bIsArray"))
  int64 ArraySize = 0;

  /**
   * Whether or not the values in this property are normalized. Only applicable
   * to scalar, vecN, and matN types with integer components.
   *
   * For unsigned integer component types, values are normalized between
   * [0.0, 1.0]. For signed integer component types, values are normalized
   * between [-1.0, 1.0]
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Boolean && Type != EITwinCesiumMetadataType::Enum && Type != EITwinCesiumMetadataType::String && ComponentType != EITwinCesiumMetadataComponentType::None && ComponentType != EITwinCesiumMetadataComponentType::Float32 && ComponentType != EITwinCesiumMetadataComponentType::Float64"))
  bool bIsNormalized = false;

  /**
   * Whether or not the property is transformed by an offset. This value is
   * defined either in the class property, or in the instance of the property
   * itself.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Boolean && Type != EITwinCesiumMetadataType::Enum && Type != EITwinCesiumMetadataType::String"))
  bool bHasOffset = false;

  /**
   * Whether or not the property is transformed by a scale. This value is
   * defined either in the class property, or in the instance of the property
   * itself.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Boolean && Type != EITwinCesiumMetadataType::Enum && Type != EITwinCesiumMetadataType::String"))
  bool bHasScale = false;

  /**
   * Whether or not the property specifies a "no data" value. This value
   * functions a sentinel value, indicating missing data wherever it appears.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Boolean && Type != EITwinCesiumMetadataType::Enum"))
  bool bHasNoDataValue = false;

  /**
   * Whether or not the property specifies a default value. This default value
   * is used use when encountering a "no data" value in the property, or when a
   * non-required property has been omitted.
   */
  UPROPERTY(
      EditAnywhere,
      Category = "Cesium",
      Meta =
          (EditCondition =
               "Type != EITwinCesiumMetadataType::Invalid && Type != EITwinCesiumMetadataType::Enum"))
  bool bHasDefaultValue = false;

  inline bool
  operator==(const FITwinCesiumMetadataPropertyDetails& ValueType) const {
    return Type == ValueType.Type && ComponentType == ValueType.ComponentType &&
           bIsArray == ValueType.bIsArray;
  }

  inline bool
  operator!=(const FITwinCesiumMetadataPropertyDetails& ValueType) const {
    return !operator==(ValueType);
  }

  /**
   * Returns the internal types as a FITwinCesiumMetadataValueType.
   */
  FITwinCesiumMetadataValueType GetValueType() const {
    return FITwinCesiumMetadataValueType(Type, ComponentType, bIsArray);
  }

  /**
   * Sets the internal types to the values supplied by the input
   * FITwinCesiumMetadataValueType.
   */
  void SetValueType(FITwinCesiumMetadataValueType ValueType) {
    Type = ValueType.Type;
    ComponentType = ValueType.ComponentType;
    bIsArray = ValueType.bIsArray;
  }

  /**
   * Whether this property has one or more value transforms. This includes
   * normalization, offset, and scale, as well as the "no data" and default
   * values.
   */
  bool HasValueTransforms() const {
    return bIsNormalized || bHasOffset || bHasScale || bHasNoDataValue ||
           bHasDefaultValue;
  }
};
