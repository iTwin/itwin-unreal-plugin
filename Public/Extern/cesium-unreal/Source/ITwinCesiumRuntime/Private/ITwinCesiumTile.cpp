#include "ITwinCesiumTile.h"
#include "ITwinCalcBounds.h"
#include "ITwinCesiumTransforms.h"
#include "Components/PrimitiveComponent.h"
#include "ITwinVecMath.h"

bool UITwinCesiumTile::TileBoundsOverlapsPrimitive(
    const UPrimitiveComponent* Other) const {
  if (IsValid(Other)) {
    return Bounds.GetBox().Intersect(Other->Bounds.GetBox()) &&
           Bounds.GetSphere().Intersects(Other->Bounds.GetSphere());
  } else {
    return false;
  }
}

bool UITwinCesiumTile::PrimitiveBoxFullyContainsTileBounds(
    const UPrimitiveComponent* Other) const {
  if (IsValid(Other)) {
    return Bounds.GetBox().Intersect(Other->Bounds.GetBox()) ||
           Bounds.GetSphere().Intersects(Other->Bounds.GetSphere());
  } else {
    return false;
  }
}

FBoxSphereBounds UITwinCesiumTile::CalcBounds(const FTransform& LocalToWorld) const {
  FBoxSphereBounds bounds = std::visit(
      FITwinCalcBoundsOperation{LocalToWorld, this->_tileTransform},
      _tileBounds);
  return bounds;
}
