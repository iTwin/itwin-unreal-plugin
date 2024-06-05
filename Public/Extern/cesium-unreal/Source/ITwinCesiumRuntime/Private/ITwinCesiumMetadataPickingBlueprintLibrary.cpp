// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumMetadataPickingBlueprintLibrary.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCesiumMetadataValue.h"

static TMap<FString, FITwinCesiumMetadataValue> EmptyCesiumMetadataValueMap;

TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumMetadataPickingBlueprintLibrary::GetMetadataValuesForFace(
    const UPrimitiveComponent* Component,
    int64 FaceIndex,
    int64 FeatureIDSetIndex) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Component);
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
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(features);

  if (FeatureIDSetIndex < 0 || FeatureIDSetIndex >= featureIDSets.Num()) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[FeatureIDSetIndex];
  const int64 propertyTableIndex =
      UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(featureIDSet);

  const TArray<FITwinCesiumPropertyTable>& propertyTables =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(pModel->Metadata);
  if (propertyTableIndex < 0 || propertyTableIndex >= propertyTables.Num()) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumPropertyTable& propertyTable =
      propertyTables[propertyTableIndex];

  int64 featureID =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromFace(
          features,
          FaceIndex,
          FeatureIDSetIndex);
  if (featureID < 0) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  return UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
      propertyTable,
      featureID);
}

TMap<FString, FString>
UITwinCesiumMetadataPickingBlueprintLibrary::GetMetadataValuesForFaceAsStrings(
    const UPrimitiveComponent* Component,
    int64 FaceIndex,
    int64 FeatureIDSetIndex) {
  TMap<FString, FITwinCesiumMetadataValue> values =
      UITwinCesiumMetadataPickingBlueprintLibrary::GetMetadataValuesForFace(
          Component,
          FaceIndex,
          FeatureIDSetIndex);
  TMap<FString, FString> strings;
  for (auto valuesIt : values) {
    strings.Add(
        valuesIt.Key,
        UITwinCesiumMetadataValueBlueprintLibrary::GetString(valuesIt.Value, ""));
  }

  return strings;
}

bool UITwinCesiumMetadataPickingBlueprintLibrary::FindUVFromHit(
    const FHitResult& Hit,
    int64 GltfTexCoordSetIndex,
    FVector2D& UV) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Hit.Component);
  if (!IsValid(pGltfComponent) || !pGltfComponent->pMeshPrimitive) {
    return false;
  }

  if (pGltfComponent->PositionAccessor.status() !=
      CesiumGltf::AccessorViewStatus::Valid) {
    return false;
  }

  auto accessorIt =
      pGltfComponent->TexCoordAccessorMap.find(GltfTexCoordSetIndex);
  if (accessorIt == pGltfComponent->TexCoordAccessorMap.end()) {
    return false;
  }

  auto VertexIndices = std::visit(
      CesiumGltf::IndicesForFaceFromAccessor{
          Hit.FaceIndex,
          pGltfComponent->PositionAccessor.size(),
          pGltfComponent->pMeshPrimitive->mode},
      pGltfComponent->IndexAccessor);

  // Adapted from UBodySetup::CalcUVAtLocation. Compute the barycentric
  // coordinates of the point relative to the face, then use those to
  // interpolate the UVs.
  std::array<FVector2D, 3> UVs;
  const CesiumGltf::TexCoordAccessorType& accessor = accessorIt->second;
  for (size_t i = 0; i < UVs.size(); i++) {
    auto maybeTexCoord = std::visit(
        CesiumGltf::TexCoordFromAccessor{VertexIndices[i]},
        accessor);
    if (!maybeTexCoord) {
      return false;
    }
    const glm::dvec2& texCoord = *maybeTexCoord;
    UVs[i] = FVector2D(texCoord[0], texCoord[1]);
  }

  std::array<FVector, 3> Positions;
  for (size_t i = 0; i < Positions.size(); i++) {
    auto& Position = pGltfComponent->PositionAccessor[VertexIndices[i]];
    // The Y-component of glTF positions must be inverted
    Positions[i] = FVector(Position[0], -Position[1], Position[2]);
  }

  const FVector Location =
      pGltfComponent->GetComponentToWorld().InverseTransformPosition(
          Hit.Location);
  FVector BaryCoords = FMath::ComputeBaryCentric2D(
      Location,
      Positions[0],
      Positions[1],
      Positions[2]);

  UV = (BaryCoords.X * UVs[0]) + (BaryCoords.Y * UVs[1]) +
       (BaryCoords.Z * UVs[2]);

  return true;
}

TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
    const FHitResult& Hit,
    int64 FeatureIDSetIndex) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Hit.Component);
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
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(features);

  if (FeatureIDSetIndex < 0 || FeatureIDSetIndex >= featureIDSets.Num()) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[FeatureIDSetIndex];
  const int64 propertyTableIndex =
      UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(featureIDSet);

  const TArray<FITwinCesiumPropertyTable>& propertyTables =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(pModel->Metadata);
  if (propertyTableIndex < 0 || propertyTableIndex >= propertyTables.Num()) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  const FITwinCesiumPropertyTable& propertyTable =
      propertyTables[propertyTableIndex];

  int64 featureID =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDFromHit(
          features,
          Hit,
          FeatureIDSetIndex);
  if (featureID < 0) {
    return TMap<FString, FITwinCesiumMetadataValue>();
  }

  return UITwinCesiumPropertyTableBlueprintLibrary::GetMetadataValuesForFeature(
      propertyTable,
      featureID);
}

TMap<FString, FITwinCesiumMetadataValue>
UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTextureValuesFromHit(
    const FHitResult& Hit,
    int64 PropertyTextureIndex) {
  if (!Hit.Component.IsValid()) {
    return EmptyCesiumMetadataValueMap;
  }

  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(Hit.Component.Get());
  if (!IsValid(pGltfComponent)) {
    return EmptyCesiumMetadataValueMap;
  }

  const UITwinCesiumGltfComponent* pModel =
      Cast<UITwinCesiumGltfComponent>(pGltfComponent->GetOuter());
  if (!IsValid(pModel)) {
    return EmptyCesiumMetadataValueMap;
  }

  const TArray<FITwinCesiumPropertyTexture>& propertyTextures =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTextures(
          pModel->Metadata);
  if (PropertyTextureIndex < 0 ||
      PropertyTextureIndex >= propertyTextures.Num()) {
    return EmptyCesiumMetadataValueMap;
  }

  return UITwinCesiumPropertyTextureBlueprintLibrary::GetMetadataValuesFromHit(
      propertyTextures[PropertyTextureIndex],
      Hit);
}

/*static*/
const FITwinCesiumPropertyTableProperty*
UITwinCesiumMetadataPickingBlueprintLibrary::FindValidProperty(
    const FITwinCesiumPrimitiveFeatures& Features,
    const FITwinCesiumModelMetadata& Metadata,
    const FString& PropertyName,
    int64 FeatureIDSetIndex) {
    const TArray<FITwinCesiumFeatureIdSet>& featureIDSets =
        UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features);

    if (FeatureIDSetIndex < 0 || FeatureIDSetIndex >= featureIDSets.Num()) {
        return nullptr;
    }

    const FITwinCesiumFeatureIdSet& featureIDSet = featureIDSets[FeatureIDSetIndex];
    const int64 propertyTableIndex =
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetPropertyTableIndex(featureIDSet);

    const TArray<FITwinCesiumPropertyTable>& propertyTables =
        UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(Metadata);
    if (propertyTableIndex < 0 || propertyTableIndex >= propertyTables.Num()) {
        return nullptr;
    }
    const FITwinCesiumPropertyTableProperty& propWithName =
        UITwinCesiumPropertyTableBlueprintLibrary::FindProperty(
            propertyTables[propertyTableIndex],
            PropertyName);
    const ECesiumPropertyTablePropertyStatus status =
        UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetPropertyTablePropertyStatus(propWithName);
    if (status != ECesiumPropertyTablePropertyStatus::Valid) {
        return nullptr;
    }
    return &propWithName;
}
