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

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Assert.h>
#include <Compil/AfterNonUnrealIncludes.h>


inline bool AITwinPopulation::SetInstanceTransformUEOnly(int32 instanceIndex, const FTransform& tm, bool bMarkRenderStateDirty /*= true*/)
{
	bool bValidIndex = false;
	if (instanceIndex >= 0)
	{
		for (auto& FoliageComp : FoliageComponents)
		{
			if (instanceIndex < FoliageComp.GetInstanceCount())
			{
				FoliageComp.InstancedMeshComponent->UpdateInstanceTransform(instanceIndex, tm,
					/*bWorldSpace*/true, bMarkRenderStateDirty, /*bTeleport*/true);
				bValidIndex = true;
			}
		}
	}
	return bValidIndex;
}

FORCEINLINE void AITwinPopulation::CheckNumCustomDataFloats(UInstancedStaticMeshComponent& MeshComponent)
{
	if (MeshComponent.NumCustomDataFloats != NUM_CUSTOM_FLOATS_PER_INSTANCE)
	{
		MeshComponent.SetNumCustomDataFloats(NUM_CUSTOM_FLOATS_PER_INSTANCE);
	}
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
				auto* meshComp(FoliageComp.InstancedMeshComponent.Get());

				BE_ASSERT(meshComp->NumCustomDataFloats == NUM_CUSTOM_FLOATS_PER_INSTANCE);
				meshComp->SetCustomDataValue(instanceIndex, 0, v.X, bMarkRenderStateDirty);
				meshComp->SetCustomDataValue(instanceIndex, 1, v.Y, bMarkRenderStateDirty);
				meshComp->SetCustomDataValue(instanceIndex, 2, v.Z, bMarkRenderStateDirty);

				bValidIndex = true;
			}
		}
	}
	return bValidIndex;
}

inline bool AITwinPopulation::SetInstanceSelectedUEOnly(int32 InstanceIndex, bool bSelected, bool bMarkRenderStateDirty /*= true*/)
{
	bool bValidIndex = false;
	if (InstanceIndex >= 0)
	{
		for (auto& FoliageComp : FoliageComponents)
		{
			if (InstanceIndex < FoliageComp.GetInstanceCount())
			{
				auto* meshComp(FoliageComp.InstancedMeshComponent.Get());

				BE_ASSERT(meshComp->NumCustomDataFloats == NUM_CUSTOM_FLOATS_PER_INSTANCE);
				meshComp->SetCustomDataValue(InstanceIndex,
					CUSTOM_FLOAT_SELECTION_INDEX,
					bSelected ? 1.0f : 0.0f, bMarkRenderStateDirty);
				bValidIndex = true;
			}
		}
	}
	if (IsClippingPrimitive())
	{
		// AzDev#1967146: when a cutout cube/plane is selected, the other ones should be hidden.
		// There is no way to hide individual instances directly, so we do it from the shader.
		for (auto& FoliageComp : FoliageComponents)
		{
			int32 const NbInstances = FoliageComp.GetInstanceCount();
			auto* meshComp(FoliageComp.InstancedMeshComponent.Get());
			BE_ASSERT(meshComp->NumCustomDataFloats == NUM_CUSTOM_FLOATS_PER_INSTANCE);
			for (int32 i(0); i < NbInstances; ++i)
			{
				bool const bVisibleInstance = (bSelected ? (i == InstanceIndex) : true);
				// See graph in Content/Clipping/Clipping/M_TranslucentWithEdges typically.
				meshComp->SetCustomDataValue(i,
					CUSTOM_FLOAT_OPACITY_INDEX,
					bVisibleInstance ? 1.0f : 0.0f, bMarkRenderStateDirty);
			}
		}
	}
	return bValidIndex;
}
