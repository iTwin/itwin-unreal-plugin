// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCalcBounds.h"
#include "ITwinCesiumLifetime.h"
#include "ITwinCesiumMaterialUserData.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"
#include "ITwinVecMath.h"

#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltf/Model.h>
#include <variant>

// Prevent deprecation warnings while initializing deprecated metadata structs.
PRAGMA_DISABLE_DEPRECATION_WARNINGS

// Sets default values for this component's properties
UITwinCesiumGltfPrimitiveComponent::UITwinCesiumGltfPrimitiveComponent() {
  PrimaryComponentTick.bCanEverTick = false;
  pModel = nullptr;
  pMeshPrimitive = nullptr;
  pTilesetActor = nullptr;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

UITwinCesiumGltfPrimitiveComponent::~UITwinCesiumGltfPrimitiveComponent() {}

void UITwinCesiumGltfPrimitiveComponent::UpdateTransformFromCesium(
    const glm::dmat4& CesiumToUnrealTransform) {
  const FTransform transform = FTransform(FITwinVecMath::createMatrix(
      CesiumToUnrealTransform * this->HighPrecisionNodeTransform));

  if (this->Mobility == EComponentMobility::Movable) {
    // For movable objects, move the component in the normal way, but don't
    // generate collisions along the way. Teleporting physics is imperfect, but
    // it's the best available option.
    this->SetRelativeTransform(
        transform,
        false,
        nullptr,
        ETeleportType::TeleportPhysics);
  } else {
    // Unreall will yell at us for calling SetRelativeTransform on a static
    // object, but we still need to adjust (accurately!) for origin rebasing and
    // georeference changes. It's "ok" to move a static object in this way
    // because, we assume, the globe and globe-oriented lights, etc. are moving
    // too, so in a relative sense the object isn't actually moving. This isn't
    // a perfect assumption, of course.
    this->SetRelativeTransform_Direct(transform);
    this->UpdateComponentToWorld();
    this->MarkRenderTransformDirty();
    this->SendPhysicsTransform(ETeleportType::ResetPhysics);
  }
}

namespace {

void destroyMaterialTexture(
    UMaterialInstanceDynamic* pMaterial,
    const char* name,
    EMaterialParameterAssociation assocation,
    int32 index) {
  UTexture* pTexture = nullptr;
  if (pMaterial->GetTextureParameterValue(
          FMaterialParameterInfo(name, assocation, index),
          pTexture,
          true)) {
    ITwinCesiumTextureUtility::destroyTexture(pTexture);
  }
}

void destroyGltfParameterValues(
    UMaterialInstanceDynamic* pMaterial,
    EMaterialParameterAssociation assocation,
    int32 index) {
  destroyMaterialTexture(pMaterial, "baseColorTexture", assocation, index);
  destroyMaterialTexture(
      pMaterial,
      "metallicRoughnessTexture",
      assocation,
      index);
  destroyMaterialTexture(pMaterial, "normalTexture", assocation, index);
  destroyMaterialTexture(pMaterial, "emissiveTexture", assocation, index);
  destroyMaterialTexture(pMaterial, "occlusionTexture", assocation, index);
}

void destroyWaterParameterValues(
    UMaterialInstanceDynamic* pMaterial,
    EMaterialParameterAssociation assocation,
    int32 index) {
  destroyMaterialTexture(pMaterial, "WaterMask", assocation, index);
}
} // namespace

void UITwinCesiumGltfPrimitiveComponent::BeginDestroy() {
  // This should mirror the logic in loadPrimitiveGameThreadPart in
  // ITwinCesiumGltfComponent.cpp
  UMaterialInstanceDynamic* pMaterial =
      Cast<UMaterialInstanceDynamic>(this->GetMaterial(0));
  if (pMaterial) {

    destroyGltfParameterValues(
        pMaterial,
        EMaterialParameterAssociation::GlobalParameter,
        INDEX_NONE);
    destroyWaterParameterValues(
        pMaterial,
        EMaterialParameterAssociation::GlobalParameter,
        INDEX_NONE);

    UMaterialInterface* pBaseMaterial = pMaterial->Parent;
    UMaterialInstance* pBaseAsMaterialInstance =
        Cast<UMaterialInstance>(pBaseMaterial);
    UITwinCesiumMaterialUserData* pCesiumData =
        pBaseAsMaterialInstance
            ? pBaseAsMaterialInstance
                  ->GetAssetUserData<UITwinCesiumMaterialUserData>()
            : nullptr;
    if (pCesiumData) {
      destroyGltfParameterValues(
          pMaterial,
          EMaterialParameterAssociation::LayerParameter,
          0);

      int32 waterIndex = pCesiumData->LayerNames.Find("Water");
      if (waterIndex >= 0) {
        destroyWaterParameterValues(
            pMaterial,
            EMaterialParameterAssociation::LayerParameter,
            waterIndex);
      }
    }

    ITwinCesiumEncodedFeaturesMetadata::destroyEncodedPrimitiveFeatures(
        this->EncodedFeatures);

    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    if (this->EncodedMetadata_DEPRECATED) {
      ITwinCesiumEncodedMetadataUtility::destroyEncodedMetadataPrimitive(
          *this->EncodedMetadata_DEPRECATED);
      this->EncodedMetadata_DEPRECATED = std::nullopt;
    }
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    FITwinCesiumLifetime::destroy(pMaterial);
  }

  UStaticMesh* pMesh = this->GetStaticMesh();
  if (pMesh) {
    UBodySetup* pBodySetup = pMesh->GetBodySetup();

    if (pBodySetup) {
      FITwinCesiumLifetime::destroy(pBodySetup);
    }

    FITwinCesiumLifetime::destroy(pMesh);
  }

  Super::BeginDestroy();
}

FBoxSphereBounds UITwinCesiumGltfPrimitiveComponent::CalcBounds(
    const FTransform& LocalToWorld) const {
  if (!this->boundingVolume) {
    return Super::CalcBounds(LocalToWorld);
  }

  return std::visit(
      FITwinCalcBoundsOperation{LocalToWorld, this->HighPrecisionNodeTransform},
      *this->boundingVolume);
}
