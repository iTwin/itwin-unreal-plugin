/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationFoliage.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Misc/Guid.h"
#include <FoliageInstancedStaticMeshComponent.h>

#include "ITwinPopulationFoliage.generated.h"

UCLASS()
class ITWINRUNTIME_API UITwinInstancedStaticMeshComponent : public UFoliageInstancedStaticMeshComponent
{
	GENERATED_BODY()
public:
  virtual bool UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewInstanceTransform, bool bWorldSpace, bool bMarkRenderStateDirty = false, bool bTeleport = false) override;
  UITwinInstancedStaticMeshComponent();

};