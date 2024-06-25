// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumWgs84Ellipsoid.h"
#include "ITwinVecMath.h"
#include <CesiumGeospatial/Ellipsoid.h>
#include <CesiumGeospatial/GlobeTransforms.h>
#include <CesiumUtility/Math.h>

using namespace CesiumGeospatial;
using namespace CesiumUtility;

FVector UITwinCesiumWgs84Ellipsoid::GetRadii() {
  const glm::dvec3& radii = Ellipsoid::WGS84.getRadii();
  return FITwinVecMath::createVector(radii);
}

double UITwinCesiumWgs84Ellipsoid::GetMaximumRadius() {
  return Ellipsoid::WGS84.getRadii().x;
}

double UITwinCesiumWgs84Ellipsoid::GetMinimumRadius() {
  return Ellipsoid::WGS84.getRadii().z;
}

FVector UITwinCesiumWgs84Ellipsoid::ScaleToGeodeticSurface(
    const FVector& EarthCenteredEarthFixedPosition) {
  std::optional<glm::dvec3> result = Ellipsoid::WGS84.scaleToGeodeticSurface(
      FITwinVecMath::createVector3D(EarthCenteredEarthFixedPosition));
  if (result) {
    return FITwinVecMath::createVector(*result);
  } else {
    return FVector(0.0, 0.0, 0.0);
  }
}

FVector UITwinCesiumWgs84Ellipsoid::GeodeticSurfaceNormal(
    const FVector& EarthCenteredEarthFixedPosition) {
  return FITwinVecMath::createVector(Ellipsoid::WGS84.geodeticSurfaceNormal(
      FITwinVecMath::createVector3D(EarthCenteredEarthFixedPosition)));
}

FVector UITwinCesiumWgs84Ellipsoid::LongitudeLatitudeHeightToEarthCenteredEarthFixed(
    const FVector& LongitudeLatitudeHeight) {
  glm::dvec3 cartesian =
      Ellipsoid::WGS84.cartographicToCartesian(Cartographic::fromDegrees(
          LongitudeLatitudeHeight.X,
          LongitudeLatitudeHeight.Y,
          LongitudeLatitudeHeight.Z));
  return FITwinVecMath::createVector(cartesian);
}

FVector UITwinCesiumWgs84Ellipsoid::EarthCenteredEarthFixedToLongitudeLatitudeHeight(
    const FVector& EarthCenteredEarthFixedPosition) {
  std::optional<Cartographic> result = Ellipsoid::WGS84.cartesianToCartographic(
      FITwinVecMath::createVector3D(EarthCenteredEarthFixedPosition));
  if (result) {
    return FVector(
        Math::radiansToDegrees(result->longitude),
        Math::radiansToDegrees(result->latitude),
        result->height);
  } else {
    return FVector(0.0, 0.0, 0.0);
  }
}

FMatrix UITwinCesiumWgs84Ellipsoid::EastNorthUpToEarthCenteredEarthFixed(
    const FVector& EarthCenteredEarthFixedPosition) {
  return FITwinVecMath::createMatrix(
      CesiumGeospatial::GlobeTransforms::eastNorthUpToFixedFrame(
          FITwinVecMath::createVector3D(EarthCenteredEarthFixedPosition),
          CesiumGeospatial::Ellipsoid::WGS84));
}
