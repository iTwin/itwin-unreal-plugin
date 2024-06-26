// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPropertyTextureProperty.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinUnrealMetadataConversions.h"

#include <CesiumGltf/MetadataConversions.h>
#include <cstdint>
#include <limits>

using namespace CesiumGltf;

namespace {
/**
 * Callback on a std::any, assuming that it contains a
 * PropertyTexturePropertyView of the specified type. If the type does not
 * match, the callback is performed on an invalid PropertyTexturePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param callback The callback function.
 *
 * @tparam TProperty The property type.
 * @tparam Normalized Whether the PropertyTexturePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <
    typename TProperty,
    bool Normalized,
    typename TResult,
    typename Callback>
TResult
propertyTexturePropertyCallback(const std::any& property, Callback&& callback) {
  const PropertyTexturePropertyView<TProperty, Normalized>* pProperty =
      std::any_cast<PropertyTexturePropertyView<TProperty, Normalized>>(
          &property);
  if (pProperty) {
    return callback(*pProperty);
  }

  return callback(PropertyTexturePropertyView<uint8_t>());
}

/**
 * Callback on a std::any, assuming that it contains a
 * PropertyTexturePropertyView on a scalar type. If the valueType does not have
 * a valid component type, the callback is performed on an invalid
 * PropertyTexturePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTexturePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult scalarPropertyTexturePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTexturePropertyCallback<
        int8_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTexturePropertyCallback<
        uint8_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTexturePropertyCallback<
        int16_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTexturePropertyCallback<
        uint16_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTexturePropertyCallback<
        int32_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTexturePropertyCallback<
        uint32_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTexturePropertyCallback<float, false, TResult, Callback>(
        property,
        std::forward<Callback>(callback));
  default:
    return callback(PropertyTexturePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a
 * PropertyTexturePropertyView on a scalar array type. If the valueType does not
 * have a valid component type, the callback is performed on an invalid
 * PropertyTexturePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTexturePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult scalarArrayPropertyTexturePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTexturePropertyCallback<
        PropertyArrayView<int8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTexturePropertyCallback<
        PropertyArrayView<uint8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTexturePropertyCallback<
        PropertyArrayView<int16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTexturePropertyCallback<
        PropertyArrayView<uint16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTexturePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a
 * PropertyTexturePropertyView on a glm::vecN type. If the valueType does not
 * have a valid component type, the callback is performed on an invalid
 * PropertyTexturePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam N The dimensions of the glm::vecN
 * @tparam Normalized Whether the PropertyTexturePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <glm::length_t N, bool Normalized, typename TResult, typename Callback>
TResult vecNPropertyTexturePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTexturePropertyCallback<
        glm::vec<N, int8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTexturePropertyCallback<
        glm::vec<N, uint8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTexturePropertyCallback<
        glm::vec<N, int16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTexturePropertyCallback<
        glm::vec<N, uint16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTexturePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a
 * PropertyTexturePropertyView on a glm::vecN type. If the valueType does not
 * have a valid component type, the callback is performed on an invalid
 * PropertyTexturePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTexturePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult vecNPropertyTexturePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  if (valueType.Type == EITwinCesiumMetadataType::Vec2) {
    return vecNPropertyTexturePropertyCallback<
        2,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec3) {
    return vecNPropertyTexturePropertyCallback<
        3,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec4) {
    return vecNPropertyTexturePropertyCallback<
        4,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  return callback(PropertyTexturePropertyView<uint8_t>());
}

template <typename TResult, typename Callback>
TResult propertyTexturePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    bool normalized,
    Callback&& callback) {

  if (valueType.bIsArray && valueType.Type != EITwinCesiumMetadataType::Scalar) {

    // Only scalar property arrays are supported.
    return callback(PropertyTexturePropertyView<uint8_t>());
  }

  if (valueType.bIsArray) {
    return normalized ? scalarArrayPropertyTexturePropertyCallback<
                            true,
                            TResult,
                            Callback>(
                            property,
                            valueType,
                            std::forward<Callback>(callback))
                      : scalarArrayPropertyTexturePropertyCallback<
                            false,
                            TResult,
                            Callback>(
                            property,
                            valueType,
                            std::forward<Callback>(callback));
  }

  switch (valueType.Type) {
  case EITwinCesiumMetadataType::Scalar:
    return normalized
               ? scalarPropertyTexturePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : scalarPropertyTexturePropertyCallback<
                     false,
                     TResult,
                     Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Vec2:
  case EITwinCesiumMetadataType::Vec3:
  case EITwinCesiumMetadataType::Vec4:
    return normalized
               ? vecNPropertyTexturePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : vecNPropertyTexturePropertyCallback<false, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  default:
    return callback(PropertyTexturePropertyView<uint8_t>());
  }
}

} // namespace

const int64 FITwinCesiumPropertyTextureProperty::getTexCoordSetIndex() const {
  return propertyTexturePropertyCallback<int64>(
      this->_property,
      this->_valueType,
      this->_normalized,
      [](const auto& view) -> int64 { return view.getTexCoordSetIndex(); });
}

const CesiumGltf::Sampler* FITwinCesiumPropertyTextureProperty::getSampler() const {
  return propertyTexturePropertyCallback<const CesiumGltf::Sampler*>(
      this->_property,
      this->_valueType,
      this->_normalized,
      [](const auto& view) -> const CesiumGltf::Sampler* {
        return view.getSampler();
      });
}

const CesiumGltf::ImageCesium*
FITwinCesiumPropertyTextureProperty::getImage() const {
  return propertyTexturePropertyCallback<const CesiumGltf::ImageCesium*>(
      this->_property,
      this->_valueType,
      this->_normalized,
      [](const auto& view) -> const CesiumGltf::ImageCesium* {
        return view.getImage();
      });
}

EITwinCesiumPropertyTexturePropertyStatus
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
    GetPropertyTexturePropertyStatus(
        UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return Property._status;
}

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetBlueprintType(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return CesiumMetadataValueTypeToBlueprintType(Property._valueType);
}

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArrayElementBlueprintType(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  if (!Property._valueType.bIsArray) {
    return EITwinCesiumMetadataBlueprintType::None;
  }

  FITwinCesiumMetadataValueType valueType(Property._valueType);
  valueType.bIsArray = false;

  return CesiumMetadataValueTypeToBlueprintType(valueType);
}

FITwinCesiumMetadataValueType
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return Property._valueType;
}

int64 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<int64>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> int64 { return view.arrayCount(); });
}

int64 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
    GetGltfTextureCoordinateSetIndex(
        UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return Property.getTexCoordSetIndex();
}

int64 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetUnrealUVChannel(
    const UPrimitiveComponent* Component,
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  const UITwinCesiumGltfPrimitiveComponent* pPrimitive =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Component);
  if (!pPrimitive) {
    return -1;
  }

  int64_t texCoordSetIndex = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
      GetGltfTextureCoordinateSetIndex(Property);

  auto textureCoordinateIndexIt =
      pPrimitive->GltfToUnrealTexCoordMap.find(texCoordSetIndex);
  if (textureCoordinateIndexIt == pPrimitive->GltfToUnrealTexCoordMap.end()) {
    return -1;
  }

  return textureCoordinateIndexIt->second;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FString UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetSwizzle(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  const std::string swizzle =
      propertyTexturePropertyCallback<const std::string>(
          Property._property,
          Property._valueType,
          Property._normalized,
          [](const auto& view) { return view.getSwizzle(); });
  return UTF8_TO_TCHAR(swizzle.c_str());
}

int64 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetComponentCount(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<int64>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) {
        return static_cast<int64>(view.getChannels().size());
      });
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

TArray<int64> UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetChannels(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<TArray<int64>>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> TArray<int64> {
        const std::vector<int64_t>& channels = view.getChannels();
        TArray<int64> result;
        result.Reserve(static_cast<int32>(channels.size()));
        for (size_t i = 0; i < channels.size(); i++) {
          result.Emplace(channels[i]);
        }

        return result;
      });
}

uint8 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    uint8 DefaultValue) {
  return propertyTexturePropertyCallback<uint8>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, DefaultValue](const auto& view) -> uint8 {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<uint8, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

int32 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    int32 DefaultValue) {
  return propertyTexturePropertyCallback<int32>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, DefaultValue](const auto& view) -> int32 {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<int32, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

float UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    float DefaultValue) {
  return propertyTexturePropertyCallback<float>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, DefaultValue](const auto& view) -> float {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<float, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

double UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat64(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    double DefaultValue) {
  return propertyTexturePropertyCallback<double>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, DefaultValue](const auto& view) -> double {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<double, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

FIntPoint UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntPoint(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    const FIntPoint& DefaultValue) {
  return propertyTexturePropertyCallback<FIntPoint>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, &DefaultValue](const auto& view) -> FIntPoint {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (!maybeValue) {
          return DefaultValue;
        }
        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toIntPoint(
              *maybeValue,
              DefaultValue);
        } else {
          auto maybeVec2 = CesiumGltf::
              MetadataConversions<glm::ivec2, decltype(value)>::convert(value);
          return maybeVec2 ? FITwinUnrealMetadataConversions::toIntPoint(*maybeVec2)
                           : DefaultValue;
        }
      });
}

FVector2D UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    const FVector2D& DefaultValue) {
  return propertyTexturePropertyCallback<FVector2D>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, &DefaultValue](const auto& view) -> FVector2D {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (!maybeValue) {
          return DefaultValue;
        }
        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toVector2D(value, DefaultValue);
        } else {
          auto maybeVec2 = CesiumGltf::
              MetadataConversions<glm::dvec2, decltype(value)>::convert(value);
          return maybeVec2 ? FITwinUnrealMetadataConversions::toVector2D(*maybeVec2)
                           : DefaultValue;
        }
      });
}

FIntVector UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntVector(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    const FIntVector& DefaultValue) {
  return propertyTexturePropertyCallback<FIntVector>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, &DefaultValue](const auto& view) -> FIntVector {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (!maybeValue) {
          return DefaultValue;
        }
        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toIntVector(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::ivec3, decltype(value)>::convert(value);
          return maybeVec3 ? FITwinUnrealMetadataConversions::toIntVector(*maybeVec3)
                           : DefaultValue;
        }
      });
}

FVector UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    const FVector& DefaultValue) {
  return propertyTexturePropertyCallback<FVector>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, &DefaultValue](const auto& view) -> FVector {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (!maybeValue) {
          return DefaultValue;
        }
        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toVector(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::dvec3, decltype(value)>::convert(value);
          return maybeVec3 ? FITwinUnrealMetadataConversions::toVector(*maybeVec3)
                           : DefaultValue;
        }
      });
}

FVector4 UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector4(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV,
    const FVector4& DefaultValue) {
  return propertyTexturePropertyCallback<FVector4>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV, &DefaultValue](const auto& view) -> FVector4 {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return DefaultValue;
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (!maybeValue) {
          return DefaultValue;
        }
        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toVector(value, DefaultValue);
        } else {
          auto maybeVec4 = CesiumGltf::
              MetadataConversions<glm::dvec4, decltype(value)>::convert(value);
          return maybeVec4 ? FITwinUnrealMetadataConversions::toVector4(*maybeVec4)
                           : DefaultValue;
        }
      });
}

FITwinCesiumPropertyArray UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV) {
  return propertyTexturePropertyCallback<FITwinCesiumPropertyArray>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV](const auto& view) -> FITwinCesiumPropertyArray {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return FITwinCesiumPropertyArray();
        }
        auto maybeValue = view.get(UV.X, UV.Y);
        if (maybeValue) {
          auto value = *maybeValue;
          if constexpr (CesiumGltf::IsMetadataArray<decltype(value)>::value) {
            return FITwinCesiumPropertyArray(value);
          }
        }
        return FITwinCesiumPropertyArray();
      });
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV](const auto& view) -> FITwinCesiumMetadataValue {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid &&
            view.status() !=
                PropertyTexturePropertyViewStatus::EmptyPropertyWithDefault) {
          return FITwinCesiumMetadataValue();
        }

        return FITwinCesiumMetadataValue(view.get(UV.X, UV.Y));
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetRawValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property,
    const FVector2D& UV) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [&UV](const auto& view) -> FITwinCesiumMetadataValue {
        if (view.status() != PropertyTexturePropertyViewStatus::Valid) {
          return FITwinCesiumMetadataValue();
        }

        return FITwinCesiumMetadataValue(view.getRaw(UV.X, UV.Y));
      });
}

bool UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return Property._normalized;
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetOffset(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no offset is specified.
        return FITwinCesiumMetadataValue(view.offset());
      });
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetScale(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no scale is specified.
        return FITwinCesiumMetadataValue(view.scale());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMinimumValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no min is specified.
        return FITwinCesiumMetadataValue(view.min());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMaximumValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no max is specified.
        return FITwinCesiumMetadataValue(view.max());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetNoDataValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no "no data" value is specified.
        return FITwinCesiumMetadataValue(view.noData());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
    UPARAM(ref) const FITwinCesiumPropertyTextureProperty& Property) {
  return propertyTexturePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no default value is specified.
        return FITwinCesiumMetadataValue(view.defaultValue());
      });
}
