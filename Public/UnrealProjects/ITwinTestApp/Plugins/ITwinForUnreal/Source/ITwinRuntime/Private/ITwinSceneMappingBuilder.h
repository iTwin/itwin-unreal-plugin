/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCesiumMeshBuildCallbacks.h>

class AITwinIModel;
class FITwinSceneMapping;

class FITwinSceneMappingBuilder : public ICesiumMeshBuildCallbacks
{
	using Super = ICesiumMeshBuildCallbacks;
public:
	FITwinSceneMappingBuilder(FITwinSceneMapping& SceneMapping, AITwinIModel& IModel);

	virtual void OnMeshConstructed(
		const Cesium3DTilesSelection::Tile& Tile,
		const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
		const FITwinCesiumMeshData& CesiumData) override;

	virtual bool ShouldAllocateUVForFeatures() const override;

	virtual UMaterialInstanceDynamic* CreateMaterial_GameThread(
		CesiumGltf::MeshPrimitive const* pMeshPrimitive,
		UMaterialInterface*& pBaseMaterial,
		UObject* InOuter,
		FName const& Name) override;

private:
	FITwinSceneMapping& SceneMapping;
	AITwinIModel& IModel;
};
