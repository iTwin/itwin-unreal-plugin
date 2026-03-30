/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationFoliage.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinPopulationFoliage.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"

bool UITwinInstancedStaticMeshComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewInstanceTransform, bool bWorldSpace, bool bMarkRenderStateDirty /*= false*/, bool bTeleport /*= false*/)
{
	if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	bIsOutOfDate = true;
	// invalidate the results of the current async build we need to modify the tree
	bConcurrentChanges |= IsAsyncBuilding();

	const int32 RenderIndex = GetRenderIndex(InstanceIndex);
	const FMatrix OldTransform = PerInstanceSMData[InstanceIndex].Transform;
	const FTransform NewLocalTransform = bWorldSpace ? NewInstanceTransform.GetRelativeTransform(GetComponentTransform()) : NewInstanceTransform;
	const FVector NewLocalLocation = NewLocalTransform.GetTranslation();

	const bool bIsOmittedInstance = (RenderIndex == INDEX_NONE);
	const bool bIsBuiltInstance = !bIsOmittedInstance && RenderIndex < NumBuiltRenderInstances;

	bool bAllowInPlaceUpdateForRotationOrScaleChange = true;

	// Code path using 'bDoInPlaceUpdate' indicates that it updates the cluster tree but
	// alwasys do in place( optim)
#if WITH_EDITOR
	if (const UWorld* World = GetWorld())
	{
		const bool bIsGameWorld = World->IsGameWorld();
		bAllowInPlaceUpdateForRotationOrScaleChange = bIsGameWorld;
	}
#endif // WITH_EDITOR

	// if we are only updating rotation/scale then we update the instance directly in the cluster tree
	// optim for ItE: instance index for 0 is always in place, it causes glitches
	const bool bDoInPlaceUpdate = (InstanceIndex == 0 && !bAutoRebuildTreeOnInstanceChanges) || (bAllowInPlaceUpdateForRotationOrScaleChange && bIsBuiltInstance && NewLocalLocation.Equals(OldTransform.GetOrigin()));


	bool Result = UInstancedStaticMeshComponent::UpdateInstanceTransform(InstanceIndex, NewInstanceTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);

	// The tree will be fully rebuilt once the static mesh compilation is finished, no need for incremental update in that case.
	if (Result && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		const FBox NewInstanceBounds = GetStaticMesh()->GetBounds().GetBox().TransformBy(NewLocalTransform);

		if (bDoInPlaceUpdate)
		{
			// If the new bounds are larger than the old ones, then expand the bounds on the tree to make sure culling works correctly
			const FBox OldInstanceBounds = GetStaticMesh()->GetBounds().GetBox().TransformBy(OldTransform);
			if (!OldInstanceBounds.IsInside(NewInstanceBounds))
			{
				BuiltInstanceBounds += NewInstanceBounds;
			}
		}
		else
		{
			UnbuiltInstanceBounds += NewInstanceBounds;
			UnbuiltInstanceBoundsList.Add(NewInstanceBounds);

			BuildTreeIfOutdated(/*Async*/true, /*ForceUpdate*/false);
		}
	}

	return Result;
}

UITwinInstancedStaticMeshComponent::UITwinInstancedStaticMeshComponent()
{

}

