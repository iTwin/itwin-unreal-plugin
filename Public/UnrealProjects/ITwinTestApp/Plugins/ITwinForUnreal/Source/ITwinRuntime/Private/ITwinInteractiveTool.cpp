/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinInteractiveTool.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinInteractiveTool.h>

#include <Helpers/ITwinPickingActor.h>
#include <Helpers/WorldSingleton.h>

AITwinInteractiveTool::AITwinInteractiveTool()
	: AActor()
{
	// Following commit h=d0d50d13, we use the PickingActor to deal with visibilities of elements.
	// By default, the trace extent of the picking actor is 1km, which can be too small when painting
	// from top view => restore the previous value of extent, before we find a more generic fix.
	// Note that in the case of the cutout polygon, we may adjust this value afterwards, see
	// U3DMapWidgetImpl::#OnOverviewCamera.
	// A corresponding task was added: AzDev#1616103
	SetCustomPickingExtentInMeters(1e6f);
}

void AITwinInteractiveTool::SetEnabled(bool bValue)
{
	SetEnabledImpl(bValue);
}

bool AITwinInteractiveTool::IsEnabled() const
{
	return IsEnabledImpl();
}

bool AITwinInteractiveTool::DoMouseClickAction()
{
	return DoMouseClickActionImpl();
}

bool AITwinInteractiveTool::HasSelection() const
{
	return HasSelectionImpl();
}

FTransform AITwinInteractiveTool::GetSelectionTransform() const
{
	return GetSelectionTransformImpl();
}

void AITwinInteractiveTool::SetSelectionTransform(const FTransform& Transform)
{
	SetSelectionTransformImpl(Transform);
}

void AITwinInteractiveTool::DeleteSelection()
{
	DeleteSelectionImpl();
}

void AITwinInteractiveTool::ResetToDefault()
{
	ResetToDefaultImpl();
}

bool AITwinInteractiveTool::IsPopulationTool() const
{
	return IsPopulationToolImpl();
}

void AITwinInteractiveTool::SetCustomPickingExtentInMeters(float PickingExtent)
{
	CustomPickingExtentInMeters = PickingExtent;
}

float AITwinInteractiveTool::GetCustomPickingExtentInMeters() const
{
	return (CustomPickingExtentInMeters ? *CustomPickingExtentInMeters : -1.f);
}

FHitResult AITwinInteractiveTool::DoPickingAtMousePosition() const
{
	auto PickingActor = TWorldSingleton<AITwinPickingActor>().Get(GetWorld());
	FString ElementId; FVector2D MousePosition;
	FHitResult hitResult;
	PickingActor->PickUnderCursorWithOptions(ElementId, MousePosition, nullptr, hitResult,
		FITwinPickingOptions{
			.bSelectElement = false,
			.bSelectMaterial = false,
			.CustomTraceExtentInMeters = GetCustomPickingExtentInMeters()
		});
	return hitResult;
}
