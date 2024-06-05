#pragma once

#include "CesiumGltf/ImageCesium.h"
#include "CesiumGltf/PropertyTextureProperty.h"
#include "CesiumGltf/PropertyTransformations.h"
#include "CesiumGltf/PropertyTypeTraits.h"
#include "CesiumGltf/PropertyView.h"
#include "CesiumGltf/Sampler.h"
#include "CesiumGltf/SamplerUtility.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace CesiumGltf {
/**
 * @brief Indicates the status of a property texture property view.
 *
 * The {@link PropertyTexturePropertyView} constructor always completes
 * successfully. However it may not always reflect the actual content of the
 * corresponding property texture property. This enumeration provides the
 * reason.
 */
class PropertyTexturePropertyViewStatus : public PropertyViewStatus {
public:
  /**
   * @brief This property view was initialized from an invalid
   * {@link PropertyTexture}.
   */
  static const int ErrorInvalidPropertyTexture = 14;

  /**
   * @brief This property view is associated with a {@link ClassProperty} of an
   * unsupported type.
   */
  static const int ErrorUnsupportedProperty = 15;

  /**
   * @brief This property view does not have a valid texture index.
   */
  static const int ErrorInvalidTexture = 16;

  /**
   * @brief This property view does not have a valid sampler index.
   */
  static const int ErrorInvalidSampler = 17;

  /**
   * @brief This property view does not have a valid image index.
   */
  static const int ErrorInvalidImage = 18;

  /**
   * @brief This property is viewing an empty image.
   */
  static const int ErrorEmptyImage = 19;

  /**
   * @brief This property uses an image with multi-byte channels. Only
   * single-byte channels are supported.
   */
  static const int ErrorInvalidBytesPerChannel = 20;

  /**
   * @brief The channels of this property texture property are invalid.
   * Channels must be in the range 0-N, where N is the number of available
   * channels in the image. There must be a minimum of one channel. Although
   * more than four channels can be defined for specialized texture
   * formats, this implementation only supports four channels max.
   */
  static const int ErrorInvalidChannels = 21;

  /**
   * @brief The channels of this property texture property do not provide
   * the exact number of bytes required by the property type. This may be
   * because an incorrect number of channels was provided, or because the
   * image itself has a different channel count / byte size than expected.
   */
  static const int ErrorChannelsAndTypeMismatch = 22;
};

template <typename ElementType>
ElementType assembleScalarValue(const gsl::span<uint8_t> bytes) noexcept {
  if constexpr (std::is_same_v<ElementType, float>) {
    assert(
        bytes.size() == sizeof(float) &&
        "Not enough channel inputs to construct a float.");
    uint32_t resultAsUint = 0;
    for (size_t i = 0; i < bytes.size(); i++) {
      resultAsUint |= static_cast<uint32_t>(bytes[i]) << i * 8;
    }

    // Reinterpret the bits as a float.
    return *reinterpret_cast<float*>(&resultAsUint);
  }

  if constexpr (IsMetadataInteger<ElementType>::value) {
    using UintType = std::make_unsigned_t<ElementType>;
    UintType resultAsUint = 0;
    for (size_t i = 0; i < bytes.size(); i++) {
      resultAsUint |= static_cast<UintType>(bytes[i]) << i * 8;
    }

    // Reinterpret the bits with the correct signedness.
    return *reinterpret_cast<ElementType*>(&resultAsUint);
  }
}

template <typename ElementType>
ElementType assembleVecNValue(const gsl::span<uint8_t> bytes) noexcept {
  ElementType result = ElementType();

  const glm::length_t N =
      getDimensionsFromPropertyType(TypeToPropertyType<ElementType>::value);
  using T = typename ElementType::value_type;

  assert(
      sizeof(T) <= 2 && "Components cannot be larger than two bytes in size.");

  if constexpr (std::is_same_v<T, int16_t>) {
    assert(N == 2 && "Only vec2s can contain two-byte integer components.");
    uint16_t x = static_cast<uint16_t>(bytes[0]) |
                 (static_cast<uint16_t>(bytes[1]) << 8);
    uint16_t y = static_cast<uint16_t>(bytes[2]) |
                 (static_cast<uint16_t>(bytes[3]) << 8);

    result[0] = *reinterpret_cast<int16_t*>(&x);
    result[1] = *reinterpret_cast<int16_t*>(&y);
  }

  if constexpr (std::is_same_v<T, uint16_t>) {
    assert(N == 2 && "Only vec2s can contain two-byte integer components.");
    result[0] = static_cast<uint16_t>(bytes[0]) |
                (static_cast<uint16_t>(bytes[1]) << 8);
    result[1] = static_cast<uint16_t>(bytes[2]) |
                (static_cast<uint16_t>(bytes[3]) << 8);
  }

  if constexpr (std::is_same_v<T, int8_t>) {
    for (size_t i = 0; i < bytes.size(); i++) {
      result[i] = *reinterpret_cast<const int8_t*>(&bytes[i]);
    }
  }

  if constexpr (std::is_same_v<T, uint8_t>) {
    for (size_t i = 0; i < bytes.size(); i++) {
      result[i] = bytes[i];
    }
  }

  return result;
}

template <typename T>
PropertyArrayView<T>
assembleArrayValue(const gsl::span<uint8_t> bytes) noexcept {
  std::vector<T> result(bytes.size() / sizeof(T));

  if constexpr (sizeof(T) == 2) {
    for (int i = 0, b = 0; i < result.size(); i++, b += 2) {
      using UintType = std::make_unsigned_t<T>;
      UintType resultAsUint = static_cast<UintType>(bytes[b]) |
                              (static_cast<UintType>(bytes[b + 1]) << 8);
      result[i] = *reinterpret_cast<T*>(&resultAsUint);
    }
  } else {
    for (size_t i = 0; i < bytes.size(); i++) {
      result[i] = *reinterpret_cast<const T*>(&bytes[i]);
    }
  }

  return PropertyArrayView<T>(std::move(result));
}

template <typename ElementType>
ElementType assembleValueFromChannels(const gsl::span<uint8_t> bytes) noexcept {
  assert(bytes.size() > 0 && "Channel input must have at least one value.");

  if constexpr (IsMetadataScalar<ElementType>::value) {
    return assembleScalarValue<ElementType>(bytes);
  }

  if constexpr (IsMetadataVecN<ElementType>::value) {
    return assembleVecNValue<ElementType>(bytes);
  }

  if constexpr (IsMetadataArray<ElementType>::value) {
    return assembleArrayValue<typename MetadataArrayType<ElementType>::type>(
        bytes);
  }
}

std::array<uint8_t, 4> sampleNearestPixel(
    const ImageCesium& image,
    const std::vector<int64_t>& channels,
    const double u,
    const double v);

/**
 * @brief A view of the data specified by a {@link PropertyTextureProperty}.
 *
 * Provides utilities to sample the property texture property using texture
 * coordinates. Property values are retrieved from the NEAREST texel without
 * additional filtering applied.
 *
 * @tparam ElementType The type of the elements represented in the property
 * view
 * @tparam Normalized Whether or not the property is normalized. If
 * normalized, the elements can be retrieved as normalized floating-point
 * numbers, as opposed to their integer values.
 */
template <typename ElementType, bool Normalized = false>
class PropertyTexturePropertyView;

/**
 * @brief A view of the non-normalized data specified by a
 * {@link PropertyTextureProperty}.
 *
 * Provides utilities to sample the property texture property using texture
 * coordinates.
 *
 * @tparam ElementType The type of the elements represented in the property view
 */
template <typename ElementType>
class PropertyTexturePropertyView<ElementType, false>
    : public PropertyView<ElementType, false> {
public:
  /**
   * @brief Constructs an invalid instance for a non-existent property.
   */
  PropertyTexturePropertyView() noexcept
      : PropertyView<ElementType, false>(),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {}

  /**
   * @brief Constructs an invalid instance for an erroneous property.
   *
   * @param status The code from {@link PropertyTexturePropertyViewStatus} indicating the error with the property.
   */
  PropertyTexturePropertyView(PropertyViewStatusType status) noexcept
      : PropertyView<ElementType, false>(status),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {
    assert(
        this->_status != PropertyTexturePropertyViewStatus::Valid &&
        "An empty property view should not be constructed with a valid status");
  }

  /**
   * @brief Constructs an instance of an empty property that specifies a default
   * value. Although this property has no data, it can return the default value
   * when {@link PropertyTexturePropertyView::get} is called. However,
   * {@link PropertyTexturePropertyView::getRaw} cannot be used.
   *
   * @param classProperty The {@link ClassProperty} this property conforms to.
   */
  PropertyTexturePropertyView(const ClassProperty& classProperty) noexcept
      : PropertyView<ElementType, false>(classProperty),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {
    if (this->_status != PropertyTexturePropertyViewStatus::Valid) {
      // Don't override the status / size if something is wrong with the class
      // property's definition.
      return;
    }

    if (!classProperty.defaultProperty) {
      // This constructor should only be called if the class property *has* a
      // default value. But in the case that it does not, this property view
      // becomes invalid.
      this->_status =
          PropertyTexturePropertyViewStatus::ErrorNonexistentProperty;
      return;
    }

    this->_status = PropertyTexturePropertyViewStatus::EmptyPropertyWithDefault;
  }

  /**
   * @brief Construct a view of the data specified by a {@link PropertyTextureProperty}.
   *
   * @param property The {@link PropertyTextureProperty}
   * @param classProperty The {@link ClassProperty} this property conforms to.
   * @param sampler The {@link Sampler} used by the property.
   * @param image The {@link ImageCesium} used by the property.
   * @param channels The code from {@link PropertyTextureProperty::channels}.
   */
  PropertyTexturePropertyView(
      const PropertyTextureProperty& property,
      const ClassProperty& classProperty,
      const Sampler& sampler,
      const ImageCesium& image) noexcept
      : PropertyView<ElementType, false>(classProperty, property),
        _pSampler(&sampler),
        _pImage(&image),
        _texCoordSetIndex(property.texCoord),
        _channels(property.channels),
        _swizzle() {
    if (this->_status != PropertyTexturePropertyViewStatus::Valid) {
      return;
    }

    _swizzle.reserve(_channels.size());

    for (size_t i = 0; i < _channels.size(); ++i) {
      switch (_channels[i]) {
      case 0:
        _swizzle += "r";
        break;
      case 1:
        _swizzle += "g";
        break;
      case 2:
        _swizzle += "b";
        break;
      case 3:
        _swizzle += "a";
        break;
      default:
        assert(false && "A valid channels vector must be passed to the view.");
      }
    }
  }

  /**
   * @brief Gets the value of the property for the given texture coordinates
   * with all value transforms applied. That is, if the property specifies an
   * offset and scale, they will be applied to the value before the value is
   * returned. The sampler's wrapping mode will be used when sampling the
   * texture.
   *
   * If this property has a specified "no data" value, this will return the
   * property's default value for any elements that equal this "no data" value.
   * If the property did not specify a default value, this returns std::nullopt.
   *
   * @param u The u-component of the texture coordinates.
   * @param v The v-component of the texture coordinates.
   *
   * @return The value of the element, or std::nullopt if it matches the "no
   * data" value
   */
  std::optional<ElementType> get(double u, double v) const noexcept {
    if (this->_status ==
        PropertyTexturePropertyViewStatus::EmptyPropertyWithDefault) {
      return this->defaultValue();
    }

    ElementType value = getRaw(u, v);

    if (value == this->noData()) {
      return this->defaultValue();
    }

    if constexpr (IsMetadataNumeric<ElementType>::value) {
      value = transformValue(value, this->offset(), this->scale());
    }

    if constexpr (IsMetadataNumericArray<ElementType>::value) {
      value = transformArray(value, this->offset(), this->scale());
    }

    return value;
  }

  /**
   * @brief Gets the raw value of the property for the given texture
   * coordinates. The sampler's wrapping mode will be used when sampling the
   * texture.
   *
   * If this property has a specified "no data" value, the raw value will still
   * be returned, even if it equals the "no data" value.
   *
   * @param u The u-component of the texture coordinates.
   * @param v The v-component of the texture coordinates.
   *
   * @return The value at the nearest pixel to the texture coordinates.
   */

  ElementType getRaw(double u, double v) const noexcept {
    assert(
        this->_status == PropertyTexturePropertyViewStatus::Valid &&
        "Check the status() first to make sure view is valid");

    double wrappedU = applySamplerWrapS(u, this->_pSampler->wrapS);
    double wrappedV = applySamplerWrapT(v, this->_pSampler->wrapT);

    std::array<uint8_t, 4> sample =
        sampleNearestPixel(*this->_pImage, this->_channels, wrappedU, wrappedV);
    return assembleValueFromChannels<ElementType>(
        gsl::span(sample.data(), this->_channels.size()));
  }

  /**
   * @brief Get the texture coordinate set index for this property.
   */
  int64_t getTexCoordSetIndex() const noexcept {
    return this->_texCoordSetIndex;
  }

  /**
   * @brief Get the sampler describing how to sample the data from the
   * property's texture.
   *
   * This will be nullptr if the property texture property view runs into
   * problems during construction.
   */
  const Sampler* getSampler() const noexcept { return this->_pSampler; }

  /**
   * @brief Get the image containing this property's data.
   *
   * This will be nullptr if the property texture property view runs into
   * problems during construction.
   */
  const ImageCesium* getImage() const noexcept { return this->_pImage; }

  /**
   * @brief Gets the channels of this property texture property.
   */
  const std::vector<int64_t>& getChannels() const noexcept {
    return this->_channels;
  }

  /**
   * @brief Gets this property's channels as a swizzle string.
   */
  const std::string& getSwizzle() const noexcept { return this->_swizzle; }

private:
  const Sampler* _pSampler;
  const ImageCesium* _pImage;
  int64_t _texCoordSetIndex;
  std::vector<int64_t> _channels;
  std::string _swizzle;
};

/**
 * @brief A view of the normalized data specified by a
 * {@link PropertyTextureProperty}.
 *
 * Provides utilities to sample the property texture property using texture
 * coordinates.
 */
template <typename ElementType>
class PropertyTexturePropertyView<ElementType, true>
    : public PropertyView<ElementType, true> {
private:
  using NormalizedType = typename TypeToNormalizedType<ElementType>::type;

public:
  /**
   * @brief Constructs an invalid instance for a non-existent property.
   */
  PropertyTexturePropertyView() noexcept
      : PropertyView<ElementType, true>(),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {}

  /**
   * @brief Constructs an invalid instance for an erroneous property.
   *
   * @param status The code from {@link PropertyTexturePropertyViewStatus} indicating the error with the property.
   */
  PropertyTexturePropertyView(PropertyViewStatusType status) noexcept
      : PropertyView<ElementType, true>(status),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {
    assert(
        this->_status != PropertyTexturePropertyViewStatus::Valid &&
        "An empty property view should not be constructed with a valid status");
  }

  /**
   * @brief Constructs an instance of an empty property that specifies a default
   * value. Although this property has no data, it can return the default value
   * when {@link PropertyTexturePropertyView::get} is called. However,
   * {@link PropertyTexturePropertyView::getRaw} cannot be used.
   *
   * @param classProperty The {@link ClassProperty} this property conforms to.
   */
  PropertyTexturePropertyView(const ClassProperty& classProperty) noexcept
      : PropertyView<ElementType, true>(classProperty),
        _pSampler(nullptr),
        _pImage(nullptr),
        _texCoordSetIndex(0),
        _channels(),
        _swizzle() {
    if (this->_status != PropertyTexturePropertyViewStatus::Valid) {
      // Don't override the status / size if something is wrong with the class
      // property's definition.
      return;
    }

    if (!classProperty.defaultProperty) {
      // This constructor should only be called if the class property *has* a
      // default value. But in the case that it does not, this property view
      // becomes invalid.
      this->_status =
          PropertyTexturePropertyViewStatus::ErrorNonexistentProperty;
      return;
    }

    this->_status = PropertyTexturePropertyViewStatus::EmptyPropertyWithDefault;
  }

  /**
   * @brief Construct a view of the data specified by a {@link PropertyTextureProperty}.
   *
   * @param property The {@link PropertyTextureProperty}
   * @param classProperty The {@link ClassProperty} this property conforms to.
   * @param sampler The {@link Sampler} used by the property.
   * @param image The {@link ImageCesium} used by the property.
   * @param channels The value of {@link PropertyTextureProperty::channels}.
   */
  PropertyTexturePropertyView(
      const PropertyTextureProperty& property,
      const ClassProperty& classProperty,
      const Sampler& sampler,
      const ImageCesium& image) noexcept
      : PropertyView<ElementType, true>(classProperty, property),
        _pSampler(&sampler),
        _pImage(&image),
        _texCoordSetIndex(property.texCoord),
        _channels(property.channels),
        _swizzle() {
    if (this->_status != PropertyTexturePropertyViewStatus::Valid) {
      return;
    }

    _swizzle.reserve(_channels.size());
    for (size_t i = 0; i < _channels.size(); ++i) {
      switch (_channels[i]) {
      case 0:
        _swizzle += "r";
        break;
      case 1:
        _swizzle += "g";
        break;
      case 2:
        _swizzle += "b";
        break;
      case 3:
        _swizzle += "a";
        break;
      default:
        assert(false && "A valid channels vector must be passed to the view.");
      }
    }
  }

  /**
   * @brief Gets the value of the property for the given texture coordinates
   * with all value transforms applied. That is, if the property specifies an
   * offset and scale, they will be applied to the value before the value is
   * returned. The sampler's wrapping mode will be used when sampling the
   * texture.
   *
   * If this property has a specified "no data" value, and the retrieved element
   * is equal to that value, then this will return the property's specified
   * default value. If the property did not provide a default value, this
   * returns std::nullopt.
   *
   * @param u The u-component of the texture coordinates.
   * @param v The v-component of the texture coordinates.
   *
   * @return The value of the element, or std::nullopt if it matches the "no
   * data" value
   */
  std::optional<NormalizedType> get(double u, double v) const noexcept {
    if (this->_status ==
        PropertyTexturePropertyViewStatus::EmptyPropertyWithDefault) {
      return this->defaultValue();
    }

    ElementType value = getRaw(u, v);

    if (value == this->noData()) {
      return this->defaultValue();
    }

    if constexpr (IsMetadataScalar<ElementType>::value) {
      return transformValue<NormalizedType>(
          normalize<ElementType>(value),
          this->offset(),
          this->scale());
    }

    if constexpr (IsMetadataVecN<ElementType>::value) {
      constexpr glm::length_t N = ElementType::length();
      using T = typename ElementType::value_type;
      using NormalizedT = typename NormalizedType::value_type;
      return transformValue<glm::vec<N, NormalizedT>>(
          normalize<N, T>(value),
          this->offset(),
          this->scale());
    }

    if constexpr (IsMetadataArray<ElementType>::value) {
      using ArrayElementType = typename MetadataArrayType<ElementType>::type;
      if constexpr (IsMetadataScalar<ArrayElementType>::value) {
        return transformNormalizedArray<ArrayElementType>(
            value,
            this->offset(),
            this->scale());
      }

      if constexpr (IsMetadataVecN<ArrayElementType>::value) {
        constexpr glm::length_t N = ArrayElementType::length();
        using T = typename ArrayElementType::value_type;
        return transformNormalizedVecNArray<N, T>(
            value,
            this->offset(),
            this->scale());
      }
    }
  }

  /**
   * @brief Gets the raw value of the property for the given texture
   * coordinates. The sampler's wrapping mode will be used when sampling the
   * texture.
   *
   * If this property has a specified "no data" value, the raw value will still
   * be returned, even if it equals the "no data" value.
   *
   * @param u The u-component of the texture coordinates.
   * @param v The v-component of the texture coordinates.
   *
   * @return The value at the nearest pixel to the texture coordinates.
   */

  ElementType getRaw(double u, double v) const noexcept {
    assert(
        this->_status == PropertyTexturePropertyViewStatus::Valid &&
        "Check the status() first to make sure view is valid");

    double wrappedU = applySamplerWrapS(u, this->_pSampler->wrapS);
    double wrappedV = applySamplerWrapT(v, this->_pSampler->wrapT);

    std::array<uint8_t, 4> sample =
        sampleNearestPixel(*this->_pImage, this->_channels, wrappedU, wrappedV);

    return assembleValueFromChannels<ElementType>(
        gsl::span(sample.data(), this->_channels.size()));
  }

  /**
   * @brief Get the texture coordinate set index for this property.
   */
  int64_t getTexCoordSetIndex() const noexcept {
    return this->_texCoordSetIndex;
  }

  /**
   * @brief Get the sampler describing how to sample the data from the
   * property's texture.
   *
   * This will be nullptr if the property texture property view runs into
   * problems during construction.
   */
  const Sampler* getSampler() const noexcept { return this->_pSampler; }

  /**
   * @brief Get the image containing this property's data.
   *
   * This will be nullptr if the property texture property view runs into
   * problems during construction.
   */
  const ImageCesium* getImage() const noexcept { return this->_pImage; }

  /**
   * @brief Gets the channels of this property texture property.
   */
  const std::vector<int64_t>& getChannels() const noexcept {
    return this->_channels;
  }

  /**
   * @brief Gets this property's channels as a swizzle string.
   */
  const std::string& getSwizzle() const noexcept { return this->_swizzle; }

private:
  const Sampler* _pSampler;
  const ImageCesium* _pImage;
  int64_t _texCoordSetIndex;
  std::vector<int64_t> _channels;
  std::string _swizzle;
};

} // namespace CesiumGltf
