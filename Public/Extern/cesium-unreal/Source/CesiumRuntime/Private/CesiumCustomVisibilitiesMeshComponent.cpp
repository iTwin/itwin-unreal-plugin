/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumCustomVisibilitiesMeshComponent.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "CesiumCustomVisibilitiesMeshComponent.h"

void UCesiumCustomVisibilitiesMeshComponent::OnVisibilityChanged()
{
	if (FullyHidden && (*FullyHidden) && GetVisibleFlag())
	{
		// Visibility is being set from outside, eg. by the Cesium visibility rules => ensure we do not show
		// the mesh if we are not allowed to...
		// (initially, I would have hoped that to override IsVisible was sufficient, but it is not, because
		// there are places in Unreal where GetVisibleFlag is tested directly...)
		SetVisibleFlag(false);
	}
	Super::OnVisibilityChanged();
}

void UCesiumCustomVisibilitiesMeshComponent::SetFullyHidden(bool bHidden)
{
	if (!FullyHidden || (*FullyHidden) != bHidden)
	{
		FullyHidden = bHidden;
		bool bNewVisibility = !bHidden;
		if (bNewVisibility)
		{
			// If the parent component is not visible, we should not un-hide the mesh.
			USceneComponent* pParent = GetAttachParent();
			if (IsValid(pParent))
			{
				bNewVisibility = pParent->IsVisible();
			}
		}
		SetVisibility(bNewVisibility, /*bPropagateToChildren*/true);
	}
}

bool UCesiumCustomVisibilitiesMeshComponent::IsVisible() const
{
	if (FullyHidden && (*FullyHidden))
	{
		return false;
	}
	return Super::IsVisible();
}

bool UCesiumCustomVisibilitiesMeshComponent::IsVisibleInEditor() const
{
	if (FullyHidden && (*FullyHidden))
	{
		return false;
	}
	return Super::IsVisibleInEditor();
}
