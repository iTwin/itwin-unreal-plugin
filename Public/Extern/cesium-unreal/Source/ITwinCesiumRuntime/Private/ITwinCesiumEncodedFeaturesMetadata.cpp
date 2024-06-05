// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumEncodedFeaturesMetadata.h"
#include "ITwinCesiumEncodedMetadataConversions.h"
#include "ITwinCesiumFeatureIdSet.h"
#include "ITwinCesiumFeaturesMetadataComponent.h"
#include "ITwinCesiumLifetime.h"
#include "ITwinCesiumModelMetadata.h"
#include "ITwinCesiumPrimitiveFeatures.h"
#include "ITwinCesiumPrimitiveMetadata.h"
#include "ITwinCesiumPropertyArray.h"
#include "ITwinCesiumPropertyTable.h"
#include "ITwinCesiumPropertyTexture.h"
#include "ITwinCesiumRuntime.h"
#include "Containers/Map.h"
#include "PixelFormat.h"
#include "TextureResource.h"
#include "ITwinUnrealMetadataConversions.h"
#include <CesiumGltf/FeatureIdTextureView.h>
#include <CesiumUtility/Tracing.h>
#include <glm/gtx/integer.hpp>
#include <optional>
#include <unordered_map>

using namespace CesiumTextureUtility;

namespace CesiumEncodedFeaturesMetadata {

FString getNameForFeatureIDSet(
    const FITwinCesiumFeatureIdSet& featureIDSet,
    int32& FeatureIdTextureCounter) {
  FString label = UITwinCesiumFeatureIdSetBlueprintLibrary::GetLabel(featureIDSet);
  if (!label.IsEmpty()) {
    return label;
  }

  ECesiumFeatureIdSetType type =
      UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(featureIDSet);

  if (type == ECesiumFeatureIdSetType::Attribute) {
    FITwinCesiumFeatureIdAttribute attribute =
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(
            featureIDSet);
    ECesiumFeatureIdAttributeStatus status =
        UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDAttributeStatus(
            attribute);
    if (status == ECesiumFeatureIdAttributeStatus::Valid) {
      std::string generatedName =
          "_FEATURE_ID_" + std::to_string(attribute.getAttributeIndex());
      return FString(generatedName.c_str());
    }
  }

  if (type == ECesiumFeatureIdSetType::Texture) {
    std::string generatedName =
        "_FEATURE_ID_TEXTURE_" + std::to_string(FeatureIdTextureCounter);
    FeatureIdTextureCounter++;
    return FString(generatedName.c_str());
  }

  if (type == ECesiumFeatureIdSetType::Implicit) {
    return FString("_IMPLICIT_FEATURE_ID");
  }

  // If for some reason an empty / invalid feature ID set was constructed,
  // return an empty name.
  return FString();
}

namespace {

/**
 * @brief Encodes a feature ID attribute for access in a Unreal Engine Material.
 * The feature IDs are simply sent to the GPU as texture coordinates, so this
 * just handles the variable names necessary for material access.
 *
 * @returns The encoded feature ID attribute, or std::nullopt if the attribute
 * was somehow invalid.
 */
std::optional<EncodedFeatureIdSet>
encodeFeatureIdAttribute(const FITwinCesiumFeatureIdAttribute& attribute) {
  const ECesiumFeatureIdAttributeStatus status =
      UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureIDAttributeStatus(
          attribute);

  if (status != ECesiumFeatureIdAttributeStatus::Valid) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("Can't encode invalid feature ID attribute, skipped."));
    return std::nullopt;
  }

  EncodedFeatureIdSet result;
  result.attribute = attribute.getAttributeIndex();
  return result;
}

std::optional<EncodedFeatureIdSet> encodeFeatureIdTexture(
    const FITwinCesiumFeatureIdTexture& texture,
    TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>&
        featureIdTextureMap) {
  const ECesiumFeatureIdTextureStatus status =
      UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureIDTextureStatus(
          texture);
  if (status != ECesiumFeatureIdTextureStatus::Valid) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("Can't encode invalid feature ID texture, skipped."));
    return std::nullopt;
  }

  const CesiumGltf::FeatureIdTextureView& featureIdTextureView =
      texture.getFeatureIdTextureView();
  const CesiumGltf::ImageCesium* pFeatureIdImage =
      featureIdTextureView.getImage();

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureIdTexture)

  EncodedFeatureIdSet result;
  EncodedFeatureIdTexture& encodedFeatureIdTexture = result.texture.emplace();

  encodedFeatureIdTexture.channels = featureIdTextureView.getChannels();
  encodedFeatureIdTexture.textureCoordinateSetIndex =
      featureIdTextureView.getTexCoordSetIndex();

  TWeakPtr<LoadedTextureResult>* pMappedUnrealImageIt =
      featureIdTextureMap.Find(pFeatureIdImage);
  if (pMappedUnrealImageIt) {
    encodedFeatureIdTexture.pTexture = pMappedUnrealImageIt->Pin();
  } else {
    encodedFeatureIdTexture.pTexture = MakeShared<LoadedTextureResult>();
    encodedFeatureIdTexture.pTexture->sRGB = false;
    // TODO: upgrade to new texture creation path
    encodedFeatureIdTexture.pTexture->textureSource = LegacyTextureSource{};
    featureIdTextureMap.Emplace(
        pFeatureIdImage,
        encodedFeatureIdTexture.pTexture);
    encodedFeatureIdTexture.pTexture->pTextureData = createTexturePlatformData(
        pFeatureIdImage->width,
        pFeatureIdImage->height,
        // TODO: currently this is always the case, but doesn't have to be
        EPixelFormat::PF_R8G8B8A8_UINT);

    encodedFeatureIdTexture.pTexture->addressX = TextureAddress::TA_Clamp;
    encodedFeatureIdTexture.pTexture->addressY = TextureAddress::TA_Clamp;
    encodedFeatureIdTexture.pTexture->filter = TextureFilter::TF_Nearest;

    if (!encodedFeatureIdTexture.pTexture->pTextureData) {
      UE_LOG(
          LogCesium,
          Error,
          TEXT(
              "Error encoding a feature ID texture. Most likely could not allocate enough texture memory."));
      return std::nullopt;
    }

    FTexture2DMipMap* pMip = new FTexture2DMipMap();
    encodedFeatureIdTexture.pTexture->pTextureData->Mips.Add(pMip);
    pMip->SizeX = pFeatureIdImage->width;
    pMip->SizeY = pFeatureIdImage->height;
    pMip->BulkData.Lock(LOCK_READ_WRITE);

    void* pTextureData =
        pMip->BulkData.Realloc(pFeatureIdImage->pixelData.size());

    FMemory::Memcpy(
        pTextureData,
        pFeatureIdImage->pixelData.data(),
        pFeatureIdImage->pixelData.size());

    pMip->BulkData.Unlock();
    pMip->BulkData.SetBulkDataFlags(BULKDATA_SingleUse);
  }

  return result;
}
} // namespace

EncodedPrimitiveFeatures encodePrimitiveFeaturesAnyThreadPart(
    const FITwinCesiumPrimitiveFeaturesDescription& featuresDescription,
    const FITwinCesiumPrimitiveFeatures& features) {
  EncodedPrimitiveFeatures result;

  const TArray<FITwinCesiumFeatureIdSetDescription>& featureIDSetDescriptions =
      featuresDescription.FeatureIdSets;
  result.featureIdSets.Reserve(featureIDSetDescriptions.Num());

  // Not all feature ID sets are necessarily textures, but reserve the max
  // amount just in case.
  TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>
      featureIdTextureMap;
  featureIdTextureMap.Reserve(featureIDSetDescriptions.Num());

  const TArray<FITwinCesiumFeatureIdSet>& featureIdSets =
      UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(features);
  int32_t featureIdTextureCounter = 0;

  for (int32 i = 0; i < featureIdSets.Num(); i++) {
    const FITwinCesiumFeatureIdSet& set = featureIdSets[i];
    FString name = getNameForFeatureIDSet(set, featureIdTextureCounter);
    const FITwinCesiumFeatureIdSetDescription* pDescription =
        featureIDSetDescriptions.FindByPredicate(
            [&name](
                const FITwinCesiumFeatureIdSetDescription& existingFeatureIDSet) {
              return existingFeatureIDSet.Name == name;
            });

    if (!pDescription) {
      // The description doesn't need this feature ID set, skip.
      continue;
    }

    std::optional<EncodedFeatureIdSet> encodedSet;
    ECesiumFeatureIdSetType type =
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDSetType(set);

    if (type == ECesiumFeatureIdSetType::Attribute) {
      const FITwinCesiumFeatureIdAttribute& attribute =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDAttribute(set);
      encodedSet = encodeFeatureIdAttribute(attribute);
    } else if (type == ECesiumFeatureIdSetType::Texture) {
      const FITwinCesiumFeatureIdTexture& texture =
          UITwinCesiumFeatureIdSetBlueprintLibrary::GetAsFeatureIDTexture(set);
      encodedSet = encodeFeatureIdTexture(texture, featureIdTextureMap);
    } else if (type == ECesiumFeatureIdSetType::Implicit) {
      encodedSet = EncodedFeatureIdSet();
    }

    if (!encodedSet)
      continue;

    encodedSet->name = name;
    encodedSet->index = i;
    encodedSet->propertyTableName = pDescription->PropertyTableName;
    encodedSet->nullFeatureId =
        UITwinCesiumFeatureIdSetBlueprintLibrary::GetNullFeatureID(set);

    result.featureIdSets.Add(*encodedSet);
  }

  return result;
}

bool encodePrimitiveFeaturesGameThreadPart(
    EncodedPrimitiveFeatures& encodedFeatures) {
  bool success = true;

  // Not all feature ID sets are necessarily textures, but reserve the max
  // amount just in case.
  TArray<const LoadedTextureResult*> uniqueFeatureIdImages;
  uniqueFeatureIdImages.Reserve(encodedFeatures.featureIdSets.Num());

  for (EncodedFeatureIdSet& encodedFeatureIdSet :
       encodedFeatures.featureIdSets) {
    if (!encodedFeatureIdSet.texture) {
      continue;
    }

    auto& encodedFeatureIdTexture = *encodedFeatureIdSet.texture;
    if (uniqueFeatureIdImages.Find(encodedFeatureIdTexture.pTexture.Get()) ==
        INDEX_NONE) {
      success &= loadTextureGameThreadPart(
                     encodedFeatureIdTexture.pTexture.Get()) != nullptr;
      uniqueFeatureIdImages.Emplace(encodedFeatureIdTexture.pTexture.Get());
    }
  }

  return success;
}

void destroyEncodedPrimitiveFeatures(
    EncodedPrimitiveFeatures& encodedFeatures) {
  for (EncodedFeatureIdSet& encodedFeatureIdSet :
       encodedFeatures.featureIdSets) {
    if (!encodedFeatureIdSet.texture) {
      continue;
    }

    auto& encodedFeatureIdTexture = *encodedFeatureIdSet.texture;
    if (encodedFeatureIdTexture.pTexture->pTexture.IsValid()) {
      CesiumLifetime::destroy(encodedFeatureIdTexture.pTexture->pTexture.Get());
      encodedFeatureIdTexture.pTexture->pTexture.Reset();
    }
  }
}

FString getNameForPropertyTable(const FITwinCesiumPropertyTable& PropertyTable) {
  FString propertyTableName =
      UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableName(PropertyTable);

  if (propertyTableName.IsEmpty()) {
    // Substitute the name with the property table's class.
    propertyTableName = PropertyTable.getClassName();
  }

  return propertyTableName;
}

FString
getNameForPropertyTexture(const FITwinCesiumPropertyTexture& PropertyTexture) {
  FString propertyTextureName =
      UITwinCesiumPropertyTextureBlueprintLibrary::GetPropertyTextureName(
          PropertyTexture);

  if (propertyTextureName.IsEmpty()) {
    // Substitute the name with the property texture's class.
    propertyTextureName = PropertyTexture.getClassName();
  }

  return propertyTextureName;
}

FString getMaterialNameForPropertyTableProperty(
    const FString& propertyTableName,
    const FString& propertyName) {
  // Example: "PTABLE_houses_roofColor"
  return createHlslSafeName(
      MaterialPropertyTablePrefix + propertyTableName + "_" + propertyName);
}

FString getMaterialNameForPropertyTextureProperty(
    const FString& propertyTextureName,
    const FString& propertyName) {
  // Example: "PTEXTURE_house_temperature"
  return createHlslSafeName(
      MaterialPropertyTexturePrefix + propertyTextureName + "_" + propertyName);
}

namespace {

struct EncodedPixelFormat {
  EPixelFormat format;
  size_t pixelSize;
};

// TODO: consider picking better pixel formats when they are available for the
// current platform.
EncodedPixelFormat
getPixelFormat(FITwinCesiumMetadataEncodingDetails encodingDetails) {

  switch (encodingDetails.ComponentType) {
  case ECesiumEncodedMetadataComponentType::Uint8:
    switch (encodingDetails.Type) {
    case ECesiumEncodedMetadataType::Scalar:
      return {EPixelFormat::PF_R8_UINT, 1};
    case ECesiumEncodedMetadataType::Vec2:
    case ECesiumEncodedMetadataType::Vec3:
    case ECesiumEncodedMetadataType::Vec4:
      return {EPixelFormat::PF_R8G8B8A8_UINT, 4};
    default:
      return {EPixelFormat::PF_Unknown, 0};
    }
  case ECesiumEncodedMetadataComponentType::Float:
    switch (encodingDetails.Type) {
    case ECesiumEncodedMetadataType::Scalar:
      return {EPixelFormat::PF_R32_FLOAT, 4};
    case ECesiumEncodedMetadataType::Vec2:
    case ECesiumEncodedMetadataType::Vec3:
    case ECesiumEncodedMetadataType::Vec4:
      // Note this is ABGR
      return {EPixelFormat::PF_A32B32G32R32F, 16};
    }
  default:
    return {EPixelFormat::PF_Unknown, 0};
  }
}

bool isValidPropertyTablePropertyDescription(
    const FITwinCesiumPropertyTablePropertyDescription& propertyDescription,
    const FITwinCesiumPropertyTableProperty& property) {
  if (propertyDescription.EncodingDetails.Type ==
      ECesiumEncodedMetadataType::None) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "No encoded metadata type was specified for this property table property; skip encoding."));
    return false;
  }

  if (propertyDescription.EncodingDetails.ComponentType ==
      ECesiumEncodedMetadataComponentType::None) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "No encoded metadata component type was specified for this property table property; skip encoding."));
    return false;
  }

  const FITwinCesiumMetadataValueType expectedType =
      propertyDescription.PropertyDetails.GetValueType();
  const FITwinCesiumMetadataValueType valueType =
      UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(property);
  if (valueType != expectedType) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "The value type of the metadata property %s does not match the type specified by the metadata description. It will still attempt to be encoded, but may result in empty or unexpected values."),
        *propertyDescription.Name);
  }

  bool isNormalized =
      UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property);
  if (propertyDescription.PropertyDetails.bIsNormalized != isNormalized) {
    FString error =
        propertyDescription.PropertyDetails.bIsNormalized
            ? "Description incorrectly marked a property table property as normalized; skip encoding."
            : "Description incorrectly marked a property table property as not normalized; skip encoding.";
    UE_LOG(LogCesium, Warning, TEXT("%s"), *error);
    return false;
  }

  // Only uint8 normalization is currently supported.
  if (isNormalized &&
      valueType.ComponentType != ECesiumMetadataComponentType::Uint8) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("Only normalization of uint8 properties is currently supported."));
    return false;
  }

  return true;
}

bool isValidPropertyTexturePropertyDescription(
    const FITwinCesiumPropertyTexturePropertyDescription& propertyDescription,
    const FITwinCesiumPropertyTextureProperty& property) {
  const FITwinCesiumMetadataValueType expectedType =
      propertyDescription.PropertyDetails.GetValueType();
  const FITwinCesiumMetadataValueType valueType =
      UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(property);
  if (valueType != expectedType) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "The value type of the metadata property %s does not match the type specified by the metadata description. It will still attempt to be encoded, but may result in empty or unexpected values."),
        *propertyDescription.Name);
  }

  bool isNormalized =
      UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(property);
  if (propertyDescription.PropertyDetails.bIsNormalized != isNormalized) {
    FString error =
        propertyDescription.PropertyDetails.bIsNormalized
            ? "Description incorrectly marked a property texture property as normalized; skip encoding."
            : "Description incorrectly marked a property texture property as not normalized; skip encoding.";
    UE_LOG(LogCesium, Warning, TEXT("%s"), *error);
    return false;
  }

  // Only uint8 normalization is currently supported.
  if (isNormalized &&
      valueType.ComponentType != ECesiumMetadataComponentType::Uint8) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("Only normalization of uint8 properties is currently supported."));
    return false;
  }

  return true;
}

} // namespace

EncodedPropertyTable encodePropertyTableAnyThreadPart(
    const FITwinCesiumPropertyTableDescription& propertyTableDescription,
    const FITwinCesiumPropertyTable& propertyTable) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTable)

  EncodedPropertyTable encodedPropertyTable;

  int64 propertyTableCount =
      UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(
          propertyTable);

  const TMap<FString, FITwinCesiumPropertyTableProperty>& properties =
      UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(propertyTable);

  encodedPropertyTable.properties.Reserve(properties.Num());
  for (const auto& pair : properties) {
    const FITwinCesiumPropertyTableProperty& property = pair.Value;

    const FITwinCesiumPropertyTablePropertyDescription* pDescription =
        propertyTableDescription.Properties.FindByPredicate(
            [&key = pair.Key](const FITwinCesiumPropertyTablePropertyDescription&
                                  expectedProperty) {
              return key == expectedProperty.Name;
            });

    if (!pDescription) {
      continue;
    }

    const FITwinCesiumMetadataEncodingDetails& encodingDetails =
        pDescription->EncodingDetails;
    if (encodingDetails.Conversion == ECesiumEncodedMetadataConversion::None) {
      // No encoding to be done; skip.
      continue;
    }

    if (!isValidPropertyTablePropertyDescription(*pDescription, property)) {
      continue;
    }

    if (encodingDetails.Conversion ==
            ECesiumEncodedMetadataConversion::Coerce &&
        !CesiumEncodedMetadataCoerce::canEncode(*pDescription)) {
      UE_LOG(
          LogCesium,
          Warning,
          TEXT(
              "Cannot use 'Coerce' with the specified property info; skipped."));
      continue;
    }

    if (encodingDetails.Conversion ==
            ECesiumEncodedMetadataConversion::ParseColorFromString &&
        !CesiumEncodedMetadataParseColorFromString::canEncode(*pDescription)) {
      UE_LOG(
          LogCesium,
          Warning,
          TEXT(
              "Cannot use `Parse Color From String` with the specified property info; skipped."));
      continue;
    }

    EncodedPixelFormat encodedFormat = getPixelFormat(encodingDetails);
    if (encodedFormat.format == EPixelFormat::PF_Unknown) {
      UE_LOG(
          LogCesium,
          Warning,
          TEXT(
              "Unable to determine a suitable GPU format for this property table property; skipped."));
      continue;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTableProperty)

    EncodedPropertyTableProperty& encodedProperty =
        encodedPropertyTable.properties.Emplace_GetRef();
    encodedProperty.name = createHlslSafeName(pDescription->Name);
    encodedProperty.type = pDescription->EncodingDetails.Type;

    if (UITwinCesiumPropertyTablePropertyBlueprintLibrary::
            GetPropertyTablePropertyStatus(property) ==
        ECesiumPropertyTablePropertyStatus::Valid) {

      int64 floorSqrtFeatureCount = glm::sqrt(propertyTableCount);
      int64 textureDimension =
          (floorSqrtFeatureCount * floorSqrtFeatureCount == propertyTableCount)
              ? floorSqrtFeatureCount
              : (floorSqrtFeatureCount + 1);

      encodedProperty.pTexture = MakeUnique<LoadedTextureResult>();
      encodedProperty.pTexture->sRGB = false;
      // TODO: upgrade to new texture creation path.
      encodedProperty.pTexture->textureSource = LegacyTextureSource{};
      encodedProperty.pTexture->pTextureData = createTexturePlatformData(
          textureDimension,
          textureDimension,
          encodedFormat.format);

      encodedProperty.pTexture->addressX = TextureAddress::TA_Clamp;
      encodedProperty.pTexture->addressY = TextureAddress::TA_Clamp;
      encodedProperty.pTexture->filter = TextureFilter::TF_Nearest;

      if (!encodedProperty.pTexture->pTextureData) {
        UE_LOG(
            LogCesium,
            Error,
            TEXT(
                "Error encoding a property table property. Most likely could not allocate enough texture memory."));
        continue;
      }

      FTexture2DMipMap* pMip = new FTexture2DMipMap();
      encodedProperty.pTexture->pTextureData->Mips.Add(pMip);
      pMip->SizeX = textureDimension;
      pMip->SizeY = textureDimension;

      pMip->BulkData.Lock(LOCK_READ_WRITE);

      void* pTextureData = pMip->BulkData.Realloc(
          textureDimension * textureDimension * encodedFormat.pixelSize);

      gsl::span<std::byte> textureData(
          reinterpret_cast<std::byte*>(pTextureData),
          static_cast<size_t>(pMip->BulkData.GetBulkDataSize()));

      if (encodingDetails.Conversion ==
          ECesiumEncodedMetadataConversion::ParseColorFromString) {
        CesiumEncodedMetadataParseColorFromString::encode(
            *pDescription,
            property,
            textureData,
            encodedFormat.pixelSize);
      } else /* info.Conversion == ECesiumEncodedMetadataConversion::Coerce */ {
        CesiumEncodedMetadataCoerce::encode(
            *pDescription,
            property,
            textureData,
            encodedFormat.pixelSize);
      }
      pMip->BulkData.Unlock();
      pMip->BulkData.SetBulkDataFlags(BULKDATA_SingleUse);
    }

    if (pDescription->PropertyDetails.bHasOffset) {
      // If no offset is provided, default to 0, as specified by the spec.
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetOffset(property);
      encodedProperty.offset =
          !UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value)
              ? value
              : FITwinCesiumMetadataValue(0);
    }

    if (pDescription->PropertyDetails.bHasScale) {
      // If no scale is provided, default to 1, as specified by the spec.
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetScale(property);
      encodedProperty.scale =
          !UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value)
              ? value
              : FITwinCesiumMetadataValue(1);
    }

    if (pDescription->PropertyDetails.bHasNoDataValue) {
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetNoDataValue(
              property);
      encodedProperty.noData =
          !UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value)
              ? value
              : FITwinCesiumMetadataValue(0);
    }

    if (pDescription->PropertyDetails.bHasDefaultValue) {
      FITwinCesiumMetadataValue value =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetDefaultValue(
              property);
      encodedProperty.defaultValue =
          !UITwinCesiumMetadataValueBlueprintLibrary::IsEmpty(value)
              ? value
              : FITwinCesiumMetadataValue(0);
    }
  }

  return encodedPropertyTable;
}

EncodedPropertyTexture encodePropertyTextureAnyThreadPart(
    const FITwinCesiumPropertyTextureDescription& propertyTextureDescription,
    const FITwinCesiumPropertyTexture& propertyTexture,
    TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>&
        propertyTexturePropertyMap) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTexture)

  EncodedPropertyTexture encodedPropertyTexture;

  const TMap<FString, FITwinCesiumPropertyTextureProperty>& properties =
      UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(propertyTexture);

  encodedPropertyTexture.properties.Reserve(properties.Num());

  for (const auto& pair : properties) {
    const FITwinCesiumPropertyTextureProperty& property = pair.Value;

    const FITwinCesiumPropertyTexturePropertyDescription* pDescription =
        propertyTextureDescription.Properties.FindByPredicate(
            [&key = pair.Key](const FITwinCesiumPropertyTexturePropertyDescription&
                                  expectedProperty) {
              return key == expectedProperty.Name;
            });

    if (!pDescription) {
      continue;
    }

    if (!isValidPropertyTexturePropertyDescription(*pDescription, property)) {
      continue;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTextureProperty)

    EncodedPropertyTextureProperty& encodedProperty =
        encodedPropertyTexture.properties.Emplace_GetRef();
    encodedProperty.name = createHlslSafeName(pDescription->Name);
    encodedProperty.type =
        CesiumMetadataTypeToEncodingType(pDescription->PropertyDetails.Type);
    encodedProperty.textureCoordinateSetIndex = property.getTexCoordSetIndex();

    if (UITwinCesiumPropertyTexturePropertyBlueprintLibrary::
            GetPropertyTexturePropertyStatus(property) ==
        ECesiumPropertyTexturePropertyStatus::Valid) {

      const TArray<int64>& channels =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetChannels(property);
      const int32 channelCount = channels.Num();
      for (int32 i = 0; i < channelCount; i++) {
        encodedProperty.channels[i] = channels[i];
      }

      const CesiumGltf::ImageCesium* pImage = property.getImage();

      TWeakPtr<LoadedTextureResult>* pMappedUnrealImageIt =
          propertyTexturePropertyMap.Find(pImage);
      if (pMappedUnrealImageIt) {
        encodedProperty.pTexture = pMappedUnrealImageIt->Pin();
      } else {
        encodedProperty.pTexture = MakeShared<LoadedTextureResult>();
        // TODO: upgrade to new texture creation path.
        encodedProperty.pTexture->textureSource = LegacyTextureSource{};
        propertyTexturePropertyMap.Emplace(pImage, encodedProperty.pTexture);
        // This assumes that the texture's image only contains one byte per
        // channel.
        encodedProperty.pTexture->pTextureData = createTexturePlatformData(
            pImage->width,
            pImage->height,
            EPixelFormat::PF_R8G8B8A8_UINT);

        const CesiumGltf::Sampler* pSampler = property.getSampler();
        switch (pSampler->wrapS) {
        case CesiumGltf::Sampler::WrapS::REPEAT:
          encodedProperty.pTexture->addressX = TextureAddress::TA_Wrap;
          break;
        case CesiumGltf::Sampler::WrapS::MIRRORED_REPEAT:
          encodedProperty.pTexture->addressX = TextureAddress::TA_Mirror;
        case CesiumGltf::Sampler::WrapS::CLAMP_TO_EDGE:
        default:
          encodedProperty.pTexture->addressX = TextureAddress::TA_Clamp;
        }

        switch (pSampler->wrapT) {
        case CesiumGltf::Sampler::WrapT::REPEAT:
          encodedProperty.pTexture->addressY = TextureAddress::TA_Wrap;
          break;
        case CesiumGltf::Sampler::WrapT::MIRRORED_REPEAT:
          encodedProperty.pTexture->addressY = TextureAddress::TA_Mirror;
        case CesiumGltf::Sampler::WrapT::CLAMP_TO_EDGE:
        default:
          encodedProperty.pTexture->addressY = TextureAddress::TA_Clamp;
        }

        // TODO: account for texture filter
        encodedProperty.pTexture->filter = TextureFilter::TF_Nearest;

        if (!encodedProperty.pTexture->pTextureData) {
          UE_LOG(
              LogCesium,
              Error,
              TEXT(
                  "Error encoding a property texture property. Most likely could not allocate enough texture memory."));
          continue;
        }

        FTexture2DMipMap* pMip = new FTexture2DMipMap();
        encodedProperty.pTexture->pTextureData->Mips.Add(pMip);
        pMip->SizeX = pImage->width;
        pMip->SizeY = pImage->height;
        pMip->BulkData.Lock(LOCK_READ_WRITE);

        void* pTextureData = pMip->BulkData.Realloc(pImage->pixelData.size());

        FMemory::Memcpy(
            pTextureData,
            pImage->pixelData.data(),
            pImage->pixelData.size());

        pMip->BulkData.Unlock();
        pMip->BulkData.SetBulkDataFlags(BULKDATA_SingleUse);
      }
    };

    if (pDescription->PropertyDetails.bHasOffset) {
      encodedProperty.offset =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetOffset(property);
    }

    if (pDescription->PropertyDetails.bHasScale) {
      encodedProperty.scale =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetScale(property);
    }

    if (pDescription->PropertyDetails.bHasNoDataValue) {
      encodedProperty.noData =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetNoDataValue(
              property);
    }

    if (pDescription->PropertyDetails.bHasDefaultValue) {
      encodedProperty.defaultValue =
          UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetDefaultValue(
              property);
    }
  }

  return encodedPropertyTexture;
}

EncodedPrimitiveMetadata encodePrimitiveMetadataAnyThreadPart(
    const FITwinCesiumPrimitiveMetadataDescription& metadataDescription,
    const FITwinCesiumPrimitiveMetadata& primitiveMetadata,
    const FITwinCesiumModelMetadata& modelMetadata) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeMetadataPrimitive)

  EncodedPrimitiveMetadata result;

  const TArray<FITwinCesiumPropertyTexture>& propertyTextures =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTextures(modelMetadata);
  result.propertyTextureIndices.Reserve(
      metadataDescription.PropertyTextureNames.Num());

  for (int32 i = 0; i < propertyTextures.Num(); i++) {
    const FITwinCesiumPropertyTexture& propertyTexture = propertyTextures[i];
    FString propertyTextureName = getNameForPropertyTexture(propertyTexture);
    const FString* pName =
        metadataDescription.PropertyTextureNames.Find(propertyTextureName);
    // Confirm that the named property texture is actually present. This
    // indicates that it is acceptable to pass the texture coordinate index to
    // the material layer.
    if (pName) {
      result.propertyTextureIndices.Add(i);
    }
  }

  return result;
}

EncodedModelMetadata encodeModelMetadataAnyThreadPart(
    const FITwinCesiumModelMetadataDescription& metadataDescription,
    const FITwinCesiumModelMetadata& metadata) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeModelMetadata)

  EncodedModelMetadata result;

  const TArray<FITwinCesiumPropertyTable>& propertyTables =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTables(metadata);
  result.propertyTables.Reserve(propertyTables.Num());
  for (const auto& propertyTable : propertyTables) {
    const FString propertyTableName = getNameForPropertyTable(propertyTable);

    const FITwinCesiumPropertyTableDescription* pExpectedPropertyTable =
        metadataDescription.PropertyTables.FindByPredicate(
            [&propertyTableName](
                const FITwinCesiumPropertyTableDescription& expectedPropertyTable) {
              return propertyTableName == expectedPropertyTable.Name;
            });

    if (pExpectedPropertyTable) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTable)

      auto& encodedPropertyTable =
          result.propertyTables.Emplace_GetRef(encodePropertyTableAnyThreadPart(
              *pExpectedPropertyTable,
              propertyTable));
      encodedPropertyTable.name = propertyTableName;
    }
  }

  const TArray<FITwinCesiumPropertyTexture>& propertyTextures =
      UITwinCesiumModelMetadataBlueprintLibrary::GetPropertyTextures(metadata);
  result.propertyTextures.Reserve(propertyTextures.Num());

  TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>
      propertyTexturePropertyMap;
  propertyTexturePropertyMap.Reserve(propertyTextures.Num());

  for (const auto& propertyTexture : propertyTextures) {
    const FString propertyTextureName =
        getNameForPropertyTexture(propertyTexture);

    const FITwinCesiumPropertyTextureDescription* pExpectedPropertyTexture =
        metadataDescription.PropertyTextures.FindByPredicate(
            [&propertyTextureName](const FITwinCesiumPropertyTextureDescription&
                                       expectedPropertyTexture) {
              return propertyTextureName == expectedPropertyTexture.Name;
            });

    if (pExpectedPropertyTexture) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTexture)

      auto& encodedPropertyTexture = result.propertyTextures.Emplace_GetRef(
          encodePropertyTextureAnyThreadPart(
              *pExpectedPropertyTexture,
              propertyTexture,
              propertyTexturePropertyMap));
      encodedPropertyTexture.name = propertyTextureName;
    }
  }

  return result;
}

bool encodePropertyTableGameThreadPart(
    EncodedPropertyTable& encodedPropertyTable) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTable)

  bool success = true;

  for (EncodedPropertyTableProperty& encodedProperty :
       encodedPropertyTable.properties) {
    if (encodedProperty.pTexture) {
      success &=
          loadTextureGameThreadPart(encodedProperty.pTexture.Get()) != nullptr;
    }
  }

  return success;
}

bool encodePropertyTextureGameThreadPart(
    TArray<LoadedTextureResult*>& uniqueTextures,
    EncodedPropertyTexture& encodedPropertyTexture) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyTexture)

  bool success = true;

  for (EncodedPropertyTextureProperty& property :
       encodedPropertyTexture.properties) {
    if (uniqueTextures.Find(property.pTexture.Get()) == INDEX_NONE) {
      success &= loadTextureGameThreadPart(property.pTexture.Get()) != nullptr;
      uniqueTextures.Emplace(property.pTexture.Get());
    }
  }

  return success;
}

bool encodeModelMetadataGameThreadPart(EncodedModelMetadata& encodedMetadata) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeMetadata)

  bool success = true;

  TArray<LoadedTextureResult*> uniqueTextures;
  uniqueTextures.Reserve(encodedMetadata.propertyTextures.Num());
  for (auto& encodedPropertyTextureIt : encodedMetadata.propertyTextures) {
    success &= encodePropertyTextureGameThreadPart(
        uniqueTextures,
        encodedPropertyTextureIt);
  }

  for (auto& encodedPropertyTable : encodedMetadata.propertyTables) {
    success &= encodePropertyTableGameThreadPart(encodedPropertyTable);
  }

  return success;
}

void destroyEncodedModelMetadata(EncodedModelMetadata& encodedMetadata) {
  for (auto& propertyTable : encodedMetadata.propertyTables) {
    for (EncodedPropertyTableProperty& encodedProperty :
         propertyTable.properties) {
      if (encodedProperty.pTexture &&
          encodedProperty.pTexture->pTexture.IsValid()) {
        CesiumLifetime::destroy(encodedProperty.pTexture->pTexture.Get());
        encodedProperty.pTexture->pTexture.Reset();
      }
    }
  }

  for (auto& encodedPropertyTextureIt : encodedMetadata.propertyTextures) {
    for (EncodedPropertyTextureProperty& encodedPropertyTextureProperty :
         encodedPropertyTextureIt.properties) {
      if (encodedPropertyTextureProperty.pTexture->pTexture.IsValid()) {
        CesiumLifetime::destroy(
            encodedPropertyTextureProperty.pTexture->pTexture.Get());
        encodedPropertyTextureProperty.pTexture->pTexture.Reset();
      }
    }
  }
}

// The result should be a safe hlsl identifier, but any name clashes
// after fixing safety will not be automatically handled.
FString createHlslSafeName(const FString& rawName) {
  static const FString identifierHeadChar =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  static const FString identifierTailChar = identifierHeadChar + "0123456789";

  FString safeName = rawName;
  int32 _;
  if (safeName.Len() == 0) {
    return "_";
  } else {
    if (!identifierHeadChar.FindChar(safeName[0], _)) {
      safeName = "_" + safeName;
    }
  }

  for (size_t i = 1; i < safeName.Len(); ++i) {
    if (!identifierTailChar.FindChar(safeName[i], _)) {
      safeName[i] = '_';
    }
  }

  return safeName;
}

} // namespace CesiumEncodedFeaturesMetadata
