/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinExtractedMeshComponent.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinExtractedMeshComponent.h"


UITwinExtractedMeshComponent::UITwinExtractedMeshComponent()
{
}

UITwinExtractedMeshComponent::~UITwinExtractedMeshComponent()
{

}

//void UITwinExtractedMeshComponent::BeginDestroy()
//{
//	Super::BeginDestroy();
//}

void UITwinExtractedMeshComponent::OnVisibilityChanged()
{
	if (FullyHidden && (*FullyHidden) && GetVisibleFlag())
	{
		// Visibility is being set from outside, eg. by the Cesium visibility rules => ensure we do not show
		// the extracted mesh if we are not allowed to...
		// (initially, I would have hoped that to override IsVisible was sufficient, but it is not, because
		// there are places in Unreal where GetVisibleFlag is tested directly...)
		SetVisibleFlag(false);
	}
	Super::OnVisibilityChanged();
}

void UITwinExtractedMeshComponent::SetFullyHidden(bool bHidden)
{
	if (!FullyHidden || (*FullyHidden) != bHidden)
	{
		FullyHidden = bHidden;
		bool bNewVisibility = !bHidden;
		if (bNewVisibility)
		{
			// If the parent component (ie. the mesh this entity was extracted from) is hidden, we should not
			// show the extracted mesh.
			USceneComponent* pParent = GetAttachParent();
			if (ensure(IsValid(pParent)))
			{
				bNewVisibility = pParent->IsVisible();
			}
		}
		SetVisibility(bNewVisibility, /*bPropagateToChildren*/true);
	}
}

bool UITwinExtractedMeshComponent::IsVisible() const
{
	if (FullyHidden && (*FullyHidden))
	{
		return false;
	}
	return Super::IsVisible();
}

bool UITwinExtractedMeshComponent::IsVisibleInEditor() const
{
	if (FullyHidden && (*FullyHidden))
	{
		return false;
	}
	return Super::IsVisibleInEditor();
}
