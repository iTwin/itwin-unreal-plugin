#include "ITwinCesiumGltfPointsSceneProxy.h"
#include "ITwinCesiumGltfPointsComponent.h"
#include "Engine/StaticMesh.h"
#include "RHIResources.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "StaticMeshResources.h"

#if ENGINE_VERSION_5_2_OR_HIGHER
#include "DataDrivenShaderPlatformInfo.h"
#endif

FITwinCesiumGltfPointsSceneProxyTilesetData::FITwinCesiumGltfPointsSceneProxyTilesetData()
    : PointCloudShading(),
      MaximumScreenSpaceError(0.0),
      UsesAdditiveRefinement(false),
      GeometricError(0.0f),
      Dimensions() {}

void FITwinCesiumGltfPointsSceneProxyTilesetData::UpdateFromComponent(
    UITwinCesiumGltfPointsComponent* Component) {
  AITwinCesium3DTileset* Tileset = Component->pTilesetActor;
  PointCloudShading = Tileset->GetPointCloudShading();
  MaximumScreenSpaceError = Tileset->MaximumScreenSpaceError;
  UsesAdditiveRefinement = Component->UsesAdditiveRefinement;
  GeometricError = Component->GeometricError;
  Dimensions = Component->Dimensions;
}

SIZE_T FITwinCesiumGltfPointsSceneProxy::GetTypeHash() const {
  static size_t UniquePointer;
  return reinterpret_cast<size_t>(&UniquePointer);
}

FITwinCesiumGltfPointsSceneProxy::FITwinCesiumGltfPointsSceneProxy(
    UITwinCesiumGltfPointsComponent* InComponent,
    ERHIFeatureLevel::Type InFeatureLevel)
    : FPrimitiveSceneProxy(InComponent),
      RenderData(InComponent->GetStaticMesh()->GetRenderData()),
      NumPoints(RenderData->LODResources[0].IndexBuffer.GetNumIndices()),
      bAttenuationSupported(
          RHISupportsManualVertexFetch(GetScene().GetShaderPlatform())),
      TilesetData(),
      AttenuationVertexFactory(
          InFeatureLevel,
          &RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer),
      AttenuationIndexBuffer(NumPoints, bAttenuationSupported),
      Material(InComponent->GetMaterial(0)),
      MaterialRelevance(InComponent->GetMaterialRelevance(InFeatureLevel)) {}

FITwinCesiumGltfPointsSceneProxy::~FITwinCesiumGltfPointsSceneProxy() {}

void FITwinCesiumGltfPointsSceneProxy::CreateRenderThreadResources() {
  AttenuationVertexFactory.InitResource();
  AttenuationIndexBuffer.InitResource();
}

void FITwinCesiumGltfPointsSceneProxy::DestroyRenderThreadResources() {
  AttenuationVertexFactory.ReleaseResource();
  AttenuationIndexBuffer.ReleaseResource();
}

void FITwinCesiumGltfPointsSceneProxy::GetDynamicMeshElements(
    const TArray<const FSceneView*>& Views,
    const FSceneViewFamily& ViewFamily,
    uint32 VisibilityMap,
    FMeshElementCollector& Collector) const {
  QUICK_SCOPE_CYCLE_COUNTER(STAT_GltfPointsSceneProxy_GetDynamicMeshElements);

  const bool useAttenuation =
      bAttenuationSupported && TilesetData.PointCloudShading.Attenuation;

  for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++) {
    if (VisibilityMap & (1 << ViewIndex)) {
      const FSceneView* View = Views[ViewIndex];
      FMeshBatch& Mesh = Collector.AllocateMesh();
      if (useAttenuation) {
        CreateMeshWithAttenuation(Mesh, View, Collector);
      } else {
        CreateMesh(Mesh);
      }
      Collector.AddMesh(ViewIndex, Mesh);
    }
  }
}

FPrimitiveViewRelevance
FITwinCesiumGltfPointsSceneProxy::GetViewRelevance(const FSceneView* View) const {
  FPrimitiveViewRelevance Result;
  Result.bDrawRelevance = IsShown(View);
  // Always render dynamically; the appearance of the points can change
  // via point cloud shading.
  Result.bDynamicRelevance = true;
  Result.bStaticRelevance = false;

  Result.bRenderCustomDepth = ShouldRenderCustomDepth();
  Result.bRenderInMainPass = ShouldRenderInMainPass();
  Result.bRenderInDepthPass = ShouldRenderInDepthPass();
  Result.bUsesLightingChannels =
      GetLightingChannelMask() != GetDefaultLightingChannelMask();
  Result.bShadowRelevance = IsShadowCast(View);
  Result.bVelocityRelevance =
      IsMovable() & Result.bOpaque & Result.bRenderInMainPass;

  MaterialRelevance.SetPrimitiveViewRelevance(Result);

  return Result;
}

uint32 FITwinCesiumGltfPointsSceneProxy::GetMemoryFootprint(void) const {
  return (sizeof(*this) + GetAllocatedSize());
}

void FITwinCesiumGltfPointsSceneProxy::UpdateTilesetData(
    const FITwinCesiumGltfPointsSceneProxyTilesetData& InTilesetData) {
  TilesetData = InTilesetData;
}

float FITwinCesiumGltfPointsSceneProxy::GetGeometricError() const {
  FITwinCesiumPointCloudShading PointCloudShading = TilesetData.PointCloudShading;
  float GeometricError = TilesetData.GeometricError;
  if (GeometricError > 0.0f) {
    return GeometricError;
  }

  if (PointCloudShading.BaseResolution > 0.0f) {
    return PointCloudShading.BaseResolution;
  }

  // Estimate the geometric error.
  glm::vec3 Dimensions = TilesetData.Dimensions;
  float Volume = Dimensions.x * Dimensions.y * Dimensions.z;
  return FMath::Pow(Volume / NumPoints, 1.0f / 3.0f);
}

void FITwinCesiumGltfPointsSceneProxy::CreatePointAttenuationUserData(
    FMeshBatchElement& BatchElement,
    const FSceneView* View,
    FMeshElementCollector& Collector) const {
  FITwinCesiumPointAttenuationBatchElementUserDataWrapper* UserDataWrapper =
      &Collector.AllocateOneFrameResource<
          FITwinCesiumPointAttenuationBatchElementUserDataWrapper>();

  FITwinCesiumPointAttenuationBatchElementUserData& UserData = UserDataWrapper->Data;
  const FLocalVertexFactory& OriginalVertexFactory =
      RenderData->LODVertexFactories[0].VertexFactory;

  UserData.PositionBuffer = OriginalVertexFactory.GetPositionsSRV();
  UserData.PackedTangentsBuffer = OriginalVertexFactory.GetTangentsSRV();
  UserData.ColorBuffer = OriginalVertexFactory.GetColorComponentsSRV();
  UserData.TexCoordBuffer = OriginalVertexFactory.GetTextureCoordinatesSRV();
  UserData.NumTexCoords = OriginalVertexFactory.GetNumTexcoords();
  UserData.bHasPointColors = RenderData->LODResources[0].bHasColorVertexData;

  FITwinCesiumPointCloudShading PointCloudShading = TilesetData.PointCloudShading;

  float MaximumPointSize = TilesetData.UsesAdditiveRefinement
                               ? 5.0f
                               : TilesetData.MaximumScreenSpaceError;

  if (PointCloudShading.MaximumAttenuation > 0.0f) {
    // Don't multiply by DPI scale; let Unreal handle scaling.
    MaximumPointSize = PointCloudShading.MaximumAttenuation;
  }

  float GeometricError = GetGeometricError();
  GeometricError *= PointCloudShading.GeometricErrorScale;

  // Depth Multiplier
  float SSEDenominator = 2.0f * tanf(0.5f * FMath::DegreesToRadians(View->FOV));
  float DepthMultiplier =
      static_cast<float>(View->UnconstrainedViewRect.Height()) / SSEDenominator;

  UserData.AttenuationParameters =
      FVector3f(MaximumPointSize, GeometricError, DepthMultiplier);

  BatchElement.UserData = &UserDataWrapper->Data;
}

void FITwinCesiumGltfPointsSceneProxy::CreateMeshWithAttenuation(
    FMeshBatch& Mesh,
    const FSceneView* View,
    FMeshElementCollector& Collector) const {
  Mesh.VertexFactory = &AttenuationVertexFactory;
  Mesh.MaterialRenderProxy = Material->GetRenderProxy();
  Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
  Mesh.Type = PT_TriangleList;
  Mesh.DepthPriorityGroup = SDPG_World;
  Mesh.LODIndex = 0;
  Mesh.bCanApplyViewModeOverrides = false;
  Mesh.bUseAsOccluder = false;
  Mesh.bWireframe = false;

  FMeshBatchElement& BatchElement = Mesh.Elements[0];
  BatchElement.IndexBuffer = &AttenuationIndexBuffer;
  BatchElement.NumPrimitives = NumPoints * 2;
  BatchElement.FirstIndex = 0;
  BatchElement.MinVertexIndex = 0;
  BatchElement.MaxVertexIndex = NumPoints * 4 - 1;
  BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

  CreatePointAttenuationUserData(BatchElement, View, Collector);
}

void FITwinCesiumGltfPointsSceneProxy::CreateMesh(FMeshBatch& Mesh) const {
  Mesh.VertexFactory = &RenderData->LODVertexFactories[0].VertexFactory;
  Mesh.MaterialRenderProxy = Material->GetRenderProxy();
  Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
  Mesh.Type = PT_PointList;
  Mesh.DepthPriorityGroup = SDPG_World;
  Mesh.LODIndex = 0;
  Mesh.bCanApplyViewModeOverrides = false;
  Mesh.bUseAsOccluder = false;
  Mesh.bWireframe = false;

  FMeshBatchElement& BatchElement = Mesh.Elements[0];
  BatchElement.IndexBuffer = &RenderData->LODResources[0].IndexBuffer;
  BatchElement.NumPrimitives = NumPoints;
  BatchElement.FirstIndex = 0;
  BatchElement.MinVertexIndex = 0;
  BatchElement.MaxVertexIndex = BatchElement.NumPrimitives - 1;
}
