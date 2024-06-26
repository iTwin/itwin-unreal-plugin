// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumGltf/Class.h"
#include "ITwinCesiumMetadataValue.h"
#include "ITwinCesiumPropertyTableProperty.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"
#include "ITwinCesiumPropertyTable.generated.h"

namespace CesiumGltf {
struct Model;
struct PropertyTable;
} // namespace CesiumGltf

/**
 * @brief Reports the status of a FITwinCesiumPropertyTable. If the property table
 * cannot be accessed, this briefly indicates why.
 */
UENUM(BlueprintType)
enum class EITwinCesiumPropertyTableStatus : uint8 {
  /* The property table is valid. */
  Valid = 0,
  /* The property table instance was not initialized from an actual glTF
     property table. */
  ErrorInvalidPropertyTable,
  /* The property table's class could be found in the schema of the metadata
     extension. */
  ErrorInvalidPropertyTableClass
};

/**
 * A Blueprint-accessible wrapper for a glTF property table. A property table is
 * a collection of properties for the features in a mesh. It knows how to
 * look up the metadata values associated with a given feature ID.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumPropertyTable {
  GENERATED_USTRUCT_BODY()

public:
  /**
   * Construct an empty property table instance.
   */
  FITwinCesiumPropertyTable()
      : _status(EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable){};

  /**
   * Constructs a property table from a glTF Property Table.
   *
   * @param Model The model that stores EXT_structural_metadata.
   * @param PropertyTable The target property table.
   */
  FITwinCesiumPropertyTable(
      const CesiumGltf::Model& Model,
      const CesiumGltf::PropertyTable& PropertyTable);

  /**
   * Gets the name of the metadata class that this property table conforms to.
   */
  FString getClassName() const { return _className; }

private:
  EITwinCesiumPropertyTableStatus _status;
  FString _name;
  FString _className;

  int64 _count;
  TMap<FString, FITwinCesiumPropertyTableProperty> _properties;

  friend class UITwinCesiumPropertyTableBlueprintLibrary;
};

UCLASS()
class ITWINCESIUMRUNTIME_API UITwinCesiumPropertyTableBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  /**
   * Gets the status of the property table. If an error occurred while parsing
   * the property table from the glTF extension, this briefly conveys why.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static EITwinCesiumPropertyTableStatus
  GetPropertyTableStatus(UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable);

  /**
   * Gets the name of the property table. If no name was specified in the glTF
   * extension, this returns an empty string.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static const FString&
  GetPropertyTableName(UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable);

  /**
   * Gets the number of values each property in the table is expected to have.
   * If an error occurred while parsing the property table, this returns zero.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static int64
  GetPropertyTableCount(UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable);

  /**
   * Gets all the properties of the property table, mapped by property name.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static const TMap<FString, FITwinCesiumPropertyTableProperty>&
  GetProperties(UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable);

  /**
   * Gets the names of the properties in this property table.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static const TArray<FString>
  GetPropertyNames(UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable);

  /**
   * Retrieve a FITwinCesiumPropertyTableProperty by name. If the property table
   * does not contain a property with that name, this returns an invalid
   * FITwinCesiumPropertyTableProperty.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static const FITwinCesiumPropertyTableProperty& FindProperty(
      UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
      const FString& PropertyName);

  /**
   * Gets all of the property values for a given feature, mapped by property
   * name. This will only include values from valid property table properties.
   *
   * If the feature ID is out-of-bounds, the returned map will be empty.
   *
   * @param featureID The ID of the feature.
   * @return The property values mapped by property name.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable")
  static TMap<FString, FITwinCesiumMetadataValue> GetMetadataValuesForFeature(
      UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
      int64 FeatureID);

  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  /**
   * Gets all of the property values for a given feature as strings, mapped by
   * property name. This will only include values from valid property table
   * properties.
   *
   * Array properties cannot be converted to strings, so empty strings
   * will be returned for their values.
   *
   * If the feature ID is out-of-bounds, the returned map will be empty.
   *
   * @param FeatureID The ID of the feature.
   * @return The property values as strings mapped by property name.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|PropertyTable",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "Use GetValuesAsStrings to convert the output of GetMetadataValuesForFeature instead."))
  static TMap<FString, FString> GetMetadataValuesForFeatureAsStrings(
      UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
      int64 FeatureID);
  PRAGMA_ENABLE_DEPRECATION_WARNINGS
};
