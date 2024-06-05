#include "CesiumGltf/ExtensionModelExtStructuralMetadata.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCesiumGltfSpecUtility.h"
#include "ITwinCesiumPropertyTexture.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPropertyTextureSpec,
    "Cesium.Unit.PropertyTexture",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
Model model;
MeshPrimitive* pPrimitive;
ExtensionModelExtStructuralMetadata* pExtension;
PropertyTexture* pPropertyTexture;
TObjectPtr<UITwinCesiumGltfComponent> pModelComponent;
TObjectPtr<UITwinCesiumGltfPrimitiveComponent> pPrimitiveComponent;

const std::vector<FVector2D> texCoords{
    FVector2D(0, 0),
    FVector2D(0.5, 0),
    FVector2D(0, 0.5),
    FVector2D(0.5, 0.5)};
END_DEFINE_SPEC(FITwinCesiumPropertyTextureSpec)

void FITwinCesiumPropertyTextureSpec::Define() {
  BeforeEach([this]() {
    model = Model();
    pExtension = &model.addExtension<ExtensionModelExtStructuralMetadata>();
    pExtension->schema = Schema();
    pPropertyTexture = &pExtension->propertyTextures.emplace_back();
  });

  Describe("Constructor", [this]() {
    It("constructs invalid instance by default", [this]() {
      FITwinCesiumPropertyTexture propertyTexture;
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTexture);
      TestTrue(
          "Properties",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .IsEmpty());
    });

    It("constructs invalid instance for missing schema", [this]() {
      pExtension->schema = std::nullopt;

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTextureClass);
      TestTrue(
          "Properties",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .IsEmpty());
    });

    It("constructs invalid instance for missing class", [this]() {
      pPropertyTexture->classProperty = "nonexistent class";

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTextureClass);
      TestTrue(
          "Properties",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .IsEmpty());
    });

    It("constructs valid instance with valid property", [this]() {
      pPropertyTexture->classProperty = "testClass";
      std::string propertyName("testProperty");
      std::array<int8_t, 4> values{1, 2, 3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          values,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual<int64>(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);
    });

    It("constructs valid instance with invalid property", [this]() {
      // Even if one of its properties is invalid, the property texture itself
      // is still valid.
      pPropertyTexture->classProperty = "testClass";
      std::string propertyName("testProperty");
      std::array<int8_t, 4> values{1, 2, 3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32, // Incorrect component type
          values,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual<int64>(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);
    });
  });

  Describe("GetProperties", [this]() {
    BeforeEach([this]() { pPropertyTexture->classProperty = "testClass"; });

    It("returns no properties for invalid property texture", [this]() {
      FITwinCesiumPropertyTexture propertyTexture;
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTexture);
      const auto properties =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(
              propertyTexture);
      TestTrue("properties are empty", properties.IsEmpty());
    });

    It("gets valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      std::string vec2PropertyName("vec2Property");
      std::array<glm::u8vec2, 4> vec2Values{
          glm::u8vec2(1, 2),
          glm::u8vec2(0, 4),
          glm::u8vec2(8, 9),
          glm::u8vec2(11, 0),
      };
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::UINT8,
          vec2Values,
          {0, 1});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);

      const auto properties =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(
              propertyTexture);

      TestTrue(
          "has scalar property",
          properties.Contains(FString(scalarPropertyName.c_str())));
      const FITwinCesiumPropertyTextureProperty& scalarProperty =
          *properties.Find(FString(scalarPropertyName.c_str()));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(scalarProperty),
          ECesiumPropertyTexturePropertyStatus::Valid);
      for (size_t i = 0; i < texCoords.size(); i++) {
        std::string label("Property value " + std::to_string(i));
        TestEqual(
            label.c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetInteger(
                scalarProperty,
                texCoords[i]),
            scalarValues[i]);
      }

      TestTrue(
          "has vec2 property",
          properties.Contains(FString(vec2PropertyName.c_str())));
      const FITwinCesiumPropertyTextureProperty& vec2Property =
          *properties.Find(FString(vec2PropertyName.c_str()));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(vec2Property),
          ECesiumPropertyTexturePropertyStatus::Valid);
      for (size_t i = 0; i < texCoords.size(); i++) {
        std::string label("Property value " + std::to_string(i));
        FVector2D expected(
            static_cast<double>(vec2Values[i][0]),
            static_cast<double>(vec2Values[i][1]));
        TestEqual(
            label.c_str(),
            UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetVector2D(
                vec2Property,
                texCoords[i],
                FVector2D::Zero()),
            expected);
      }
    });

    It("gets invalid property", [this]() {
      // Even invalid properties should still be retrieved.
      std::array<int8_t, 4> values{0, 1, 2, 3};
      std::string propertyName("badProperty");

      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          values,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);

      const auto properties =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(
              propertyTexture);

      TestTrue(
          "has invalid property",
          properties.Contains(FString(propertyName.c_str())));
      const FITwinCesiumPropertyTextureProperty& property =
          *properties.Find(FString(propertyName.c_str()));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          ECesiumPropertyTexturePropertyStatus::ErrorInvalidPropertyData);
    });
  });

  Describe("GetPropertyNames", [this]() {
    BeforeEach([this]() { pPropertyTexture->classProperty = "testClass"; });

    It("returns empty array for invalid property texture", [this]() {
      FITwinCesiumPropertyTexture propertyTexture;
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTexture);
      const auto properties =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(
              propertyTexture);
      TestTrue("properties are empty", properties.IsEmpty());
    });

    It("gets all property names", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      std::string vec2PropertyName("vec2Property");
      std::array<glm::u8vec2, 4> vec2Values{
          glm::u8vec2(1, 2),
          glm::u8vec2(0, 4),
          glm::u8vec2(8, 9),
          glm::u8vec2(11, 0),
      };
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::UINT8,
          vec2Values,
          {0, 1});

      std::string invalidPropertyName("badProperty");
      std::array<uint8_t, 4> invalidPropertyValues{0, 1, 2, 3};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          invalidPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32, // Incorrect component type
          invalidPropertyValues,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);

      const auto propertyNames =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyNames(
              propertyTexture);
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
    BeforeEach([this]() { pPropertyTexture->classProperty = "testClass"; });

    It("returns invalid instance for nonexistent property", [this]() {
      std::string propertyName("testProperty");
      std::array<int8_t, 4> values{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          values,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);

      FITwinCesiumPropertyTextureProperty property =
          UITwinCesiumPropertyTextureBlueprintLibrary::FindProperty(
              propertyTexture,
              FString("nonexistent property"));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(property),
          ECesiumPropertyTexturePropertyStatus::ErrorInvalidProperty);
    });

    It("finds existing properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      std::string vec2PropertyName("vec2Property");
      std::array<glm::u8vec2, 4> vec2Values{
          glm::u8vec2(1, 2),
          glm::u8vec2(0, 4),
          glm::u8vec2(8, 9),
          glm::u8vec2(11, 0),
      };
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::UINT8,
          vec2Values,
          {0, 1});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);
      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          2);

      FITwinCesiumPropertyTextureProperty scalarProperty =
          UITwinCesiumPropertyTextureBlueprintLibrary::FindProperty(
              propertyTexture,
              FString(scalarPropertyName.c_str()));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(scalarProperty),
          ECesiumPropertyTexturePropertyStatus::Valid);

      FITwinCesiumPropertyTextureProperty vec2Property =
          UITwinCesiumPropertyTextureBlueprintLibrary::FindProperty(
              propertyTexture,
              FString(vec2PropertyName.c_str()));
      TestEqual(
          "PropertyTexturePropertyStatus",
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
              GetPropertyTexturePropertyStatus(vec2Property),
          ECesiumPropertyTexturePropertyStatus::Valid);
    });
  });

  Describe("GetMetadataValuesForUV", [this]() {
    BeforeEach([this]() { pPropertyTexture->classProperty = "testClass"; });

    It("returns empty map for invalid property texture", [this]() {
      FITwinCesiumPropertyTexture propertyTexture;

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::ErrorInvalidPropertyTexture);
      TestTrue(
          "Properties",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .IsEmpty());

      const auto values =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesForUV(
              propertyTexture,
              FVector2D::Zero());
      TestTrue("values map is empty", values.IsEmpty());
    });

    It("returns values of valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      std::string vec2PropertyName("vec2Property");
      std::array<glm::u8vec2, 4> vec2Values{
          glm::u8vec2(1, 2),
          glm::u8vec2(0, 4),
          glm::u8vec2(8, 9),
          glm::u8vec2(11, 0),
      };
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::UINT8,
          vec2Values,
          {0, 1});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          2);

      for (size_t i = 0; i < texCoords.size(); i++) {
        const auto values =
            UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesForUV(
                propertyTexture,
                texCoords[i]);
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
      std::string propertyName("badProperty");
      std::array<int8_t, 4> data{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          data,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);

      const auto values =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesForUV(
              propertyTexture,
              FVector2D::Zero());
      TestTrue("values map is empty", values.IsEmpty());
    });
  });

  Describe("GetMetadataValuesFromHit", [this]() {
    BeforeEach([this]() {
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pPrimitive->mode = MeshPrimitive::Mode::TRIANGLES;

      std::vector<glm::vec3> positions{
          glm::vec3(-1, 0, 0),
          glm::vec3(0, 1, 0),
          glm::vec3(1, 0, 0),
          glm::vec3(-1, 3, 0),
          glm::vec3(0, 4, 0),
          glm::vec3(1, 3, 0),
      };

      CreateAttributeForPrimitive(
          model,
          *pPrimitive,
          "POSITION",
          AccessorSpec::Type::VEC3,
          AccessorSpec::ComponentType::FLOAT,
          positions);

      int32_t positionAccessorIndex =
          static_cast<int32_t>(model.accessors.size() - 1);

      // For convenience when testing, the UVs are the same as the positions
      // they correspond to. This means that the interpolated UV value should be
      // directly equal to the barycentric coordinates of the triangle.
      std::vector<glm::vec2> texCoords0{
          glm::vec2(-1, 0),
          glm::vec2(0, 1),
          glm::vec2(1, 0),
          glm::vec2(-1, 0),
          glm::vec2(0, 1),
          glm::vec2(1, 0)};

      CreateAttributeForPrimitive(
          model,
          *pPrimitive,
          "TEXCOORD_0",
          AccessorSpec::Type::VEC2,
          AccessorSpec::ComponentType::FLOAT,
          texCoords0);

      pModelComponent = NewObject<UITwinCesiumGltfComponent>();
      pPrimitiveComponent =
          NewObject<UITwinCesiumGltfPrimitiveComponent>(pModelComponent);
      pPrimitiveComponent->AttachToComponent(
          pModelComponent,
          FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

      pPrimitiveComponent->pMeshPrimitive = pPrimitive;
      pPrimitiveComponent->PositionAccessor =
          CesiumGltf::AccessorView<FVector3f>(model, positionAccessorIndex);
      pPrimitiveComponent->TexCoordAccessorMap.emplace(
          0,
          AccessorView<CesiumGltf::AccessorTypes::VEC2<float>>(
              model,
              static_cast<int32_t>(model.accessors.size() - 1)));

      pPropertyTexture->classProperty = "testClass";
    });

    It("returns empty map for invalid hit component", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);

      FHitResult Hit;
      Hit.Component = nullptr;
      Hit.FaceIndex = 0;
      Hit.Location = {0, 0, 0};

      const auto values =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesFromHit(
              propertyTexture,
              Hit);
      TestTrue("values is empty", values.IsEmpty());
    });

    It("returns values of valid properties", [this]() {
      std::string scalarPropertyName("scalarProperty");
      std::array<int8_t, 4> scalarValues{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          scalarPropertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT8,
          scalarValues,
          {0});

      std::string vec2PropertyName("vec2Property");
      std::array<glm::u8vec2, 4> vec2Values{
          glm::u8vec2(1, 2),
          glm::u8vec2(0, 4),
          glm::u8vec2(8, 9),
          glm::u8vec2(11, 0),
      };
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          vec2PropertyName,
          ClassProperty::Type::VEC2,
          ClassProperty::ComponentType::UINT8,
          vec2Values,
          {0, 1});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          2);

      FHitResult Hit;
      Hit.Component = pPrimitiveComponent;
      Hit.FaceIndex = 0;

      std::array<FVector_NetQuantize, 3> locations{
          FVector_NetQuantize(1, 0, 0),
          FVector_NetQuantize(0, -1, 0),
          FVector_NetQuantize(0, -0.25, 0)};
      std::array<int8_t, 3> expectedScalar{2, -3, -1};
      std::array<FIntPoint, 3> expectedVec2{
          FIntPoint(0, 4),
          FIntPoint(8, 9),
          FIntPoint(1, 2)};

      for (size_t i = 0; i < locations.size(); i++) {
        Hit.Location = locations[i];
        const auto values =
            UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesFromHit(
                propertyTexture,
                Hit);
        TestEqual("number of values", values.Num(), 2);

        TestTrue(
            "contains scalar value",
            values.Contains(FString(scalarPropertyName.c_str())));
        TestTrue(
            "contains vec2 value",
            values.Contains(FString(vec2PropertyName.c_str())));

        const FITwinCesiumMetadataValue* pScalarValue =
            values.Find(FString(scalarPropertyName.c_str()));
        if (pScalarValue) {
          TestEqual(
              "scalar value",
              UITwinCesiumMetadataValueBlueprintLibrary::GetInteger(
                  *pScalarValue,
                  0),
              expectedScalar[i]);
        }

        const FITwinCesiumMetadataValue* pVec2Value =
            values.Find(FString(vec2PropertyName.c_str()));
        if (pVec2Value) {
          TestEqual(
              "vec2 value",
              UITwinCesiumMetadataValueBlueprintLibrary::GetIntPoint(
                  *pVec2Value,
                  FIntPoint(0)),
              expectedVec2[i]);
        }
      }
    });

    It("does not return value for invalid property", [this]() {
      std::string propertyName("badProperty");
      std::array<int8_t, 4> data{-1, 2, -3, 4};
      AddPropertyTexturePropertyToModel(
          model,
          *pPropertyTexture,
          propertyName,
          ClassProperty::Type::SCALAR,
          ClassProperty::ComponentType::INT32,
          data,
          {0});

      FITwinCesiumPropertyTexture propertyTexture(model, *pPropertyTexture);

      TestEqual(
          "PropertyTextureStatus",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureStatus(
              propertyTexture),
          ECesiumPropertyTextureStatus::Valid);
      TestEqual(
          "Property Count",
          UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture)
              .Num(),
          1);

      FHitResult Hit;
      Hit.Component = pPrimitiveComponent;
      Hit.FaceIndex = 0;
      Hit.Location = {0, 0, 0};

      const auto values =
          UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesFromHit(
              propertyTexture,
              Hit);
      TestTrue("values map is empty", values.IsEmpty());
    });
  });
}
