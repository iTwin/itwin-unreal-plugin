// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumGltfPointsComponent.h"
#include "ITwinCesiumGltfPointsSceneProxy.h"
#include "SceneInterface.h"

// Sets default values for this component's properties
UITwinCesiumGltfPointsComponent::UITwinCesiumGltfPointsComponent()
    : UsesAdditiveRefinement(false),
      GeometricError(0),
      Dimensions(glm::vec3(0)) {}

UITwinCesiumGltfPointsComponent::~UITwinCesiumGltfPointsComponent() {}

FPrimitiveSceneProxy* UITwinCesiumGltfPointsComponent::CreateSceneProxy() {
  if (!IsValid(this)) {
    return nullptr;
  }

  FITwinCesiumGltfPointsSceneProxy* Proxy =
      new FITwinCesiumGltfPointsSceneProxy(this, GetScene()->GetFeatureLevel());

  FITwinCesiumGltfPointsSceneProxyTilesetData TilesetData;
  TilesetData.UpdateFromComponent(this);
  Proxy->UpdateTilesetData(TilesetData);

  return Proxy;
}
