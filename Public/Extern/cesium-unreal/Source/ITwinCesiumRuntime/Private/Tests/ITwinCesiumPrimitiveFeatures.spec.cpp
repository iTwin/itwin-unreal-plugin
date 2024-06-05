#include "CesiumGltf/ExtensionExtMeshFeatures.h"
#include "ITwinCesiumGltfSpecUtility.h"
#include "ITwinCesiumPrimitiveFeatures.h"
#include "Misc/AutomationTest.h"

using namespace CesiumGltf;

BEGIN_DEFINE_SPEC(
    FITwinCesiumPrimitiveFeaturesSpec,
    "Cesium.Unit.PrimitiveFeatures",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)
Model model;
MeshPrimitive* pPrimitive;
ExtensionExtMeshFeatures* pExtension;
END_DEFINE_SPEC(FITwinCesiumPrimitiveFeaturesSpec)

void FITwinCesiumPrimitiveFeaturesSpec::Define() {
  Describe("Constructor", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pExtension = &pPrimitive->addExtension<ExtensionExtMeshFeatures>();
    });

    It("constructs with no feature ID sets", [this]() {
      // This is technically disallowed by the spec, but just make sure it's
      // handled reasonably.
      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      TArray<FITwinCesiumFeatureIdSet> featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
              primitiveFeatures);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 0);
    });

    It("constructs with single feature ID set", [this]() {
      FeatureId& featureID = pExtension->featureIds.emplace_back();
      featureID.featureCount = 10;

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
              primitiveFeatures);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 1);

      const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[0];
      TestEqual(
          "Feature Count",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
          static_cast<int64>(featureID.featureCount));
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          ECesiumFeatureIdSetType::Implicit);
    });

    It("constructs with multiple feature ID sets", [this]() {
      const std::vector<uint8_t> attributeIDs{0, 0, 0};
      AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 1, 0);

      const std::vector<uint8_t> textureIDs{1, 2, 3};
      const std::vector<glm::vec2> texCoords{
          glm::vec2(0, 0),
          glm::vec2(0.34, 0),
          glm::vec2(0.67, 0)};
      AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          textureIDs,
          3,
          3,
          1,
          texCoords,
          0);

      FeatureId& implicitIDs = pExtension->featureIds.emplace_back();
      implicitIDs.featureCount = 3;

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
              primitiveFeatures);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 3);

      const std::vector<ECesiumFeatureIdSetType> expectedTypes{
          ECesiumFeatureIdSetType::Attribute,
          ECesiumFeatureIdSetType::Texture,
          ECesiumFeatureIdSetType::Implicit};

      for (size_t i = 0; i < featureIDSets.Num(); i++) {
        const FITwinCesiumFeatureIdSet& featureIDSet =
            featureIDSets[static_cast<int32>(i)];
        const FeatureId& gltfFeatureID = pExtension->featureIds[i];
        TestEqual(
            "Feature Count",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(featureIDSet),
            static_cast<int64>(gltfFeatureID.featureCount));
        TestEqual(
            "FeatureIDType",
            UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
                featureIDSet),
            expectedTypes[i]);
      }
    });
  });

  Describe("GetFeatureIDSetsOfType", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pExtension = &pPrimitive->addExtension<ExtensionExtMeshFeatures>();

      const std::vector<uint8_t> attributeIDs{0, 0, 0};
      AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 1, 0);

      const std::vector<uint8_t> textureIDs{1, 2, 3};
      const std::vector<glm::vec2> texCoords{
          glm::vec2(0, 0),
          glm::vec2(0.34, 0),
          glm::vec2(0.67, 0)};
      AddFeatureIDsAsTextureToModel(
          model,
          *pPrimitive,
          textureIDs,
          3,
          3,
          1,
          texCoords,
          0);

      FeatureId& implicitIDs = pExtension->featureIds.emplace_back();
      implicitIDs.featureCount = 3;
    });

    It("gets feature ID attribute", [this]() {
      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const TArray<FITwinCesiumFeatureIdSet> featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
              primitiveFeatures,
              ECesiumFeatureIdSetType::Attribute);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 1);

      const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[0];
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          ECesiumFeatureIdSetType::Attribute);

      const FITwinCesiumFeatureIdAttribute& attribute =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
              featureIDSet);
      TestEqual(
          "AttributeStatus",
          UITwinCesiumFeatureIdAttributeBlueprintLibrary::
              GetFeatureIDAttributeStatus(attribute),
          ECesiumFeatureIdAttributeStatus::Valid);
    });

    It("gets feature ID texture", [this]() {
      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const TArray<FITwinCesiumFeatureIdSet> featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
              primitiveFeatures,
              ECesiumFeatureIdSetType::Texture);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 1);

      const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[0];
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          ECesiumFeatureIdSetType::Texture);

      const FITwinCesiumFeatureIdTexture& texture =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
              featureIDSet);
      TestEqual(
          "TextureStatus",
          UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
              texture),
          ECesiumFeatureIdTextureStatus::Valid);
    });

    It("gets implicit feature ID", [this]() {
      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const TArray<FITwinCesiumFeatureIdSet> featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
              primitiveFeatures,
              ECesiumFeatureIdSetType::Implicit);
      TestEqual("Number of FeatureIDSets", featureIDSets.Num(), 1);

      const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[0];
      TestEqual(
          "FeatureIDType",
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
              featureIDSet),
          ECesiumFeatureIdSetType::Implicit);
    });
  });

  Describe("GetFirstVertexFromFace", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pExtension = &pPrimitive->addExtension<ExtensionExtMeshFeatures>();
    });

    It("returns -1 for out-of-bounds face index", [this]() {
      const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3};
      CreateIndicesForPrimitive(
          model,
          *pPrimitive,
          AccessorSpec::ComponentType::UNSIGNED_BYTE,
          indices);

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);
      TestEqual(
          "VertexIndexForNegativeFace",
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
              primitiveFeatures,
              -1),
          -1);
      TestEqual(
          "VertexIndexForOutOfBoundsFace",
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
              primitiveFeatures,
              2),
          -1);
    });

    It("returns correct value for primitive without indices", [this]() {
      Accessor& accessor = model.accessors.emplace_back();
      accessor.count = 9;
      const int64 numFaces = accessor.count / 3;

      pPrimitive->attributes.insert(
          {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);
      for (int64 i = 0; i < numFaces; i++) {
        TestEqual(
            "VertexIndexForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
                primitiveFeatures,
                i),
            i * 3);
      }
    });

    It("returns correct value for primitive with indices", [this]() {
      const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6};
      CreateIndicesForPrimitive(
          model,
          *pPrimitive,
          AccessorSpec::ComponentType::UNSIGNED_BYTE,
          indices);

      Accessor& accessor = model.accessors.emplace_back();
      accessor.count = 7;
      pPrimitive->attributes.insert(
          {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

      const size_t numFaces = indices.size() / 3;
      for (size_t i = 0; i < numFaces; i++) {
        TestEqual(
            "VertexIndexForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
                primitiveFeatures,
                static_cast<int64>(i)),
            indices[i * 3]);
      }
    });
  });

  Describe("GetFeatureIDFromFace", [this]() {
    BeforeEach([this]() {
      model = Model();
      Mesh& mesh = model.meshes.emplace_back();
      pPrimitive = &mesh.primitives.emplace_back();
      pExtension = &pPrimitive->addExtension<ExtensionExtMeshFeatures>();
    });

    It("returns -1 for primitive with empty feature ID sets", [this]() {
      const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3};
      CreateIndicesForPrimitive(
          model,
          *pPrimitive,
          AccessorSpec::ComponentType::UNSIGNED_BYTE,
          indices);

      Accessor& accessor = model.accessors.emplace_back();
      accessor.count = 6;
      pPrimitive->attributes.insert(
          {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

      // Adds empty feature ID.
      pExtension->featureIds.emplace_back();

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);
      const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
              primitiveFeatures);

      TestEqual(
          "FeatureIDForPrimitiveWithNoSets",
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
              primitiveFeatures,
              0),
          -1);
    });

    It("returns -1 for out of bounds feature ID set index", [this]() {
      std::vector<uint8_t> attributeIDs{1, 1, 1, 1, 0, 0, 0};
      AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 2, 0);

      const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6};
      CreateIndicesForPrimitive(
          model,
          *pPrimitive,
          AccessorSpec::ComponentType::UNSIGNED_BYTE,
          indices);

      Accessor& accessor = model.accessors.emplace_back();
      accessor.count = 7;
      pPrimitive->attributes.insert(
          {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

      FITwinCesiumPrimitiveFeatures primitiveFeatures =
          FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);
      const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
              primitiveFeatures);

      TestEqual(
          "FeatureIDForOutOfBoundsSetIndex",
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
              primitiveFeatures,
              0,
              -1),
          -1);
      TestEqual(
          "FeatureIDForOutOfBoundsSetIndex",
          UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
              primitiveFeatures,
              0,
              2),
          -1);
    });

    Describe("FeatureIDAttribute", [this]() {
      It("returns -1 for out-of-bounds face index", [this]() {
        std::vector<uint8_t> attributeIDs{1, 1, 1};
        AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 1, 0);

        const std::vector<uint8_t> indices{0, 1, 2};
        CreateIndicesForPrimitive(
            model,
            *pPrimitive,
            AccessorSpec::ComponentType::UNSIGNED_BYTE,
            indices);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 3;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForNegativeFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                -1),
            -1);
        TestEqual(
            "FeatureIDForOutOfBoundsFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                2),
            -1);
      });

      It("returns correct values for primitive without indices", [this]() {
        std::vector<uint8_t> attributeIDs{1, 1, 1, 2, 2, 2, 0, 0, 0};
        AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 3, 0);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 9;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        const size_t numFaces = static_cast<size_t>(accessor.count / 3);
        for (size_t i = 0; i < numFaces; i++) {
          TestEqual(
              "FeatureIDForFace",
              UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                  primitiveFeatures,
                  static_cast<int64>(i)),
              attributeIDs[i * 3]);
        }
      });

      It("returns correct values for primitive with indices", [this]() {
        std::vector<uint8_t> attributeIDs{1, 1, 1, 1, 0, 0, 0};
        AddFeatureIDsAsAttributeToModel(model, *pPrimitive, attributeIDs, 2, 0);

        const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6};
        CreateIndicesForPrimitive(
            model,
            *pPrimitive,
            AccessorSpec::ComponentType::UNSIGNED_BYTE,
            indices);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 7;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        const size_t numFaces = indices.size() / 3;
        for (size_t i = 0; i < numFaces; i++) {
          TestEqual(
              "FeatureIDForFace",
              UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                  primitiveFeatures,
                  static_cast<int64>(i)),
              attributeIDs[i * 3]);
        }
      });
    });

    Describe("FeatureIDTexture", [this]() {
      It("returns -1 for out-of-bounds face index", [this]() {
        const std::vector<uint8_t> textureIDs{0};
        const std::vector<glm::vec2> texCoords{
            glm::vec2(0, 0),
            glm::vec2(0, 0),
            glm::vec2(0, 0)};
        AddFeatureIDsAsTextureToModel(
            model,
            *pPrimitive,
            textureIDs,
            4,
            4,
            1,
            texCoords,
            0);

        const std::vector<uint8_t> indices{0, 1, 2};
        CreateIndicesForPrimitive(
            model,
            *pPrimitive,
            AccessorSpec::ComponentType::UNSIGNED_BYTE,
            indices);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 3;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForNegativeFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                -1),
            -1);
        TestEqual(
            "FeatureIDForOutOfBoundsFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                2),
            -1);
      });

      It("returns correct values for primitive without indices", [this]() {
        const std::vector<uint8_t> textureIDs{0, 1, 2, 3};
        const std::vector<glm::vec2> texCoords{
            glm::vec2(0, 0),
            glm::vec2(0, 0),
            glm::vec2(0, 0),
            glm::vec2(0.75, 0),
            glm::vec2(0.75, 0),
            glm::vec2(0.75, 0)};
        AddFeatureIDsAsTextureToModel(
            model,
            *pPrimitive,
            textureIDs,
            4,
            4,
            1,
            texCoords,
            0);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 6;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                0),
            0);
        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                1),
            3);
      });

      It("returns correct values for primitive with indices", [this]() {
        const std::vector<uint8_t> textureIDs{0, 1, 2, 3};
        const std::vector<glm::vec2> texCoords{
            glm::vec2(0, 0),
            glm::vec2(0.25, 0),
            glm::vec2(0.5, 0),
            glm::vec2(0.75, 0)};
        AddFeatureIDsAsTextureToModel(
            model,
            *pPrimitive,
            textureIDs,
            4,
            4,
            1,
            texCoords,
            0);

        const std::vector<uint8_t> indices{0, 1, 2, 2, 0, 3};
        CreateIndicesForPrimitive(
            model,
            *pPrimitive,
            AccessorSpec::ComponentType::UNSIGNED_BYTE,
            indices);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 4;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                0),
            0);
        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                1),
            2);
      });
    });

    Describe("ImplicitFeatureIDs", [this]() {
      BeforeEach([this]() {
        FeatureId& implicitIDs = pExtension->featureIds.emplace_back();
        implicitIDs.featureCount = 6;
      });

      It("returns -1 for out-of-bounds face index", [this]() {
        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 6;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForNegativeFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                -1),
            -1);
        TestEqual(
            "FeatureIDForOutOfBoundsFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                10),
            -1);
      });

      It("returns correct values for primitive without indices", [this]() {
        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 6;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                0),
            0);
        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                1),
            3);
      });

      It("returns correct values for primitive with indices", [this]() {
        const std::vector<uint8_t> indices{2, 1, 0, 3, 4, 5};
        CreateIndicesForPrimitive(
            model,
            *pPrimitive,
            AccessorSpec::ComponentType::UNSIGNED_BYTE,
            indices);

        Accessor& accessor = model.accessors.emplace_back();
        accessor.count = 4;
        pPrimitive->attributes.insert(
            {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

        FITwinCesiumPrimitiveFeatures primitiveFeatures =
            FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                0),
            2);
        TestEqual(
            "FeatureIDForFace",
            UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                primitiveFeatures,
                1),
            3);
      });
    });

    It("gets feature ID from correct set with specified feature ID set index",
       [this]() {
         // First feature ID set is attribute
         std::vector<uint8_t> attributeIDs{1, 1, 1, 1, 0, 0, 0};
         AddFeatureIDsAsAttributeToModel(
             model,
             *pPrimitive,
             attributeIDs,
             2,
             0);

         const std::vector<uint8_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6};
         CreateIndicesForPrimitive(
             model,
             *pPrimitive,
             AccessorSpec::ComponentType::UNSIGNED_BYTE,
             indices);

         Accessor& accessor = model.accessors.emplace_back();
         accessor.count = 7;
         pPrimitive->attributes.insert(
             {"POSITION", static_cast<int32_t>(model.accessors.size() - 1)});

         // Second feature ID set is implicit
         FeatureId& implicitIDs = pExtension->featureIds.emplace_back();
         implicitIDs.featureCount = 7;

         FITwinCesiumPrimitiveFeatures primitiveFeatures =
             FITwinCesiumPrimitiveFeatures(model, *pPrimitive, *pExtension);

         const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
             UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
                 primitiveFeatures);
         TestEqual("FeatureIDSetCount", featureIDSets.Num(), 2);

         int64 setIndex = 0;
         for (size_t index = 0; index < indices.size(); index += 3) {
           std::string label("FeatureIDAttribute" + std::to_string(index));
           int64 faceIndex = static_cast<int64>(index) / 3;
           int64 featureID = static_cast<int64>(attributeIDs[indices[index]]);
           TestEqual(
               FString(label.c_str()),
               UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                   primitiveFeatures,
                   faceIndex,
                   setIndex),
               featureID);
         }

         setIndex = 1;
         for (size_t index = 0; index < indices.size(); index += 3) {
           std::string label("ImplicitFeatureID" + std::to_string(index));
           int64 faceIndex = static_cast<int64>(index) / 3;
           int64 featureID = static_cast<int64>(indices[index]);
           TestEqual(
               FString(label.c_str()),
               UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
                   primitiveFeatures,
                   faceIndex,
                   setIndex),
               featureID);
         }
       });
  });
}
