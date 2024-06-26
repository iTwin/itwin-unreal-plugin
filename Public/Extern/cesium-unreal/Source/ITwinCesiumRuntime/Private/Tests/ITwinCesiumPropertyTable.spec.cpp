#include "CesiumGltf/ExtensionModelExtStructuralMetadata.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfSpecUtility.h"
#include "ITwinCesiumPropertyTable.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPropertyTableSpec,
    "Cesium.Unit.PropertyTable",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
Model model;
ExtensionModelExtStructuralMetadata* pExtension;
PropertyTable* pPropertyTable;
END_DEFINE_SPEC(FITwinCesiumPropertyTableSpec)

void FITwinCesiumPropertyTableSpec::Define() {
  BeforeEach([this]() {
    model = Model();
    pExtension = &model.addExtension<ExtensionModelExtStructuralMetadata>();
    pExtension->schema = Schema();
    pPropertyTable = &pExtension->propertyTables.emplace_back();
  });

  Describe("Constructor", [this]() {
    It("constructs invalid instance by default", [this]() {
      FITwinCesiumPropertyTable propertyTable;
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(0));
    });

    It("constructs invalid instance for missing schema", [this]() {
      pExtension->schema = std::nullopt;

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTableClass);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(0));
    });

    It("constructs invalid instance for missing class", [this]() {
      pPropertyTable->classProperty = "nonexistent class";

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTableClass);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(0));
    });

    It("constructs valid instance with valid property", [this]() {
      pPropertyTable->classProperty = "testClass";
      std::string propertyName("testProperty");
      std::vector<int32_t> values{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(values.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(values.size()));
    });

    It("constructs valid instance with invalid property", [this]() {
      // Even if one of its properties is invalid, the property table itself is
      // still valid.
      pPropertyTable->classProperty = "testClass";
      std::string propertyName("testProperty");
      std::vector<int8_t> values{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(values.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32, // Incorrect component type
          values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(values.size()));
    });
  });

  Describe("GetProperties", [this]() {
    BeforeEach([this]() { pPropertyTable->classProperty = "testClass"; });

    It("returns no properties for invalid property table", [this]() {
      FITwinCesiumPropertyTable propertyTable;
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable);
      const auto properties =
          UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(propertyTable);
      TestTrue("properties are empty", properties.IsEmpty());
    });

    It("gets valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(scalarValues.size()));

      const auto properties =
          UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(propertyTable);

      TestTrue(
          "has scalar property",
          properties.Contains(FString(scalarPropertyName.c_str())));
      const FITwinCesiumPropertyTableProperty& scalarProperty =
          *properties.Find(FString(scalarPropertyName.c_str()));
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(scalarProperty),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              scalarProperty),
          static_cast<int64>(scalarValues.size()));
      for (size_t i = 0; i < scalarValues.size(); i++) {
        std::string label("Property value " + std::to_string(i));
        TestEqual(
            label.c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetInteger(
                scalarProperty,
                static_cast<int64>(i)),
            scalarValues[i]);
      }

      TestTrue(
          "has vec2 property",
          properties.Contains(FString(vec2PropertyName.c_str())));
      const FITwinCesiumPropertyTableProperty& vec2Property =
          *properties.Find(FString(vec2PropertyName.c_str()));
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(vec2Property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              vec2Property),
          static_cast<int64>(vec2Values.size()));
      for (size_t i = 0; i < vec2Values.size(); i++) {
        std::string label("Property value " + std::to_string(i));
        FVector2D expected(
            static_cast<double>(vec2Values[i][0]),
            static_cast<double>(vec2Values[i][1]));
        TestEqual(
            label.c_str(),
            UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetVector2D(
                vec2Property,
                static_cast<int64>(i),
                FVector2D::Zero()),
            expected);
      }
    });

    It("gets invalid property", [this]() {
      // Even invalid properties should still be retrieved.
      std::vector<int8_t> values{0, 1, 2};
      pPropertyTable->count = static_cast<int64_t>(values.size());
      std::string propertyName("badProperty");

      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(values.size()));

      const auto properties =
          UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(propertyTable);

      TestTrue(
          "has invalid property",
          properties.Contains(FString(propertyName.c_str())));
      const FITwinCesiumPropertyTableProperty& property =
          *properties.Find(FString(propertyName.c_str()));
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
    });
  });

  Describe("GetPropertyNames", [this]() {
    BeforeEach([this]() { pPropertyTable->classProperty = "testClass"; });

    It("returns empty array for invalid property table", [this]() {
      FITwinCesiumPropertyTable propertyTable;
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable);
      const auto properties =
          UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(propertyTable);
      TestTrue("properties are empty", properties.IsEmpty());
    });

    It("gets all property names", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      std::string invalidPropertyName("badProperty");
      std::vector<int8_t> invalidPropertyValues{0, 1, 2};
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          invalidPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32, // Incorrect component type
          invalidPropertyValues);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(scalarValues.size()));

      const auto propertyNames =
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyNames(propertyTable);
      TestEqual("number of names", propertyNames.Num(), 3);
      TestTrue(
          "has scalar property name",
          propertyNames.Contains(FString(scalarPropertyName.c_str())));
      TestTrue(
          "has vec2 property name",
          propertyNames.Contains(FString(vec2PropertyName.c_str())));
      TestTrue(
          "has invalid property name",
          propertyNames.Contains(FString(invalidPropertyName.c_str())));
    });
  });

  Describe("FindProperty", [this]() {
    BeforeEach([this]() { pPropertyTable->classProperty = "testClass"; });

    It("returns invalid instance for nonexistent property", [this]() {
      std::string propertyName("testProperty");
      std::vector<int32_t> values{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(values.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(values.size()));

      FITwinCesiumPropertyTableProperty property =
          UITwinCesiumPropertyTableBlueprintLibrary::FindProperty(
              propertyTable,
              FString("nonexistent property"));
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
    });

    It("finds existing properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);
      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(scalarValues.size()));

      FITwinCesiumPropertyTableProperty scalarProperty =
          UITwinCesiumPropertyTableBlueprintLibrary::FindProperty(
              propertyTable,
              FString(scalarPropertyName.c_str()));
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(scalarProperty),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              scalarProperty),
          static_cast<int64>(scalarValues.size()));

      FITwinCesiumPropertyTableProperty vec2Property =
          UITwinCesiumPropertyTableBlueprintLibrary::FindProperty(
              propertyTable,
              FString(vec2PropertyName.c_str()));
      TestEqual(
          "PropertyTablePropertyStatus",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::
              GetPropertyTablePropertyStatus(vec2Property),
          EITwinCesiumPropertyTablePropertyStatus::Valid);
      TestEqual<int64>(
          "Size",
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertySize(
              vec2Property),
          static_cast<int64>(vec2Values.size()));
    });
  });

  Describe("GetMetadataValuesForFeature", [this]() {
    BeforeEach([this]() { pPropertyTable->classProperty = "testClass"; });

    It("returns empty map for invalid property table", [this]() {
      FITwinCesiumPropertyTable propertyTable;

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(0));

      const auto values =
          UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
              propertyTable,
              0);
      TestTrue("values map is empty", values.IsEmpty());
    });

    It("returns empty map for out-of-bounds feature IDs", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(scalarValues.size()));

      auto values =
          UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
              propertyTable,
              -1);
      TestTrue("no values for for negative feature ID", values.IsEmpty());

      values =
          UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
              propertyTable,
              10);
      TestTrue(
          "no values for positive out-of-range feature ID",
          values.IsEmpty());
    });

    It("returns values of valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(scalarValues.size()));

      for (size_t i = 0; i < scalarValues.size(); i++) {
        const auto values =
            UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
                propertyTable,
                static_cast<int64>(i));
        TestEqual("number of values", values.Num(), 2);

        TestTrue(
            "contains scalar value",
            values.Contains(FString(scalarPropertyName.c_str())));
        TestTrue(
            "contains vec2 value",
            values.Contains(FString(vec2PropertyName.c_str())));

        const FITwinCesiumMetadataValue& scalarValue =
            *values.Find(FString(scalarPropertyName.c_str()));
        TestEqual(
            "scalar value",
            UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(scalarValue, 0),
            scalarValues[i]);

        const FITwinCesiumMetadataValue& vec2Value =
            *values.Find(FString(vec2PropertyName.c_str()));
        FVector2D expected(vec2Values[i][0], vec2Values[i][1]);
        TestEqual(
            "vec2 value",
            UITwinCesiumMetadataValueBlueprintLibrary::GetVector2D(
                vec2Value,
                FVector2D::Zero()),
            expected);
      }
    });

    It("does not return value for invalid property", [this]() {
      std::vector<int8_t> propertyValues{0, 1, 2};
      pPropertyTable->count = static_cast<int64_t>(propertyValues.size());
      std::string propertyName("badProperty");

      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          propertyValues);
      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(propertyValues.size()));

      const auto values =
          UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
              propertyTable,
              0);
      TestTrue("values map is empty", values.IsEmpty());
    });
  });

  Describe("GetMetadataValuesForFeatureAsStrings", [this]() {
    BeforeEach([this]() { pPropertyTable->classProperty = "testClass"; });

    It("returns empty map for invalid property table", [this]() {
      FITwinCesiumPropertyTable propertyTable;

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::ErrorInvalidPropertyTable);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(0));

      const auto values = UITwinCesiumPropertyTableBlueprintLibrary::
          GetMetadataValuesForFeatureAsStrings(propertyTable, 0);
      TestTrue("values map is empty", values.IsEmpty());
    });

    It("returns empty map for out-of-bounds feature IDs", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64_t>(scalarValues.size()));

      auto values = UITwinCesiumPropertyTableBlueprintLibrary::
          GetMetadataValuesForFeatureAsStrings(propertyTable, -1);
      TestTrue("no values for for negative feature ID", values.IsEmpty());

      values = UITwinCesiumPropertyTableBlueprintLibrary::
          GetMetadataValuesForFeatureAsStrings(propertyTable, 10);
      TestTrue(
          "no values for positive out-of-range feature ID",
          values.IsEmpty());
    });

    It("returns values of valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::vector<int32_t> scalarValues{1, 2, 3, 4};
      pPropertyTable->count = static_cast<int64_t>(scalarValues.size());
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          scalarValues);

      std::string vec2PropertyName("vec2Property");
      std::vector<glm::vec2> vec2Values{
          glm::vec2(1.0f, 2.5f),
          glm::vec2(-0.7f, 4.9f),
          glm::vec2(8.0f, 2.0f),
          glm::vec2(11.0f, 0.0f),
      };
      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::FLOAT32,
          vec2Values);

      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(scalarValues.size()));

      for (size_t i = 0; i < scalarValues.size(); i++) {
        const auto values = UITwinCesiumPropertyTableBlueprintLibrary::
            GetMetadataValuesForFeatureAsStrings(
                propertyTable,
                static_cast<int64>(i));
        TestEqual("number of values", values.Num(), 2);

        TestTrue(
            "contains scalar value",
            values.Contains(FString(scalarPropertyName.c_str())));
        TestTrue(
            "contains vec2 value",
            values.Contains(FString(vec2PropertyName.c_str())));

        const FString& scalarValue =
            *values.Find(FString(scalarPropertyName.c_str()));
        FString expected(std::to_string(scalarValues[i]).c_str());
        TestEqual("scalar value as string", scalarValue, expected);

        const FString& vec2Value =
            *values.Find(FString(vec2PropertyName.c_str()));
        std::string expectedString(
            "X=" + std::to_string(vec2Values[i][0]) +
            " Y=" + std::to_string(vec2Values[i][1]));
        expected = FString(expectedString.c_str());
        TestEqual("vec2 value as string", vec2Value, expected);
      }
    });

    It("does not return value for invalid property", [this]() {
      std::vector<int8_t> propertyValues{0, 1, 2};
      pPropertyTable->count = static_cast<int64_t>(propertyValues.size());
      std::string propertyName("badProperty");

      AddPropertyTablePropertyToModel(
          model,
          *pPropertyTable,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          propertyValues);
      FITwinCesiumPropertyTable propertyTable(model, *pPropertyTable);

      TestEqual(
          "PropertyTableStatus",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableStatus(
              propertyTable),
          EITwinCesiumPropertyTableStatus::Valid);
      TestEqual<int64>(
          "Count",
          UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
              propertyTable),
          static_cast<int64>(propertyValues.size()));

      const auto values = UITwinCesiumPropertyTableBlueprintLibrary::
          GetMetadataValuesForFeatureAsStrings(propertyTable, 0);
      TestTrue("values map is empty", values.IsEmpty());
    });
  });
}
