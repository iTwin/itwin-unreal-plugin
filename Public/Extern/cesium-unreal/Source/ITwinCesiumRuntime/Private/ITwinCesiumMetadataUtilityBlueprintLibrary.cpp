// Copyright 2020-2023 CesiumGS, Inc. and Contributors

PRAGMA_DISABLE_DEPRECATION_WARNINGS

#include "ITwinCesiumMetadataUtilityBlueprintLibrary.h"
#include "ITwinCesiumFeatureIdTexture.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"

static FITwinCesiumMetadataPrimitive EmptyMetadataPrimitive;

const FITwinCesiumMetadataPrimitive&
UITwinCesiumMetadataUtilityBlueprintLibrary::GetPrimitiveMetadata(
    const UPrimitiveComponent* component) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(component);
  if (!IsValid(pGltfComponent)) {
    return EmptyMetadataPrimitive;
  }

  return pGltfComponent->Metadata_DEPRECATED;
}

TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumMetadataUtilityBlueprintLibrary::GetMetadataValuesForFace(
    const UPrimitiveComponent* component,
    int64 FaceIndex) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(component);
  if (!IsValid(pGltfComponent)) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const UITwinCesiumGltfComponent* pModel =
      Cast<UITwinCesiumGltfComponent>(pGltfComponent->GetOuter());
  if (!IsValid(pModel)) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumPrimitiveFeatures& features = pGltfComponent->Features;
  const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSetsOfType(
          features,
          EITwinCesiumFeatureIdSetType::Attribute);
  if (featureIDSets.Num() == 0) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumModelMetadata& modelMetadata = pModel->Metadata;
  const FITwinCesiumPrimitiveMetadata& primitiveMetadata = pGltfComponent->Metadata;

  // For now, only considers the first feature ID set
  const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[0];
  const int64 propertyTableIndex =
      UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(featureIDSet);

  const TArray<FITwinCesiumPropertyTable>& propertyTables =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(modelMetadata);
  if (propertyTableIndex < 0 || propertyTableIndex >= propertyTables.Num()) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumPropertyTable& propertyTable =
      propertyTables[propertyTableIndex];

  int64 featureID =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
          features,
          FaceIndex,
          0);
  if (featureID < 0) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  return UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
      propertyTable,
      featureID);
}

TMap<FString, FString>
UITwinCesiumMetadataUtilityBlueprintLibrary::GetMetadataValuesAsStringForFace(
    const UPrimitiveComponent* Component,
    int64 FaceIndex) {
  TMap<FString, FITwinCesiumMetadataValue> values =
      UITwinCesiumMetadataUtilityBlueprintLibrary::GetMetadataValuesForFace(
          Component,
          FaceIndex);
  TMap<FString, FString> strings;
  for (auto valuesIt : values) {
    strings.Add(
        valuesIt.Key,
        UITwinCesiumMetadataValueBlueprintLibrary::GetString(valuesIt.Value, ""));
  }

  return strings;
}

int64 UITwinCesiumMetadataUtilityBlueprintLibrary::GetFeatureIDFromFaceID(
    UPARAM(ref) const FITwinCesiumMetadataPrimitive& Primitive,
    UPARAM(ref) const FITwinCesiumFeatureIdAttribute& FeatureIDAttribute,
    int64 FaceID) {
  return UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDForVertex(
      FeatureIDAttribute,
      UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFirstVertexIDFromFaceID(
          Primitive,
          FaceID));
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
