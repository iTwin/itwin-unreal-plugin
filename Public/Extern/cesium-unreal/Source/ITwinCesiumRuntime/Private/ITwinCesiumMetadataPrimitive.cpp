// Copyright 2020-2023 CesiumGS, Inc. and Contributors

PRAGMA_DISABLE_DEPRECATION_WARNINGS

#include "ITwinCesiumMetadataPrimitive.h"
#include "CesiumGltf/ExtensionMeshPrimitiveExtFeatureMetadata.h"
#include "CesiumGltf/Model.h"

FITwinCesiumMetadataPrimitive::FITwinCesiumMetadataPrimitive(
    const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
    const FITwinCesiumPrimitiveMetadata& PrimitiveMetadata,
    const FITwinCesiumModelMetadata& ModelMetadata)
    : _pPrimitiveFeatures(&PrimitiveFeatures),
      _pPrimitiveMetadata(&PrimitiveMetadata),
      _pModelMetadata(&ModelMetadata) {}

const TArray<FITwinCesiumFeatureIdAttribute>
UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureIdAttributes(
    UPARAM(ref) const FITwinCesiumMetadataPrimitive& MetadataPrimitive) {
  TArray<FITwinCesiumFeatureIdAttribute> featureIDAttributes;
  if (!MetadataPrimitive._pPrimitiveFeatures) {
    return featureIDAttributes;
  }

  const TArray<FITwinCesiumFeatureIdSet> featureIDSets =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
          *MetadataPrimitive._pPrimitiveFeatures,
          ECesiumFeatureIdSetType::Attribute);

  featureIDAttributes.Reserve(featureIDSets.Num());
  for (const FITwinCesiumFeatureIdSet& featureIDSet : featureIDSets) {
    featureIDAttributes.Add(
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
            featureIDSet));
  }

  return featureIDAttributes;
}

const TArray<FITwinCesiumFeatureIdTexture>
UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureIdTextures(
    UPARAM(ref) const FITwinCesiumMetadataPrimitive& MetadataPrimitive) {
  TArray<FITwinCesiumFeatureIdTexture> featureIDTextures;
  if (!MetadataPrimitive._pPrimitiveFeatures) {
    return featureIDTextures;
  }

  const TArray<FITwinCesiumFeatureIdSet> featureIDSets =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
          *MetadataPrimitive._pPrimitiveFeatures,
          ECesiumFeatureIdSetType::Texture);

  featureIDTextures.Reserve(featureIDSets.Num());
  for (const FITwinCesiumFeatureIdSet& featureIDSet : featureIDSets) {
    featureIDTextures.Add(
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(
            featureIDSet));
  }

  return featureIDTextures;
}

const TArray<FString>
UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureTextureNames(
    UPARAM(ref) const FITwinCesiumMetadataPrimitive& MetadataPrimitive) {
  TArray<FString> propertyTextureNames;
  if (!MetadataPrimitive._pPrimitiveMetadata ||
      !MetadataPrimitive._pModelMetadata) {
    return TArray<FString>();
  }

  const TArray<int64>& propertyTextureIndices =
      UITwinCesiumPrimitiveMetadataBlueprintLibrary::GetPropertyTextureIndices(
          *MetadataPrimitive._pPrimitiveMetadata);

  const TArray<FITwinCesiumPropertyTexture> propertyTextures =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTexturesAtIndices(
          *MetadataPrimitive._pModelMetadata,
          propertyTextureIndices);

  propertyTextureNames.Reserve(propertyTextures.Num());
  for (auto propertyTexture : propertyTextures) {
    propertyTextureNames.Add(
        UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureName(
            propertyTexture));
  }

  return propertyTextureNames;
}

int64 UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFirstVertexIDFromFaceID(
    UPARAM(ref) const FITwinCesiumMetadataPrimitive& MetadataPrimitive,
    int64 FaceID) {
  if (!MetadataPrimitive._pPrimitiveFeatures) {
    return -1;
  }

  return UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFirstVertexFromFace(
      *MetadataPrimitive._pPrimitiveFeatures,
      FaceID);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
