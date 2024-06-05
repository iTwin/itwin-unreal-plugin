#include "ITwinCesiumMetadataValue.h"
#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "Misc/AutomationTest.h"

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPropertyArraySpec,
    "Cesium.Unit.PropertyArray",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FITwinCesiumPropertyArraySpec)

void FITwinCesiumPropertyArraySpec::Define() {
  Describe("Constructor", [this]() {
    It("constructs empty array by default", [this]() {
      FITwinCesiumPropertyArray array;
      TestEqual(
          "size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);

      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array);
      TestEqual("type", valueType.Type, ECesiumMetadataType::Invalid);
      TestEqual(
          "componentType",
          valueType.ComponentType,
          ECesiumMetadataComponentType::None);

      TestEqual(
          "blueprint type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementBlueprintType(array),
          ECesiumMetadataBlueprintType::None);
    });

    It("constructs empty array from empty view", [this]() {
      PropertyArrayView<uint8_t> arrayView;
      FITwinCesiumPropertyArray array(arrayView);
      TestEqual(
          "size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          0);

      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array);
      TestEqual("type", valueType.Type, ECesiumMetadataType::Scalar);
      TestEqual(
          "componentType",
          valueType.ComponentType,
          ECesiumMetadataComponentType::Uint8);

      TestEqual(
          "blueprint type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementBlueprintType(array),
          ECesiumMetadataBlueprintType::Byte);
    });

    It("constructs non-empty array", [this]() {
      std::vector<uint8_t> values{1, 2, 3, 4};
      PropertyArrayView<uint8_t> arrayView(std::move(values));
      FITwinCesiumPropertyArray array(arrayView);
      TestEqual(
          "size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementValueType(array);
      TestEqual("type", valueType.Type, ECesiumMetadataType::Scalar);
      TestEqual(
          "componentType",
          valueType.ComponentType,
          ECesiumMetadataComponentType::Uint8);

      TestEqual(
          "blueprint type",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetElementBlueprintType(array),
          ECesiumMetadataBlueprintType::Byte);
    });
  });

  Describe("GetValue", [this]() {
    It("gets bogus value for out-of-bounds index", [this]() {
      std::vector<uint8_t> values{1};
      PropertyArrayView<uint8_t> arrayView(std::move(values));
      FITwinCesiumPropertyArray array(arrayView);
      TestEqual(
          "size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64>(values.size()));

      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, -1);
      FITwinCesiumMetadataValueType valueType =
          UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);

      TestEqual("type", valueType.Type, ECesiumMetadataType::Invalid);
      TestEqual(
          "componentType",
          valueType.ComponentType,
          ECesiumMetadataComponentType::None);

      value = UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(array, 1);
      valueType = UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
      TestEqual("type", valueType.Type, ECesiumMetadataType::Invalid);
      TestEqual(
          "componentType",
          valueType.ComponentType,
          ECesiumMetadataComponentType::None);
    });

    It("gets value for valid index", [this]() {
      std::vector<uint8_t> values{1, 2, 3, 4};
      PropertyArrayView<uint8_t> arrayView(std::move(values));
      FITwinCesiumPropertyArray array(arrayView);
      TestEqual(
          "size",
          UITwinCesiumPropertyArrayBlueprintLibrary::GetSize(array),
          static_cast<int64>(values.size()));

      for (size_t i = 0; i < values.size(); i++) {
        FITwinCesiumMetadataValue value =
            UITwinCesiumPropertyArrayBlueprintLibrary::GetValue(
                array,
                static_cast<int64>(i));

        FITwinCesiumMetadataValueType valueType =
            UITwinCesiumMetadataValueBlueprintLibrary::GetValueType(value);
        TestEqual("type", valueType.Type, ECesiumMetadataType::Scalar);
        TestEqual(
            "componentType",
            valueType.ComponentType,
            ECesiumMetadataComponentType::Uint8);

        TestEqual(
            "byte value",
            UITwinCesiumMetadataValueBlueprintLibrary::GetByte(value, 0),
            values[i]);
      }
    });
  });
}
