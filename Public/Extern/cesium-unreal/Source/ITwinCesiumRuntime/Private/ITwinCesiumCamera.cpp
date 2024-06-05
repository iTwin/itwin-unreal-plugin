// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumCamera.h"
#include "ITwinCesiumRuntime.h"
#include "Math/UnrealMathUtility.h"

FITwinCesiumCamera::FITwinCesiumCamera()
    : ViewportSize(1.0, 1.0),
      Location(0.0, 0.0, 0.0),
      Rotation(0.0, 0.0, 0.0),
      FieldOfViewDegrees(0.0),
      OverrideAspectRatio(0.0) {}

FITwinCesiumCamera::FITwinCesiumCamera(
    const FVector2D& ViewportSize_,
    const FVector& Location_,
    const FRotator& Rotation_,
    double FieldOfViewDegrees_)
    : ViewportSize(ViewportSize_),
      Location(Location_),
      Rotation(Rotation_),
      FieldOfViewDegrees(FieldOfViewDegrees_),
      OverrideAspectRatio(0.0) {}

FITwinCesiumCamera::FITwinCesiumCamera(
    const FVector2D& ViewportSize_,
    const FVector& Location_,
    const FRotator& Rotation_,
    double FieldOfViewDegrees_,
    double OverrideAspectRatio_)
    : ViewportSize(ViewportSize_),
      Location(Location_),
      Rotation(Rotation_),
      FieldOfViewDegrees(FieldOfViewDegrees_),
      OverrideAspectRatio(OverrideAspectRatio_) {}
