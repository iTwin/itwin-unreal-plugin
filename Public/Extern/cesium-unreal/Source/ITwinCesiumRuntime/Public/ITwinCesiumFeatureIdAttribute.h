// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include <CesiumGltf/AccessorUtility.h>
#include "ITwinCesiumFeatureIdAttribute.generated.h"

namespace CesiumGltf {
struct Model;
struct Accessor;
} // namespace CesiumGltf

/**
 * @brief Reports the status of a FITwinCesiumFeatureIdAttribute. If the feature ID
 * attribute cannot be accessed, this briefly indicates why.
 */
UENUM(BlueprintType)
enum class EITwinCesiumFeatureIdAttributeStatus : uint8 {
  /* The feature ID attribute is valid. */
  Valid = 0,
  /* The feature ID attribute does not exist in the glTF primitive. */
  ErrorInvalidAttribute,
  /* The feature ID attribute uses an invalid accessor in the glTF. */
  ErrorInvalidAccessor
};

/**
 * @brief A blueprint-accessible wrapper for a feature ID attribute from a glTF
 * primitive. Provides access to per-vertex feature IDs which can be used with
 * the corresponding {@link FITwinCesiumFeatureTable} to access per-vertex metadata.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumFeatureIdAttribute {
  GENERATED_USTRUCT_BODY()

public:
  /**
   * @brief Constructs an empty feature ID attribute instance. Empty feature ID
   * attributes can be constructed while trying to convert a FITwinCesiumFeatureIdSet
   * that is not an attribute. In this case, the status reports it is an invalid
   * attribute.
   */
  FITwinCesiumFeatureIdAttribute()
      : _status(EITwinCesiumFeatureIdAttributeStatus::ErrorInvalidAttribute),
        _featureIdAccessor(),
        _attributeIndex(-1) {}

  /**
   * @brief Constructs a feature ID attribute instance.
   *
   * @param Model The model.
   * @param Primitive The mesh primitive containing the feature ID attribute.
   * @param FeatureIDAttribute The attribute index specified by the FeatureId.
   * @param PropertyTableName The name of the property table this attribute
   * corresponds to, if one exists, for backwards compatibility.
   */
  FITwinCesiumFeatureIdAttribute(
      const CesiumGltf::Model& Model,
      const CesiumGltf::MeshPrimitive& Primitive,
      const int64 FeatureIDAttribute,
      const FString& PropertyTableName);

  /**
   * Gets the index of this feature ID attribute in the glTF primitive.
   */
  int64 getAttributeIndex() const { return this->_attributeIndex; }

private:
  EITwinCesiumFeatureIdAttributeStatus _status;
  CesiumGltf::FeatureIdAccessorType _featureIdAccessor;
  int64 _attributeIndex;

  // For backwards compatibility.
  FString _propertyTableName;

  friend class UITwinCesiumFeatureIdAttributeBlueprintLibrary;
};

UCLASS()
class ITWINCESIUMRUNTIME_API UITwinCesiumFeatureIdAttributeBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  /**
   * Get the name of the feature table corresponding to this feature ID
   * attribute. The name can be used to fetch the appropriate
   * FITwinCesiumFeatureTable from the FITwinCesiumMetadataModel.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|FeatureIdAttribute",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "Use GetPropertyTableIndex on a CesiumFeatureIdSet instead."))
  static const FString&
  GetFeatureTableName(UPARAM(ref)
                          const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute);
  PRAGMA_ENABLE_DEPRECATION_WARNINGS

  /**
   * Gets the status of the feature ID attribute. If this attribute is
   * invalid in any way, this will briefly indicate why.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Features|FeatureIDAttribute")
  static EITwinCesiumFeatureIdAttributeStatus GetFeatureIDAttributeStatus(
      UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute);

  /**
   * Get the number of vertices in the primitive containing the feature
   * ID attribute. If the feature ID attribute is invalid, this returns 0.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Features|FeatureIDAttribute")
  static int64
  GetVertexCount(UPARAM(ref)
                     const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute);

  /**
   * Gets the feature ID associated with the given vertex. The feature ID can be
   * used with a FITwinCesiumFeatureTable to retrieve the per-vertex metadata. If
   * the feature ID attribute is invalid, this returns -1.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Features|FeatureIDAttribute")
  static int64 GetFeatureIDForVertex(
      UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute,
      int64 VertexIndex);
};
