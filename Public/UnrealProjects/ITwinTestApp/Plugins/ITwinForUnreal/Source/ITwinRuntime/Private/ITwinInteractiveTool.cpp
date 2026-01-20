/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinInteractiveTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinInteractiveTool.h>

#include <Helpers/ITwinPickingActor.h>
#include <Helpers/WorldSingleton.h>

// UE headers
#include <EngineUtils.h> // for TActorIterator<>

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

/*static*/ void AITwinInteractiveTool::DisableAll(UWorld* World)
{
	for (TActorIterator<AITwinInteractiveTool> It(World); It; ++It)
	{
		AITwinInteractiveTool* ItTool = *It;
		if (ItTool->IsEnabled())
			ItTool->SetEnabled(false);
	}
}

AITwinInteractiveTool::IActiveStateRecord::~IActiveStateRecord()
{

}

TUniquePtr<AITwinInteractiveTool::IActiveStateRecord> AITwinInteractiveTool::MakeStateRecord() const
{
	return MakeUnique<IActiveStateRecord>();
}

bool AITwinInteractiveTool::RestoreState(IActiveStateRecord const& /*State*/)
{
	ResetToDefault();
	return true;
}

/// Enable the tool, while deactivating the others if needed.
bool AITwinInteractiveTool::MakeActiveTool(IActiveStateRecord const& State)
{
	if (!this->IsEnabled())
	{
		AITwinInteractiveTool::DisableAll(GetWorld());
		// Some tools (spline tool...) also handle different states, and need to restore the good one upon
		// activation.
		if (this->RestoreState(State))
			this->SetEnabled(true);
	}
	return this->IsEnabled();
}

bool AITwinInteractiveTool::StartInteractiveCreation()
{
	return StartInteractiveCreationImpl();
}

bool AITwinInteractiveTool::IsInteractiveCreationMode() const
{
	return IsInteractiveCreationModeImpl();
}

bool AITwinInteractiveTool::DoMouseClickAction()
{
	return DoMouseClickActionImpl();
}

AITwinInteractiveTool::ISelectionRecord::~ISelectionRecord()
{
}

bool AITwinInteractiveTool::HasSelection() const
{
	return HasSelectionImpl();
}

FTransform AITwinInteractiveTool::GetSelectionTransform() const
{
	return GetSelectionTransformImpl();
}

void AITwinInteractiveTool::OnSelectionTransformStarted()
{
	OnSelectionTransformStartedImpl();
}

void AITwinInteractiveTool::OnSelectionTransformCompleted()
{
	OnSelectionTransformCompletedImpl();
}

void AITwinInteractiveTool::SetSelectionTransform(const FTransform& Transform)
{
	SetSelectionTransformImpl(Transform);
}

AITwinInteractiveTool::IItemBackup::~IItemBackup()
{
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

bool AITwinInteractiveTool::IsCompatibleWithGizmo() const
{
	return IsCompatibleWithGizmoImpl();
}

void AITwinInteractiveTool::SetCustomPickingExtentInMeters(float PickingExtent)
{
	CustomPickingExtentInMeters = PickingExtent;
}

float AITwinInteractiveTool::GetCustomPickingExtentInMeters() const
{
	return (CustomPickingExtentInMeters ? *CustomPickingExtentInMeters : -1.f);
}

FHitResult AITwinInteractiveTool::DoPickingAtMousePosition(FITwinPickingResult* OutPickingResult /*= nullptr*/,
	TArray<const AActor*>&& IgnoredActors /*= {}*/,
	TArray<UPrimitiveComponent*>&& IgnoredComponents /*= {}*/) const
{
	auto PickingActor = TWorldSingleton<AITwinPickingActor>().Get(GetWorld());
	FITwinPickingResult PickingResult;
	PickingActor->PickUnderCursorWithOptions(PickingResult, nullptr,
		FITwinPickingOptions{
			.bSelectElement = false,
			.bSelectMaterial = false,
			.CustomTraceExtentInMeters = GetCustomPickingExtentInMeters(),
			.ActorsToIgnore = std::move(IgnoredActors),
			.ComponentsToIgnore = std::move(IgnoredComponents)
		});
	if (OutPickingResult)
	{
		*OutPickingResult = MoveTemp(PickingResult);
		return OutPickingResult->HitResult;
	}
	else
	{
		return MoveTemp(PickingResult.HitResult);
	}
}
