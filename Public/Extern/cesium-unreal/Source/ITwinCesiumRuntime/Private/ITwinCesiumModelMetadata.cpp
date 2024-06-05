// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumModelMetadata.h"
#include "CesiumGltf/ExtensionModelExtStructuralMetadata.h"
#include "CesiumGltf/Model.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"

using namespace CesiumGltf;

static FITwinCesiumModelMetadata EmptyModelMetadata;

static FITwinCesiumPropertyTable EmptyPropertyTable;
static FITwinCesiumPropertyTexture EmptyPropertyTexture;

FITwinCesiumModelMetadata::FITwinCesiumModelMetadata(
    const Model& InModel,
    const ExtensionModelExtStructuralMetadata& Metadata) {
  this->_propertyTables.Reserve(Metadata.propertyTables.size());
  for (const auto& propertyTable : Metadata.propertyTables) {
    this->_propertyTables.Emplace(FITwinCesiumPropertyTable(InModel, propertyTable));
  }

  this->_propertyTextures.Reserve(Metadata.propertyTextures.size());
  for (const auto& propertyTexture : Metadata.propertyTextures) {
    this->_propertyTextures.Emplace(
        FITwinCesiumPropertyTexture(InModel, propertyTexture));
  }
}

/*static*/
const FITwinCesiumModelMetadata&
UITwinCesiumModelMetadataBlueprintLibrary::GetModelMetadata(
    const UPrimitiveComponent* component) {
  const UITwinCesiumGltfPrimitiveComponent* pGltfComponent =
      Cast<UITwinCesiumGltfPrimitiveComponent>(component);

  if (!IsValid(pGltfComponent)) {
    return EmptyModelMetadata;
  }

  const UITwinCesiumGltfComponent* pModel =
      Cast<UITwinCesiumGltfComponent>(pGltfComponent->GetOuter());
  if (!IsValid(pModel)) {
    return EmptyModelMetadata;
  }

  return pModel->Metadata;
}

/*static*/ const TMap<FString, FITwinCesiumPropertyTable>
UITwinCesiumModelMetadataBlueprintLibrary::GetFeatureTables(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata) {
  TMap<FString, FITwinCesiumPropertyTable> result;
  for (const FITwinCesiumPropertyTable& propertyTable :
       ModelMetadata._propertyTables) {
    result.Add(
        UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableName(
            propertyTable),
        propertyTable);
  }
  return result;
}

/*static*/ const TMap<FString, FITwinCesiumPropertyTexture>
UITwinCesiumModelMetadataBlueprintLibrary::GetFeatureTextures(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata) {
  TMap<FString, FITwinCesiumPropertyTexture> result;
  for (const FITwinCesiumPropertyTexture& propertyTexture :
       ModelMetadata._propertyTextures) {
    result.Add(
        UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureName(
            propertyTexture),
        propertyTexture);
  }
  return result;
}

/*static*/
const TArray<FITwinCesiumPropertyTable>&
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata) {
  return ModelMetadata._propertyTables;
}

/*static*/ const FITwinCesiumPropertyTable&
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTable(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
    const int64 Index) {
  if (Index < 0 || Index >= ModelMetadata._propertyTables.Num()) {
    return EmptyPropertyTable;
  }

  return ModelMetadata._propertyTables[Index];
}

/*static*/ const TArray<FITwinCesiumPropertyTable>
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTablesAtIndices(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
    const TArray<int64>& Indices) {
  TArray<FITwinCesiumPropertyTable> result;
  for (int64 Index : Indices) {
    result.Add(GetPropertyTable(ModelMetadata, Index));
  }
  return result;
}

/*static*/
const TArray<FITwinCesiumPropertyTexture>&
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTextures(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata) {
  return ModelMetadata._propertyTextures;
}

/*static*/ const FITwinCesiumPropertyTexture&
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTexture(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
    const int64 Index) {
  if (Index < 0 || Index >= ModelMetadata._propertyTextures.Num()) {
    return EmptyPropertyTexture;
  }

  return ModelMetadata._propertyTextures[Index];
}

/*static*/ const TArray<FITwinCesiumPropertyTexture>
UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTexturesAtIndices(
    UPARAM(ref) const FITwinCesiumModelMetadata& ModelMetadata,
    const TArray<int64>& Indices) {
  TArray<FITwinCesiumPropertyTexture> result;
  for (int64 Index : Indices) {
    result.Add(GetPropertyTexture(ModelMetadata, Index));
  }
  return result;
}
