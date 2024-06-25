// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPropertyTexture.h"
#include "CesiumGltf/Model.h"
#include "CesiumGltf/PropertyTexturePropertyView.h"
#include "CesiumGltf/PropertyTextureView.h"
#include "ITwinCesiumMetadataPickingBlueprintLibrary.h"

using namespace CesiumGltf;

static FITwinCesiumPropertyTextureProperty EmptyPropertyTextureProperty;

FITwinCesiumPropertyTexture::FITwinCesiumPropertyTexture(
    const CesiumGltf::Model& Model,
    const CesiumGltf::PropertyTexture& PropertyTexture)
    : _status(EITwinCesiumPropertyTextureStatus::ErrorInvalidPropertyTextureClass),
      _name(PropertyTexture.name.value_or("").c_str()),
      _className(PropertyTexture.classProperty.c_str()) {
  PropertyTextureView propertyTextureView(Model, PropertyTexture);
  switch (propertyTextureView.status()) {
  case PropertyTextureViewStatus::Valid:
    _status = EITwinCesiumPropertyTextureStatus::Valid;
    break;
  default:
    // Status was already set in initializer list.
    return;
  }

  propertyTextureView.forEachProperty([&properties = _properties](
                                          const std::string& propertyName,
                                          auto propertyValue) mutable {
    FString key(UTF8_TO_TCHAR(propertyName.data()));
    properties.Add(key, FITwinCesiumPropertyTextureProperty(propertyValue));
  });
}

/*static*/ const EITwinCesiumPropertyTextureStatus
UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture) {
  return PropertyTexture._status;
}

/*static*/ const FString&
UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureName(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture) {
  return PropertyTexture._name;
}

/*static*/ const TMap<FString, FITwinCesiumPropertyTextureProperty>
UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture) {
  return PropertyTexture._properties;
}

/*static*/ const TArray<FString>
UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyNames(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture) {
  TArray<FString> names;
  PropertyTexture._properties.GenerateKeyArray(names);
  return names;
}

/*static*/ const FITwinCesiumPropertyTextureProperty&
UITwinCesiumPropertyTextureBlueprintLibrary::FindProperty(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture,
    const FString& PropertyName) {
  const FITwinCesiumPropertyTextureProperty* property =
      PropertyTexture._properties.Find(PropertyName);
  return property ? *property : EmptyPropertyTextureProperty;
}

/*static*/ TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesForUV(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture,
    const FVector2D& UV) {
  TMap<FString, FITwinCesiumMetadataValue> values;

  for (const auto& propertyIt : PropertyTexture._properties) {
    const FITwinCesiumPropertyTextureProperty& property = propertyIt.Value;
    EITwinCesiumPropertyTexturePropertyStatus status =
        UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
            GetPropertyTexturePropertyStatus(property);
    if (status == EITwinCesiumPropertyTexturePropertyStatus::Valid) {
      values.Add(
          propertyIt.Key,
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
              propertyIt.Value,
              UV));
    } else if (
        status ==
        EITwinCesiumPropertyTexturePropertyStatus::EmptyPropertyWithDefault) {
      values.Add(
          propertyIt.Key,
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
              propertyIt.Value));
    }
  }

  return values;
}

/*static*/ TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesFromHit(
    UPARAM(ref) const FITwinCesiumPropertyTexture& PropertyTexture,
    const FHitResult& Hit) {
  TMap<FString, FITwinCesiumMetadataValue> values;

  for (const auto& propertyIt : PropertyTexture._properties) {
    if (UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
            GetPropertyTexturePropertyStatus(propertyIt.Value) ==
        EITwinCesiumPropertyTexturePropertyStatus::EmptyPropertyWithDefault) {
      values.Add(
          propertyIt.Key,
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
              propertyIt.Value));
      continue;
    }

    if (UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
            GetPropertyTexturePropertyStatus(propertyIt.Value) !=
        EITwinCesiumPropertyTexturePropertyStatus::Valid) {
      continue;
    }

    auto glTFTexCoordIndex = propertyIt.Value.getTexCoordSetIndex();
    FVector2D UV;
    if (UITwinCesiumMetadataPickingBlueprintLibrary::FindUVFromHit(
            Hit,
            glTFTexCoordIndex,
            UV)) {
      values.Add(
          propertyIt.Key,
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
              propertyIt.Value,
              UV));
    }
  }
  return values;
}
