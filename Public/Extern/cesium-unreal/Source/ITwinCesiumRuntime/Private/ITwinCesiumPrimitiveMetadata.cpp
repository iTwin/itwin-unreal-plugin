// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumPrimitiveMetadata.h"
#include "CesiumGltf/AccessorView.h"
#include "CesiumGltf/ExtensionMeshPrimitiveExtStructuralMetadata.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"

static FITwinCesiumPrimitiveMetadata EmptyPrimitiveMetadata;

FITwinCesiumPrimitiveMetadata::FITwinCesiumPrimitiveMetadata(
    const CesiumGltf::MeshPrimitive& Primitive,
    const CesiumGltf::ExtensionMeshPrimitiveExtStructuralMetadata& Metadata)
    : _propertyTextureIndices(), _propertyAttributeIndices() {
  this->_propertyTextureIndices.Reserve(Metadata.propertyTextures.size());
  for (const int64 propertyTextureIndex : Metadata.propertyTextures) {
    this->_propertyTextureIndices.Emplace(propertyTextureIndex);
  }

  this->_propertyAttributeIndices.Reserve(Metadata.propertyAttributes.size());
  for (const int64 propertyAttributeIndex : Metadata.propertyAttributes) {
    this->_propertyAttributeIndices.Emplace(propertyAttributeIndex);
  }
}

const FITwinCesiumPrimitiveMetadata&
UITwinCesiumPrimitiveMetadataBlueprintLibrary::GetPrimitiveMetadata(
    const UPrimitiveComponent* component) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(component);
  if (!IsValid(pGltfComponent)) {
    return EmptyPrimitiveMetadata;
  }

  return pGltfComponent->Metadata;
}

const TArray<int64>&
UITwinCesiumPrimitiveMetadataBlueprintLibrary::GetPropertyTextureIndices(
    UPARAM(ref) const FITwinCesiumPrimitiveMetadata& PrimitiveMetadata) {
  return PrimitiveMetadata._propertyTextureIndices;
}

const TArray<int64>&
UITwinCesiumPrimitiveMetadataBlueprintLibrary::GetPropertyAttributeIndices(
    UPARAM(ref) const FITwinCesiumPrimitiveMetadata& PrimitiveMetadata) {
  return PrimitiveMetadata._propertyAttributeIndices;
}
