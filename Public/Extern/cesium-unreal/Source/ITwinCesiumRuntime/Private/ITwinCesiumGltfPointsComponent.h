// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "ITwinCesiumGltfPointsComponent.generated.h"

UCLASS()
class UITwinCesiumGltfPointsComponent : public UITwinCesiumGltfPrimitiveComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UITwinCesiumGltfPointsComponent();
  virtual ~UITwinCesiumGltfPointsComponent();

  // Whether the tile that contains this point component uses additive
  // refinement.
  bool UsesAdditiveRefinement;

  // The geometric error of the tile containing this point component.
  float GeometricError;

  // The dimensions of the point component. Used to estimate the geometric
  // error.
  glm::vec3 Dimensions;

  // Override UPrimitiveComponent interface.
  virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};
