/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinExtractedMeshComponent.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
	if (bFullyHidden && GetVisibleFlag())
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
	bFullyHidden = bHidden;
	bool bNewVisibility = !bHidden;
	if (!bFullyHidden)
	{
		// If the parent component (ie. the mesh this entity was extracted from) is hidden, we should not
		// show the extracted mesh.
		USceneComponent* pParent = GetAttachParent();
		if (pParent && IsValid(pParent))
		{
			bNewVisibility = pParent->IsVisible();
		}
	}
	SetVisibility(bNewVisibility, /*bPropagateToChildren*/true);
}

bool UITwinExtractedMeshComponent::IsVisible() const
{
	if (bFullyHidden)
	{
		return false;
	}
	return Super::IsVisible();
}

bool UITwinExtractedMeshComponent::IsVisibleInEditor() const
{
	if (bFullyHidden)
	{
		return false;
	}
	return Super::IsVisibleInEditor();
}
