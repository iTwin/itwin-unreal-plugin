// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumFeatureIdSet.h"
#include "CesiumGltf/Accessor.h"
#include "CesiumGltf/ExtensionModelExtStructuralMetadata.h"
#include "CesiumGltf/FeatureId.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"

using namespace CesiumGltf;

static FITwinCesiumFeatureIdAttribute EmptyFeatureIDAttribute;
static FITwinCesiumFeatureIdTexture EmptyFeatureIDTexture;

FITwinCesiumFeatureIdSet::FITwinCesiumFeatureIdSet(
    const Model& InModel,
    const MeshPrimitive& Primitive,
    const FeatureId& FeatureID)
    : _featureID(),
      _featureIDSetType(ECesiumFeatureIdSetType::None),
      _featureCount(FeatureID.featureCount),
      _nullFeatureID(FeatureID.nullFeatureId.value_or(-1)),
      _propertyTableIndex(FeatureID.propertyTable.value_or(-1)),
      _label(FString(FeatureID.label.value_or("").c_str())) {
  FString propertyTableName;

  // For backwards compatibility with GetFeatureTableName.
  const ExtensionModelExtStructuralMetadata* pMetadata =
      InModel.getExtension<ExtensionModelExtStructuralMetadata>();
  if (pMetadata && _propertyTableIndex >= 0) {
    size_t index = static_cast<size_t>(_propertyTableIndex);
    if (index < pMetadata->propertyTables.size()) {
      const PropertyTable& propertyTable = pMetadata->propertyTables[index];
      std::string name = propertyTable.name.value_or("");
      propertyTableName = FString(name.c_str());
    }
  }

  if (FeatureID.attribute) {
    _featureID = FITwinCesiumFeatureIdAttribute(
        InModel,
        Primitive,
        *FeatureID.attribute,
        propertyTableName);
    _featureIDSetType = ECesiumFeatureIdSetType::Attribute;

    return;
  }

  if (FeatureID.texture) {
    _featureID = FITwinCesiumFeatureIdTexture(
        InModel,
        Primitive,
        *FeatureID.texture,
        propertyTableName);
    _featureIDSetType = ECesiumFeatureIdSetType::Texture;

    return;
  }

  if (_featureCount > 0) {
    _featureIDSetType = ECesiumFeatureIdSetType::Implicit;
  }
}

const ECesiumFeatureIdSetType
UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  return FeatureIDSet._featureIDSetType;
}

const FITwinCesiumFeatureIdAttribute&
UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Attribute) {
    return std::get<FITwinCesiumFeatureIdAttribute>(FeatureIDSet._featureID);
  }

  return EmptyFeatureIDAttribute;
}

const FITwinCesiumFeatureIdTexture&
UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Texture) {
    return std::get<FITwinCesiumFeatureIdTexture>(FeatureIDSet._featureID);
  }

  return EmptyFeatureIDTexture;
}

const int64 UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  return FeatureIDSet._propertyTableIndex;
}

int64 UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureCount(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  return FeatureIDSet._featureCount;
}

const int64 UITwinCesiumFeatureIdSetBlueprintLibrary::GetNullFeatureID(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  return FeatureIDSet._nullFeatureID;
}

const FString UITwinCesiumFeatureIdSetBlueprintLibrary::GetLabel(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet) {
  return FeatureIDSet._label;
}

int64 UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet,
    int64 VertexIndex) {
  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Attribute) {
    FITwinCesiumFeatureIdAttribute attribute =
        std::get<FITwinCesiumFeatureIdAttribute>(FeatureIDSet._featureID);
    return UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDForVertex(
        attribute,
        VertexIndex);
  }

  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Texture) {
    FITwinCesiumFeatureIdTexture texture =
        std::get<FITwinCesiumFeatureIdTexture>(FeatureIDSet._featureID);
    return UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDForVertex(
        texture,
        VertexIndex);
  }

  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Implicit) {
    return (VertexIndex >= 0 && VertexIndex < FeatureIDSet._featureCount)
               ? VertexIndex
               : -1;
  }

  return -1;
}

int64 UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDFromHit(
    UPARAM(ref) const FITwinCesiumFeatureIdSet& FeatureIDSet,
    const FHitResult& Hit) {
  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Texture) {
    FITwinCesiumFeatureIdTexture texture =
        std::get<FITwinCesiumFeatureIdTexture>(FeatureIDSet._featureID);
    return UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDFromHit(
        texture,
        Hit);
  }

  // Find the first vertex of the face.
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Hit.Component);
  if (!IsValid(pGltfComponent) || !pGltfComponent->pMeshPrimitive) {
    return -1;
  }

  auto VertexIndices = std::visit(
      CesiumGltf::IndicesForFaceFromAccessor{
          Hit.FaceIndex,
          pGltfComponent->PositionAccessor.size(),
          pGltfComponent->pMeshPrimitive->mode},
      pGltfComponent->IndexAccessor);

  int64 VertexIndex = VertexIndices[0];

  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Attribute) {
    FITwinCesiumFeatureIdAttribute attribute =
        std::get<FITwinCesiumFeatureIdAttribute>(FeatureIDSet._featureID);
    return UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDForVertex(
        attribute,
        VertexIndex);
  }

  if (FeatureIDSet._featureIDSetType == ECesiumFeatureIdSetType::Implicit) {
    return (VertexIndex >= 0 && VertexIndex < FeatureIDSet._featureCount)
               ? VertexIndex
               : -1;
  }

  return -1;
}
