// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumModelMetadata.h"
#include "ITwinCesiumPrimitiveFeatures.h"
#include "ITwinCesiumPrimitiveMetadata.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"

#include "ITwinCesiumMetadataPrimitive.generated.h"

struct UE_DEPRECATED(
    5.0,
    "FITwinCesiumMetadataPrimitive is deprecated. Instead, use FITwinCesiumPrimitiveFeatures and FITwinCesiumPrimitiveMetadata to retrieve feature IDs and metadata from a glTF primitive.")
    FITwinCesiumMetadataPrimitive;

/**
 * A Blueprint-accessible wrapper for a glTF Primitive's EXT_feature_metadata
 * extension. This class is deprecated and only exists for backwards
 * compatibility.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumMetadataPrimitive {
  GENERATED_USTRUCT_BODY()

public:
  /**
   * Construct an empty primitive metadata instance.
   */
  FITwinCesiumMetadataPrimitive()
      : _pPrimitiveFeatures(nullptr),
        _pPrimitiveMetadata(nullptr),
        _pModelMetadata(nullptr) {}

  /**
   * Constructs a primitive metadata instance from the new features / metadata
   * implementations for backwards compatibility.
   *
   * This class exists for backwards compatibility, so it requires a
   * FITwinCesiumPrimitiveFeatures to have been constructed beforehand. It assumes
   * the given FITwinCesiumPrimitiveFeatures will have the same lifetime as this
   * instance.
   *
   * @param PrimitiveFeatures The FITwinCesiumPrimitiveFeatures denoting the feature
   * IDs in the glTF mesh primitive.
   * @param PrimitiveMetadata The FITwinCesiumPrimitiveMetadata containing references
   * to the metadata for the glTF mesh primitive.
   */
  FITwinCesiumMetadataPrimitive(
      const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
      const FITwinCesiumPrimitiveMetadata& PrimitiveMetadata,
      const FITwinCesiumModelMetadata& ModelMetadata);

private:
  const FITwinCesiumPrimitiveFeatures* _pPrimitiveFeatures;
  const FITwinCesiumPrimitiveMetadata* _pPrimitiveMetadata;
  const FITwinCesiumModelMetadata* _pModelMetadata;

  friend class UITwinCesiumMetadataPrimitiveBlueprintLibrary;
};

UCLASS()
class ITWINCESIUMRUNTIME_API UITwinCesiumMetadataPrimitiveBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  PRAGMA_DISABLE_DEPRECATION_WARNINGS

  /**
   * Get all the feature ID attributes that are associated with the
   * primitive.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Primitive",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "CesiumMetadataPrimitive is deprecated. Get feature IDs from CesiumPrimitiveFeatures instead."))
  static const TArray<FITwinCesiumFeatureIdAttribute>
  GetFeatureIdAttributes(UPARAM(ref)
                             const FITwinCesiumMetadataPrimitive& MetadataPrimitive);

  /**
   * Get all the feature ID textures that are associated with the
   * primitive.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Primitive",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "CesiumMetadataPrimitive is deprecated. Get feature IDs from CesiumPrimitiveFeatures instead."))
  static const TArray<FITwinCesiumFeatureIdTexture>
  GetFeatureIdTextures(UPARAM(ref)
                           const FITwinCesiumMetadataPrimitive& MetadataPrimitive);

  /**
   * @brief Get all the feature textures that are associated with the
   * primitive.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Primitive",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "CesiumMetadataPrimitive is deprecated. Get the associated property texture indices from CesiumPrimitiveMetadata instead."))
  static const TArray<FString>
  GetFeatureTextureNames(UPARAM(ref)
                             const FITwinCesiumMetadataPrimitive& MetadataPrimitive);

  /**
   * Gets the ID of the first vertex that makes up a given face of this
   * primitive.
   *
   * @param faceID The ID of the face.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Metadata|Primitive",
      Meta =
          (DeprecatedFunction,
           DeprecationMessage =
               "CesiumMetadataPrimitive is deprecated. Use GetFirstVertexFromFace with CesiumPrimitiveFeatures instead."))
  static int64 GetFirstVertexIDFromFaceID(
      UPARAM(ref) const FITwinCesiumMetadataPrimitive& MetadataPrimitive,
      int64 FaceID);

  PRAGMA_ENABLE_DEPRECATION_WARNINGS
};
