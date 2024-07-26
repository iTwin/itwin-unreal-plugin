// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "ITwinUnrealMetadataConversions.h"
#include <CesiumGltf/MetadataConversions.h>

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyArrayBlueprintLibrary::GetElementBlueprintType(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return CesiumMetadataValueTypeToBlueprintType(array._elementType);
}

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyArrayBlueprintLibrary::GetBlueprintComponentType(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return CesiumMetadataValueTypeToBlueprintType(array._elementType);
}

FITwinCesiumMetadataValueType
UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return array._elementType;
}

int64 UITwinCesiumPropertyArrayBlueprintLibrary::GetArraySize(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return std::visit([](const auto& view) { return view.size(); }, array._value);
}

int64 UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return std::visit([](const auto& view) { return view.size(); }, array._value);
}

FITwinCesiumMetadataValue UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index) {
  return std::visit(
      [index](const auto& v) -> FITwinCesiumMetadataValue {
        if (index < 0 || index >= v.size()) {
          FFrame::KismetExecutionMessage(
              *FString::Printf(
                  TEXT(
                      "Attempted to access index %d from CesiumPropertyArray of length %d!"),
                  index,
                  v.size()),
              ELogVerbosity::Warning,
              FName("CesiumPropertyArrayOutOfBoundsWarning"));
          return FITwinCesiumMetadataValue();
        }
        return FITwinCesiumMetadataValue(v[index]);
      },
      array._value);
}

EITwinCesiumMetadataTrueType_DEPRECATED
UITwinCesiumPropertyArrayBlueprintLibrary::GetTrueComponentType(
    UPARAM(ref) const FITwinCesiumPropertyArray& array) {
  return CesiumMetadataValueTypeToTrueType(array._elementType);
}

bool UITwinCesiumPropertyArrayBlueprintLibrary::GetBoolean(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    bool defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> bool {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        return CesiumGltf::MetadataConversions<bool, decltype(value)>::convert(
                   value)
            .value_or(defaultValue);
      },
      array._value);
}

uint8 UITwinCesiumPropertyArrayBlueprintLibrary::GetByte(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    uint8 defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> uint8 {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        return CesiumGltf::MetadataConversions<uint8, decltype(value)>::convert(
                   value)
            .value_or(defaultValue);
      },
      array._value);
}

int32 UITwinCesiumPropertyArrayBlueprintLibrary::GetInteger(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    int32 defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> int32 {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        return CesiumGltf::MetadataConversions<int32, decltype(value)>::convert(
                   value)
            .value_or(defaultValue);
      },
      array._value);
}

int64 UITwinCesiumPropertyArrayBlueprintLibrary::GetInteger64(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    int64 defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> int64 {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        return CesiumGltf::MetadataConversions<int64_t, decltype(value)>::
            convert(value)
                .value_or(defaultValue);
      },
      array._value);
}

float UITwinCesiumPropertyArrayBlueprintLibrary::GetFloat(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    float defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> float {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        return CesiumGltf::MetadataConversions<float, decltype(value)>::convert(
                   value)
            .value_or(defaultValue);
      },
      array._value);
}

double UITwinCesiumPropertyArrayBlueprintLibrary::GetFloat64(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    double defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> double {
        auto value = v[index];
        return CesiumGltf::MetadataConversions<double, decltype(value)>::
            convert(value)
                .value_or(defaultValue);
      },
      array._value);
}

FString UITwinCesiumPropertyArrayBlueprintLibrary::GetString(
    UPARAM(ref) const FITwinCesiumPropertyArray& array,
    int64 index,
    const FString& defaultValue) {
  return std::visit(
      [index, defaultValue](const auto& v) -> FString {
        if (index < 0 || index >= v.size()) {
          return defaultValue;
        }
        auto value = v[index];
        auto maybeString = CesiumGltf::
            MetadataConversions<std::string, decltype(value)>::convert(value);
        if (!maybeString) {
          return defaultValue;
        }
        return FITwinUnrealMetadataConversions::toString(*maybeString);
      },
      array._value);
}
