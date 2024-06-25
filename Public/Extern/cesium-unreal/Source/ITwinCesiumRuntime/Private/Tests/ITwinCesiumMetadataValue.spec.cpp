#include "ITwinCesiumMetadataValue.h"
#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "Misc/AutomationTest.h"

#include <limits>

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumMetadataValueSpec,
    "Cesium.Unit.MetadataValue",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FITwinCesiumMetadataValueSpec)

void FITwinCesiumMetadataValueSpec::Define() {
  Describe("Constructor", [this]() {
    It("constructs value with unknown type by default", [this]() {
      FITwinCesiumMetadataValue value;
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Invalid);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::None);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs boolean value with correct type", [this]() {
      FITwinCesiumMetadataValue value(true);
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Boolean);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::None);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs scalar value with correct type", [this]() {
      FITwinCesiumMetadataValue value(1.6);
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Scalar);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::Float64);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs vecN value with correct type", [this]() {
      FITwinCesiumMetadataValue value(glm::u8vec4(1, 2, 3, 4));
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Vec4);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::Uint8);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs matN value with correct type", [this]() {
      FITwinCesiumMetadataValue value(glm::imat2x2(-1, -2, 3, 0));
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Mat2);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::Int32);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs string value with correct type", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("Hello"));
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::String);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::None);
      TestFalse("IsArray", valueType.bIsArray);
    });

    It("constructs array value with correct type", [this]() {
      PropertyArrayView<uint8_t> arrayView;
      FITwinCesiumMetadataValue value(arrayView);
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("Type", valueType.Type, EITwinCesiumMetadataType::Scalar);
      TestEqual(
          "ComponentType",
          valueType.ComponentType,
          EITwinCesiumMetadataComponentType::Uint8);
      TestTrue("IsArray", valueType.bIsArray);
    });
  });

  Describe("GetBoolean", [this]() {
    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestTrue(
          "true",
          UITwinCesiumMetadataValueBlueprintLibrary::GetBoolean(value, false));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(1.0f);
      TestTrue(
          "true",
          UITwinCesiumMetadataValueBlueprintLibrary::GetBoolean(value, false));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("true"));
      TestTrue(
          "true",
          UITwinCesiumMetadataValueBlueprintLibrary::GetBoolean(value, false));
    });
  });

  Describe("GetByte", [this]() {
    It("gets from uint8", [this]() {
      FITwinCesiumMetadataValue value(static_cast<uint8_t>(23));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          23);
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          1);
    });

    It("gets from in-range integers", [this]() {
      FITwinCesiumMetadataValue value(static_cast<int32_t>(255));
      TestEqual(
          "larger signed integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          255);

      value = FITwinCesiumMetadataValue(static_cast<uint64_t>(255));
      TestEqual(
          "larger unsigned integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          255);
    });

    It("gets from in-range floating-point numbers", [this]() {
      FITwinCesiumMetadataValue value(254.5f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          254);

      value = FITwinCesiumMetadataValue(0.85);
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 255),
          0);
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("123"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          123);
    });

    It("returns default value for out-of-range numbers", [this]() {
      FITwinCesiumMetadataValue value(static_cast<int8_t>(-1));
      TestEqual(
          "negative integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 255),
          255);

      value = FITwinCesiumMetadataValue(-1.0);
      TestEqual(
          "negative floating-point number",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 255),
          255);

      value = FITwinCesiumMetadataValue(256);
      TestEqual(
          "positive integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          0);

      value = FITwinCesiumMetadataValue(255.5f);
      TestEqual(
          "positive floating-point number",
          UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
          0);
    });
  });

  Describe("GetInteger", [this]() {
    It("gets from in-range integers", [this]() {
      FITwinCesiumMetadataValue value(static_cast<int32_t>(123));
      TestEqual(
          "int32_t",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          123);

      value = FITwinCesiumMetadataValue(static_cast<int64_t>(-123));
      TestEqual(
          "larger signed integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          -123);

      value = FITwinCesiumMetadataValue(static_cast<uint64_t>(456));
      TestEqual(
          "larger unsigned integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          456);
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(false);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, -1),
          0);
    });

    It("gets from in-range floating point number", [this]() {
      FITwinCesiumMetadataValue value(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          1234);

      value = FITwinCesiumMetadataValue(-78.9);
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          -78);
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("-1234"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          -1234);
    });

    It("returns default value for out-of-range numbers", [this]() {
      FITwinCesiumMetadataValue value(std::numeric_limits<int64_t>::min());
      TestEqual(
          "negative integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          0);

      value = FITwinCesiumMetadataValue(std::numeric_limits<float>::lowest());
      TestEqual(
          "negative floating-point number",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          0);

      value = FITwinCesiumMetadataValue(std::numeric_limits<int64_t>::max());
      TestEqual(
          "positive integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          0);

      value = FITwinCesiumMetadataValue(std::numeric_limits<float>::max());
      TestEqual(
          "positive floating-point number",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(value, 0),
          0);
    });
  });

  Describe("GetInteger64", [this]() {
    const int64_t defaultValue = static_cast<int64_t>(0);

    It("gets from in-range integers", [this, defaultValue]() {
      FITwinCesiumMetadataValue value(std::numeric_limits<int64_t>::max() - 1);
      TestEqual<int64>(
          "int64_t",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          std::numeric_limits<int64>::max() - 1);

      value = FITwinCesiumMetadataValue(static_cast<int16_t>(-12345));
      TestEqual<int64>(
          "smaller signed integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(-12345));

      value = FITwinCesiumMetadataValue(static_cast<uint8_t>(255));
      TestEqual<int64>(
          "smaller unsigned integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(255));
    });

    It("gets from boolean", [this, defaultValue]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual<int64>(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(1));
    });

    It("gets from in-range floating point number", [this, defaultValue]() {
      FITwinCesiumMetadataValue value(1234.56f);
      TestEqual<int64>(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(1234));

      value = FITwinCesiumMetadataValue(-78.9);
      TestEqual<int64>(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(-78));
    });

    It("gets from string", [this, defaultValue]() {
      FITwinCesiumMetadataValue value(std::string_view("-1234"));
      TestEqual<int64>(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
              value,
              defaultValue),
          static_cast<int64_t>(-1234));
    });

    It("returns default value for out-of-range numbers",
       [this, defaultValue]() {
         FITwinCesiumMetadataValue value(std::numeric_limits<float>::lowest());
         TestEqual<int64>(
             "negative floating-point number",
             UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
                 value,
                 defaultValue),
             defaultValue);

         value = FITwinCesiumMetadataValue(std::numeric_limits<uint64_t>::max());
         TestEqual<int64>(
             "positive integer",
             UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
                 value,
                 defaultValue),
             defaultValue);

         value = FITwinCesiumMetadataValue(std::numeric_limits<float>::max());
         TestEqual<int64>(
             "positive floating-point number",
             UITwinCesiumMetadataValueBlueprintLibrary::GetInteger64(
                 value,
                 defaultValue),
             defaultValue);
       });
  });

  Describe("GetUnsignedInteger64", [this]() {
      const uint64_t defaultValue = static_cast<uint64_t>(0);

      It("gets from in-range integers", [this, defaultValue]() {
          FITwinCesiumMetadataValue value(std::numeric_limits<uint64_t>::max() - 1);
          TestEqual<uint64_t>(
              "uint64_t",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              std::numeric_limits<uint64_t>::max() - 1);

          value = FITwinCesiumMetadataValue(std::numeric_limits<int64_t>::max() - 1);
          TestEqual<uint64_t>(
              "uint64_t",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - 1));

          value = FITwinCesiumMetadataValue(static_cast<int16_t>(12345));
          TestEqual<uint64_t>(
              "smaller signed integer",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(12345));

          value = FITwinCesiumMetadataValue(static_cast<uint8_t>(255));
          TestEqual<uint64_t>(
              "smaller unsigned integer",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(255));
      });

      It("gets from boolean", [this, defaultValue]() {
          FITwinCesiumMetadataValue value(true);
          TestEqual<uint64_t>(
              "value",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(1));
      });

      It("gets from in-range floating point number", [this, defaultValue]() {
          FITwinCesiumMetadataValue value(1234.56f);
          TestEqual<uint64_t>(
              "float",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(1234));
      });

      It("gets from string", [this, defaultValue]() {
          FITwinCesiumMetadataValue value(std::string_view("1234"));
          TestEqual<uint64_t>(
              "value",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              static_cast<uint64_t>(1234));
      });

      It("returns default value for out-of-range numbers",
          [this, defaultValue]() {
          FITwinCesiumMetadataValue value(-5);
          TestEqual<uint64_t>(
              "negative integer",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              defaultValue);

          value = FITwinCesiumMetadataValue(-59.62f);
          TestEqual<uint64_t>(
              "negative floating-point number",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              defaultValue);

          value = FITwinCesiumMetadataValue(std::numeric_limits<float>::max());
          TestEqual<uint64_t>(
              "positive floating-point number",
              UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
                  value,
                  defaultValue),
              defaultValue);
      });
  });

  Describe("GetFloat", [this]() {
    It("gets from in-range floating point number", [this]() {
      FITwinCesiumMetadataValue value(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0.0f),
          1234.56f);

      double doubleValue = -78.9;
      value = FITwinCesiumMetadataValue(doubleValue);
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0.0f),
          static_cast<float>(doubleValue));
    });

    It("gets from integer", [this]() {
      FITwinCesiumMetadataValue value(-12345);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0.0f),
          static_cast<float>(-12345));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, -1.0f),
          1.0f);
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("-123.01"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0.0f),
          static_cast<float>(-123.01));
    });

    It("returns default value for out-of-range numbers", [this]() {
      FITwinCesiumMetadataValue value(std::numeric_limits<double>::lowest());
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat(value, 0.0f),
          0.0f);
    });
  });

  Describe("GetFloat64", [this]() {
    It("gets from floating point number", [this]() {
      FITwinCesiumMetadataValue value(78.91);
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          78.91);

      value = FITwinCesiumMetadataValue(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          static_cast<double>(1234.56f));
    });

    It("gets from integer", [this]() {
      FITwinCesiumMetadataValue value(-12345);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0f),
          static_cast<double>(-12345));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, -1.0),
          1.0);
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("-1234.05"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetFloat64(value, 0.0),
          -1234.05);
    });
  });

  Describe("GetIntPoint", [this]() {
    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::ivec2(1, -2));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(1, -2));

      value = FITwinCesiumMetadataValue(glm::vec2(-5.2f, 6.68f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(-5, 6));
    });

    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::u8vec3(4, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(4, 5));

      value = FITwinCesiumMetadataValue(glm::vec3(-5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(-5, 6));
    });

    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::i16vec4(4, 2, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(4, 2));

      value = FITwinCesiumMetadataValue(glm::vec4(1.01f, -5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(1, -5));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(123);
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(123));

      value = FITwinCesiumMetadataValue(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(1234));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(-1)),
          FIntPoint(1));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1 Y=2"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(1, 2));
    });
  });

  Describe("GetVector2D", [this]() {
    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::ivec2(1, -2));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(static_cast<double>(1), static_cast<double>(-2)));

      value = FITwinCesiumMetadataValue(glm::dvec2(-5.2, 6.68));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(-5.2, 6.68));
    });

    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::u8vec3(4, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(static_cast<double>(4), static_cast<double>(5)));

      value = FITwinCesiumMetadataValue(glm::dvec3(-5.2, 6.68, -23));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(-5.2, 6.68));
    });

    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::i16vec4(4, 2, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(static_cast<double>(4), static_cast<double>(2)));

      value = FITwinCesiumMetadataValue(glm::dvec4(1.01, -5.2, 6.68, -23.8));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(1.01, -5.2));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(123);
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(static_cast<double>(123)));

      value = FITwinCesiumMetadataValue(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(static_cast<double>(1234.56f)));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D(-1.0)),
          FVector2D(1.0));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1.5 Y=2.5"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
              value,
              FVector2D::Zero()),
          FVector2D(1.5, 2.5));
    });
  });

  Describe("GetIntVector", [this]() {
    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::u8vec3(4, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(4, 5, 12));

      value = FITwinCesiumMetadataValue(glm::vec3(-5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(-5, 6, -23));
    });

    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::ivec2(1, -2));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(1, -2, 0));

      value = FITwinCesiumMetadataValue(glm::vec2(-5.2f, 6.68f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(-5, 6, 0));
    });

    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::i16vec4(4, 2, 5, 12));
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(4, 2, 5));

      value = FITwinCesiumMetadataValue(glm::vec4(1.01f, -5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(1, -5, 6));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(123);
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(123));

      value = FITwinCesiumMetadataValue(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(0)),
          FIntVector(1234));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntVector(
              value,
              FIntVector(-1)),
          FIntVector(1));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1 Y=2"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
              value,
              FIntPoint(0)),
          FIntPoint(1, 2));
    });
  });

  Describe("GetVector3f", [this]() {
    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::vec3(-5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f::Zero()),
          FVector3f(-5.2f, 6.68f, -23.8f));
    });

    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::vec2(-5.2f, 6.68f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f::Zero()),
          FVector3f(-5.2f, 6.68f, 0.0f));
    });

    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::vec4(1.01f, -5.2f, 6.68f, -23.8f));
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f::Zero()),
          FVector3f(1.01f, -5.2f, 6.68f));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(1234.56f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f::Zero()),
          FVector3f(1234.56f));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f(-1.0f)),
          FVector3f(1.0f));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1 Y=2 Z=3"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector3f(
              value,
              FVector3f::Zero()),
          FVector3f(1, 2, 3));
    });
  });

  Describe("GetVector", [this]() {
    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec3(-5.2, 6.68, -23.8));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
              value,
              FVector::Zero()),
          FVector(-5.2, 6.68, -23.8));
    });

    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec2(-5.2, 6.68));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
              value,
              FVector::Zero()),
          FVector(-5.2, 6.68, 0.0));
    });

    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec4(1.01, -5.2, 6.68, -23.8));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
              value,
              FVector::Zero()),
          FVector(1.01, -5.2, 6.68));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(12345);
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
              value,
              FVector::Zero()),
          FVector(static_cast<double>(12345)));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(value, FVector(-1.0)),
          FVector(1.0));
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1.5 Y=2.5 Z=3.5"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector(
              value,
              FVector::Zero()),
          FVector(1.5, 2.5, 3.5));
    });
  });

  Describe("GetVector4", [this]() {
    It("gets from vec4", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec4(1.01, -5.2, 6.68, -23.8));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(1.01, -5.2, 6.68, -23.8));
    });

    It("gets from vec3", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec3(-5.2, 6.68, -23.8));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(-5.2, 6.68, -23.8, 0.0));
    });

    It("gets from vec2", [this]() {
      FITwinCesiumMetadataValue value(glm::dvec2(-5.2, 6.68));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(-5.2, 6.68, 0.0, 0.0));
    });

    It("gets from scalar", [this]() {
      float floatValue = 7.894f;
      double doubleValue = static_cast<double>(floatValue);
      FITwinCesiumMetadataValue value(floatValue);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(doubleValue, doubleValue, doubleValue, doubleValue));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(false);
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4(-1.0)),
          FVector4::Zero());
    });

    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("X=1.5 Y=2.5 Z=3.5 W=4.5"));
      TestEqual(
          "value without W-component",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(1.5, 2.5, 3.5, 4.5));

      value = FITwinCesiumMetadataValue(std::string_view("X=1.5 Y=2.5 Z=3.5"));
      TestEqual(
          "value without W-component",
          UITwinCesiumMetadataValueBlueprintLibrary::GetVector4(
              value,
              FVector4::Zero()),
          FVector4(1.5, 2.5, 3.5, 1.0));
    });
  });

  Describe("GetMatrix", [this]() {
    It("gets from mat4", [this]() {
      // clang-format off
      glm::dmat4 input = glm::dmat4(
           1.0,  2.0, 3.0, 4.0,
           5.0,  6.0, 7.0, 8.0,
           9.0, 11.0, 4.0, 1.0,
          10.0, 12.0, 3.0, 1.0);
      // clang-format on
      input = glm::transpose(input);

      FITwinCesiumMetadataValue value(input);
      FMatrix expected(
          FPlane4d(1.0, 2.0, 3.0, 4.0),
          FPlane4d(5.0, 6.0, 7.0, 8.0),
          FPlane4d(9.0, 11.0, 4.0, 1.0),
          FPlane4d(10.0, 12.0, 3.0, 1.0));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
              value,
              FMatrix::Identity),
          expected);
    });

    It("gets from mat3", [this]() {
      // clang-format off
      glm::dmat3 input = glm::dmat3(
          1.0, 2.0, 3.0,
          4.0, 5.0, 6.0,
          7.0, 8.0, 9.0);
      // clang-format on
      input = glm::transpose(input);

      FITwinCesiumMetadataValue value(input);
      FMatrix expected(
          FPlane4d(1.0, 2.0, 3.0, 0.0),
          FPlane4d(4.0, 5.0, 6.0, 0.0),
          FPlane4d(7.0, 8.0, 9.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
              value,
              FMatrix::Identity),
          expected);
    });

    It("gets from mat2", [this]() {
      // clang-format off
      glm::dmat2 input = glm::dmat2(
          1.0, 2.0,
          3.0, 4.0);
      // clang-format on
      input = glm::transpose(input);

      FITwinCesiumMetadataValue value(input);
      FMatrix expected(
          FPlane4d(1.0, 2.0, 0.0, 0.0),
          FPlane4d(3.0, 4.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
              value,
              FMatrix::Identity),
          expected);
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(7.894);
      FMatrix expected(
          FPlane4d(7.894, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 7.894, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 7.894, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 7.894));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
              value,
              FMatrix::Identity),
          expected);
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(false);
      FMatrix expected(
          FPlane4d(0.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0),
          FPlane4d(0.0, 0.0, 0.0, 0.0));
      TestEqual(
          "double",
          UITwinCesiumMetadataValueBlueprintLibrary::GetMatrix(
              value,
              FMatrix::Identity),
          expected);
    });
  });

  Describe("GetFString", [this]() {
    It("gets from string", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("Hello"));
      TestEqual(
          "value",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString("Hello"));
    });

    It("gets from boolean", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestEqual(
          "true",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString("true"));

      value = FITwinCesiumMetadataValue(false);
      TestEqual(
          "false",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString("false"));
    });

    It("gets from scalar", [this]() {
      FITwinCesiumMetadataValue value(1234);
      TestEqual(
          "integer",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString("1234"));

      value = FITwinCesiumMetadataValue(1.2345f);
      TestEqual(
          "float",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString(std::to_string(1.2345f).c_str()));
    });

    It("gets from vecN", [this]() {
      FITwinCesiumMetadataValue value(glm::ivec4(1, 2, 3, 4));
      TestEqual(
          "vec4",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString("X=1 Y=2 Z=3 W=4"));
    });

    It("gets from matN", [this]() {
      // clang-format off
      FITwinCesiumMetadataValue value(
          glm::i32mat4x4(
            1,   2,  3, -7,
            4,   5,  6, 88,
            0,  -1, -4,  4,
            2 , 70,  8,  9));
      // clang-format on
      std::string expected("[1 4 0 2] [2 5 -1 70] [3 6 -4 8] [-7 88 4 9]");
      TestEqual(
          "mat4",
          UITwinCesiumMetadataValueBlueprintLibrary::GetString(value, FString("")),
          FString(expected.c_str()));
    });
  });

  Describe("GetArray", [this]() {
    It("gets empty array from non-array value", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("not an array"));
      FITwinCesiumPropertyArray array =
          UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);
      TestEqual(
          "array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64>(0));

      FITwinCesiumMetadataValueType elementType =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array);
      TestEqual(
          "array element type",
          elementType.Type,
          EITwinCesiumMetadataType::Invalid);
      TestEqual(
          "array element component type",
          elementType.ComponentType,
          EITwinCesiumMetadataComponentType::None);
    });

    It("gets array from array value", [this]() {
      std::vector<uint8_t> arrayValues{1, 2};
      PropertyArrayView<uint8_t> arrayView(std::move(arrayValues));

      FITwinCesiumMetadataValue value(arrayView);
      FITwinCesiumPropertyArray array =
          UITwinCesiumMetadataValueBlueprintLibrary::GetArray(value);
      TestEqual(
          "array size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64>(arrayValues.size()));

      FITwinCesiumMetadataValueType elementType =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array);
      TestEqual(
          "array element type",
          elementType.Type,
          EITwinCesiumMetadataType::Scalar);
      TestEqual(
          "array element component type",
          elementType.ComponentType,
          EITwinCesiumMetadataComponentType::Uint8);
    });
  });

  Describe("IsEmpty", [this]() {
    It("returns true for default value", [this]() {
      FITwinCesiumMetadataValue value;
      TestTrue("IsEmpty", UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for boolean value", [this]() {
      FITwinCesiumMetadataValue value(true);
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for scalar value", [this]() {
      FITwinCesiumMetadataValue value(1.6);
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for vecN value", [this]() {
      FITwinCesiumMetadataValue value(glm::u8vec4(1, 2, 3, 4));
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for matN value", [this]() {
      FITwinCesiumMetadataValue value(glm::imat2x2(-1, -2, 3, 0));
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for string value", [this]() {
      FITwinCesiumMetadataValue value(std::string_view("Hello"));
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });

    It("returns false for array value", [this]() {
      PropertyArrayView<uint8_t> arrayView;
      FITwinCesiumMetadataValue value(arrayView);
      TestFalse(
          "IsEmpty",
          UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value));
    });
  });

  Describe("GetValuesAsStrings", [this]() {
    It("returns empty map if input is empty", [this]() {
      TMap<FString, FITwinCesiumMetadataValue> values;
      const auto strings =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValuesAsStrings(values);
      TestTrue("values map is empty", strings.IsEmpty());
    });

    It("returns values as strings", [this]() {
      TMap<FString, FITwinCesiumMetadataValue> values;
      values.Add({"scalar", FITwinCesiumMetadataValue(-1)});
      values.Add({"vec2", FITwinCesiumMetadataValue(glm::u8vec2(2, 3))});
      values.Add(
          {"array", FITwinCesiumMetadataValue(PropertyArrayView<uint8>({1, 2, 3}))});

      const auto strings =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValuesAsStrings(values);
      TestEqual("map count", values.Num(), strings.Num());

      const FString* pString = strings.Find(FString("scalar"));
      TestTrue("has scalar value", pString != nullptr);
      TestEqual("scalar value as string", *pString, FString("-1"));

      pString = strings.Find(FString("vec2"));
      TestTrue("has vec2 value", pString != nullptr);
      TestEqual("vec2 value as string", *pString, FString("X=2 Y=3"));

      pString = strings.Find(FString("array"));
      TestTrue("has array value", pString != nullptr);
      TestEqual("array value as string", *pString, FString());
    });
  });
}
