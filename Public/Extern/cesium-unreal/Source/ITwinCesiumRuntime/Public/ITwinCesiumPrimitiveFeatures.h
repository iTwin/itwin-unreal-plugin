// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumFeatureIdSet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"
#include <CesiumGltf/AccessorUtility.h>

#include "ITwinCesiumPrimitiveFeatures.generated.h"

namespace CesiumGltf {
struct ExtensionExtMeshFeatures;
} // namespace CesiumGltf

/**
 * A Blueprint-accessible wrapper for a glTF Primitive's mesh features. It holds
 * views of the feature ID sets associated with this primitive.
 */
USTRUCT(BlueprintType)
struct ITWINCESIUMRUNTIME_API FITwinCesiumPrimitiveFeatures {
  GENERATED_USTRUCT_BODY()

public:
  /**
   * Constructs an empty primitive features instance.
   */
  FITwinCesiumPrimitiveFeatures() : _vertexCount(0) {}

  /**
   * Constructs a primitive features instance.
   *
   * @param Model The model that contains the EXT_mesh_features extension
   * @param Primitive The mesh primitive that stores EXT_mesh_features
   * extension
   * @param Features The EXT_mesh_features of the glTF mesh primitive.
   * primitive
   */
  FITwinCesiumPrimitiveFeatures(
      const CesiumGltf::Model& Model,
      const CesiumGltf::MeshPrimitive& Primitive,
      const CesiumGltf::ExtensionExtMeshFeatures& Features);

private:
  TArray<FITwinCesiumFeatureIdSet> _featureIdSets;
  CesiumGltf::IndexAccessorType _indexAccessor;
  int64_t _vertexCount;
  int32_t _primitiveMode;

  friend class UITwinCesiumPrimitiveFeaturesBlueprintLibrary;
};

UCLASS()
class ITWINCESIUMRUNTIME_API UITwinCesiumPrimitiveFeaturesBlueprintLibrary
    : public UBlueprintFunctionLibrary {
  GENERATED_BODY()

public:
  /**
   * Gets the primitive features of a glTF primitive component. If component is
   * not a Cesium glTF primitive component, the returned features are empty.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static const FITwinCesiumPrimitiveFeatures&
  GetPrimitiveFeatures(const UPrimitiveComponent* component);

  /**
   * Gets all the feature ID sets that are associated with the
   * primitive.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static const TArray<FITwinCesiumFeatureIdSet>&
  GetFeatureIDSets(UPARAM(ref)
                       const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures);

  /**
   * Gets all the feature ID sets of the given type. If the primitive has no
   * sets of that type, the returned array will be empty.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static const TArray<FITwinCesiumFeatureIdSet> GetFeatureIDSetsOfType(
      UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
      EITwinCesiumFeatureIdSetType Type);

  /**
   * Get the number of vertices in the primitive.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static int64
  GetVertexCount(UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures);

  /**
   * Gets the index of the first vertex that makes up a given face of this
   * primitive.
   *
   * @param FaceIndex The index of the face.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static int64 GetFirstVertexFromFace(
      UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
      int64 FaceIndex);

  /**
   * Gets the feature ID associated with the given face.
   *
   * A primitive may have multiple feature ID sets, so this allows a feature ID
   * set to be specified by index. This value should index into the array of
   * CesiumFeatureIdSets in the CesiumPrimitiveFeatures. If the specified
   * feature ID set index is invalid, this returns -1.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static int64 GetFeatureIDFromFace(
      UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
      int64 FaceIndex,
      int64 FeatureIDSetIndex = 0);

  /**
   * Gets the feature ID from the given line trace hit, assuming it
   * has hit a glTF primitive component containing this CesiumPrimitiveFeatures.
   *
   * A primitive may have multiple feature ID sets, so this allows a feature ID
   * set to be specified by index. This value should index into the array of
   * CesiumFeatureIdSets in the CesiumPrimitiveFeatures. If the specified
   * feature ID set index is invalid, this returns -1.
   */
  UFUNCTION(
      BlueprintCallable,
      BlueprintPure,
      Category = "Cesium|Primitive|Features")
  static int64 GetFeatureIDFromHit(
      UPARAM(ref) const FITwinCesiumPrimitiveFeatures& PrimitiveFeatures,
      const FHitResult& Hit,
      int64 FeatureIDSetIndex = 0);
};
