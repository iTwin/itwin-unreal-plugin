/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCesiumMeshBuildCallbacks.h>

class FITwinSceneMapping;

class FITwinSceneMappingBuilder : public ICesiumMeshBuildCallbacks
{
public:
	FITwinSceneMappingBuilder(FITwinSceneMapping& sceneMapping);

	virtual void OnMeshConstructed(
		const Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumData) override;

	virtual bool ShouldAllocateUVForFeatures() const override;

private:
	FITwinSceneMapping& sceneMapping_;
};
