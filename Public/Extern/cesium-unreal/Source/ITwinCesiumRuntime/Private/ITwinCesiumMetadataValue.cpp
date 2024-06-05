// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumMetadataValue.h"
#include "ITwinCesiumPropertyArray.h"
#include "ITwinUnrealMetadataConversions.h"
#include <CesiumGltf/MetadataConversions.h>
#include <CesiumGltf/PropertyTypeTraits.h>

using namespace CesiumGltf;

ECesiumMetadataBlueprintType
UITwinCesiumMetadataValueBlueprintLibrary::GetBlueprintType(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  return CesiumMetadataValueTypeToBlueprintType(Value._valueType);
}

ECesiumMetadataBlueprintType
UITwinCesiumMetadataValueBlueprintLibrary::GetArrayElementBlueprintType(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  if (!Value._valueType.bIsArray) {
    return ECesiumMetadataBlueprintType::None;
  }

  FITwinCesiumMetadataValueType types(Value._valueType);
  types.bIsArray = false;

  return CesiumMetadataValueTypeToBlueprintType(types);
}

FITwinCesiumMetadataValueType UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  return Value._valueType;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS

ECesiumMetadataTrueType_DEPRECATED
UITwinCesiumMetadataValueBlueprintLibrary::GetTrueType(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  return CesiumMetadataValueTypeToTrueType(Value._valueType);
}

ECesiumMetadataTrueType_DEPRECATED
UITwinCesiumMetadataValueBlueprintLibrary::GetTrueComponentType(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  FITwinCesiumMetadataValueType type = Value._valueType;
  type.bIsArray = false;
  return CesiumMetadataValueTypeToTrueType(type);
}

bool UITwinCesiumMetadataValueBlueprintLibrary::GetBoolean(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    bool DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> bool {
        return CesiumGltf::MetadataConversions<bool, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

uint8 UITwinCesiumMetadataValueBlueprintLibrary::GetByte(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    uint8 DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> uint8 {
        return CesiumGltf::MetadataConversions<uint8, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

int32 UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    int32 DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) {
        return CesiumGltf::MetadataConversions<int32, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

int64 UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    int64 DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> int64 {
        return CesiumGltf::MetadataConversions<int64, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

uint64 UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    uint64 DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> uint64 {
        return CesiumGltf::MetadataConversions<uint64, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

static uint64 GetUnsignedInteger64(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    uint64 DefaultValue);

float UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    float DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> float {
        return CesiumGltf::MetadataConversions<float, decltype(value)>::convert(
                   value)
            .value_or(DefaultValue);
      },
      Value._value);
}

double UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    double DefaultValue) {
  return std::visit(
      [DefaultValue](auto value) -> double {
        return CesiumGltf::MetadataConversions<double, decltype(value)>::
            convert(value)
                .value_or(DefaultValue);
      },
      Value._value);
}

FIntPoint UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FIntPoint& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FIntPoint {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toIntPoint(value, DefaultValue);
        } else {
          auto maybeVec2 = CesiumGltf::
              MetadataConversions<glm::ivec2, decltype(value)>::convert(value);
          return maybeVec2 ? UnrealMetadataConversions::toIntPoint(*maybeVec2)
                           : DefaultValue;
        }
      },
      Value._value);
}

FVector2D UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FVector2D& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FVector2D {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toVector2D(value, DefaultValue);
        } else {
          auto maybeVec2 = CesiumGltf::
              MetadataConversions<glm::dvec2, decltype(value)>::convert(value);
          return maybeVec2 ? UnrealMetadataConversions::toVector2D(*maybeVec2)
                           : DefaultValue;
        }
      },
      Value._value);
}

FIntVector UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FIntVector& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FIntVector {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toIntVector(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::ivec3, decltype(value)>::convert(value);
          return maybeVec3 ? UnrealMetadataConversions::toIntVector(*maybeVec3)
                           : DefaultValue;
        }
      },
      Value._value);
}

FVector3f UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FVector3f& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FVector3f {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toVector3f(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::vec3, decltype(value)>::convert(value);
          return maybeVec3 ? UnrealMetadataConversions::toVector3f(*maybeVec3)
                           : DefaultValue;
        }
      },
      Value._value);
}

FVector UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FVector& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FVector {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toVector(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::dvec3, decltype(value)>::convert(value);
          return maybeVec3 ? UnrealMetadataConversions::toVector(*maybeVec3)
                           : DefaultValue;
        }
      },
      Value._value);
}

FVector4 UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FVector4& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FVector4 {
        if constexpr (CesiumGltf::IsMetadataString<decltype(value)>::value) {
          return UnrealMetadataConversions::toVector4(value, DefaultValue);
        } else {
          auto maybeVec4 = CesiumGltf::
              MetadataConversions<glm::dvec4, decltype(value)>::convert(value);
          return maybeVec4 ? UnrealMetadataConversions::toVector4(*maybeVec4)
                           : DefaultValue;
        }
      },
      Value._value);
}

FMatrix UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FMatrix& DefaultValue) {
  auto maybeMat4 = std::visit(
      [&DefaultValue](auto value) -> std::optional<glm::dmat4> {
        return CesiumGltf::MetadataConversions<glm::dmat4, decltype(value)>::
            convert(value);
      },
      Value._value);

  return maybeMat4 ? UnrealMetadataConversions::toMatrix(*maybeMat4)
                   : DefaultValue;
}

FString UITwinCesiumMetadataValueBlueprintLibrary::GetString(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value,
    const FString& DefaultValue) {
  return std::visit(
      [&DefaultValue](auto value) -> FString {
        using ValueType = decltype(value);
        if constexpr (
            IsMetadataVecN<ValueType>::value ||
            IsMetadataMatN<ValueType>::value ||
            IsMetadataString<ValueType>::value) {
          return UnrealMetadataConversions::toString(value);
        } else {
          auto maybeString = CesiumGltf::
              MetadataConversions<std::string, decltype(value)>::convert(value);

          return maybeString ? UnrealMetadataConversions::toString(*maybeString)
                             : DefaultValue;
        }
      },
      Value._value);
}

FITwinCesiumPropertyArray UITwinCesiumMetadataValueBlueprintLibrary::GetArray(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  return std::visit(
      [](auto value) -> FITwinCesiumPropertyArray {
        if constexpr (CesiumGltf::IsMetadataArray<decltype(value)>::value) {
          return FITwinCesiumPropertyArray(value);
        }
        return FITwinCesiumPropertyArray();
      },
      Value._value);
}

bool UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(
    UPARAM(ref) const FITwinCesiumMetadataValue& Value) {
  return std::holds_alternative<std::monostate>(Value._value);
}

TMap<FString, FString> UITwinCesiumMetadataValueBlueprintLibrary::GetValuesAsStrings(
    const TMap<FString, FITwinCesiumMetadataValue>& Values) {
  TMap<FString, FString> strings;
  for (auto valuesIt : Values) {
    strings.Add(
        valuesIt.Key,
        UITwinCesiumMetadataValueBlueprintLibrary::GetString(
            valuesIt.Value,
            FString()));
  }

  return strings;
}
