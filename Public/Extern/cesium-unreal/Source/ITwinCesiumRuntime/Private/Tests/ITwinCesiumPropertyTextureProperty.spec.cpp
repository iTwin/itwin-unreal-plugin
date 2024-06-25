#include "ITwinCesiumGltfSpecUtility.h"
#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "ITwinCesiumPropertyTextureProperty.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPropertyTexturePropertySpec,
    "Cesium.Unit.PropertyTextureProperty",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
const std::vector<FVector2D> texCoords{
    FVector2D(0, 0),
    FVector2D(0.5, 0),
    FVector2D(0, 0.5),
    FVector2D(0.5, 0.5)};
END_DEFINE_SPEC(FITwinCesiumPropertyTexturePropertySpec)

void FITwinCesiumPropertyTexturePropertySpec::Define() {
  Describe("Constructor", [this]() {
    It("constructs invalid instance by default", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);

      FITwinCesiumMetadataValueType expectedType; // Invalid type
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
    });

    It("constructs invalid instance from view with invalid definition",
       [this]() {
         PropertyTexturePropertyView<int8_t> propertyView(
             PropertyTexturePropertyViewStatus::ErrorArrayTypeMismatch);
         FITwinCesiumPropertyTextureProperty property(propertyView);
         TestEqual(
             "PropertyTexturePropertyStatus",
             UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
                 GetPropertyTexturePropertyStatus(property),
             EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);

         FITwinCesiumMetadataValueType expectedType; // Invalid type
         TestTrue(
             "ValueType",
             UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
                 property) == expectedType);
       });

    It("constructs invalid instance from view with invalid data", [this]() {
      PropertyTexturePropertyView<int8_t> propertyView(
          PropertyTexturePropertyViewStatus::ErrorInvalidImage);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidPropertyData);

      FITwinCesiumMetadataValueType expectedType; // Invalid type
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
    });

    It("constructs valid instance", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);

      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Uint8,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Byte);

      TestFalse(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);

      // Check that undefined properties return empty values
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetOffset(property);
      TestTrue("Offset", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetScale(property);
      TestTrue("Scale", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMaximumValue(
          property);
      TestTrue("Max", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMinimumValue(
          property);
      TestTrue("Min", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetNoDataValue(
          property);
      TestTrue("NoData", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
          property);
      TestTrue("Default", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("constructs valid normalized instance", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{0, 1, 255, 128};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);

      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Uint8,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Byte);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);
    });

    It("constructs instance for fixed-length array property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.array = true;
      classProperty.count = 2;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4, 5, 6, 7, 8};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<PropertyArrayView<uint8_t>> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);

      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Uint8,
          true);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Array);

      TestFalse(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          *classProperty.count);
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::Byte);
    });

    It("constructs valid instance with additional properties", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
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

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);

      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType expectedType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Uint8,
          false);
      TestTrue(
          "ValueType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
              property) == expectedType);
      TestEqual(
          "BlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetBlueprintType(
              property),
          EITwinCesiumMetadataBlueprintType::Byte);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      // Test that the returns are as expected for non-array properties.
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          static_cast<int64_t>(0));
      TestEqual(
          "ArrayElementBlueprintType",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetArrayElementBlueprintType(property),
          EITwinCesiumMetadataBlueprintType::None);

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetOffset(property);
      TestEqual(
          "Offset",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          offset);

      value =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetScale(property);
      TestEqual(
          "Scale",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          scale);

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMaximumValue(
          property);
      TestEqual(
          "Max",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          max);

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetMinimumValue(
          property);
      TestEqual(
          "Min",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          min);

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetNoDataValue(
          property);
      TestEqual(
          "NoData",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0.0),
          noData);

      value = UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
          property);
      TestEqual(
          "Default",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          defaultValue);
    });
  });

  Describe("GetByte", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
              property,
              FVector2D::Zero()),
          0);
    });

    It("gets from uint8 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
                property,
                texCoords[i]),
            values[i]);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT16;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<int16_t> values{-1, 2, 256, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1};
      } else {
        propertyTextureProperty.channels = {1, 0};
      }

      PropertyTexturePropertyView<int16_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      std::vector<uint8_t> expected{0, 2, 0, 4};
      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
                property,
                texCoords[i],
                0),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      uint8_t noDataValue = 0;
      uint8_t defaultValue = 255;

      classProperty.noData = noDataValue;
      classProperty.defaultProperty = defaultValue;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 0};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        if (values[i] == noDataValue) {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
                  property,
                  texCoords[i]),
              defaultValue);
        } else {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetByte(
                  property,
                  texCoords[i]),
              values[i]);
        }
      }
    });
  });

  Describe("GetInteger", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
              property,
              FVector2D::Zero()),
          0);
    });

    It("gets from int32 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{-1, 2, -3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
                property,
                texCoords[i]),
            values[i]);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{
          1.234f,
          -24.5f,
          std::numeric_limits<float>::lowest(),
          2456.80f};
      image.pixelData = GetValuesAsBytes(values);
      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      std::vector<int32_t> expected{1, -24, 0, 2456};
      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
                property,
                texCoords[i]),
            expected[i]);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noDataValue = -1;
      int32_t defaultValue = 10;

      classProperty.noData = noDataValue;
      classProperty.defaultProperty = defaultValue;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{-1, 2, -3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        if (values[i] == noDataValue) {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
                  property,
                  texCoords[i]),
              defaultValue);
        } else {
          TestEqual(
              std::string("value" + std::to_string(i)).c_str(),
              UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
                  property,
                  texCoords[i]),
              values[i]);
        }
      }
    });
  });

  Describe("GetFloat", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat(
              property,
              FVector2D::Zero()),
          0.0f);
    });

    It("gets from float property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{-1.1f, 2.2f, -3.3f, 4.0f};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat(
                property,
                texCoords[i]),
            values[i]);
      }
    });

    It("converts uint8 values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat(
                property,
                texCoords[i]),
            static_cast<float>(values[i]));
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      float offset = 5.0f;
      float scale = 2.0f;

      classProperty.offset = offset;
      classProperty.scale = scale;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{-1.1f, 2.2f, -3.3f, 4.0f};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat(
                property,
                texCoords[i]),
            values[i] * scale + offset);
      }
    });
  });

  Describe("GetFloat64", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat64(
              property,
              FVector2D::Zero()),
          0.0);
    });

    It("gets from normalized uint8 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{0, 128, 255, 0};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8_t, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat64(
                property,
                texCoords[i]),
            static_cast<double>(values[i]) / 255.0);
      }
    });

    It("converts float values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{-1.1, 2.2, -3.3, 4.0};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat64(
                property,
                texCoords[i]),
            static_cast<double>(values[i]));
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      float offset = 5.0;
      float scale = 2.0;

      classProperty.offset = offset;
      classProperty.scale = scale;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 1;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{0, 128, 255, 0};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<uint8, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetFloat64(
                property,
                texCoords[i]),
            (static_cast<double>(values[i]) / 255.0) * scale + offset);
      }
    });
  });

  Describe("GetIntPoint", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntPoint(
              property,
              FVector2D::Zero(),
              FIntPoint(0)),
          FIntPoint(0));
    });

    It("gets from i8vec2 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec2> values{
          glm::i8vec2(1, 1),
          glm::i8vec2(-1, -1),
          glm::i8vec2(2, 4),
          glm::i8vec2(0, -8)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec2> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntPoint expected(values[i][0], values[i][1]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntPoint(
                property,
                texCoords[i],
                FIntPoint(0)),
            expected);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{
          1.234f,
          -24.5f,
          std::numeric_limits<float>::lowest(),
          2456.80f};
      image.pixelData = GetValuesAsBytes(values);
      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      std::vector<int32_t> expected{1, -24, 0, 2456};
      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntPoint expectedIntPoint(expected[i]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntPoint(
                property,
                texCoords[i],
                FIntPoint(0)),
            expectedIntPoint);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      glm::i8vec2 noData(-1, -1);
      FIntPoint defaultValue(5, 22);

      classProperty.noData = {noData[0], noData[1]};
      classProperty.defaultProperty = {defaultValue[0], defaultValue[1]};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec2> values{
          glm::i8vec2(1, 1),
          glm::i8vec2(-1, -1),
          glm::i8vec2(2, 4),
          glm::i8vec2(0, -8)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec2> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntPoint expected;
        if (values[i] == noData) {
          expected = defaultValue;
        } else {
          expected = FIntPoint(values[i][0], values[i][1]);
        }

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntPoint(
                property,
                texCoords[i],
                FIntPoint(0)),
            expected);
      }
    });
  });

  Describe("GetVector2D", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
              property,
              FVector2D::Zero(),
              FVector2D::Zero()),
          FVector2D::Zero());
    });

    It("gets from normalized glm::u8vec2 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<glm::u8vec2> values{
          glm::u8vec2(1, 1),
          glm::u8vec2(0, 255),
          glm::u8vec2(10, 4),
          glm::u8vec2(128, 8)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::u8vec2, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      for (size_t i = 0; i < texCoords.size(); i++) {
        glm::dvec2 expected = glm::dvec2(values[i][0], values[i][1]) / 255.0;
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
                property,
                texCoords[i],
                FVector2D::Zero()),
            FVector2D(expected[0], expected[1]));
      }
    });

    It("converts unnormalized glm::u8vec2 values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<glm::u8vec2> values{
          glm::u8vec2(1, 1),
          glm::u8vec2(0, 255),
          glm::u8vec2(10, 4),
          glm::u8vec2(128, 8)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::u8vec2> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
                property,
                texCoords[i],
                FVector2D::Zero()),
            FVector2D(values[i][0], values[i][1]));
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC2;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      FVector2D offset(3.0, 2.4);
      FVector2D scale(2.0, -1.0);

      classProperty.offset = {offset[0], offset[1]};
      classProperty.scale = {scale[0], scale[1]};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<glm::u8vec2> values{
          glm::u8vec2(1, 1),
          glm::u8vec2(0, 255),
          glm::u8vec2(10, 4),
          glm::u8vec2(128, 8)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::u8vec2, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FVector2D expected(
            static_cast<double>(values[i][0]) / 255.0 * scale[0] + offset[0],
            static_cast<double>(values[i][1]) / 255.0 * scale[1] + offset[1]);

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
                property,
                texCoords[i],
                FVector2D::Zero()),
            expected);
      }
    });
  });

  Describe("GetIntVector", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntVector(
              property,
              FVector2D::Zero(),
              FIntVector(0)),
          FIntVector(0));
    });

    It("gets from glm::i8vec3 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 3;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec3> values{
          glm::i8vec3(1, 1, -1),
          glm::i8vec3(-1, -1, 2),
          glm::i8vec3(0, 4, 2),
          glm::i8vec3(10, 8, 5)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec3> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntVector expected(values[i][0], values[i][1], values[i][2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntVector(
                property,
                texCoords[i],
                FIntVector(0)),
            expected);
      }
    });

    It("converts compatible values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{
          1.234f,
          -24.5f,
          std::numeric_limits<float>::lowest(),
          2456.80f};
      image.pixelData = GetValuesAsBytes(values);
      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      std::vector<int32_t> expected{1, -24, 0, 2456};
      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntVector expectedIntVector(expected[i]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntVector(
                property,
                texCoords[i],
                FIntVector(0)),
            expectedIntVector);
      }
    });

    It("gets with noData / default value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      glm::i8vec3 noData(-1, -1, 2);
      FIntVector defaultValue(1, 2, 3);

      classProperty.noData = {noData[0], noData[1], noData[2]};
      classProperty.defaultProperty = {
          defaultValue[0],
          defaultValue[1],
          defaultValue[2]};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 3;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec3> values{
          glm::i8vec3(1, 1, -1),
          glm::i8vec3(-1, -1, 2),
          glm::i8vec3(0, 4, 2),
          glm::i8vec3(10, 8, 5)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec3> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FIntVector expected;
        if (values[i] == noData) {
          expected = defaultValue;
        } else {
          expected = FIntVector(values[i][0], values[i][1], values[i][2]);
        }

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetIntVector(
                property,
                texCoords[i],
                FIntVector(0)),
            expected);
      }
    });
  });

  Describe("GetVector", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
              property,
              FVector2D::Zero(),
              FVector::Zero()),
          FVector::Zero());
    });

    It("gets from normalized glm::i8vec3 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 3;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec3> values{
          glm::i8vec3(1, 1, -1),
          glm::i8vec3(-1, -1, 2),
          glm::i8vec3(0, 4, 2),
          glm::i8vec3(10, 8, 5)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec3, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      for (size_t i = 0; i < texCoords.size(); i++) {
        glm::dvec3 expected =
            glm::dvec3(values[i][0], values[i][1], values[i][2]) / 127.0;
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
                property,
                texCoords[i],
                FVector::Zero()),
            FVector(expected[0], expected[1], expected[2]));
      }
    });

    It("converts unnormalized glm::i8vec3 values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 3;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec3> values{
          glm::i8vec3(1, 1, -1),
          glm::i8vec3(-1, -1, 2),
          glm::i8vec3(0, 4, 2),
          glm::i8vec3(10, 8, 5)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec3> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
                property,
                texCoords[i],
                FVector::Zero()),
            FVector(values[i][0], values[i][1], values[i][2]));
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC3;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.normalized = true;

      FVector offset(1.0, 2.0, 3.0);
      FVector scale(0.5, -1.0, 2.0);

      classProperty.offset = {offset[0], offset[1], offset[2]};
      classProperty.scale = {scale[0], scale[1], scale[2]};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 3;
      image.bytesPerChannel = 1;

      std::vector<glm::u8vec3> values{
          glm::u8vec3(0, 128, 255),
          glm::u8vec3(255, 255, 255),
          glm::u8vec3(10, 20, 30),
          glm::u8vec3(128, 0, 0)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::u8vec3, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FVector expected(
            static_cast<double>(values[i][0]) / 255.0 * scale[0] + offset[0],
            static_cast<double>(values[i][1]) / 255.0 * scale[1] + offset[1],
            static_cast<double>(values[i][2]) / 255.0 * scale[2] + offset[2]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
                property,
                texCoords[i],
                FVector::Zero()),
            expected);
      }
    });
  });

  Describe("GetVector4", [this]() {
    It("returns default value for invalid property", [this]() {
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
      TestEqual(
          "value",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector4(
              property,
              FVector2D::Zero(),
              FVector4::Zero()),
          FVector4::Zero());
    });

    It("gets from normalized glm::i8vec4 property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2, 3};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec4> values{
          glm::i8vec4(1, 1, -1, 1),
          glm::i8vec4(-1, -1, 2, 0),
          glm::i8vec4(0, 4, 2, -8),
          glm::i8vec4(10, 8, 5, 27)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec4, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      TestTrue(
          "IsNormalized",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
              property));

      for (size_t i = 0; i < texCoords.size(); i++) {
        glm::dvec4 expected(
            values[i][0],
            values[i][1],
            values[i][2],
            values[i][3]);
        expected /= 127.0;

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
                property,
                texCoords[i],
                FVector4::Zero()),
            FVector4(expected[0], expected[1], expected[2], expected[3]));
      }
    });

    It("converts unnormalized glm::i8vec4 values", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2, 3};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::INT8;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec4> values{
          glm::i8vec4(-1, 2, 5, 8),
          glm::i8vec4(-1, -1, 2, 0),
          glm::i8vec4(3, 5, 7, 0),
          glm::i8vec4(1, -1, -2, 5)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec4> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        glm::dvec4 expected(
            values[i][0],
            values[i][1],
            values[i][2],
            values[i][3]);

        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector4(
                property,
                texCoords[i],
                FVector4::Zero()),
            FVector4(expected[0], expected[1], expected[2], expected[3]));
      }
    });

    It("gets with offset / scale", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1, 2, 3};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::VEC4;
      classProperty.componentType = ClassProperty::ComponentType::INT8;
      classProperty.normalized = true;

      FVector4 offset(1.0, 2.0, 3.0, -1.0);
      FVector4 scale(0.5, -1.0, 2.0, 3.5);

      classProperty.offset = {offset[0], offset[1], offset[2], offset[3]};
      classProperty.scale = {scale[0], scale[1], scale[2], scale[3]};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<glm::i8vec4> values{
          glm::i8vec4(1, 1, -1, 1),
          glm::i8vec4(-1, -1, 2, 0),
          glm::i8vec4(0, 4, 2, -8),
          glm::i8vec4(10, 8, 5, 27)};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<glm::i8vec4, true> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FVector4 expected(
            values[i][0] / 127.0 * scale[0] + offset[0],
            values[i][1] / 127.0 * scale[1] + offset[1],
            values[i][2] / 127.0 * scale[2] + offset[2],
            values[i][3] / 127.0 * scale[3] + offset[3]);
        TestEqual(
            std::string("value" + std::to_string(i)).c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector(
                property,
                texCoords[i],
                FVector4::Zero()),
            expected);
      }
    });
  });

  Describe("GetArray", [this]() {
    It("returns empty array for non-array property", [this]() {
      PropertyTextureProperty propertyTextureProperty;

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
              property,
              FVector2D::Zero());
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
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);

      FITwinCesiumPropertyArray array =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
              property,
              FVector2D::Zero());
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

    It("returns array for fixed-length array property", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.array = true;
      classProperty.count = 2;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4, 5, 6, 7, 8};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<PropertyArrayView<uint8_t>> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          *classProperty.count);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
                property,
                texCoords[i]);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Uint8,
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

    It("gets with noData value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.array = true;
      classProperty.count = 2;
      classProperty.noData = {0, 0};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4, 5, 6, 0, 0};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<PropertyArrayView<uint8_t>> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          *classProperty.count);

      for (size_t i = 0; i < texCoords.size() - 1; i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
                property,
                texCoords[i]);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Uint8,
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
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
              property,
              texCoords[texCoords.size() - 1]);
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
      PropertyTextureProperty propertyTextureProperty;
      propertyTextureProperty.channels = {0, 1};

      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::UINT8;
      classProperty.array = true;
      classProperty.count = 2;
      classProperty.noData = {0, 0};
      classProperty.defaultProperty = {10, 20};

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 2;
      image.bytesPerChannel = 1;

      std::vector<uint8_t> values{1, 2, 3, 4, 5, 6, 0, 0};
      image.pixelData = GetValuesAsBytes(values);

      PropertyTexturePropertyView<PropertyArrayView<uint8_t>> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);
      TestEqual<int64>(
          "ArraySize",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArraySize(
              property),
          *classProperty.count);

      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumPropertyArray array =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetArray(
                property,
                texCoords[i]);
        int64 arraySize = UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array);
        TestEqual<int64>("array size", arraySize, *classProperty.count);
        FITwinCesiumMetadataValueType valueType(
            EITwinCesiumMetadataType::Scalar,
            EITwinCesiumMetadataComponentType::Uint8,
            false);
        TestTrue(
            "array element type",
            UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array) ==
                valueType);

        if (i == texCoords.size() - 1) {
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
      FITwinCesiumPropertyTextureProperty property;
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
              property,
              FVector2D::Zero());
      FITwinCesiumMetadataValueType valueType; // Unknown type
      TestTrue(
          "value type",
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value) ==
              valueType);
    });

    It("gets value", [this]() {
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{1, 2, 3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
                property,
                texCoords[i]);
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
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::FLOAT32;

      float offset = 1.0f;
      float scale = 2.0f;

      classProperty.offset = offset;
      classProperty.scale = scale;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<float> values{-1.1f, 2.0f, -3.5f, 4.0f};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<float> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Float32,
          false);
      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
                property,
                texCoords[i]);
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
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noData = -1;
      classProperty.noData = noData;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{-1, 2, -3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
                property,
                texCoords[i]);
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
      PropertyTextureProperty propertyTextureProperty;
      ClassProperty classProperty;
      classProperty.type = ClassProperty::Type::SCALAR;
      classProperty.componentType = ClassProperty::ComponentType::INT32;

      int32_t noData = -1;
      int32_t defaultValue = 15;

      classProperty.noData = noData;
      classProperty.defaultProperty = defaultValue;

      Sampler sampler;
      ImageCesium image;
      image.width = 2;
      image.height = 2;
      image.channels = 4;
      image.bytesPerChannel = 1;

      std::vector<int32_t> values{-1, 2, -3, 4};
      image.pixelData = GetValuesAsBytes(values);

      if (FPlatformProperties::IsLittleEndian()) {
        propertyTextureProperty.channels = {0, 1, 2, 3};
      } else {
        propertyTextureProperty.channels = {3, 2, 1, 0};
      }

      PropertyTexturePropertyView<int32_t> propertyView(
          propertyTextureProperty,
          classProperty,
          sampler,
          image);
      FITwinCesiumPropertyTextureProperty property(propertyView);
      TestEqual(
          "status",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          EITwinCesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumMetadataValueType valueType(
          EITwinCesiumMetadataType::Scalar,
          EITwinCesiumMetadataComponentType::Int32,
          false);
      for (size_t i = 0; i < texCoords.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValue(
                property,
                texCoords[i]);
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
