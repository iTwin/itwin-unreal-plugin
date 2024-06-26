#include "ITwinCesiumGltfSpecUtility.h"
#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "ITwinCesiumPropertyTableProperty.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPropertyTablePropertySpec,
    "Cesium.Unit.PropertyTableProperty",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FITwinCesiumPropertyTablePropertySpec)

void FITwinCesiumPropertyTablePropertySpec::Define() {
  Describe("Constructor", [this]() {
    It("constructs invalid instance by default", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          0);

      FITwinCesiumMetadataValueType expectedType; // Invalid type
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
    });

    It("constructs invalid instance from view with invalid definition",
       [this]() {
         PropertyTablePropertyView<int8_t> propertyView(
             PropertyTablePropertyViewStatus::ErrorArrayTypeMismatch);
         FITwinCesiumPropertyTableProperty property(propertyView);
         TestEqual(
             "PropertyTablePropertyStatus",
             UITwinCesiumPropertyTablePropertyBlueprintLibrary::
                 GetPropertyTablePropertyStatus(property),
             EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
         TestEqual<int64>(
             "Size",
             UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
                 property),
             0);

         FITwinCesiumMetadataValueType expectedType; // Invalid type
         TestTrue(
             "ValueType",
             UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
                 property) == expectedType);
       });

    It("constructs invalid instance from view with invalid data", [this]() {
      PropertyTablePropertyView<int8_t> propertyView(
          PropertyTablePropertyViewStatus::ErrorBufferViewOutOfBounds);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidPropertyData);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          0);

      FITwinCesiumMetadataValueType expectedType; // Invalid type
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
    });

    It("constructs valid instance", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{1, 2, 3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(values.size()));

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Integer);

      TestFalse(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);

      // Check that undefined properties return empty values
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetOffset(property);
      TestTrue("Offset", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetScale(property);
      TestTrue("Scale", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMaximumValue(
          property);
      TestTrue("Max", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMinimumValue(
          property);
      TestTrue("Min", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNoDataValue(
          property);
      TestTrue("NoData", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
          property);
      TestTrue("Default", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("constructs valid normalized instance", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      std::vector<uint8_t> values{0, 1, 255, 128};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      PropertyTablePropertyView<uint8_t, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(values.size()));

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Uint8,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Byte);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);
    });

    It("constructs instance for fixed-length array property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;
      classProperty.count = 3;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      const int64_t size =
          static_cast<int64_t>(values.size()) / *classProperty.count;
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(),
          PropertyComponentType::None,
          PropertyComponentType::None);

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(size));

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          true);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Array);

      TestFalse(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          *classProperty.count);
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::Integer);
    });

    It("constructs instance for variable-length array property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      std::vector<uint16_t> offsets{0, 1, 3, 6};
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      int64_t size = static_cast<int64_t>(offsets.size()) - 1;
      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          gsl::span<const std::byte>(),
          PropertyComponentType::Uint16,
          PropertyComponentType::None);

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          true);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Array);

      TestFalse(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      // The arrays vary in length, so GetArraySize() should return zero.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::Integer);
    });

    It("constructs valid instance with additional properties", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.normalized = true;

      double offset = 1.0;
      double scale = 2.0;
      double min = 1.0;
      double max = 3.0;
      int32_t noData = 1;
      double defaultValue = 12.3;

      classProperty.offset = offset;
      classProperty.scale = scale;
      classProperty.min = min;
      classProperty.max = max;
      classProperty.noData = noData;
      classProperty.defaultProperty = defaultValue;

      std::vector<int32_t> values{1, 2, 3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      PropertyTablePropertyView<int32_t, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(values.size()));

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Integer);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetOffset(property);
      TestEqual(
          "Offset",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          offset);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetScale(property);
      TestEqual(
          "Scale",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          scale);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMaximumValue(
          property);
      TestEqual(
          "Max",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          max);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMinimumValue(
          property);
      TestEqual(
          "Min",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          min);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNoDataValue(
          property);
      TestEqual(
          "NoData",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0.0),
          noData);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
          property);
      TestEqual(
          "Default",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          defaultValue);
    });

    It("constructs valid array instance with additional properties", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.normalized = true;
      classProperty.array = true;
      classProperty.count = 2;

      std::vector<double> offset = {1.0, 2.0};
      std::vector<double> scale = {2.0, -1.0};
      std::vector<double> min = {1.0, 1.0};
      std::vector<double> max = {3.0, 2.0};
      std::vector<int32_t> noData = {-1, -1};
      std::vector<double> defaultValue = {12.3, 4.5};

      classProperty.offset = {offset[0], offset[1]};
      classProperty.scale = {scale[0], scale[1]};
      classProperty.min = {min[0], min[1]};
      classProperty.max = {max[0], max[1]};
      classProperty.noData = {noData[0], noData[1]};
      classProperty.defaultProperty = {defaultValue[0], defaultValue[1]};

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6, -1, -1};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      PropertyTablePropertyView<PropertyArrayView<int32_t>, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(values.size()));

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          true);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Array);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::Integer);

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetOffset(property);
      FITwinCesiumPropertyArray array =
          UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "Offset0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0.0),
          offset[0]);
      TestEqual(
          "Offset1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0.0),
          offset[1]);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetScale(property);
      array = UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "Scale0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0.0),
          scale[0]);
      TestEqual(
          "Scale1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0.0),
          scale[1]);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMaximumValue(
          property);
      array = UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "Max0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0.0),
          max[0]);
      TestEqual(
          "Max1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0.0),
          max[1]);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMinimumValue(
          property);
      array = UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "Min0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0.0),
          min[0]);
      TestEqual(
          "Min1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0.0),
          min[1]);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNoDataValue(
          property);
      array = UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "NoData0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0),
          noData[0]);
      TestEqual(
          "NoData1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0),
          noData[1]);

      value = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
          property);
      array = UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);

      TestEqual(
          TEXT("Size"),
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(*classProperty.count));
      TestEqual(
          "DefaultValue0",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0),
              0.0),
          defaultValue[0]);
      TestEqual(
          "DefaultValue1",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1),
              0.0),
          defaultValue[1]);
    });
  });

  Describe("GetBoolean", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestFalse(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
              property,
              0));
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::BOOLEAN;

      std::vector<std::byte> data{static_cast<std::byte>(0b10110001)};

      PropertyTablePropertyView<bool> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(8),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(8));

      TestFalse(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
              property,
              -1));
      TestFalse(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
              property,
              10));
    });

    It("gets from boolean property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::BOOLEAN;

      std::vector<std::byte> data{static_cast<std::byte>(0b10110001)};

      PropertyTablePropertyView<bool> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(8),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);

      std::vector<bool>
          expected{true, false, false, false, true, true, false, true};
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(expected.size()));

      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
                property,
                static_cast<int64>(i),
                false),
            expected[i]);
      }
    });

    It("converts compatible values", [this]() {
      std::vector<std::string>
          values{"yes", "false", "invalid value", "true", "yeah", "1"};
      size_t totalSize = 0;
      for (const auto& value : values) {
        totalSize += value.size();
      }

      std::vector<std::byte> data(totalSize);
      std::vector<uint32_t> offsets(values.size() + 1);
      uint32_t currentOffset = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        std::memcpy(
            data.data() + currentOffset,
            values[i].data(),
            values[i].size());
        offsets[i] = currentOffset;
        currentOffset += static_cast<uint32_t>(values[i].size());
      }
      offsets[offsets.size() - 1] = currentOffset;
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::STRING;

      PropertyTablePropertyView<std::string_view> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          PropertyComponentType::None,
          PropertyComponentType::Uint32);

      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<bool> expected{true, false, false, true, false, true};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetBoolean(
                property,
                static_cast<int64>(i),
                false),
            expected[i]);
      }
    });
  });

  Describe("GetByte", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(property, 0),
          0);
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      std::vector<uint8_t> values{1, 2, 3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<uint8_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(property, -1),
          0);
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(property, 10),
          0);
    });

    It("gets from uint8 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      std::vector<uint8_t> values{1, 2, 3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<uint8_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
                property,
                static_cast<int64>(i),
                0),
            values[i]);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{1, 24, 255, 256, -1, 28};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<uint8_t> expected{1, 24, 255, 0, 0, 28};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
                property,
                static_cast<int64>(i),
                0),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      uint8_t noDataValue = 0;
      uint8_t defaultValue = 255;

      classProperty.noData = noDataValue;
      classProperty.defaultProperty = defaultValue;

      std::vector<uint8_t> values{1, 2, 3, 0, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<uint8_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == noDataValue) {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
                  property,
                  static_cast<int64>(i),
                  0),
              defaultValue);
        } else {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
                  property,
                  static_cast<int64>(i),
                  0),
              values[i]);
        }
      }
    });
  });

  Describe("GetInteger", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(property, 0),
          0);
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
              property,
              -1),
          0);
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
              property,
              10),
          0);
    });

    It("gets from int32 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
                property,
                static_cast<int64>(i),
                0),
            values[i]);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<float> values{
          1.234f,
          -24.5f,
          std::numeric_limits<float>::lowest(),
          2456.80f,
          std::numeric_limits<float>::max(),
      };
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<float> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<int32_t> expected{1, -24, 0, 2456, 0};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
                property,
                static_cast<int64>(i),
                0),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noDataValue = 0;
      int32_t defaultValue = 10;

      classProperty.noData = noDataValue;
      classProperty.defaultProperty = defaultValue;

      std::vector<int32_t> values{-1, 2, -3, 0, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == noDataValue) {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
                  property,
                  static_cast<int64>(i),
                  0),
              defaultValue);
        } else {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
                  property,
                  static_cast<int64>(i),
                  0),
              values[i]);
        }
      }
    });
  });

  Describe("GetInteger64", [this]() {
    int64_t defaultInt64 = static_cast<int64_t>(0);

    It("returns default value for invalid property", [this, defaultInt64]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual<int64>(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
              property,
              0),
          defaultInt64);
    });

    It("returns default value for invalid feature ID", [this, defaultInt64]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT64;

      std::vector<int64_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int64_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual<int64>(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
              property,
              -1),
          defaultInt64);
      TestEqual<int64>(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
              property,
              10),
          defaultInt64);
    });

    It("gets from int64 property", [this, defaultInt64]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT64;

      std::vector<int64_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int64_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual<int64>(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
                property,
                static_cast<int64>(i),
                defaultInt64),
            values[i]);
      }
    });

    It("converts compatible values", [this, defaultInt64]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT64;

      std::vector<uint64_t> values{
          10,
          20,
          30,
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 100};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<uint64_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<int64_t> expected{10, 20, 30, 0};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual<int64>(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
                property,
                static_cast<int64>(i),
                defaultInt64),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this, defaultInt64]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT64;

      int64_t noDataValue = 0;
      int64_t defaultValue = 10;

      classProperty.noData = noDataValue;
      classProperty.defaultProperty = defaultValue;

      std::vector<int64_t> values{-1, 2, 0, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int64_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == noDataValue) {
          TestEqual<int64>(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
                  property,
                  static_cast<int64>(i),
                  defaultInt64),
              defaultValue);
        } else {
          TestEqual<int64>(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger64(
                  property,
                  static_cast<int64>(i),
                  defaultInt64),
              values[i]);
        }
      }
    });
  });

  Describe("GetFloat", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
              property,
              0.0f),
          0.0f);
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<float> values{-1.1f, 2.2f, -3.3f, 4.0f};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<float> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(property, -1),
          0.0f);
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(property, 10),
          0.0f);
    });

    It("gets from float property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<float> values{-1.1f, 2.2f, -3.3f, 4.0f};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<float> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
                property,
                static_cast<int64>(i),
                0),
            values[i]);
      }
    });

    It("converts compatible values", [this]() {
      std::vector<double> values{
          -1.1,
          2.2,
          -3.3,
          std::numeric_limits<double>::max()};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      PropertyTablePropertyView<double> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<float> expected(4);
      for (size_t i = 0; i < values.size(); i++) {
        expected[i] = static_cast<float>(values[i]);
      }
      expected[3] = 0.0f;

      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
                property,
                static_cast<int64>(i),
                0.0f),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      float offset = 5.0f;
      float scale = 2.0f;

      classProperty.offset = offset;
      classProperty.scale = scale;

      std::vector<float> values{-1.1f, 2.2f, -3.3f, 4.0f};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<float> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
                property,
                static_cast<int64>(i),
                0),
            values[i] * scale + offset);
      }
    });
  });

  Describe("GetFloat64", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
              property,
              0.0),
          0.0);
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<double> values{-1.1, 2.2, -3.3, 4.0};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<double> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
              property,
              -1),
          0.0);
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
              property,
              10),
          0.0);
    });

    It("gets from double property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<double> values{-1.1, 2.2, -3.3, 4.0};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<double> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
                property,
                static_cast<int64>(i),
                0),
            values[i]);
      }
    });

    It("gets from normalized uint8 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      std::vector<uint8_t> values{0, 128, 255, 0};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<uint8_t, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
                property,
                static_cast<int64>(i),
                0),
            static_cast<double>(values[i]) / 255.0);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::STRING;

      std::vector<std::string> values{"not a number", "10", "-2"};
      size_t totalSize = 0;
      for (const auto& value : values) {
        totalSize += value.size();
      }

      std::vector<std::byte> data(totalSize);
      std::vector<uint8_t> offsets(values.size() + 1);
      uint8_t currentOffset = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        std::memcpy(
            data.data() + currentOffset,
            values[i].data(),
            values[i].size());
        offsets[i] = currentOffset;
        currentOffset += static_cast<uint8_t>(values[i].size());
      }
      offsets[offsets.size() - 1] = currentOffset;
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      PropertyTablePropertyView<std::string_view> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          PropertyComponentType::None,
          PropertyComponentType::Uint8);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<double> expected{
          0.0,
          static_cast<double>(10),
          static_cast<double>(-2)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
                property,
                static_cast<int64>(i),
                0.0),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      float offset = 5.0;
      float scale = 2.0;

      classProperty.offset = offset;
      classProperty.scale = scale;

      std::vector<double> values{-1.1, 2.2, -3.3, 4.0};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<double> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat64(
                property,
                static_cast<int64>(i),
                0),
            values[i] * scale + offset);
      }
    });
  });

  Describe("GetIntPoint", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
              property,
              0,
              FIntPoint(0)),
          FIntPoint(0));
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<glm::ivec2> values{
          glm::ivec2(1, 1),
          glm::ivec2(-1, -1),
          glm::ivec2(10, 4)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
              property,
              -1,
              FIntPoint(0)),
          FIntPoint(0));
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
              property,
              10,
              FIntPoint(0)),
          FIntPoint(0));
    });

    It("gets from glm::ivec2 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<glm::ivec2> values{
          glm::ivec2(1, 1),
          glm::ivec2(-1, -1),
          glm::ivec2(10, 4)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FIntPoint expected(values[i][0], values[i][1]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
                property,
                static_cast<int64>(i),
                FIntPoint(0)),
            expected);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<glm::vec3> values{
          glm::vec3(1.0f, 2.0f, 3.0f),
          glm::vec3(20.5f, -1.5f, std::numeric_limits<float>::lowest()),
          glm::vec3(std::numeric_limits<float>::max(), -1.0f, 2.0f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FIntPoint> expected{
          FIntPoint(1, 2),
          FIntPoint(20, -1),
          FIntPoint(0)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
                property,
                static_cast<int64>(i),
                FIntPoint(0)),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      glm::ivec2 noData(-1, -1);
      FIntPoint defaultValue(5, 22);

      classProperty.noData = {noData[0], noData[1]};
      classProperty.defaultProperty = {defaultValue[0], defaultValue[1]};

      std::vector<glm::ivec2> values{
          glm::ivec2(1, 1),
          glm::ivec2(-1, -1),
          glm::ivec2(10, 4)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FIntPoint expected;
        if (values[i] == noData) {
          expected = defaultValue;
        } else {
          expected = FIntPoint(values[i][0], values[i][1]);
        }

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntPoint(
                property,
                static_cast<int64>(i),
                FIntPoint(0)),
            expected);
      }
    });
  });

  Describe("GetVector2D", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
              property,
              0,
              FVector2D::Zero()),
          FVector2D::Zero());
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec2> values{
          glm::dvec2(-1.0, 2.0),
          glm::dvec2(3.4, 5.6),
          glm::dvec2(1.5, -1.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
              property,
              -1,
              FVector2D::Zero()),
          FVector2D::Zero());
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
              property,
              10,
              FVector2D::Zero()),
          FVector2D::Zero());
    });

    It("gets from glm::dvec2 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec2> values{
          glm::dvec2(-1.0, 2.0),
          glm::dvec2(3.4, 5.6),
          glm::dvec2(1.5, -1.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector2D expected(values[i][0], values[i][1]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
                property,
                static_cast<int64>(i),
                FVector2D::Zero()),
            expected);
      }
    });

    It("gets from normalized glm::u8vec2 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      std::vector<glm::u8vec2> values{
          glm::u8vec2(1, 1),
          glm::u8vec2(0, 255),
          glm::u8vec2(10, 4)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::u8vec2, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      for (size_t i = 0; i < values.size(); i++) {
        glm::dvec2 expected = glm::dvec2(values[i][0], values[i][1]) / 255.0;
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
                property,
                static_cast<int64>(i),
                FVector2D::Zero()),
            FVector2D(expected[0], expected[1]));
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::STRING;

      std::vector<std::string> values{"X=10 Y=3", "not a vector", "X=-2 Y=4"};
      size_t totalSize = 0;
      for (const auto& value : values) {
        totalSize += value.size();
      }

      std::vector<std::byte> data(totalSize);
      std::vector<uint8_t> offsets(values.size() + 1);
      uint8_t currentOffset = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        std::memcpy(
            data.data() + currentOffset,
            values[i].data(),
            values[i].size());
        offsets[i] = currentOffset;
        currentOffset += static_cast<uint8_t>(values[i].size());
      }
      offsets[offsets.size() - 1] = currentOffset;
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      PropertyTablePropertyView<std::string_view> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          PropertyComponentType::None,
          PropertyComponentType::Uint8);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FVector2D> expected{
          FVector2D(10, 3),
          FVector2D::Zero(),
          FVector2D(-2, 4)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
                property,
                static_cast<int64>(i),
                FVector2D::Zero()),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      FVector2D offset(3.0, 2.4);
      FVector2D scale(2.0, -1.0);

      classProperty.offset = {offset[0], offset[1]};
      classProperty.scale = {scale[0], scale[1]};

      std::vector<glm::dvec2> values{
          glm::dvec2(-1.0, 2.0),
          glm::dvec2(3.4, 5.6),
          glm::dvec2(1.5, -1.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector2D expected(
            values[i][0] * scale[0] + offset[0],
            values[i][1] * scale[1] + offset[1]);

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
                property,
                static_cast<int64>(i),
                FVector2D::Zero()),
            expected);
      }
    });
  });

  Describe("GetIntVector", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
              property,
              0,
              FIntVector(0)),
          FIntVector(0));
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<glm::ivec3> values{
          glm::ivec3(1, 1, -1),
          glm::ivec3(-1, -1, 2),
          glm::ivec3(10, 4, 5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
              property,
              -1,
              FIntVector(0)),
          FIntVector(0));
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
              property,
              10,
              FIntVector(0)),
          FIntVector(0));
    });

    It("gets from glm::ivec3 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<glm::ivec3> values{
          glm::ivec3(1, 1, -1),
          glm::ivec3(-1, -1, 2),
          glm::ivec3(10, 4, 5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FIntVector expected(values[i][0], values[i][1], values[i][2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
                property,
                static_cast<int64>(i),
                FIntVector(0)),
            expected);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<glm::vec3> values{
          glm::vec3(1.0f, 2.0f, 3.0f),
          glm::vec3(-5.9f, 8.2f, 1.15f),
          glm::vec3(20.5f, -1.5f, std::numeric_limits<float>::lowest()),
          glm::vec3(std::numeric_limits<float>::max(), -1.0f, 2.0f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FIntVector> expected{
          FIntVector(1, 2, 3),
          FIntVector(-5, 8, 1),
          FIntVector(0),
          FIntVector(0)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
                property,
                static_cast<int64>(i),
                FIntVector(0)),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      glm::ivec3 noData(-1, -1, -1);
      FIntVector defaultValue(1, 2, 3);

      classProperty.noData = {noData[0], noData[1], noData[2]};
      classProperty.defaultProperty = {
          defaultValue[0],
          defaultValue[1],
          defaultValue[2]};

      std::vector<glm::ivec3> values{
          glm::ivec3(1, 1, -1),
          glm::ivec3(-1, -1, -1),
          glm::ivec3(-1, -1, 2),
          glm::ivec3(10, 4, 5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::ivec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FIntVector expected;
        if (values[i] == noData) {
          expected = defaultValue;
        } else {
          expected = FIntVector(values[i][0], values[i][1], values[i][2]);
        }

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetIntVector(
                property,
                static_cast<int64>(i),
                FIntVector(0)),
            expected);
      }
    });
  });

  Describe("GetVector3f", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
              property,
              0,
              FVector3f::Zero()),
          FVector3f::Zero());
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<glm::vec3> values{
          glm::vec3(1.0f, 1.9f, -1.0f),
          glm::vec3(-1.0f, -1.8f, 2.5f),
          glm::vec3(10.0f, 4.4f, 5.4f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
              property,
              -1,
              FVector3f::Zero()),
          FVector3f::Zero());
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
              property,
              10,
              FVector3f::Zero()),
          FVector3f::Zero());
    });

    It("gets from glm::vec3 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<glm::vec3> values{
          glm::vec3(1.0f, 1.9f, -1.0f),
          glm::vec3(-1.0f, -1.8f, 2.5f),
          glm::vec3(10.0f, 4.4f, 5.4f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector3f expected(values[i][0], values[i][1], values[i][2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
                property,
                static_cast<int64>(i),
                FVector3f(0)),
            expected);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec2> values{
          glm::dvec2(1.0, 2.0),
          glm::dvec2(-5.9, 8.2),
          glm::dvec2(20.5, std::numeric_limits<double>::lowest()),
          glm::dvec2(std::numeric_limits<double>::max(), -1.0)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FVector3f> expected(4);
      for (size_t i = 0; i < 2; i++) {
        expected[i] = FVector3f(
            static_cast<float>(values[i][0]),
            static_cast<float>(values[i][1]),
            0.0f);
      }
      expected[2] = expected[3] = FVector3f::Zero();

      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
                property,
                static_cast<int64>(i),
                FVector3f::Zero()),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      FVector3f offset(1.0f, 4.5f, -2.0f);
      FVector3f scale(0.5f, -1.0f, 2.2f);

      classProperty.offset = {offset[0], offset[1], offset[2]};
      classProperty.scale = {scale[0], scale[1], scale[2]};

      std::vector<glm::vec3> values{
          glm::vec3(1.0f, 1.9f, -1.0f),
          glm::vec3(-1.0f, -1.8f, 2.5f),
          glm::vec3(10.0f, 4.4f, 5.4f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector3f expected(
            values[i][0] * scale[0] + offset[0],
            values[i][1] * scale[1] + offset[1],
            values[i][2] * scale[2] + offset[2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector3f(
                property,
                static_cast<int64>(i),
                FVector3f(0)),
            expected);
      }
    });
  });

  Describe("GetVector", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
              property,
              0,
              FVector::Zero()),
          FVector::Zero());
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec3> values{
          glm::dvec3(-1.0, 2.0, 5.0),
          glm::dvec3(3.4, 5.6, 7.8),
          glm::dvec3(1.5, -1.5, -2.01)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
              property,
              -1,
              FVector::Zero()),
          FVector::Zero());
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
              property,
              10,
              FVector::Zero()),
          FVector::Zero());
    });

    It("gets from glm::dvec3 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec3> values{
          glm::dvec3(-1.0, 2.0, 5.0),
          glm::dvec3(3.4, 5.6, 7.8),
          glm::dvec3(1.5, -1.5, -2.01)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector expected(values[i][0], values[i][1], values[i][2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector::Zero()),
            expected);
      }
    });

    It("gets from normalized glm::i8vec3 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      std::vector<glm::i8vec3> values{
          glm::i8vec3(-1, 2, 5),
          glm::i8vec3(3, 5, 7),
          glm::i8vec3(1, -1, -2)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::i8vec3, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      for (size_t i = 0; i < values.size(); i++) {
        glm::dvec3 expected =
            glm::dvec3(values[i][0], values[i][1], values[i][2]) / 127.0;
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector::Zero()),
            FVector(expected[0], expected[1], expected[2]));
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::STRING;

      std::vector<std::string> values{
          "X=10 Y=3 Z=4",
          "not a vector",
          "X=-2 Y=4 Z=5"};
      size_t totalSize = 0;
      for (const auto& value : values) {
        totalSize += value.size();
      }

      std::vector<std::byte> data(totalSize);
      std::vector<uint8_t> offsets(values.size() + 1);
      uint8_t currentOffset = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        std::memcpy(
            data.data() + currentOffset,
            values[i].data(),
            values[i].size());
        offsets[i] = currentOffset;
        currentOffset += static_cast<uint8_t>(values[i].size());
      }
      offsets[offsets.size() - 1] = currentOffset;
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      PropertyTablePropertyView<std::string_view> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          PropertyComponentType::None,
          PropertyComponentType::Uint8);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FVector> expected{
          FVector(10, 3, 4),
          FVector::Zero(),
          FVector(-2, 4, 5)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector::Zero()),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      FVector offset(1.0, 2.0, 3.0);
      FVector scale(0.5, -1.0, 2.0);

      classProperty.offset = {offset[0], offset[1], offset[2]};
      classProperty.scale = {scale[0], scale[1], scale[2]};

      std::vector<glm::dvec3> values{
          glm::dvec3(-1.0, 2.0, 5.0),
          glm::dvec3(3.4, 5.6, 7.8),
          glm::dvec3(1.5, -1.5, -2.01)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec3> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector expected(
            values[i][0] * scale[0] + offset[0],
            values[i][1] * scale[1] + offset[1],
            values[i][2] * scale[2] + offset[2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector::Zero()),
            expected);
      }
    });
  });

  Describe("GetVector4", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector4(
              property,
              0,
              FVector4::Zero()),
          FVector4::Zero());
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec4> values{
          glm::dvec4(-1.0, 2.0, 5.0, 8.8),
          glm::dvec4(3.4, 5.6, 7.8, 0.2),
          glm::dvec4(1.5, -1.5, -2.01, 5.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector4(
              property,
              -1,
              FVector4::Zero()),
          FVector4::Zero());
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector4(
              property,
              10,
              FVector4::Zero()),
          FVector4::Zero());
    });

    It("gets from glm::dvec4 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<glm::dvec4> values{
          glm::dvec4(-1.0, 2.0, 5.0, 8.8),
          glm::dvec4(3.4, 5.6, 7.8, 0.2),
          glm::dvec4(1.5, -1.5, -2.01, 5.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector4 expected(
            values[i][0],
            values[i][1],
            values[i][2],
            values[i][3]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector4::Zero()),
            expected);
      }
    });

    It("gets from glm::i8vec4 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      std::vector<glm::i8vec4> values{
          glm::i8vec4(-1, 2, 5, 8),
          glm::i8vec4(3, 5, 7, 0),
          glm::i8vec4(1, -1, -2, 5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::i8vec4, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      for (size_t i = 0; i < values.size(); i++) {
        glm::dvec4 expected(
            values[i][0],
            values[i][1],
            values[i][2],
            values[i][3]);
        expected /= 127.0;

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector4::Zero()),
            FVector4(expected[0], expected[1], expected[2], expected[3]));
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::STRING;

      std::vector<std::string> values{
          "X=10 Y=3 Z=4 W=2",
          "not a vector",
          "X=-2 Y=4 Z=5"};
      size_t totalSize = 0;
      for (const auto& value : values) {
        totalSize += value.size();
      }

      std::vector<std::byte> data(totalSize);
      std::vector<uint8_t> offsets(values.size() + 1);
      uint8_t currentOffset = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        std::memcpy(
            data.data() + currentOffset,
            values[i].data(),
            values[i].size());
        offsets[i] = currentOffset;
        currentOffset += static_cast<uint8_t>(values[i].size());
      }
      offsets[offsets.size() - 1] = currentOffset;
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);

      PropertyTablePropertyView<std::string_view> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          PropertyComponentType::None,
          PropertyComponentType::Uint8);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FVector> expected{
          FVector4(10, 3, 4, 2),
          FVector::Zero(),
          FVector4(-2, 4, 5, 1)};
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector4(
                property,
                static_cast<int64>(i),
                FVector4::Zero()),
            expected[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      FVector4 offset(1.0, 2.0, 4.5, -3.0);
      FVector4 scale(0.5, -1.0, 2.0, -2.5);

      classProperty.offset = {offset[0], offset[1], offset[2], offset[3]};
      classProperty.scale = {scale[0], scale[1], scale[2], scale[3]};

      std::vector<glm::dvec4> values{
          glm::dvec4(-1.0, 2.0, 5.0, 8.8),
          glm::dvec4(3.4, 5.6, 7.8, 0.2),
          glm::dvec4(1.5, -1.5, -2.01, 5.5)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dvec4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FVector4 expected(
            values[i][0] * scale[0] + offset[0],
            values[i][1] * scale[1] + offset[1],
            values[i][2] * scale[2] + offset[2],
            values[i][3] * scale[3] + offset[3]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector(
                property,
                static_cast<int64>(i),
                FVector4::Zero()),
            expected);
      }
    });
  });

  Describe("GetMatrix", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
              property,
              0,
              FMatrix::Identity),
          FMatrix::Identity);
    });

    It("returns default value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::MAT4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      // clang-format off
      std::vector<glm::dmat4> values{
          glm::dmat4(
             1.0,  2.0,  3.0,  4.0,
             5.0,  6.0,  7.0,  8.0,
             9.0, 10.0, 11.0, 12.0,
            13.0, 14.0, 15.0, 16.0),
          glm::dmat4(
             1.0,  0.0, 0.0, 0.0,
             0.0, -2.5, 0.0, 0.0,
             0.0,  0.0, 0.5, 0.0,
            -1.5,  4.0, 2.0, 1.0),
      };
      // clang-format on
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dmat4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestEqual(
          "negative index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
              property,
              -1,
              FMatrix::Identity),

          FMatrix::Identity);
      TestEqual(
          "out-of-range positive index",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
              property,
              10,
              FMatrix::Identity),
          FMatrix::Identity);
    });

    It("gets from glm::dmat4 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::MAT4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      // clang-format off
      std::vector<glm::dmat4> values{
          glm::dmat4(
             1.0,  2.0,  3.0,  4.0,
             5.0,  6.0,  7.0,  8.0,
             9.0, 10.0, 11.0, 12.0,
            13.0, 14.0, 15.0, 16.0),
          glm::dmat4(
             1.0,  0.0, 0.0, 0.0,
             0.0, -2.5, 0.0, 0.0,
             0.0,  0.0, 0.5, 0.0,
            -1.5,  4.0, 2.0, 1.0),
      };
      // clang-format on
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dmat4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FMatrix> expected(2);
      expected[0] = FMatrix(
          FPlane4d(1.0, 5.0, 9.0, 13.0),
          FPlane4d(2.0, 6.0, 10.0, 14.0),
          FPlane4d(3.0, 7.0, 11.0, 15.0),
          FPlane4d(4.0, 8.0, 12.0, 16.0));
      expected[1] = FMatrix(
          FPlane4d(1.0, 0.0, 0.0, -1.5),
          FPlane4d(0.0, -2.5, 0.0, 4.0),
          FPlane4d(0.0, 0.0, 0.5, 2.0),
          FPlane4d(0.0, 0.0, 0.0, 1.0));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
                property,
                static_cast<int64>(i),
                FMatrix::Identity),
            expected[i]);
      }
    });

    It("gets from glm::u8mat4x4 property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::MAT4;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      // clang-format off
      std::vector<glm::i8mat4x4> values{
          glm::i8mat4x4(
             127,   0,    0,    0,
               0, 127,    0,    0,
               0,   0,  127,    0,
               0,   0, -127,  127),
          glm::i8mat4x4(
               0,   -127,   0,   0,
               127,    0,   0,   0,
               0,      0, 127,   0,
               0,      0, 127, 127),
      };
      // clang-format on
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::i8mat4x4, true> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property));

      std::vector<FMatrix> expected(2);
      expected[0] = FMatrix(
          FPlane4d(1.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 1.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 1.0, -1.0),
          FPlane4d(0.0, 0.0, 0.0, 1.0));
      expected[1] = FMatrix(
          FPlane4d(0.0, 1.0, 0.0, 0.0),
          FPlane4d(-1.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 1.0, 1.0),
          FPlane4d(0.0, 0.0, 0.0, 1.0));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
                property,
                static_cast<int64>(i),
                FMatrix::Identity),
            expected[i]);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      std::vector<double> values{-2.0, 10.5};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<double> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FMatrix> expected(2);
      expected[0] = FMatrix(
          FPlane4d(-2.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, -2.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, -2.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, -2.0));
      expected[1] = FMatrix(
          FPlane4d(10.5, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 10.5, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 10.5, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 10.5));
      for (size_t i = 0; i < expected.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
                property,
                static_cast<int64>(i),
                FMatrix::Identity),
            expected[i]);
      }
    });

    It("returns default values for incompatible type", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<glm::vec2> values{
          glm::vec2(-2.0f, 10.5f),
          glm::vec2(1.5f, 0.1f)};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::vec2> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
                property,
                static_cast<int64>(i),
                FMatrix::Identity),
            FMatrix::Identity);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::MAT4;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT64;

      double offset = 1.0;
      double scale = 2.0;

      CesiumUtility::JsonValue::Array offsetArray(16);
      CesiumUtility::JsonValue::Array scaleArray(16);
      for (size_t i = 0; i < 16; i++) {
        offsetArray[i] = offset;
        scaleArray[i] = scale;
      }

      classProperty.offset = offsetArray;
      classProperty.scale = scaleArray;

      // clang-format off
      std::vector<glm::dmat4> values{
          glm::dmat4(
             1.0,  2.0,  3.0,  4.0,
             5.0,  6.0,  7.0,  8.0,
             9.0, 10.0, 11.0, 12.0,
            13.0, 14.0, 15.0, 16.0),
          glm::dmat4(
             1.0,  0.0, 0.0, 0.0,
             0.0, -2.5, 0.0, 0.0,
             0.0,  0.0, 0.5, 0.0,
            -1.5,  4.0, 2.0, 1.0),
      };
      // clang-format on
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<glm::dmat4> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      std::vector<FMatrix> expected(2);
      expected[0] = FMatrix(
          FPlane4d(3.0, 11.0, 19.0, 27.0),
          FPlane4d(5.0, 13.0, 21.0, 29.0),
          FPlane4d(7.0, 15.0, 23.0, 31.0),
          FPlane4d(9.0, 17.0, 25.0, 33.0));
      expected[1] = FMatrix(
          FPlane4d(3.0, 1.0, 1.0, -2.0),
          FPlane4d(1.0, -4.0, 1.0, 9.0),
          FPlane4d(1.0, 1.0, 2.0, 5.0),
          FPlane4d(1.0, 1.0, 1.0, 3.0));

      for (size_t i = 0; i < values.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetMatrix(
                property,
                static_cast<int64>(i),
                FMatrix::Identity),
            expected[i]);
      }
    });
  });

  Describe("GetArray", [this]() {
    It("returns empty array for non-array property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(values.size()));

      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, 0);
      TestEqual(
          "array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "array type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
              valueType);
    });

    It("returns empty array for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);

      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, 0);
      TestEqual(
          "array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "array type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
              valueType);
    });

    It("returns empty array for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;
      classProperty.count = 2;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      int64 size = static_cast<int64_t>(values.size()) / *classProperty.count;

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(),
          PropertyComponentType::None,
          PropertyComponentType::None);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);

      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, -1);
      TestEqual(
          "negative index array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "negative index array type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
              valueType);

      array =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, 10);
      TestEqual(
          "out-of-range positive index array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);
      TestTrue(
          "out-of-range positive index array type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
              valueType);
    });

    It("returns array for fixed-length array property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;
      classProperty.count = 2;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      int64 size = static_cast<int64_t>(values.size()) / *classProperty.count;

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(),
          PropertyComponentType::None,
          PropertyComponentType::None);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          *classProperty.count);

      for (int64 i = 0; i < size; i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, i);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Int32,
            false);
        TestTrue(
            "array element type",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
                valueType);

        int64 arrayOffset = i * arraySize;
        for (int64 j = 0; j < arraySize; j++) {
          std::string label(
              "array" + std::to_string(i) + " value" + std::to_string(j));
          FITwinCesiumMetadataValue value =
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, j);
          TestEqual(
              label.c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              values[static_cast<size_t>(arrayOffset + j)]);
        }
      }
    });

    It("returns array for variable-length array property", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      std::vector<uint16_t> offsets{0, 2, 3, 6};
      std::vector<std::byte> offsetsData = GetValuesAsBytes(offsets);
      int64 size = static_cast<int64_t>(offsets.size() - 1);

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(offsetsData.data(), offsetsData.size()),
          gsl::span<const std::byte>(),
          PropertyComponentType::Uint16,
          PropertyComponentType::None);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          static_cast<int64_t>(0));

      std::vector<std::vector<int32_t>> expected{{1, 2}, {3}, {4, 5, 6}};
      for (int64 i = 0; i < size; i++) {
        const std::vector<int32_t>& expectedArray =
            expected[static_cast<size_t>(i)];
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, i);
        TestEqual(
            "array size",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
            static_cast<int64>(expectedArray.size()));
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Int32,
            false);
        TestTrue(
            "element type",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
                valueType);

        for (size_t j = 0; j < expectedArray.size(); j++) {
          std::string label(
              "array" + std::to_string(i) + " value" + std::to_string(j));
          FITwinCesiumMetadataValue value =
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(
                  array,
                  static_cast<int64>(j));
          TestEqual(
              label.c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              expectedArray[j]);
        }
      }
    });

    It("gets with noData value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;
      classProperty.count = 2;

      classProperty.noData = {-1, -1};

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6, -1, -1};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      int64 size = static_cast<int64_t>(values.size()) / *classProperty.count;

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(),
          PropertyComponentType::None,
          PropertyComponentType::None);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          *classProperty.count);

      for (int64 i = 0; i < size - 1; i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, i);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Int32,
            false);
        TestTrue(
            "array element type",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
                valueType);

        int64 arrayOffset = i * arraySize;
        for (int64 j = 0; j < arraySize; j++) {
          std::string label(
              "array" + std::to_string(i) + " value" + std::to_string(j));
          FITwinCesiumMetadataValue value =
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, j);
          TestEqual(
              label.c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              values[static_cast<size_t>(arrayOffset + j)]);
        }
      }

      // Check that the "no data" value resolves to an empty array of an invalid
      // type.
      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(
              property,
              size - 1);
      TestEqual<int64>(
          "array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64_t>(0));
      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Invalid,
          EITwinCesiumMetadataComponentType::None,
          false);
      TestTrue(
          "array element type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
              valueType);
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;
      classProperty.array = true;
      classProperty.count = 2;

      classProperty.noData = {-1, -1};
      classProperty.defaultProperty = {10, 20};

      std::vector<int32_t> values{1, 2, 3, 4, 5, 6, -1, -1};
      std::vector<std::byte> data = GetValuesAsBytes(values);
      int64 size = static_cast<int64_t>(values.size()) / *classProperty.count;

      PropertyTablePropertyView<PropertyArrayView<int32_t>> propertyView(
          propertyTableProperty,
          classProperty,
          size,
          gsl::span<const std::byte>(data.data(), data.size()),
          gsl::span<const std::byte>(),
          gsl::span<const std::byte>(),
          PropertyComponentType::None,
          PropertyComponentType::None);
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          size);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property),
          *classProperty.count);

      for (int64 i = 0; i < size; i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(property, i);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Int32,
            false);
        TestTrue(
            "array element type",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
                valueType);

        if (i == size - 1) {
          std::string label("array" + std::to_string(i));
          // Check that the "no data" value resolves to the default array value.
          FITwinCesiumMetadataValue value0 =
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 0);
          TestEqual(
              label.c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value0, 0),
              10);
          FITwinCesiumMetadataValue value1 =
              UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1);
          TestEqual(
              label.c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value1, 0),
              20);
        } else {
          int64 arrayOffset = i * arraySize;
          for (int64 j = 0; j < arraySize; j++) {
            std::string label(
                "array" + std::to_string(i) + " value" + std::to_string(j));
            FITwinCesiumMetadataValue value =
                UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, j);
            TestEqual(
                label.c_str(),
                UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
                values[static_cast<size_t>(arrayOffset + j)]);
          }
        }
      }
    });
  });

  Describe("GetValue", [this]() {
    It("returns empty value for invalid property", [this]() {
      FITwinCesiumPropertyTableProperty property;
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::ErrorInvalidProperty);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64_t>(0));

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, 0);
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "value type",
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
              valueType);
    });

    It("returns empty value for invalid feature ID", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, -1);
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "negative index value type",
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
              valueType);

      value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, 10);
      TestTrue(
          "out-of-range positive index value type",
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
              valueType);
    });

    It("gets value for valid feature IDs", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < values.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, i);
        TestTrue(
            "value type",
            UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
                valueType);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
            values[i]);
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      std::vector<float> values{-1.1f, 2.0f, -3.5f, 4.0f};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      float offset = 1.0f;
      float scale = 2.0f;

      classProperty.offset = offset;
      classProperty.scale = scale;

      PropertyTablePropertyView<float> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Float32,
          false);
      for (size_t i = 0; i < values.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, i);
        TestTrue(
            "value type",
            UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
                valueType);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0),
            values[i] * scale + offset);
      }
    });

    It("gets with noData", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noData = -1;
      classProperty.noData = noData;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < values.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, i);
        if (values[i] == noData) {
          // Empty value indicated by invalid value type.
          TestTrue(
              "value type",
              UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
                  FITwinCesiumMetadataValueType());
        } else {
          TestTrue(
              "value type",
              UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
                  valueType);
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              values[i]);
        }
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTableProperty propertyTableProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noData = -1;
      int32_t defaultValue = 15;

      classProperty.noData = noData;
      classProperty.defaultProperty = defaultValue;

      std::vector<int32_t> values{-1, 2, -3, 4};
      std::vector<std::byte> data = GetValuesAsBytes(values);

      PropertyTablePropertyView<int32_t> propertyView(
          propertyTableProperty,
          classProperty,
          static_cast<int64_t>(values.size()),
          gsl::span<const std::byte>(data.data(), data.size()));
      FITwinCesiumPropertyTableProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              property),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < values.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(property, i);
        TestTrue(
            "value type",
            UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
                valueType);
        if (values[i] == noData) {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              defaultValue);
        } else {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
              values[i]);
        }
      }
    });
  });
}
