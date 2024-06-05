// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPropertyTable.h"
#include "CesiumGltf/PropertyTableView.h"

using namespace CesiumGltf;

static FITwinCesiumPropertyTableProperty EmptyPropertyTableProperty;

FITwinCesiumPropertyTable::FITwinCesiumPropertyTable(
    const Model& Model,
    const PropertyTable& PropertyTable)
    : _status(ECesiumPropertyTableStatus::ErrorInvalidPropertyTableClass),
      _name(PropertyTable.name.value_or("").c_str()),
      _className(PropertyTable.classProperty.c_str()),
      _count(PropertyTable.count),
      _properties() {
  PropertyTableView propertyTableView{Model, PropertyTable};
  switch (propertyTableView.status()) {
  case PropertyTableViewStatus::Valid:
    _status = ECesiumPropertyTableStatus::Valid;
    break;
  default:
    // Status was already set in initializer list.
    return;
  }

  propertyTableView.forEachProperty([&properties = _properties](
                                        const std::string& propertyName,
                                        auto propertyValue) mutable {
    FString key(UTF8_TO_TCHAR(propertyName.data()));
    properties.Add(key, FITwinCesiumPropertyTableProperty(propertyValue));
  });
}

/*static*/ ECesiumPropertyTableStatus
UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable) {
  return PropertyTable._status;
}

/*static*/ const FString&
UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableName(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable) {
  return PropertyTable._name;
}

/*static*/ int64 UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable) {
  if (PropertyTable._status != ECesiumPropertyTableStatus::Valid) {
    return 0;
  }

  return PropertyTable._count;
}

/*static*/ const TMap<FString, FITwinCesiumPropertyTableProperty>&
UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable) {
  return PropertyTable._properties;
}

/*static*/ const TArray<FString>
UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyNames(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable) {
  TArray<FString> names;
  PropertyTable._properties.GenerateKeyArray(names);
  return names;
}

/*static*/ const FITwinCesiumPropertyTableProperty&
UITwinCesiumPropertyTableBlueprintLibrary::FindProperty(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
    const FString& PropertyName) {
  const FITwinCesiumPropertyTableProperty* property =
      PropertyTable._properties.Find(PropertyName);
  return property ? *property : EmptyPropertyTableProperty;
}

/*static*/ TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
    int64 FeatureID) {
  TMap<FString, FITwinCesiumMetadataValue> values;
  if (FeatureID < 0 || FeatureID >= PropertyTable._count) {
    return values;
  }

  for (const auto& pair : PropertyTable._properties) {
    const FITwinCesiumPropertyTableProperty& property = pair.Value;
    ECesiumPropertyTablePropertyStatus status =
        UITwinCesiumPropertyTablePropertyBlueprintLibrary::
            GetPropertyTablePropertyStatus(property);
    if (status == ECesiumPropertyTablePropertyStatus::Valid) {
      values.Add(
          pair.Key,
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
              pair.Value,
              FeatureID));
    } else if (
        status ==
        ECesiumPropertyTablePropertyStatus::EmptyPropertyWithDefault) {
      values.Add(
          pair.Key,
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
              pair.Value));
    }
  }

  return values;
}

/*static*/ TMap<FString, FString>
UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeatureAsStrings(
    UPARAM(ref) const FITwinCesiumPropertyTable& PropertyTable,
    int64 FeatureID) {
  TMap<FString, FString> values;
  if (FeatureID < 0 || FeatureID >= PropertyTable._count) {
    return values;
  }

  for (const auto& pair : PropertyTable._properties) {
    const FITwinCesiumPropertyTableProperty& property = pair.Value;
    ECesiumPropertyTablePropertyStatus status =
        UITwinCesiumPropertyTablePropertyBlueprintLibrary::
            GetPropertyTablePropertyStatus(property);
    if (status == ECesiumPropertyTablePropertyStatus::Valid) {
      values.Add(
          pair.Key,
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetString(
              pair.Value,
              FeatureID));
    }
  }

  return values;
}
