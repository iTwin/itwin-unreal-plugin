/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Population/ITwinPopulation.h>

#include <FoliageInstancedStaticMeshComponent.h>

inline bool AITwinPopulation::SetInstanceTransformUEOnly(int32 instanceIndex, const FTransform& tm, bool bMarkRenderStateDirty /*= true*/)
{
	bool bValidIndex = false;
	if (instanceIndex >= 0)
	{
		for (auto& FoliageComp : FoliageComponents)
		{
			if (instanceIndex < FoliageComp.GetInstanceCount())
			{
				FoliageComp.FoliageInstMeshComp->UpdateInstanceTransform(instanceIndex, tm,
					/*bWorldSpace*/true, bMarkRenderStateDirty, /*bTeleport*/true);
				bValidIndex = true;
			}
		}
	}
	return bValidIndex;
}

inline bool AITwinPopulation::SetInstanceColorVariationUEOnly(int32 instanceIndex, const FVector& v, bool bMarkRenderStateDirty /*= true*/)
{
	bool bValidIndex = false;
	if (instanceIndex >= 0)
	{
		for (auto& FoliageComp : FoliageComponents)
		{
			if (instanceIndex < FoliageComp.GetInstanceCount())
			{
				auto* meshComp(FoliageComp.FoliageInstMeshComp.Get());

				if (meshComp->NumCustomDataFloats != 3)
				{
					meshComp->SetNumCustomDataFloats(3);
				}
				meshComp->SetCustomDataValue(instanceIndex, 0, v.X, bMarkRenderStateDirty);
				meshComp->SetCustomDataValue(instanceIndex, 1, v.Y, bMarkRenderStateDirty);
				meshComp->SetCustomDataValue(instanceIndex, 2, v.Z, bMarkRenderStateDirty);

				bValidIndex = true;
			}
		}
	}
	return bValidIndex;
}
