// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPrimitiveFeatures.h"
#include "CesiumGltf/ExtensionExtMeshFeatures.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"

using namespace CesiumGltf;

static FITwinCesiumPrimitiveFeatures EmptyPrimitiveFeatures;

FITwinCesiumPrimitiveFeatures::FITwinCesiumPrimitiveFeatures(
    const Model& Model,
    const MeshPrimitive& Primitive,
    const ExtensionExtMeshFeatures& Features)
    : _vertexCount(0), _primitiveMode(Primitive.mode) {
  this->_indexAccessor = CesiumGltf::getIndexAccessorView(Model, Primitive);

  auto positionIt = Primitive.attributes.find("POSITION");
  if (positionIt != Primitive.attributes.end()) {
    const Accessor& positionAccessor =
        Model.getSafe(Model.accessors, positionIt->second);
    _vertexCount = positionAccessor.count;
  }

  for (const CesiumGltf::FeatureId& FeatureId : Features.featureIds) {
    this->_featureIdSets.Add(FITwinCesiumFeatureIdSet(Model, Primitive, FeatureId));
  }
}

const FITwinCesiumPrimitiveFeatures&
UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetPrimitiveFeatures(
    const UPrimitiveComponent* component) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(component);
  if (!IsValid(pGltfComponent)) {
    return EmptyPrimitiveFeatures;
  }

  return pGltfComponent->Features;
}

const TArray<FITwinCesiumFeatureIdSet>&
UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures) {
  return PrimitiveFeatures._featureIdSets;
}

const TArray<FITwinCesiumFeatureIdSet>
UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
    ECesiumFeatureIdSetType Type) {
  TArray<FITwinCesiumFeatureIdSet> featureIdSets;
  for (int32 i = 0; i < PrimitiveFeatures._featureIdSets.Num(); i++) {
    const FITwinCesiumFeatureIdSet& featureIdSet =
        PrimitiveFeatures._featureIdSets[i];
    if (UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
            featureIdSet) == Type) {
      featureIdSets.Add(featureIdSet);
    }
  }
  return featureIdSets;
}

int64 UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetVertexCount(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures) {
  return PrimitiveFeatures._vertexCount;
}

int64 UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
    int64 FaceIndex) {
  if (FaceIndex < 0) {
    return -1;
  }

  auto VertexIndices = std::visit(
      CesiumGltf::IndicesForFaceFromAccessor{
          FaceIndex,
          PrimitiveFeatures._vertexCount,
          PrimitiveFeatures._primitiveMode},
      PrimitiveFeatures._indexAccessor);

  return VertexIndices[0];
}

int64 UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
    int64 FaceIndex,
    int64 FeatureIDSetIndex) {
  if (FeatureIDSetIndex < 0 ||
      FeatureIDSetIndex >= PrimitiveFeatures._featureIdSets.Num()) {
    return -1;
  }

  return UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
      PrimitiveFeatures._featureIdSets[FeatureIDSetIndex],
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
          PrimitiveFeatures,
          FaceIndex));
}

int64 UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromHit(
    UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
    const FHitResult& Hit,
    int64 FeatureIDSetIndex) {
  if (FeatureIDSetIndex < 0 ||
      FeatureIDSetIndex >= PrimitiveFeatures._featureIdSets.Num()) {
    return -1;
  }

  return UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
      PrimitiveFeatures._featureIdSets[FeatureIDSetIndex],
      Hit);
}
