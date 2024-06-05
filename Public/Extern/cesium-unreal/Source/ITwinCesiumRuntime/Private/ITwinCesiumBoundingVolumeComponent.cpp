// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumBoundingVolumeComponent.h"
#include "ITwinCalcBounds.h"
#include "ITwinCesiumGeoreference.h"
#include "ITwinCesiumLifetime.h"
#include "UObject/UObjectGlobals.h"
#include "ITwinVecMath.h"
#include <optional>
#include <variant>

using namespace Cesium3DTilesSelection;

UITwinCesiumBoundingVolumePoolComponent::UITwinCesiumBoundingVolumePoolComponent()
    : _cesiumToUnreal(1.0) {
  SetMobility(EComponentMobility::Movable);
}

void UITwinCesiumBoundingVolumePoolComponent::initPool(int32 maxPoolSize) {
  this->_pPool = std::make_shared<CesiumBoundingVolumePool>(this, maxPoolSize);
}

TileOcclusionRendererProxy* UITwinCesiumBoundingVolumePoolComponent::createProxy() {
  UITwinCesiumBoundingVolumeComponent* pBoundingVolume =
      NewObject<UITwinCesiumBoundingVolumeComponent>(this);
  pBoundingVolume->SetVisibility(false);
  pBoundingVolume->bUseAsOccluder = false;

  pBoundingVolume->SetMobility(EComponentMobility::Movable);
  pBoundingVolume->SetFlags(
      RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
  pBoundingVolume->SetupAttachment(this);
  pBoundingVolume->RegisterComponent();

  pBoundingVolume->UpdateTransformFromCesium(this->_cesiumToUnreal);

  return (TileOcclusionRendererProxy*)pBoundingVolume;
}

void UITwinCesiumBoundingVolumePoolComponent::destroyProxy(
    TileOcclusionRendererProxy* pProxy) {
  UITwinCesiumBoundingVolumeComponent* pBoundingVolumeComponent =
      (UITwinCesiumBoundingVolumeComponent*)pProxy;
  if (pBoundingVolumeComponent) {
    CesiumLifetime::destroyComponentRecursively(pBoundingVolumeComponent);
  }
}

UITwinCesiumBoundingVolumePoolComponent::CesiumBoundingVolumePool::
    CesiumBoundingVolumePool(
        UITwinCesiumBoundingVolumePoolComponent* pOutter,
        int32 maxPoolSize)
    : TileOcclusionRendererProxyPool(maxPoolSize), _pOutter(pOutter) {}

TileOcclusionRendererProxy*
UITwinCesiumBoundingVolumePoolComponent::CesiumBoundingVolumePool::createProxy() {
  return this->_pOutter->createProxy();
}

void UITwinCesiumBoundingVolumePoolComponent::CesiumBoundingVolumePool::destroyProxy(
    TileOcclusionRendererProxy* pProxy) {
  this->_pOutter->destroyProxy(pProxy);
}

void UITwinCesiumBoundingVolumePoolComponent::UpdateTransformFromCesium(
    const glm::dmat4& CesiumToUnrealTransform) {
  this->_cesiumToUnreal = CesiumToUnrealTransform;

  const TArray<USceneComponent*>& children = this->GetAttachChildren();
  for (USceneComponent* pChild : children) {
    UITwinCesiumBoundingVolumeComponent* pBoundingVolume =
        Cast<UITwinCesiumBoundingVolumeComponent>(pChild);
    if (pBoundingVolume) {
      pBoundingVolume->UpdateTransformFromCesium(CesiumToUnrealTransform);
    }
  }
}

class FITwinCesiumBoundingVolumeSceneProxy : public FPrimitiveSceneProxy {
public:
  FITwinCesiumBoundingVolumeSceneProxy(UITwinCesiumBoundingVolumeComponent* pComponent)
      : FPrimitiveSceneProxy(pComponent /*, name?*/) {}
  SIZE_T GetTypeHash() const override {
    static size_t UniquePointer;
    return reinterpret_cast<size_t>(&UniquePointer);
  }

  uint32 GetMemoryFootprint(void) const override {
    return sizeof(FITwinCesiumBoundingVolumeSceneProxy) + GetAllocatedSize();
  }
};

FPrimitiveSceneProxy* UITwinCesiumBoundingVolumeComponent::CreateSceneProxy() {
  return new FITwinCesiumBoundingVolumeSceneProxy(this);
}

void UITwinCesiumBoundingVolumeComponent::UpdateOcclusion(
    const CesiumViewExtension& cesiumViewExtension) {
  if (!_isMapped) {
    return;
  }

  TileOcclusionState occlusionState =
      cesiumViewExtension.getPrimitiveOcclusionState(
          this->ComponentId,
          _occlusionState == TileOcclusionState::Occluded,
          _mappedFrameTime);

  // If the occlusion result is unavailable, continue using the previous result.
  if (occlusionState != TileOcclusionState::OcclusionUnavailable) {
    _occlusionState = occlusionState;
  }
}

void UITwinCesiumBoundingVolumeComponent::_updateTransform() {
  const FTransform transform = FTransform(
      VecMath::createMatrix(this->_cesiumToUnreal * this->_tileTransform));

  this->SetRelativeTransform_Direct(transform);
  this->SetComponentToWorld(transform);
  this->MarkRenderTransformDirty();
}

void UITwinCesiumBoundingVolumeComponent::UpdateTransformFromCesium(
    const glm::dmat4& CesiumToUnrealTransform) {
  this->_cesiumToUnreal = CesiumToUnrealTransform;
  this->_updateTransform();
}

void UITwinCesiumBoundingVolumeComponent::reset(const Tile* pTile) {
  if (pTile) {
    this->_tileTransform = pTile->getTransform();
    this->_tileBounds = pTile->getBoundingVolume();
    this->_isMapped = true;
    this->_mappedFrameTime = GetWorld()->GetRealTimeSeconds();
    this->_updateTransform();
    this->SetVisibility(true);
  } else {
    this->_occlusionState = TileOcclusionState::OcclusionUnavailable;
    this->_isMapped = false;
    this->SetVisibility(false);
  }
}

FBoxSphereBounds UITwinCesiumBoundingVolumeComponent::CalcBounds(
    const FTransform& LocalToWorld) const {
  return std::visit(
      CalcBoundsOperation{LocalToWorld, this->_tileTransform},
      this->_tileBounds);
}
