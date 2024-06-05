// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumPropertyTable.h"
#include "ITwinCesiumPropertyTexture.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"

#include "ITwinCesiumModelMetadata.generated.h"

namespace CesiumGltf {
struct ExtensionModelExtStructuralMetadata;
struct Model;
} // namespace CesiumGltf

/**
 * @brief A blueprint-accessible wrapper for metadata contained in a glTF model.
 * Provides access to views of property tables, property textures, and property
 * attributes available on the glTF.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumModelMetadata {
  GENERATED_USTRUCT_BODY()

public:
  FITwinCesiumModelMetadata() {}

  FITwinCesiumModelMetadata(
      const CesiumGltf::Model& InModel,
      const CesiumGltf::ExtensionModelExtStructuralMetadata& Metadata);

private:
  TArray<FITwinCesiumPropertyTable> _propertyTables;
  TArray<FITwinCesiumPropertyTexture> _propertyTextures;
  // TODO: property attributes

  friend class UITwinCesiumModelMetadataBlueprintLibrary;
};

UCLASS()
class ITWINCESIUMRUNTIME_API UITwinCesiumModelMetadataBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  /**
   * Gets the model metadata of a glTF primitive component. If component is
   * not a Cesium glTF primitive component, the returned metadata is empty.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const FITwinCesiumModelMetadata&
  GetModelMetadata(const UPrimitiveComponent* component);

  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  /**
   * @brief Get all the feature tables for this model metadata.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Model",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "Use GetPropertyTables to get an array of property tables instead."))
  static const TMap<FString, FITwinCesiumPropertyTable>
  GetFeatureTables(UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata);

  /**
   * @brief Get all the feature textures for this model metadata.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Model",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "Use GetPropertyTextures to get an array of property textures instead."))
  static const TMap<FString, FITwinCesiumPropertyTexture>
  GetFeatureTextures(UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata);

  PRAGMA_ENABLE_DEPRECATION_WARNINGS

  /**
   * Gets an array of all the property tables for this model metadata.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const TArray<FITwinCesiumPropertyTable>&
  GetPropertyTables(UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata);

  /**
   * Gets the property table at the specified index for this model metadata. If
   * the index is out-of-bounds, this returns an invalid property table.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const FITwinCesiumPropertyTable& GetPropertyTable(
      UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
      const int64 Index);

  /**
   * Gets the property table at the specified indices for this model metadata.
   * An invalid property table will be returned for any out-of-bounds index.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const TArray<FITwinCesiumPropertyTable> GetPropertyTablesAtIndices(
      UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
      const TArray<int64>& Indices);

  /**
   * Gets an array of all the property textures for this model metadata.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const TArray<FITwinCesiumPropertyTexture>&
  GetPropertyTextures(UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata);

  /**
   * Gets the property table at the specified index for this model metadata. If
   * the index is out-of-bounds, this returns an invalid property table.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const FITwinCesiumPropertyTexture& GetPropertyTexture(
      UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
      const int64 Index);

  /**
   * Gets an array of the property textures at the specified indices for this
   * model metadata.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Model|Metadata")
  static const TArray<FITwinCesiumPropertyTexture> GetPropertyTexturesAtIndices(
      UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
      const TArray<int64>& Indices);
};
