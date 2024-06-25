// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "Cesium3DTilesSelection/Tile.h"
#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumEncodedFeaturesMetadata.h"
#include "ITwinCesiumEncodedMetadataUtility.h"
#include "ITwinCesiumModelMetadata.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "ITwinCustomDepthParameters.h"
#include "Interfaces/IHttpRequest.h"
#include <glm/mat4x4.hpp>
#include <memory>
#include "ITwinCesiumGltfComponent.generated.h"

class UMaterialInterface;
class UTexture2D;
class UStaticMeshComponent;

namespace CreateGltfOptions {
struct CreateModelOptions;
}

namespace CesiumGltf {
struct Model;
}

namespace Cesium3DTilesSelection {
class Tile;
}

namespace CesiumRasterOverlays {
class RasterOverlayTile;
}

namespace CesiumGeometry {
struct Rectangle;
}

USTRUCT()
struct FITwinRasterOverlayTile {
  GENERATED_BODY()

  UPROPERTY()
  FString OverlayName{};

  UPROPERTY()
  UTexture2D* Texture = nullptr;

  FLinearColor TranslationAndScale{};
  int32 TextureCoordinateID = -1;
};

UCLASS()
class UITwinCesiumGltfComponent : public USceneComponent {
  GENERATED_BODY()

public:
  class HalfConstructed {
  public:
    virtual ~HalfConstructed() = default;
  };

  static TUniquePtr<HalfConstructed> CreateOffGameThread(
      const glm::dmat4x4& Transform,
      const CreateGltfOptions::CreateModelOptions& Options);

  static UITwinCesiumGltfComponent* CreateOnGameThread(
      const CesiumGltf::Model& model,
      AITwinCesium3DTileset* ParentActor,
      TUniquePtr<HalfConstructed> HalfConstructed,
      const glm::dmat4x4& CesiumToUnrealTransform,
      UMaterialInterface* BaseMaterial,
      UMaterialInterface* BaseTranslucentMaterial,
      UMaterialInterface* BaseWaterMaterial,
      FITwinCustomDepthParameters CustomDepthParameters,
      const Cesium3DTilesSelection::Tile& tile,
      bool createNavCollision);

  UITwinCesiumGltfComponent();
  virtual ~UITwinCesiumGltfComponent();

  UPROPERTY(EditAnywhere, Category = "Cesium")
  UMaterialInterface* BaseMaterial = nullptr;

  UPROPERTY(EditAnywhere, Category = "Cesium")
  UMaterialInterface* BaseMaterialWithTranslucency = nullptr;

  UPROPERTY(EditAnywhere, Category = "Cesium")
  UMaterialInterface* BaseMaterialWithWater = nullptr;

  UPROPERTY(EditAnywhere, Category = "Rendering")
  FITwinCustomDepthParameters CustomDepthParameters{};

  FITwinCesiumModelMetadata Metadata{};
  ITwinCesiumEncodedFeaturesMetadata::EncodedModelMetadata EncodedMetadata{};

  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  std::optional<ITwinCesiumEncodedMetadataUtility::EncodedMetadata>
      EncodedMetadata_DEPRECATED = std::nullopt;
  PRAGMA_ENABLE_DEPRECATION_WARNINGS

  void UpdateTransformFromCesium(const glm::dmat4& CesiumToUnrealTransform);

  void AttachRasterTile(
      const Cesium3DTilesSelection::Tile& Tile,
      const CesiumRasterOverlays::RasterOverlayTile& RasterTile,
      UTexture2D* Texture,
      const glm::dvec2& Translation,
      const glm::dvec2& Scale,
      int32_t TextureCoordinateID);

  void DetachRasterTile(
      const Cesium3DTilesSelection::Tile& Tile,
      const CesiumRasterOverlays::RasterOverlayTile& RasterTile,
      UTexture2D* Texture);

  UFUNCTION(BlueprintCallable, Category = "Collision")
  virtual void SetCollisionEnabled(ECollisionEnabled::Type NewType);

  virtual void BeginDestroy() override;

  void UpdateFade(float fadePercentage, bool fadingIn);

private:
  UPROPERTY()
  UTexture2D* Transparent1x1 = nullptr;
};
