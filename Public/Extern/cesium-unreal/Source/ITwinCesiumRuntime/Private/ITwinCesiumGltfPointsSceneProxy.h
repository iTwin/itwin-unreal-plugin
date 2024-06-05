#pragma once

#include "ITwinCesiumPointAttenuationVertexFactory.h"
#include "ITwinCesiumPointCloudShading.h"
#include "PrimitiveSceneProxy.h"
#include <glm/vec3.hpp>

class UITwinCesiumGltfPointsComponent;

/**
 * Used to pass tile data and Cesium3DTileset settings to a SceneProxy, usually
 * via render thread.
 */
struct FITwinCesiumGltfPointsSceneProxyTilesetData {
  FITwinCesiumPointCloudShading PointCloudShading;
  double MaximumScreenSpaceError;
  bool UsesAdditiveRefinement;
  float GeometricError;
  glm::vec3 Dimensions;

  FITwinCesiumGltfPointsSceneProxyTilesetData();

  void UpdateFromComponent(UITwinCesiumGltfPointsComponent* Component);
};

class FITwinCesiumGltfPointsSceneProxy final : public FPrimitiveSceneProxy {
private:
  // The original render data of the static mesh.
  const FStaticMeshRenderData* RenderData;
  int32_t NumPoints;

public:
  SIZE_T GetTypeHash() const override;

  FITwinCesiumGltfPointsSceneProxy(
      UITwinCesiumGltfPointsComponent* InComponent,
      ERHIFeatureLevel::Type InFeatureLevel);

  virtual ~FITwinCesiumGltfPointsSceneProxy();

protected:
  virtual void CreateRenderThreadResources() override;
  virtual void DestroyRenderThreadResources() override;

  virtual void GetDynamicMeshElements(
      const TArray<const FSceneView*>& Views,
      const FSceneViewFamily& ViewFamily,
      uint32 VisibilityMap,
      FMeshElementCollector& Collector) const override;

  virtual FPrimitiveViewRelevance
  GetViewRelevance(const FSceneView* View) const override;

  virtual uint32 GetMemoryFootprint(void) const override;

public:
  void UpdateTilesetData(
      const FITwinCesiumGltfPointsSceneProxyTilesetData& InTilesetData);

private:
  // Whether or not the shader platform supports attenuation.
  bool bAttenuationSupported;

  // Data from the UITwinCesiumGltfComponent that owns this scene proxy, as well as
  // its AITwinCesium3DTileset.
  FITwinCesiumGltfPointsSceneProxyTilesetData TilesetData;

  // The vertex factory and index buffer for point attenuation.
  FITwinCesiumPointAttenuationVertexFactory AttenuationVertexFactory;
  FITwinCesiumPointAttenuationIndexBuffer AttenuationIndexBuffer;

  UMaterialInterface* Material;
  FMaterialRelevance MaterialRelevance;

  float GetGeometricError() const;

  void CreatePointAttenuationUserData(
      FMeshBatchElement& BatchElement,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;

  void CreateMeshWithAttenuation(
      FMeshBatch& Mesh,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;
  void CreateMesh(FMeshBatch& Mesh) const;
};
