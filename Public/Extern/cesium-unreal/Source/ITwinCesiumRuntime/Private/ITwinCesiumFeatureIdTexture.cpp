// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumFeatureIdTexture.h"
#include "CesiumGltf/FeatureIdTexture.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCesiumMetadataPickingBlueprintLibrary.h"

#include <optional>

using namespace CesiumGltf;

FITwinCesiumFeatureIdTexture::FITwinCesiumFeatureIdTexture(
    const Model& Model,
    const MeshPrimitive& Primitive,
    const FeatureIdTexture& FeatureIdTexture,
    const FString& PropertyTableName)
    : _status(EITwinCesiumFeatureIdTextureStatus::ErrorInvalidTexture),
      _featureIdTextureView(Model, FeatureIdTexture),
      _texCoordAccessor(),
      _textureCoordinateSetIndex(FeatureIdTexture.texCoord),
      _propertyTableName(PropertyTableName) {
  switch (_featureIdTextureView.status()) {
  case FeatureIdTextureViewStatus::Valid:
    _status = EITwinCesiumFeatureIdTextureStatus::Valid;
    break;
  case FeatureIdTextureViewStatus::ErrorInvalidChannels:
    _status = EITwinCesiumFeatureIdTextureStatus::ErrorInvalidTextureAccess;
    return;
  default:
    // Error with the texture or image. The status is already set by the
    // initializer list.
    return;
  }

  // The EXT_feature_metadata version of FITwinCesiumFeatureIdTexture was not
  // constructed with an "owner" primitive. It was possible to access the
  // texture data with technically arbitrary coordinates.
  //
  // To maintain this functionality in EXT_mesh_features, the texture view will
  // still be valid if the intended texcoords don't exist. However, feature IDs
  // won't be retrievable by vertex index.
  this->_texCoordAccessor = CesiumGltf::getTexCoordAccessorView(
      Model,
      Primitive,
      this->_textureCoordinateSetIndex);
}

const FString& UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureTableName(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture) {
  return FeatureIDTexture._propertyTableName;
}

EITwinCesiumFeatureIdTextureStatus
UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture) {
  return FeatureIDTexture._status;
}

int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::GetGltfTextureCoordinateSetIndex(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture) {
  return FeatureIDTexture.getFeatureIdTextureView().getTexCoordSetIndex();
}

int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::GetUnrealUVChannel(
    const UPrimitiveComponent* PrimitiveComponent,
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture) {
  const UITwinCesiumGltfPrimitiveComponent* pPrimitive =
      Cast<UITwinCesiumGltfPrimitiveComponent>(PrimitiveComponent);
  if (!pPrimitive ||
      FeatureIDTexture._status != EITwinCesiumFeatureIdTextureStatus::Valid) {
    return -1;
  }

  auto textureCoordinateIndexIt = pPrimitive->GltfToUnrealTexCoordMap.find(
      UITwinCesiumFeatureIdTextureBlueprintLibrary::GetGltfTextureCoordinateSetIndex(
          FeatureIDTexture));
  if (textureCoordinateIndexIt == pPrimitive->GltfToUnrealTexCoordMap.end()) {
    return -1;
  }

  return textureCoordinateIndexIt->second;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::
    GetFeatureIDForTextureCoordinates(
        UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture,
        float U,
        float V) {
  return FeatureIDTexture._featureIdTextureView.getFeatureID(U, V);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDForUV(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture,
    const FVector2D& UV) {
  return FeatureIDTexture._featureIdTextureView.getFeatureID(UV[0], UV[1]);
}

int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDForVertex(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture,
    int64 VertexIndex) {
  const std::optional<glm::dvec2> texCoords = std::visit(
      CesiumGltf::TexCoordFromAccessor{VertexIndex},
      FeatureIDTexture._texCoordAccessor);
  if (!texCoords) {
    return -1;
  }

  return GetFeatureIDForTextureCoordinates(
      FeatureIDTexture,
      (*texCoords)[0],
      (*texCoords)[1]);
}

int64 UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDFromHit(
    UPARAM(ref) const FITwinCesiumFeatureIdTexture& FeatureIDTexture,
    const FHitResult& Hit) {
  FVector2D UV;
  if (UITwinCesiumMetadataPickingBlueprintLibrary::FindUVFromHit(
          Hit,
          FeatureIDTexture._featureIdTextureView.getTexCoordSetIndex(),
          UV)) {
    return FeatureIDTexture._featureIdTextureView.getFeatureID(UV[0], UV[1]);
  }

  return -1;
}
