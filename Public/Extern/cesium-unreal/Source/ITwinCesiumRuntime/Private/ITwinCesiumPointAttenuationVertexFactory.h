// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumCommon.h"
#include "Engine/StaticMesh.h"
#include "LocalVertexFactory.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneManagement.h"

/**
 * This generates the indices necessary for point attenuation in a
 * FITwinCesiumGltfPointsComponent.
 */
class FITwinCesiumPointAttenuationIndexBuffer : public FIndexBuffer {
public:
  FITwinCesiumPointAttenuationIndexBuffer(
      const int32& NumPoints,
      const bool bAttenuationSupported)
      : NumPoints(NumPoints), bAttenuationSupported(bAttenuationSupported) {}
#if ENGINE_VERSION_5_3_OR_HIGHER
  virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
#else
  virtual void InitRHI() override;
#endif

private:
  // The number of points in the original point mesh. Not to be confused with
  // the number of vertices in the attenuated point mesh.
  const int32 NumPoints;
  const bool bAttenuationSupported;
};

/**
 * The parameters to be passed as UserData to the
 * shader.
 */
struct FITwinCesiumPointAttenuationBatchElementUserData {
  FRHIShaderResourceView* PositionBuffer;
  FRHIShaderResourceView* PackedTangentsBuffer;
  FRHIShaderResourceView* ColorBuffer;
  FRHIShaderResourceView* TexCoordBuffer;
  uint32 NumTexCoords;
  uint32 bHasPointColors;
  FVector3f AttenuationParameters;
};

class FITwinCesiumPointAttenuationBatchElementUserDataWrapper
    : public FOneFrameResource {
public:
  FITwinCesiumPointAttenuationBatchElementUserData Data;
};

class FITwinCesiumPointAttenuationVertexFactory : public FLocalVertexFactory {

  DECLARE_VERTEX_FACTORY_TYPE(FITwinCesiumPointAttenuationVertexFactory);

public:
  // Sets default values for this component's properties
  FITwinCesiumPointAttenuationVertexFactory(
      ERHIFeatureLevel::Type InFeatureLevel,
      const FPositionVertexBuffer* PositionVertexBuffer);

  static bool ShouldCompilePermutation(
      const FVertexFactoryShaderPermutationParameters& Parameters);

private:
#if ENGINE_VERSION_5_3_OR_HIGHER
  virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
#else
  virtual void InitRHI() override;
#endif
  virtual void ReleaseRHI() override;
};
