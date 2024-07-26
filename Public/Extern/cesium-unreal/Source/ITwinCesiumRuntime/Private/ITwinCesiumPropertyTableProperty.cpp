// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPropertyTableProperty.h"
#include "CesiumGltf/MetadataConversions.h"
#include "CesiumGltf/PropertyTypeTraits.h"
#include "ITwinUnrealMetadataConversions.h"
#include <utility>

using namespace CesiumGltf;

namespace {
/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * of the specified type. If the type does not match, the callback is performed
 * on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param callback The callback function.
 *
 * @tparam TProperty The property type.
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <
    typename TProperty,
    bool Normalized,
    typename TResult,
    typename Callback>
TResult
propertyTablePropertyCallback(const std::any& property, Callback&& callback) {
  const PropertyTablePropertyView<TProperty, Normalized>* pProperty =
      std::any_cast<PropertyTablePropertyView<TProperty, Normalized>>(
          &property);
  if (pProperty) {
    return callback(*pProperty);
  }

  return callback(PropertyTablePropertyView<uint8_t>());
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a scalar type. If the valueType does not have a valid component type, the
 * callback is performed on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult scalarPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<int8_t, Normalized, TResult, Callback>(
        property,
        std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        uint8_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        int16_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        uint16_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        int32_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        uint32_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        int64_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        uint64_t,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<float, false, TResult, Callback>(
        property,
        std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<double, false, TResult, Callback>(
        property,
        std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a scalar array type. If the valueType does not have a valid component
 * type, the callback is performed on an invalid PropertyTablePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult scalarArrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<
        PropertyArrayView<int8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        PropertyArrayView<uint8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        PropertyArrayView<int16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        PropertyArrayView<uint16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        PropertyArrayView<int32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        PropertyArrayView<uint32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        PropertyArrayView<int64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        PropertyArrayView<uint64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<
        PropertyArrayView<float>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<
        PropertyArrayView<double>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::vecN type. If the valueType does not have a valid component type,
 * the callback is performed on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam N The dimensions of the glm::vecN
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <glm::length_t N, bool Normalized, typename TResult, typename Callback>
TResult vecNPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<
        glm::vec<N, int8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        glm::vec<N, uint8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        glm::vec<N, int16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        glm::vec<N, uint16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        glm::vec<N, int32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        glm::vec<N, uint32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        glm::vec<N, int64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        glm::vec<N, uint64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<
        glm::vec<N, float>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<
        glm::vec<N, double>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::vecN type. If the valueType does not have a valid component type,
 * the callback is performed on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult vecNPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  if (valueType.Type == EITwinCesiumMetadataType::Vec2) {
    return vecNPropertyTablePropertyCallback<2, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec3) {
    return vecNPropertyTablePropertyCallback<3, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec4) {
    return vecNPropertyTablePropertyCallback<4, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  return callback(PropertyTablePropertyView<uint8_t>());
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::vecN array type. If the valueType does not have a valid component
 * type, the callback is performed on an invalid PropertyTablePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam N The dimensions of the glm::vecN
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <glm::length_t N, bool Normalized, typename TResult, typename Callback>
TResult vecNArrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, int8_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, uint8_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, int16_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, uint16_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, int32_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, uint32_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, int64_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, uint64_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, float>>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::vec<N, double>>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::vecN array type. If the valueType does not have a valid component
 * type, the callback is performed on an invalid PropertyTablePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult vecNArrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  if (valueType.Type == EITwinCesiumMetadataType::Vec2) {
    return vecNArrayPropertyTablePropertyCallback<
        2,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec3) {
    return vecNArrayPropertyTablePropertyCallback<
        3,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Vec4) {
    return vecNArrayPropertyTablePropertyCallback<
        4,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  return callback(PropertyTablePropertyView<uint8_t>());
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::matN type. If the valueType does not have a valid component type,
 * the callback is performed on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam N The dimensions of the glm::matN
 * @tparam TNormalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <glm::length_t N, bool Normalized, typename TResult, typename Callback>
TResult matNPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<
        glm::mat<N, N, int8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        glm::mat<N, N, uint8_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        glm::mat<N, N, int16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        glm::mat<N, N, uint16_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        glm::mat<N, N, int32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        glm::mat<N, N, uint32_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        glm::mat<N, N, int64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        glm::mat<N, N, uint64_t>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<
        glm::mat<N, N, float>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<
        glm::mat<N, N, double>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::matN type. If the valueType does not have a valid component type,
 * the callback is performed on an invalid PropertyTablePropertyView instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult matNPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  if (valueType.Type == EITwinCesiumMetadataType::Mat2) {
    return matNPropertyTablePropertyCallback<2, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Mat3) {
    return matNPropertyTablePropertyCallback<3, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Mat4) {
    return matNPropertyTablePropertyCallback<4, Normalized, TResult, Callback>(
        property,
        valueType,
        std::forward<Callback>(callback));
  }

  return callback(PropertyTablePropertyView<uint8>());
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::matN array type. If the valueType does not have a valid component
 * type, the callback is performed on an invalid PropertyTablePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam N The dimensions of the glm::matN
 * @tparam TNormalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <glm::length_t N, bool Normalized, typename TResult, typename Callback>
TResult matNArrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.ComponentType) {
  case EITwinCesiumMetadataComponentType::Int8:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, int8_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint8:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, uint8_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int16:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, int16_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint16:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, uint16_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, int32_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, uint32_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Int64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, int64_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Uint64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, uint64_t>>,
        Normalized,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float32:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, float>>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataComponentType::Float64:
    return propertyTablePropertyCallback<
        PropertyArrayView<glm::mat<N, N, double>>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

/**
 * Callback on a std::any, assuming that it contains a PropertyTablePropertyView
 * on a glm::matN array type. If the valueType does not have a valid component
 * type, the callback is performed on an invalid PropertyTablePropertyView
 * instead.
 *
 * @param property The std::any containing the property.
 * @param valueType The FITwinCesiumMetadataValueType of the property.
 * @param callback The callback function.
 *
 * @tparam Normalized Whether the PropertyTablePropertyView is normalized.
 * @tparam TResult The type of the output from the callback function.
 * @tparam Callback The callback function type.
 */
template <bool Normalized, typename TResult, typename Callback>
TResult matNArrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  if (valueType.Type == EITwinCesiumMetadataType::Mat2) {
    return matNArrayPropertyTablePropertyCallback<
        2,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Mat3) {
    return matNArrayPropertyTablePropertyCallback<
        3,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  if (valueType.Type == EITwinCesiumMetadataType::Mat4) {
    return matNArrayPropertyTablePropertyCallback<
        4,
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  }

  return callback(PropertyTablePropertyView<uint8_t>());
}

template <bool Normalized, typename TResult, typename Callback>
TResult arrayPropertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    Callback&& callback) {
  switch (valueType.Type) {
  case EITwinCesiumMetadataType::Scalar:
    return scalarArrayPropertyTablePropertyCallback<
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Vec2:
  case EITwinCesiumMetadataType::Vec3:
  case EITwinCesiumMetadataType::Vec4:
    return vecNArrayPropertyTablePropertyCallback<
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Mat2:
  case EITwinCesiumMetadataType::Mat3:
  case EITwinCesiumMetadataType::Mat4:
    return matNArrayPropertyTablePropertyCallback<
        Normalized,
        TResult,
        Callback>(property, valueType, std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Boolean:
    return propertyTablePropertyCallback<
        PropertyArrayView<bool>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::String:
    return propertyTablePropertyCallback<
        PropertyArrayView<std::string_view>,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

template <typename TResult, typename Callback>
TResult propertyTablePropertyCallback(
    const std::any& property,
    const FITwinCesiumMetadataValueType& valueType,
    bool normalized,
    Callback&& callback) {
  if (valueType.bIsArray) {
    return normalized
               ? arrayPropertyTablePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : arrayPropertyTablePropertyCallback<false, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  }

  switch (valueType.Type) {
  case EITwinCesiumMetadataType::Scalar:
    return normalized
               ? scalarPropertyTablePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : scalarPropertyTablePropertyCallback<false, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Vec2:
  case EITwinCesiumMetadataType::Vec3:
  case EITwinCesiumMetadataType::Vec4:
    return normalized
               ? vecNPropertyTablePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : vecNPropertyTablePropertyCallback<false, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Mat2:
  case EITwinCesiumMetadataType::Mat3:
  case EITwinCesiumMetadataType::Mat4:
    return normalized
               ? matNPropertyTablePropertyCallback<true, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback))
               : matNPropertyTablePropertyCallback<false, TResult, Callback>(
                     property,
                     valueType,
                     std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::Boolean:
    return propertyTablePropertyCallback<bool, false, TResult, Callback>(
        property,
        std::forward<Callback>(callback));
  case EITwinCesiumMetadataType::String:
    return propertyTablePropertyCallback<
        std::string_view,
        false,
        TResult,
        Callback>(property, std::forward<Callback>(callback));
  default:
    return callback(PropertyTablePropertyView<uint8_t>());
  }
}

} // namespace

EITwinCesiumPropertyTablePropertyStatus
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertyTablePropertyStatus(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return Property._status;
}

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return CesiumMetadataValueTypeToBlueprintType(Property._valueType);
}

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArrayElementBlueprintType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  if (!Property._valueType.bIsArray) {
    return EITwinCesiumMetadataBlueprintType::None;
  }

  FITwinCesiumMetadataValueType valueType(Property._valueType);
  valueType.bIsArray = false;

  return CesiumMetadataValueTypeToBlueprintType(valueType);
}

FITwinCesiumMetadataValueType
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return Property._valueType;
}

int64 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<int64>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> int64 { return view.size(); });
}

int64 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<int64>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> int64 { return view.arrayCount(); });
}

bool UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    bool DefaultValue) {
  return propertyTablePropertyCallback<bool>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> bool {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<bool, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

uint8 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    uint8 DefaultValue) {
  return propertyTablePropertyCallback<uint8_t>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> uint8 {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<uint8, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

int32 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    int32 DefaultValue) {
  return propertyTablePropertyCallback<int32>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> int32 {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<int32, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

int64 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    int64 DefaultValue) {
  return propertyTablePropertyCallback<int64>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> int64 {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<int64_t, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

float UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    float DefaultValue) {
  return propertyTablePropertyCallback<float>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> float {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<float, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

double UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    double DefaultValue) {
  return propertyTablePropertyCallback<double>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, DefaultValue](const auto& v) -> double {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          return CesiumGltf::MetadataConversions<double, decltype(value)>::
              convert(value)
                  .value_or(DefaultValue);
        }
        return DefaultValue;
      });
}

FIntPoint UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FIntPoint& DefaultValue) {
  return propertyTablePropertyCallback<FIntPoint>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FIntPoint {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (!maybeValue) {
          return DefaultValue;
        }

        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toIntPoint(value, DefaultValue);
        } else {
          auto maybeVec2 = CesiumGltf::
              MetadataConversions<glm::ivec2, decltype(value)>::convert(value);
          return maybeVec2 ? FITwinUnrealMetadataConversions::toIntPoint(*maybeVec2)
                           : DefaultValue;
        }
      });
}

FVector2D UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FVector2D& DefaultValue) {
  return propertyTablePropertyCallback<FVector2D>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FVector2D {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
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

FIntVector UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FIntVector& DefaultValue) {
  return propertyTablePropertyCallback<FIntVector>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FIntVector {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
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

FVector3f UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FVector3f& DefaultValue) {
  return propertyTablePropertyCallback<FVector3f>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FVector3f {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (!maybeValue) {
          return DefaultValue;
        }

        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toVector3f(value, DefaultValue);
        } else {
          auto maybeVec3 = CesiumGltf::
              MetadataConversions<glm::vec3, decltype(value)>::convert(value);
          return maybeVec3 ? FITwinUnrealMetadataConversions::toVector3f(*maybeVec3)
                           : DefaultValue;
        }
      });
}

FVector UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FVector& DefaultValue) {
  return propertyTablePropertyCallback<FVector>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FVector {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
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

FVector4 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector4(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FVector4& DefaultValue) {
  return propertyTablePropertyCallback<FVector4>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FVector4 {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (!maybeValue) {
          return DefaultValue;
        }

        auto value = *maybeValue;
        if constexpr (IsMetadataString<decltype(value)>::value) {
          return FITwinUnrealMetadataConversions::toVector4(value, DefaultValue);
        } else {
          auto maybeVec4 = CesiumGltf::
              MetadataConversions<glm::dvec4, decltype(value)>::convert(value);
          return maybeVec4 ? FITwinUnrealMetadataConversions::toVector4(*maybeVec4)
                           : DefaultValue;
        }
      });
}

FMatrix UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FMatrix& DefaultValue) {
  auto maybeMat4 = propertyTablePropertyCallback<std::optional<glm::dmat4>>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID](const auto& v) -> std::optional<glm::dmat4> {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return std::nullopt;
        }
        auto maybeValue = v.get(FeatureID);
        if (!maybeValue) {
          return std::nullopt;
        }

        auto value = *maybeValue;
        return CesiumGltf::MetadataConversions<glm::dmat4, decltype(value)>::
            convert(value);
      });

  return maybeMat4 ? FITwinUnrealMetadataConversions::toMatrix(*maybeMat4)
                   : DefaultValue;
}

FString UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetString(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID,
    const FString& DefaultValue) {
  return propertyTablePropertyCallback<FString>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID, &DefaultValue](const auto& v) -> FString {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return DefaultValue;
        }
        auto maybeValue = v.get(FeatureID);
        if (!maybeValue) {
          return DefaultValue;
        }

        auto value = *maybeValue;
        using ValueType = decltype(value);

        if constexpr (
            IsMetadataVecN<ValueType>::value ||
            IsMetadataMatN<ValueType>::value ||
            IsMetadataString<ValueType>::value) {
          return FITwinUnrealMetadataConversions::toString(value);
        } else {
          auto maybeString = CesiumGltf::
              MetadataConversions<std::string, decltype(value)>::convert(value);

          return maybeString ? FITwinUnrealMetadataConversions::toString(*maybeString)
                             : DefaultValue;
        }
      });
}

FITwinCesiumPropertyArray UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID) {
  return propertyTablePropertyCallback<FITwinCesiumPropertyArray>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID](const auto& v) -> FITwinCesiumPropertyArray {
        // size() returns zero if the view is invalid.
        if (FeatureID < 0 || FeatureID >= v.size()) {
          return FITwinCesiumPropertyArray();
        }
        auto maybeValue = v.get(FeatureID);
        if (maybeValue) {
          auto value = *maybeValue;
          if constexpr (CesiumGltf::IsMetadataArray<decltype(value)>::value) {
            return FITwinCesiumPropertyArray(std::move(value));
          }
        }
        return FITwinCesiumPropertyArray();
      });
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID](const auto& view) -> FITwinCesiumMetadataValue {
        // size() returns zero if the view is invalid.
        if (FeatureID >= 0 && FeatureID < view.size()) {
          return FITwinCesiumMetadataValue(view.get(FeatureID));
        }
        return FITwinCesiumMetadataValue();
      });
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetRawValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [FeatureID](const auto& view) -> FITwinCesiumMetadataValue {
        // Return an empty value if the property is empty.
        if (view.status() ==
            PropertyTablePropertyViewStatus::EmptyPropertyWithDefault) {
          return FITwinCesiumMetadataValue();
        }

        // size() returns zero if the view is invalid.
        if (FeatureID >= 0 && FeatureID < view.size()) {
          return FITwinCesiumMetadataValue(view.getRaw(FeatureID));
        }

        return FITwinCesiumMetadataValue();
      });
}

bool UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return Property._normalized;
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetOffset(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no offset is specified.
        return FITwinCesiumMetadataValue(view.offset());
      });
}

FITwinCesiumMetadataValue UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetScale(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no scale is specified.
        return FITwinCesiumMetadataValue(view.scale());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMinimumValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no min is specified.
        return FITwinCesiumMetadataValue(view.min());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMaximumValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no max is specified.
        return FITwinCesiumMetadataValue(view.max());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNoDataValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no "no data" value is specified.
        return FITwinCesiumMetadataValue(view.noData());
      });
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return propertyTablePropertyCallback<FITwinCesiumMetadataValue>(
      Property._property,
      Property._valueType,
      Property._normalized,
      [](const auto& view) -> FITwinCesiumMetadataValue {
        // Returns an empty value if no default value is specified.
        return FITwinCesiumMetadataValue(view.defaultValue());
      });
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS

EITwinCesiumMetadataBlueprintType
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintComponentType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return UITwinCesiumPropertyTablePropertyBlueprintLibrary::
      GetArrayElementBlueprintType(Property);
}

EITwinCesiumMetadataTrueType_DEPRECATED
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetTrueType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return CesiumMetadataValueTypeToTrueType(Property._valueType);
}

EITwinCesiumMetadataTrueType_DEPRECATED
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetTrueComponentType(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  FITwinCesiumMetadataValueType type = Property._valueType;
  type.bIsArray = false;
  return CesiumMetadataValueTypeToTrueType(type);
}

int64 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNumberOfFeatures(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
      Property);
}

int64 UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetComponentCount(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property) {
  return UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(Property);
}

FITwinCesiumMetadataValue
UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetGenericValue(
    UPARAM(ref) const FITwinCesiumPropertyTableProperty& Property,
    int64 FeatureID) {
  return UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
      Property,
      FeatureID);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
