// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumFeatureIdAttribute.h"
#include <CesiumGltf/Accessor.h>
#include <CesiumGltf/Model.h>

using namespace CesiumGltf;

FITwinCesiumFeatureIdAttribute::FITwinCesiumFeatureIdAttribute(
    const Model& Model,
    const MeshPrimitive& Primitive,
    const int64 FeatureIDAttribute,
    const FString& PropertyTableName)
    : _status(EITwinCesiumFeatureIdAttributeStatus::ErrorInvalidAttribute),
      _featureIdAccessor(),
      _attributeIndex(FeatureIDAttribute),
      _propertyTableName(PropertyTableName) {
  const std::string attributeName =
      "_FEATURE_ID_" + std::to_string(FeatureIDAttribute);

  auto featureID = Primitive.attributes.find(attributeName);
  if (featureID == Primitive.attributes.end()) {
    return;
  }

  const Accessor* accessor =
      Model.getSafe<Accessor>(&Model.accessors, featureID->second);
  if (!accessor || accessor->type != Accessor::Type::SCALAR) {
    this->_status = EITwinCesiumFeatureIdAttributeStatus::ErrorInvalidAccessor;
    return;
  }

  this->_featureIdAccessor = CesiumGltf::getFeatureIdAccessorView(
      Model,
      Primitive,
      this->_attributeIndex);

  this->_status = std::visit(
      [](auto view) {
        if (view.status() != AccessorViewStatus::Valid) {
          return EITwinCesiumFeatureIdAttributeStatus::ErrorInvalidAccessor;
        }

        return EITwinCesiumFeatureIdAttributeStatus::Valid;
      },
      this->_featureIdAccessor);
}

const FString& UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureTableName(
    UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute) {
  return FeatureIDAttribute._propertyTableName;
}

EITwinCesiumFeatureIdAttributeStatus
UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDAttributeStatus(
    UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute) {
  return FeatureIDAttribute._status;
}

int64 UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetVertexCount(
    UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute) {
  return std::visit(CountFromAccessor{}, FeatureIDAttribute._featureIdAccessor);
}

int64 UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDForVertex(
    UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute,
    int64 VertexIndex) {
  return std::visit(
      FeatureIdFromAccessor{VertexIndex},
      FeatureIDAttribute._featureIdAccessor);
}
