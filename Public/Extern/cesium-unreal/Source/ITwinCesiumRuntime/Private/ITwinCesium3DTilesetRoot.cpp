// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesium3DTilesetRoot.h"
#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumRuntime.h"
#include "CesiumUtility/Math.h"
#include "Engine/World.h"
#include "ITwinVecMath.h"

UITwinCesium3DTilesetRoot::UITwinCesium3DTilesetRoot()
    : _absoluteLocation(0.0, 0.0, 0.0), _tilesetToUnrealRelativeWorld(1.0) {
  PrimaryComponentTick.bCanEverTick = false;
}

void UITwinCesium3DTilesetRoot::HandleGeoreferenceUpdated() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called HandleGeoreferenceUpdated for tileset root %s"),
      *this->GetName());
  this->_updateTilesetToUnrealRelativeWorldTransform();
}

const glm::dmat4&
UITwinCesium3DTilesetRoot::GetCesiumTilesetToUnrealRelativeWorldTransform() const {
  return this->_tilesetToUnrealRelativeWorld;
}

// Called when the game starts
void UITwinCesium3DTilesetRoot::BeginPlay() {
  Super::BeginPlay();

  this->_updateAbsoluteLocation();
  this->_updateTilesetToUnrealRelativeWorldTransform();
}

bool UITwinCesium3DTilesetRoot::MoveComponentImpl(
    const FVector& Delta,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutHit,
    EMoveComponentFlags MoveFlags,
    ETeleportType Teleport) {
  bool result = USceneComponent::MoveComponentImpl(
      Delta,
      NewRotation,
      bSweep,
      OutHit,
      MoveFlags,
      Teleport);

  this->_updateAbsoluteLocation();
  this->_updateTilesetToUnrealRelativeWorldTransform();

  return result;
}

void UITwinCesium3DTilesetRoot::_updateAbsoluteLocation() {
  const FVector& newLocation = this->GetRelativeLocation();
  this->_absoluteLocation = VecMath::createVector3D(newLocation);
}

void UITwinCesium3DTilesetRoot::_updateTilesetToUnrealRelativeWorldTransform() {
  AITwinCesium3DTileset* pTileset = this->GetOwner<AITwinCesium3DTileset>();

  this->_tilesetToUnrealRelativeWorld = VecMath::createMatrix4D(
      pTileset->ResolveGeoreference()
          ->ComputeEarthCenteredEarthFixedToUnrealTransformation());

  pTileset->UpdateTransformFromCesium();
}
