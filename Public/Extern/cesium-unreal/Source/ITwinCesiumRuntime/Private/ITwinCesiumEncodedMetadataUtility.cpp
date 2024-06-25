// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumEncodedMetadataUtility.h"
#include "ITwinCesiumEncodedMetadataComponent.h"
#include "ITwinCesiumFeatureIdAttribute.h"
#include "ITwinCesiumFeatureIdTexture.h"
#include "ITwinCesiumLifetime.h"
#include "ITwinCesiumMetadataPrimitive.h"
#include "ITwinCesiumModelMetadata.h"
#include "ITwinCesiumPropertyArray.h"
#include "ITwinCesiumPropertyArrayBlueprintLibrary.h"
#include "ITwinCesiumPropertyTable.h"
#include "ITwinCesiumPropertyTableProperty.h"
#include "ITwinCesiumPropertyTexture.h"
#include "ITwinCesiumRuntime.h"
#include "Containers/Map.h"
#include "PixelFormat.h"
#include "TextureResource.h"
#include "ITwinUnrealMetadataConversions.h"
#include <CesiumGltf/FeatureIdTextureView.h>
#include <CesiumGltf/PropertyTexturePropertyView.h>
#include <CesiumGltf/PropertyTextureView.h>
#include <CesiumUtility/Tracing.h>
#include <glm/gtx/integer.hpp>
#include <unordered_map>

using namespace ITwinCesiumTextureUtility;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

namespace ITwinCesiumEncodedMetadataUtility {

namespace {

struct EncodedPixelFormat {
  EPixelFormat format;
  size_t pixelSize;
};

// TODO: consider picking better pixel formats when they are available for the
// current platform.
EncodedPixelFormat getPixelFormat(
    EITwinCesiumMetadataPackedGpuType_DEPRECATED type,
    int64 componentCount,
    bool isNormalized) {

  switch (type) {
  case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Uint8_DEPRECATED:
    switch (componentCount) {
    case 1:
      return {isNormalized ? EPixelFormat::PF_R8 : EPixelFormat::PF_R8_UINT, 1};
    case 2:
    case 3:
    case 4:
      return {
          isNormalized ? EPixelFormat::PF_R8G8B8A8
                       : EPixelFormat::PF_R8G8B8A8_UINT,
          4};
    default:
      return {EPixelFormat::PF_Unknown, 0};
    }
  case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Float_DEPRECATED:
    switch (componentCount) {
    case 1:
      return {EPixelFormat::PF_R32_FLOAT, 4};
    case 2:
    case 3:
    case 4:
      // Note this is ABGR
      return {EPixelFormat::PF_A32B32G32R32F, 16};
    }
  default:
    return {EPixelFormat::PF_Unknown, 0};
  }
}
} // namespace

EncodedMetadataFeatureTable encodeMetadataFeatureTableAnyThreadPart(
    const FITwinFeatureTableDescription& featureTableDescription,
    const FITwinCesiumPropertyTable& featureTable) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTable)

  EncodedMetadataFeatureTable encodedFeatureTable;

  int64 featureCount =
      UITwinCesiumPropertyTableBlueprintLibrary::GetPropertyTableCount(featureTable);

  const TMap<FString, FITwinCesiumPropertyTableProperty>& properties =
      UITwinCesiumPropertyTableBlueprintLibrary::GetProperties(featureTable);

  encodedFeatureTable.encodedProperties.Reserve(properties.Num());
  for (const auto& pair : properties) {
    const FITwinCesiumPropertyTableProperty& property = pair.Value;

    const FITwinPropertyDescription* pExpectedProperty =
        featureTableDescription.Properties.FindByPredicate(
            [&key = pair.Key](const FITwinPropertyDescription& expectedProperty) {
              return key == expectedProperty.Name;
            });

    if (!pExpectedProperty) {
      continue;
    }

    FITwinCesiumMetadataValueType trueType =
        UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValueType(property);
    bool isArray = trueType.bIsArray;
    bool isNormalized =
        UITwinCesiumPropertyTablePropertyBlueprintLibrary::IsNormalized(property);

    int64 componentCount;
    if (isArray) {
      componentCount =
          UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArraySize(property);
    } else {
      componentCount = 1;
    }

    int32 expectedComponentCount = 1;
    switch (pExpectedProperty->Type) {
    // case EITwinCesiumPropertyType::Scalar:
    //  expectedComponentCount = 1;
    //  break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec2_DEPRECATED:
      expectedComponentCount = 2;
      break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec3_DEPRECATED:
      expectedComponentCount = 3;
      break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec4_DEPRECATED:
      expectedComponentCount = 4;
    };

    if (expectedComponentCount != componentCount) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT("Unexpected component count in feature table property."));
      continue;
    }

    // Coerce the true type into the expected gpu component type.
    EITwinCesiumMetadataPackedGpuType_DEPRECATED gpuType =
        EITwinCesiumMetadataPackedGpuType_DEPRECATED::None_DEPRECATED;
    if (pExpectedProperty->ComponentType ==
        EITwinCesiumPropertyComponentType_DEPRECATED::Uint8_DEPRECATED) {
      gpuType = EITwinCesiumMetadataPackedGpuType_DEPRECATED::Uint8_DEPRECATED;
    } else /*if (expected type is float)*/ {
      gpuType = EITwinCesiumMetadataPackedGpuType_DEPRECATED::Float_DEPRECATED;
    }

    if (pExpectedProperty->Normalized != isNormalized) {
      if (isNormalized) {
        UE_LOG(
            LogITwinCesium,
            Warning,
            TEXT("Unexpected normalization in feature table property."));
      } else {
        UE_LOG(
            LogITwinCesium,
            Warning,
            TEXT("Feature table property not normalized as expected"));
      }
      continue;
    }

    // Only support normalization of uint8 for now
    if (isNormalized &&
        trueType.ComponentType != EITwinCesiumMetadataComponentType::Uint8) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT(
              "Feature table property has unexpected type for normalization, only normalization of Uint8 is supported."));
      continue;
    }

    EncodedPixelFormat encodedFormat =
        getPixelFormat(gpuType, componentCount, isNormalized);

    if (encodedFormat.format == EPixelFormat::PF_Unknown) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT(
              "Unable to determine a suitable GPU format for this feature table property."));
      continue;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodePropertyArray)

    EncodedMetadataProperty& encodedProperty =
        encodedFeatureTable.encodedProperties.Emplace_GetRef();
    encodedProperty.name =
        "FTB_" + featureTableDescription.Name + "_" + pair.Key;

    int64 floorSqrtFeatureCount = glm::sqrt(featureCount);
    int64 ceilSqrtFeatureCount =
        (floorSqrtFeatureCount * floorSqrtFeatureCount == featureCount)
            ? floorSqrtFeatureCount
            : (floorSqrtFeatureCount + 1);
    encodedProperty.pTexture = MakeUnique<LoadedTextureResult>();
    encodedProperty.pTexture->sRGB = false;
    // TODO: upgrade to new texture creation path.
    encodedProperty.pTexture->textureSource = LegacyTextureSource{};
    encodedProperty.pTexture->pTextureData = createTexturePlatformData(
        ceilSqrtFeatureCount,
        ceilSqrtFeatureCount,
        encodedFormat.format);

    encodedProperty.pTexture->addressX = TextureAddress::TA_Clamp;
    encodedProperty.pTexture->addressY = TextureAddress::TA_Clamp;
    encodedProperty.pTexture->filter = TextureFilter::TF_Nearest;

    if (!encodedProperty.pTexture->pTextureData) {
      UE_LOG(
          LogITwinCesium,
          Error,
          TEXT(
              "Error encoding a feature table property. Most likely could not allocate enough texture memory."));
      continue;
    }

    FTexture2DMipMap* pMip = new FTexture2DMipMap();
    encodedProperty.pTexture->pTextureData->Mips.Add(pMip);
    pMip->SizeX = ceilSqrtFeatureCount;
    pMip->SizeY = ceilSqrtFeatureCount;
    pMip->BulkData.Lock(LOCK_READ_WRITE);

    void* pTextureData = pMip->BulkData.Realloc(
        ceilSqrtFeatureCount * ceilSqrtFeatureCount * encodedFormat.pixelSize);

    if (isArray) {
      switch (gpuType) {
      case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Uint8_DEPRECATED: {
        uint8* pWritePos = reinterpret_cast<uint8*>(pTextureData);
        for (int64 i = 0; i < featureCount; ++i) {
          FITwinCesiumPropertyArray arrayProperty =
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(
                  property,
                  i);
          for (int64 j = 0; j < componentCount; ++j) {
            *(pWritePos + j) =
                UITwinCesiumPropertyArrayBlueprintLibrary::GetByte(arrayProperty, j);
          }
          pWritePos += encodedFormat.pixelSize;
        }
      } break;
      case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Float_DEPRECATED: {
        uint8* pWritePos = reinterpret_cast<uint8*>(pTextureData);
        for (int64 i = 0; i < featureCount; ++i) {
          FITwinCesiumPropertyArray arrayProperty =
              UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetArray(
                  property,
                  i);
          // Floats are encoded backwards (e.g., ABGR)
          float* pWritePosF =
              reinterpret_cast<float*>(pWritePos + encodedFormat.pixelSize) - 1;
          for (int64 j = 0; j < componentCount; ++j) {
            *pWritePosF = UITwinCesiumPropertyArrayBlueprintLibrary::GetFloat(
                arrayProperty,
                j);
            --pWritePosF;
          }
          pWritePos += encodedFormat.pixelSize;
        }
      } break;
      }
    } else {
      switch (gpuType) {
      case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Uint8_DEPRECATED: {
        uint8* pWritePos = reinterpret_cast<uint8*>(pTextureData);
        for (int64 i = 0; i < featureCount; ++i) {
          *pWritePos = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetByte(
              property,
              i);
          ++pWritePos;
        }
      } break;
      case EITwinCesiumMetadataPackedGpuType_DEPRECATED::Float_DEPRECATED: {
        float* pWritePosF = reinterpret_cast<float*>(pTextureData);
        for (int64 i = 0; i < featureCount; ++i) {
          *pWritePosF = UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetFloat(
              property,
              i);
          ++pWritePosF;
        }
      } break;
      }
    }

    pMip->BulkData.Unlock();
    pMip->BulkData.SetBulkDataFlags(BULKDATA_SingleUse);
  }

  return encodedFeatureTable;
}

EncodedFeatureTexture encodeFeatureTextureAnyThreadPart(
    TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>&
        featureTexturePropertyMap,
    const FITwinFeatureTextureDescription& featureTextureDescription,
    const FString& featureTextureName,
    const FITwinCesiumPropertyTexture& featureTexture) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTexture)

  EncodedFeatureTexture encodedFeatureTexture;

  const auto& properties =
      UITwinCesiumPropertyTextureBlueprintLibrary::GetProperties(featureTexture);
  encodedFeatureTexture.properties.Reserve(properties.Num());

  for (const auto& propertyIt : properties) {
    const FITwinFeatureTexturePropertyDescription* pPropertyDescription =
        featureTextureDescription.Properties.FindByPredicate(
            [propertyName = propertyIt.Key](
                const FITwinFeatureTexturePropertyDescription& expectedProperty) {
              return propertyName == expectedProperty.Name;
            });

    if (!pPropertyDescription) {
      continue;
    }

    const FITwinCesiumPropertyTextureProperty& featureTextureProperty =
        propertyIt.Value;

    const CesiumGltf::ImageCesium* pImage = featureTextureProperty.getImage();

    if (!pImage) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT("This feature texture property does not have a valid image."));
      continue;
    }

    int32 expectedComponentCount = 0;
    switch (pPropertyDescription->Type) {
    case EITwinCesiumPropertyType_DEPRECATED::Scalar_DEPRECATED:
      expectedComponentCount = 1;
      break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec2_DEPRECATED:
      expectedComponentCount = 2;
      break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec3_DEPRECATED:
      expectedComponentCount = 3;
      break;
    case EITwinCesiumPropertyType_DEPRECATED::Vec4_DEPRECATED:
      expectedComponentCount = 4;
      break;
    };

    int32 actualComponentCount = 0;
    FITwinCesiumMetadataValueType valueType =
        UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetValueType(
            featureTextureProperty);
    switch (valueType.Type) {
    case EITwinCesiumMetadataType::Scalar:
      actualComponentCount = 1;
      break;
    case EITwinCesiumMetadataType::Vec2:
      actualComponentCount = 2;
      break;
    case EITwinCesiumMetadataType::Vec3:
      actualComponentCount = 3;
      break;
    case EITwinCesiumMetadataType::Vec4:
      actualComponentCount = 4;
      break;
    }

    if (expectedComponentCount != actualComponentCount) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT(
              "This feature texture property does not have the expected component count"));
      continue;
    }

    bool isNormalized =
        UITwinCesiumPropertyTexturePropertyBlueprintLibrary::IsNormalized(
            featureTextureProperty);
    if (pPropertyDescription->Normalized != isNormalized) {
      UE_LOG(
          LogITwinCesium,
          Warning,
          TEXT(
              "This feature texture property does not have the expected normalization."));
      continue;
    }

    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTextureProperty)

    EncodedFeatureTextureProperty& encodedFeatureTextureProperty =
        encodedFeatureTexture.properties.Emplace_GetRef();

    encodedFeatureTextureProperty.baseName =
        "FTX_" + featureTextureName + "_" + pPropertyDescription->Name + "_";
    encodedFeatureTextureProperty.textureCoordinateAttributeId =
        featureTextureProperty.getTexCoordSetIndex();

    const auto& channels =
        UITwinCesiumPropertyTexturePropertyBlueprintLibrary::GetChannels(
            featureTextureProperty);
    encodedFeatureTextureProperty.channelOffsets[0] = channels[0];
    encodedFeatureTextureProperty.channelOffsets[1] = channels[1];
    encodedFeatureTextureProperty.channelOffsets[2] = channels[2];
    encodedFeatureTextureProperty.channelOffsets[3] = channels[3];

    TWeakPtr<LoadedTextureResult>* pMappedUnrealImageIt =
        featureTexturePropertyMap.Find(pImage);
    if (pMappedUnrealImageIt) {
      encodedFeatureTextureProperty.pTexture = pMappedUnrealImageIt->Pin();
    } else {
      encodedFeatureTextureProperty.pTexture =
          MakeShared<LoadedTextureResult>();
      // TODO: upgrade to new texture creation path.
      encodedFeatureTextureProperty.pTexture->textureSource =
          LegacyTextureSource{};
      featureTexturePropertyMap.Emplace(
          pImage,
          encodedFeatureTextureProperty.pTexture);
      encodedFeatureTextureProperty.pTexture->pTextureData =
          createTexturePlatformData(
              pImage->width,
              pImage->height,
              // TODO : currently the unnormalized pixels are always in unsigned
              // R8G8B8A8 form, but this does not necessarily need to be the
              // case in the future.
              isNormalized ? EPixelFormat::PF_R8G8B8A8
                           : EPixelFormat::PF_R8G8B8A8_UINT);

      encodedFeatureTextureProperty.pTexture->addressX =
          TextureAddress::TA_Clamp;
      encodedFeatureTextureProperty.pTexture->addressY =
          TextureAddress::TA_Clamp;
      encodedFeatureTextureProperty.pTexture->filter =
          TextureFilter::TF_Nearest;

      if (!encodedFeatureTextureProperty.pTexture->pTextureData) {
        UE_LOG(
            LogITwinCesium,
            Error,
            TEXT(
                "Error encoding a feature texture property. Most likely could not allocate enough texture memory."));
        continue;
      }

      FTexture2DMipMap* pMip = new FTexture2DMipMap();
      encodedFeatureTextureProperty.pTexture->pTextureData->Mips.Add(pMip);
      pMip->SizeX = pImage->width;
      pMip->SizeY = pImage->height;
      pMip->BulkData.Lock(LOCK_READ_WRITE);

      void* pTextureData = pMip->BulkData.Realloc(pImage->pixelData.size());

      FMemory::Memcpy(
          pTextureData,
          pImage->pixelData.data(),
          pImage->pixelData.size());

      pMip->BulkData.Unlock();
    }
  }

  return encodedFeatureTexture;
}

EncodedMetadataPrimitive encodeMetadataPrimitiveAnyThreadPart(
    const FITwinMetadataDescription& metadataDescription,
    const FITwinCesiumMetadataPrimitive& primitive) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeMetadataPrimitive)

  EncodedMetadataPrimitive result;

  const TArray<FITwinCesiumFeatureIdTexture>& featureIdTextures =
      UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureIdTextures(primitive);
  const TArray<FITwinCesiumFeatureIdAttribute>& featureIdAttributes =
      UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureIdAttributes(
          primitive);

  const TArray<FString>& featureTextureNames =
      UITwinCesiumMetadataPrimitiveBlueprintLibrary::GetFeatureTextureNames(
          primitive);
  result.featureTextureNames.Reserve(featureTextureNames.Num());

  for (const FITwinFeatureTextureDescription& expectedFeatureTexture :
       metadataDescription.FeatureTextures) {
    if (featureTextureNames.Find(expectedFeatureTexture.Name) != INDEX_NONE) {
      result.featureTextureNames.Add(expectedFeatureTexture.Name);
    }
  }

  TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>
      featureIdTextureMap;
  featureIdTextureMap.Reserve(featureIdTextures.Num());

  result.encodedFeatureIdTextures.Reserve(featureIdTextures.Num());
  result.encodedFeatureIdAttributes.Reserve(featureIdAttributes.Num());

  // Imposed implementation limitation: Assume only upto one feature id texture
  // or attribute corresponds to each feature table.
  for (const FITwinFeatureTableDescription& expectedFeatureTable :
       metadataDescription.FeatureTables) {
    const FString& featureTableName = expectedFeatureTable.Name;

    if (expectedFeatureTable.AccessType ==
        EITwinCesiumFeatureTableAccessType_DEPRECATED::Texture_DEPRECATED) {

      const FITwinCesiumFeatureIdTexture* pFeatureIdTexture =
          featureIdTextures.FindByPredicate([&featureTableName](
                                                const FITwinCesiumFeatureIdTexture&
                                                    featureIdTexture) {
            return featureTableName ==
                   UITwinCesiumFeatureIdTextureBlueprintLibrary::GetFeatureTableName(
                       featureIdTexture);
          });

      if (pFeatureIdTexture) {
        const CesiumGltf::FeatureIdTextureView& featureIdTextureView =
            pFeatureIdTexture->getFeatureIdTextureView();
        const CesiumGltf::ImageCesium* pFeatureIdImage =
            featureIdTextureView.getImage();

        if (!pFeatureIdImage) {
          UE_LOG(
              LogITwinCesium,
              Warning,
              TEXT("Feature id texture missing valid image."));
          continue;
        }

        TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureIdTexture)

        EncodedFeatureIdTexture& encodedFeatureIdTexture =
            result.encodedFeatureIdTextures.Emplace_GetRef();

        const auto channels = featureIdTextureView.getChannels();
        encodedFeatureIdTexture.baseName = "FIT_" + featureTableName + "_";
        encodedFeatureIdTexture.channel = channels.size() ? channels[0] : 0;
        encodedFeatureIdTexture.textureCoordinateAttributeId =
            featureIdTextureView.getTexCoordSetIndex();

        TWeakPtr<LoadedTextureResult>* pMappedUnrealImageIt =
            featureIdTextureMap.Find(pFeatureIdImage);
        if (pMappedUnrealImageIt) {
          encodedFeatureIdTexture.pTexture = pMappedUnrealImageIt->Pin();
        } else {
          encodedFeatureIdTexture.pTexture = MakeShared<LoadedTextureResult>();
          encodedFeatureIdTexture.pTexture->sRGB = false;
          // TODO: upgrade to new texture creation path
          encodedFeatureIdTexture.pTexture->textureSource =
              LegacyTextureSource{};
          featureIdTextureMap.Emplace(
              pFeatureIdImage,
              encodedFeatureIdTexture.pTexture);
          encodedFeatureIdTexture.pTexture->pTextureData =
              createTexturePlatformData(
                  pFeatureIdImage->width,
                  pFeatureIdImage->height,
                  // TODO: currently this is always the case, but doesn't have
                  // to be
                  EPixelFormat::PF_R8G8B8A8_UINT);

          encodedFeatureIdTexture.pTexture->addressX = TextureAddress::TA_Clamp;
          encodedFeatureIdTexture.pTexture->addressY = TextureAddress::TA_Clamp;
          encodedFeatureIdTexture.pTexture->filter = TextureFilter::TF_Nearest;

          if (!encodedFeatureIdTexture.pTexture->pTextureData) {
            UE_LOG(
                LogITwinCesium,
                Error,
                TEXT(
                    "Error encoding a feature table property. Most likely could not allocate enough texture memory."));
            continue;
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

        encodedFeatureIdTexture.featureTableName = featureTableName;
      }
    } else if (
        expectedFeatureTable.AccessType ==
        EITwinCesiumFeatureTableAccessType_DEPRECATED::Attribute_DEPRECATED) {
      for (size_t i = 0; i < featureIdAttributes.Num(); ++i) {
        const FITwinCesiumFeatureIdAttribute& featureIdAttribute =
            featureIdAttributes[i];

        if (featureTableName ==
            UITwinCesiumFeatureIdAttributeBlueprintLibrary::GetFeatureTableName(
                featureIdAttribute)) {
          EncodedFeatureIdAttribute& encodedFeatureIdAttribute =
              result.encodedFeatureIdAttributes.Emplace_GetRef();

          encodedFeatureIdAttribute.name = "FA_" + featureTableName;
          encodedFeatureIdAttribute.featureTableName = featureTableName;
          encodedFeatureIdAttribute.index = static_cast<int32>(i);

          break;
        }
      }
    }
  }

  return result;
}

EncodedMetadata encodeMetadataAnyThreadPart(
    const FITwinMetadataDescription& metadataDescription,
    const FITwinCesiumModelMetadata& metadata) {

  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeMetadataModel)

  EncodedMetadata result;

  const TMap<FString, FITwinCesiumPropertyTable>& featureTables =
      UITwinCesiumModelMetadataBlueprintLibrary::GetFeatureTables(metadata);
  result.encodedFeatureTables.Reserve(featureTables.Num());
  for (const auto& featureTableIt : featureTables) {
    const FString& featureTableName = featureTableIt.Key;

    const FITwinFeatureTableDescription* pExpectedFeatureTable =
        metadataDescription.FeatureTables.FindByPredicate(
            [&featureTableName](
                const FITwinFeatureTableDescription& expectedFeatureTable) {
              return featureTableName == expectedFeatureTable.Name;
            });

    if (pExpectedFeatureTable) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTable)

      result.encodedFeatureTables.Emplace(
          featureTableName,
          encodeMetadataFeatureTableAnyThreadPart(
              *pExpectedFeatureTable,
              featureTableIt.Value));
    }
  }

  const TMap<FString, FITwinCesiumPropertyTexture>& featureTextures =
      UITwinCesiumModelMetadataBlueprintLibrary::GetFeatureTextures(metadata);
  result.encodedFeatureTextures.Reserve(featureTextures.Num());
  TMap<const CesiumGltf::ImageCesium*, TWeakPtr<LoadedTextureResult>>
      featureTexturePropertyMap;
  featureTexturePropertyMap.Reserve(featureTextures.Num());
  for (const auto& featureTextureIt : featureTextures) {
    const FString& featureTextureName = featureTextureIt.Key;

    const FITwinFeatureTextureDescription* pExpectedFeatureTexture =
        metadataDescription.FeatureTextures.FindByPredicate(
            [&featureTextureName](
                const FITwinFeatureTextureDescription& expectedFeatureTexture) {
              return featureTextureName == expectedFeatureTexture.Name;
            });

    if (pExpectedFeatureTexture) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTexture)

      result.encodedFeatureTextures.Emplace(
          featureTextureName,
          encodeFeatureTextureAnyThreadPart(
              featureTexturePropertyMap,
              *pExpectedFeatureTexture,
              featureTextureName,
              featureTextureIt.Value));
    }
  }

  return result;
}

bool encodeMetadataFeatureTableGameThreadPart(
    EncodedMetadataFeatureTable& encodedFeatureTable) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeFeatureTable)

  bool success = true;

  for (EncodedMetadataProperty& encodedProperty :
       encodedFeatureTable.encodedProperties) {
    success &=
        loadTextureGameThreadPart(encodedProperty.pTexture.Get()) != nullptr;
  }

  return success;
}

bool encodeFeatureTextureGameThreadPart(
    TArray<LoadedTextureResult*>& uniqueTextures,
    EncodedFeatureTexture& encodedFeatureTexture) {
  bool success = true;

  for (EncodedFeatureTextureProperty& property :
       encodedFeatureTexture.properties) {
    if (uniqueTextures.Find(property.pTexture.Get()) == INDEX_NONE) {
      success &= loadTextureGameThreadPart(property.pTexture.Get()) != nullptr;
      uniqueTextures.Emplace(property.pTexture.Get());
    }
  }

  return success;
}

bool encodeMetadataPrimitiveGameThreadPart(
    EncodedMetadataPrimitive& encodedPrimitive) {
  bool success = true;

  TArray<const LoadedTextureResult*> uniqueFeatureIdImages;
  uniqueFeatureIdImages.Reserve(
      encodedPrimitive.encodedFeatureIdTextures.Num());

  for (EncodedFeatureIdTexture& encodedFeatureIdTexture :
       encodedPrimitive.encodedFeatureIdTextures) {
    if (uniqueFeatureIdImages.Find(encodedFeatureIdTexture.pTexture.Get()) ==
        INDEX_NONE) {
      success &= loadTextureGameThreadPart(
                     encodedFeatureIdTexture.pTexture.Get()) != nullptr;
      uniqueFeatureIdImages.Emplace(encodedFeatureIdTexture.pTexture.Get());
    }
  }

  return success;
}

bool encodeMetadataGameThreadPart(EncodedMetadata& encodedMetadata) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::EncodeMetadata)

  bool success = true;

  TArray<LoadedTextureResult*> uniqueTextures;
  uniqueTextures.Reserve(encodedMetadata.encodedFeatureTextures.Num());
  for (auto& encodedFeatureTextureIt : encodedMetadata.encodedFeatureTextures) {
    success &= encodeFeatureTextureGameThreadPart(
        uniqueTextures,
        encodedFeatureTextureIt.Value);
  }

  for (auto& encodedFeatureTableIt : encodedMetadata.encodedFeatureTables) {
    success &=
        encodeMetadataFeatureTableGameThreadPart(encodedFeatureTableIt.Value);
  }

  return success;
}

void destroyEncodedMetadataPrimitive(
    EncodedMetadataPrimitive& encodedPrimitive) {
  for (EncodedFeatureIdTexture& encodedFeatureIdTexture :
       encodedPrimitive.encodedFeatureIdTextures) {

    if (encodedFeatureIdTexture.pTexture->pTexture.IsValid()) {
      FITwinCesiumLifetime::destroy(encodedFeatureIdTexture.pTexture->pTexture.Get());
      encodedFeatureIdTexture.pTexture->pTexture.Reset();
    }
  }
}

void destroyEncodedMetadata(EncodedMetadata& encodedMetadata) {

  // Destroy encoded feature tables.
  for (auto& encodedFeatureTableIt : encodedMetadata.encodedFeatureTables) {
    for (EncodedMetadataProperty& encodedProperty :
         encodedFeatureTableIt.Value.encodedProperties) {
      if (encodedProperty.pTexture->pTexture.IsValid()) {
        FITwinCesiumLifetime::destroy(encodedProperty.pTexture->pTexture.Get());
        encodedProperty.pTexture->pTexture.Reset();
      }
    }
  }

  // Destroy encoded feature textures.
  for (auto& encodedFeatureTextureIt : encodedMetadata.encodedFeatureTextures) {
    for (EncodedFeatureTextureProperty& encodedFeatureTextureProperty :
         encodedFeatureTextureIt.Value.properties) {
      if (encodedFeatureTextureProperty.pTexture->pTexture.IsValid()) {
        FITwinCesiumLifetime::destroy(
            encodedFeatureTextureProperty.pTexture->pTexture.Get());
        encodedFeatureTextureProperty.pTexture->pTexture.Reset();
      }
    }
  }
}

// The result should be a safe hlsl identifier, but any name clashes after
// fixing safety will not be automatically handled.
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

} // namespace ITwinCesiumEncodedMetadataUtility

PRAGMA_ENABLE_DEPRECATION_WARNINGS
