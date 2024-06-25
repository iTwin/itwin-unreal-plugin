#include "ITwinCesiumFeatureIdSet.h"
#include "CesiumGltf/ExtensionExtMeshFeatures.h"
#include "CesiumGltf/ExtensionModelExtStructuralMetadata.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCesiumGltfSpecUtility.h"
#include "Misc/AutomationTest.h"

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumFeatureIdSetSpec,
    "Cesium.Unit.FeatureIdSet",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
Model model;
MeshPrimitive* pPrimitive;
TObjectPtr<UITwinCesiumGltfPrimitiveComponent> pPrimitiveComponent;
END_DEFINE_SPEC(FITwinCesiumFeatureIdSetSpec)

void FITwinCesiumFeatureIdSetSpec::Define() {
  Describe("Constructor", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pPrimitive->addExtension<ExtensionExtMeshFeatures>();
    });

    It("constructs from empty feature ID set", [this]() {
      // This is technically disallowed by the spec, but just make sure it's
      // handled reasonably.
      FeatureId featureId;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::None);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          0);
    });

    It("constructs implicit feature ID set", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::Implicit);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureId.featureCount));
    });

    It("constructs set with feature ID attribute", [this]() {
      const int64 attributeIndex = 0;
      const std::vector<uint8_t> featureIDs{0, 0, 0, 1, 1, 1};
      FeatureId& featureID = ITwinCesium::AddFeatureIDsAsAttributeToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          attributeIndex);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::Attribute);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureID.featureCount));
    });

    It("constructs set with feature ID texture", [this]() {
      const std::vector<uint8_t> featureIDs{0, 3, 1, 2};
      const std::vector<glm::vec2> texCoords{
          glm::vec2(0, 0),
          glm::vec2(0.5, 0),
          glm::vec2(0, 0.5),
          glm::vec2(0.5, 0.5)};

      FeatureId& featureId = ITwinCesium::AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          2,
          2,
          texCoords,
          0);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::Texture);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureId.featureCount));
    });

    It("constructs with null feature ID", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;
      featureId.nullFeatureId = 0;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::Implicit);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureId.featureCount));
      TestEqual(
          "NullFeatureID",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetNullFeatureID(featureIDSet),
          static_cast<int64>(*featureId.nullFeatureId));
    });

    It("constructs with property table index", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;
      featureId.propertyTable = 1;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          EITwinCesiumFeatureIdSetType::Implicit);
      TestEqual(
          "FeatureCount",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureId.featureCount));
      TestEqual(
          "PropertyTableIndex",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(
              featureIDSet),
          static_cast<int64>(*featureId.propertyTable));
    });
  });

  Describe("GetAsFeatureIDAttribute", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
    });

    It("returns empty instance for non-attribute feature ID set", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      const FITwinCesiumFeatureIdAttribute attribute =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
              featureIDSet);
      TestEqual(
          "AttributeStatus",
          UITwinCesiumFeatureIdAttributeBlueprintLibrary::
              GetFeatureIDAttributeStatus(attribute),
          EITwinCesiumFeatureIdAttributeStatus::ErrorInvalidAttribute);
      TestEqual("AttributeIndex", attribute.getAttributeIndex(), -1);
    });

    It("returns valid instance for attribute feature ID set", [this]() {
      const int64 attributeIndex = 0;
      const std::vector<uint8_t> featureIDs{0, 0, 0, 1, 1, 1};
      FeatureId& featureID = ITwinCesium::AddFeatureIDsAsAttributeToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          attributeIndex);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
      const FITwinCesiumFeatureIdAttribute attribute =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
              featureIDSet);
      TestEqual(
          "AttributeStatus",
          UITwinCesiumFeatureIdAttributeBlueprintLibrary::
              GetFeatureIDAttributeStatus(attribute),
          EITwinCesiumFeatureIdAttributeStatus::Valid);
      TestEqual(
          "AttributeIndex",
          attribute.getAttributeIndex(),
          attributeIndex);
    });
  });

  Describe("GetAsFeatureIDTexture", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
    });

    It("returns empty instance for non-texture feature ID set", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      const FITwinCesiumFeatureIdTexture texture =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
              featureIDSet);
      TestEqual(
          "TextureStatus",
          UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
              texture),
          EITwinCesiumFeatureIdTextureStatus::ErrorInvalidTexture);

      auto featureIDTextureView = texture.getFeatureIdTextureView();
      TestEqual(
          "FeatureIDTextureViewStatus",
          featureIDTextureView.status(),
          FeatureIdTextureViewStatus::ErrorUninitialized);
    });

    It("returns valid instance for texture feature ID set", [this]() {
      const std::vector<uint8_t> featureIDs{0, 3, 1, 2};
      const std::vector<glm::vec2> texCoords{
          glm::vec2(0, 0),
          glm::vec2(0.5, 0),
          glm::vec2(0, 0.5),
          glm::vec2(0.5, 0.5)};

      FeatureId& featureId = ITwinCesium::AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          2,
          2,
          texCoords,
          0);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      const FITwinCesiumFeatureIdTexture texture =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
              featureIDSet);
      TestEqual(
          "TextureStatus",
          UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
              texture),
          EITwinCesiumFeatureIdTextureStatus::Valid);

      auto featureIDTextureView = texture.getFeatureIdTextureView();
      TestEqual(
          "FeatureIDTextureViewStatus",
          featureIDTextureView.status(),
          FeatureIdTextureViewStatus::Valid);
    });
  });

  Describe("GetFeatureIDForVertex", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
    });

    It("returns -1 for empty feature ID set", [this]() {
      FITwinCesiumFeatureIdSet featureIDSet;
      TestEqual(
          "FeatureIDForVertex",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
              featureIDSet,
              0),
          -1);
    });

    It("returns -1 for out of bounds index", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      TestEqual(
          "FeatureIDForVertex",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
              featureIDSet,
              -1),
          -1);
      TestEqual(
          "FeatureIDForVertex",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
              featureIDSet,
              11),
          -1);
    });

    It("returns correct value for implicit set", [this]() {
      FeatureId featureId;
      featureId.featureCount = 10;

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);
      for (int64 i = 0; i < featureId.featureCount; i++) {
        TestEqual(
            "FeatureIDForVertex",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                featureIDSet,
                i),
            i);
      }
    });

    It("returns correct value for attribute set", [this]() {
      const int64 attributeIndex = 0;
      const std::vector<uint8_t> featureIDs{0, 0, 0, 1, 1, 1};
      FeatureId& featureID = ITwinCesium::AddFeatureIDsAsAttributeToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          attributeIndex);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
      for (size_t i = 0; i < featureIDs.size(); i++) {
        TestEqual(
            "FeatureIDForVertex",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                featureIDSet,
                static_cast<int64>(i)),
            featureIDs[i]);
      }
    });

    It("returns correct value for texture set", [this]() {
      const std::vector<uint8_t> featureIDs{0, 3, 1, 2};
      const std::vector<glm::vec2> texCoords{
          glm::vec2(0, 0),
          glm::vec2(0.5, 0),
          glm::vec2(0, 0.5),
          glm::vec2(0.5, 0.5)};

      FeatureId& featureID = ITwinCesium::AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          2,
          2,
          texCoords,
          0);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
      for (size_t i = 0; i < featureIDs.size(); i++) {
        TestEqual(
            "FeatureIDForVertex",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
                featureIDSet,
                static_cast<int64>(i)),
            featureIDs[i]);
      }
    });
  });

  Describe("GetFeatureIDFromHit", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pPrimitive->mode = CesiumGltf::MeshPrimitive::Mode::TRIANGLES;
      pPrimitiveComponent = NewObject<UITwinCesiumGltfPrimitiveComponent>();
      pPrimitiveComponent->pMeshPrimitive = pPrimitive;

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
    });

    It("returns -1 for empty feature ID set", [this]() {
      FITwinCesiumFeatureIdSet featureIDSet;
      TestEqual(
          "FeatureIDForVertex",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
              featureIDSet,
              0),
          -1);
    });

    It("returns -1 for invalid hit component", [this]() {
      FeatureId featureId;
      featureId.featureCount = 6;

      pPrimitiveComponent->PositionAccessor =
          CesiumGltf::AccessorView<FVector3f>(
              model,
              static_cast<int32_t>(model.accessors.size() - 1));

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);

      FHitResult Hit;
      Hit.Component = nullptr;
      Hit.FaceIndex = 0;

      TestEqual(
          "FeatureIDFromHit",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
              featureIDSet,
              Hit),
          -1);
    });

    It("returns correct value for texture set", [this]() {
      int32 positionAccessorIndex =
          static_cast<int32_t>(model.accessors.size() - 1);

      // For convenience when testing, the UVs are the same as the positions
      // they correspond to. This means that the interpolated UV value should be
      // directly equal to the barycentric coordinates of the triangle.
      std::vector<glm::vec2> texCoords{
          glm::vec2(-1, 0),
          glm::vec2(0, 1),
          glm::vec2(1, 0),
          glm::vec2(-1, 0),
          glm::vec2(0, 1),
          glm::vec2(1, 0)};
      const std::vector<uint8_t> featureIDs{0, 3, 1, 2};
      FeatureId& featureID = ITwinCesium::AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          featureIDs,
          4,
          2,
          2,
          texCoords,
          0);

      pPrimitiveComponent->PositionAccessor =
          CesiumGltf::AccessorView<FVector3f>(model, positionAccessorIndex);
      pPrimitiveComponent->TexCoordAccessorMap.emplace(
          0,
          AccessorView<CesiumGltf::AccessorTypes::VEC2<float>>(
              model,
              static_cast<int32_t>(model.accessors.size() - 1)));

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);

      FHitResult Hit;
      Hit.Component = pPrimitiveComponent;
      Hit.FaceIndex = 0;

      std::array<FVector_NetQuantize, 3> locations{
          FVector_NetQuantize(1, 0, 0),
          FVector_NetQuantize(0, -1, 0),
          FVector_NetQuantize(0.0, -0.25, 0)};
      std::array<int64, 3> expected{3, 1, 0};

      for (size_t i = 0; i < locations.size(); i++) {
        Hit.Location = locations[i];
        TestEqual(
            "FeatureIDFromHit",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
                featureIDSet,
                Hit),
            expected[i]);
      }
    });

    It("returns correct value for implicit set", [this]() {
      FeatureId featureId;
      featureId.featureCount = 6;

      pPrimitiveComponent->PositionAccessor =
          CesiumGltf::AccessorView<FVector3f>(
              model,
              static_cast<int32_t>(model.accessors.size() - 1));

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);

      FHitResult Hit;
      Hit.Component = pPrimitiveComponent;
      Hit.FaceIndex = 0;

      std::array<int32, 3> faceIndices{0, 1, 0};
      std::array<FVector_NetQuantize, 3> locations{
          FVector_NetQuantize(1, 0, 0),
          FVector_NetQuantize(0, -4, 0),
          FVector_NetQuantize(-1, 0, 0)};
      std::array<int64, 3> expected{0, 3, 0};
      for (size_t i = 0; i < locations.size(); i++) {
        Hit.FaceIndex = faceIndices[i];
        Hit.Location = locations[i];
        TestEqual(
            "FeatureIDFromHit",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
                featureIDSet,
                Hit),
            expected[i]);
      }
    });

    It("returns correct value for attribute set", [this]() {
      int32_t positionAccessorIndex =
          static_cast<int32_t>(model.accessors.size() - 1);
      const int64 attributeIndex = 0;
      const std::vector<uint8_t> featureIDs{0, 0, 0, 1, 1, 1};
      FeatureId& featureId = ITwinCesium::AddFeatureIDsAsAttributeToModel(
          model,
          *pPrimitive,
          featureIDs,
          2,
          attributeIndex);

      pPrimitiveComponent->PositionAccessor =
          CesiumGltf::AccessorView<FVector3f>(model, positionAccessorIndex);

      FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureId);

      FHitResult Hit;
      Hit.Component = pPrimitiveComponent;
      Hit.FaceIndex = 0;
      Hit.Location = FVector_NetQuantize(0, -1, 0);
      TestEqual(
          "FeatureIDFromHit",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
              featureIDSet,
              Hit),
          0);

      Hit.FaceIndex = 1;
      Hit.Location = FVector_NetQuantize(0, -4, 0);
      TestEqual(
          "FeatureIDFromHit",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
              featureIDSet,
              Hit),
          1);
    });
  });

  Describe("Deprecated", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
    });

    It("backwards compatibility for FITwinCesiumFeatureIdAttribute.GetFeatureTableName",
       [this]() {
         const int64 attributeIndex = 0;
         const std::vector<uint8_t> featureIDs{0, 0, 0, 1, 1, 1};
         FeatureId& featureID = ITwinCesium::AddFeatureIDsAsAttributeToModel(
             model,
             *pPrimitive,
             featureIDs,
             4,
             attributeIndex);
         featureID.propertyTable = 0;

         const std::string expectedName = "PropertyTableName";

         ExtensionModelExtStructuralMetadata& metadataExtension =
             model.addExtension<ExtensionModelExtStructuralMetadata>();
         PropertyTable& propertyTable =
             metadataExtension.propertyTables.emplace_back();
         propertyTable.name = expectedName;

         FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
         const FITwinCesiumFeatureIdAttribute attribute =
             UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
                 featureIDSet);
         TestEqual(
             "AttributeStatus",
             UITwinCesiumFeatureIdAttributeBlueprintLibrary::
                 GetFeatureIDAttributeStatus(attribute),
             EITwinCesiumFeatureIdAttributeStatus::Valid);
         TestEqual(
             "GetFeatureTableName",
             UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureTableName(
                 attribute),
             FString(expectedName.c_str()));
       });

    It("backwards compatibility for FITwinCesiumFeatureIdTexture.GetFeatureTableName",
       [this]() {
         const std::vector<uint8_t> featureIDs{0, 3, 1, 2};
         const std::vector<glm::vec2> texCoords{
             glm::vec2(0, 0),
             glm::vec2(0.5, 0),
             glm::vec2(0, 0.5),
             glm::vec2(0.5, 0.5)};

         FeatureId& featureID = ITwinCesium::AddFeatureIDsAsTextureToModel(
             model,
             *pPrimitive,
             featureIDs,
             4,
             2,
             2,
             texCoords,
             0);
         featureID.propertyTable = 0;

         const std::string expectedName = "PropertyTableName";

         ExtensionModelExtStructuralMetadata& metadataExtension =
             model.addExtension<ExtensionModelExtStructuralMetadata>();
         PropertyTable& propertyTable =
             metadataExtension.propertyTables.emplace_back();
         propertyTable.name = expectedName;

         FITwinCesiumFeatureIdSet featureIDSet(model, *pPrimitive, featureID);
         const FITwinCesiumFeatureIdTexture texture =
             UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
                 featureIDSet);
         TestEqual(
             "TextureStatus",
             UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
                 texture),
             EITwinCesiumFeatureIdTextureStatus::Valid);
         TestEqual(
             "GetFeatureTableName",
             UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureTableName(
                 texture),
             FString(expectedName.c_str()));
       });
  });
}
