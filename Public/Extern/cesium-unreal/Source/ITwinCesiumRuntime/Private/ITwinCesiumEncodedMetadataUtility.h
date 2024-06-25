// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumMetadataValueType.h"
#include "ITwinCesiumTextureUtility.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

struct FITwinCesiumModelMetadata;
struct FITwinCesiumMetadataPrimitive;
struct FITwinCesiumPropertyTable;
struct FITwinCesiumPropertyTexture;
struct FITwinFeatureTableDescription;
struct FITwinFeatureTextureDescription;
struct FITwinMetadataDescription;
struct FITwinCesiumPrimitiveFeaturesDescription;

/**
 * DEPRECATED. Use ITwinCesiumEncodedFeaturesMetadata instead.
 */
namespace ITwinCesiumEncodedMetadataUtility {
struct EncodedMetadataProperty {
  /**
   * @brief The name of this property.
   */
  FString name;

  /**
   * @brief The encoded property array.
   */
  TUniquePtr<ITwinCesiumTextureUtility::LoadedTextureResult> pTexture;
};

struct EncodedMetadataFeatureTable {
  /**
   * @brief The encoded properties in this feature table.
   */
  TArray<EncodedMetadataProperty> encodedProperties;
};

struct EncodedFeatureIdTexture {
  /**
   * @brief The name to use for this feature id texture in the shader.
   */
  FString baseName;

  /**
   * @brief The encoded feature table corresponding to this feature id
   * texture.
   */
  FString featureTableName;

  /**
   * @brief The actual feature id texture.
   */
  TSharedPtr<ITwinCesiumTextureUtility::LoadedTextureResult> pTexture;

  /**
   * @brief The channel that this feature id texture uses within the image.
   */
  int32 channel;

  /**
   * @brief The texture coordinate accessor index for the feature id texture.
   */
  int64 textureCoordinateAttributeId;
};

struct EncodedFeatureIdAttribute {
  FString name;
  FString featureTableName;
  int32 index;
};

struct EncodedFeatureTextureProperty {
  FString baseName;
  TSharedPtr<ITwinCesiumTextureUtility::LoadedTextureResult> pTexture;
  int64 textureCoordinateAttributeId;
  int32 channelOffsets[4];
};

struct EncodedFeatureTexture {
  TArray<EncodedFeatureTextureProperty> properties;
};

struct EncodedMetadataPrimitive {
  TArray<EncodedFeatureIdTexture> encodedFeatureIdTextures;
  TArray<EncodedFeatureIdAttribute> encodedFeatureIdAttributes;
  TArray<FString> featureTextureNames;
};

struct EncodedMetadata {
  TMap<FString, EncodedMetadataFeatureTable> encodedFeatureTables;
  TMap<FString, EncodedFeatureTexture> encodedFeatureTextures;
};

EncodedMetadataFeatureTable encodeMetadataFeatureTableAnyThreadPart(
    const FITwinFeatureTableDescription& featureTableDescription,
    const FITwinCesiumPropertyTable& featureTable);

EncodedFeatureTexture encodeFeatureTextureAnyThreadPart(
    TMap<
        const CesiumGltf::ImageCesium*,
        TWeakPtr<ITwinCesiumTextureUtility::LoadedTextureResult>>&
        featureTexturePropertyMap,
    const FITwinFeatureTextureDescription& featureTextureDescription,
    const FString& featureTextureName,
    const FITwinCesiumPropertyTexture& featureTexture);

EncodedMetadataPrimitive encodeMetadataPrimitiveAnyThreadPart(
    const FITwinMetadataDescription& metadataDescription,
    const FITwinCesiumMetadataPrimitive& primitive);

EncodedMetadata encodeMetadataAnyThreadPart(
    const FITwinMetadataDescription& metadataDescription,
    const FITwinCesiumModelMetadata& metadata);

bool encodeMetadataFeatureTableGameThreadPart(
    EncodedMetadataFeatureTable& encodedFeatureTable);

bool encodeFeatureTextureGameThreadPart(
    TArray<TUniquePtr<ITwinCesiumTextureUtility::LoadedTextureResult>>&
        uniqueTextures,
    EncodedFeatureTexture& encodedFeatureTexture);

bool encodeMetadataPrimitiveGameThreadPart(
    EncodedMetadataPrimitive& encodedPrimitive);

bool encodeMetadataGameThreadPart(EncodedMetadata& encodedMetadata);

void destroyEncodedMetadataPrimitive(
    EncodedMetadataPrimitive& encodedPrimitive);

void destroyEncodedMetadata(EncodedMetadata& encodedMetadata);

FString createHlslSafeName(const FString& rawName);

} // namespace ITwinCesiumEncodedMetadataUtility

PRAGMA_ENABLE_DEPRECATION_WARNINGS
