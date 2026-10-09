/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingToolImpl.h>

#include <CesiumGlobeAnchorComponent.h>

#include <Clipping/ITwinClippingEffectFactory.h>
#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingEventHub.h>
#include <Clipping/ITwinClippingInfoBase.inl>
#include <Clipping/ITwinClippingPersistence.h>
#include <Clipping/ITwinClippingRenderer.h>

#include <Helpers/ITwinMathUtils.h>
#include <Helpers/ITwinTracingHelper.h>
#include <ITwinGeolocation.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinFeatureChange.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

// UE headers
#include <Blueprint/WidgetLayoutLibrary.h>
#include <Engine/World.h>
#include <EngineUtils.h> // for TActorIterator<>


namespace ITwinClippingDetails
{
	constexpr double PLANE_TO_UNREAL = 0.01; // because the imported geometry is exactly 1 meter.

	std::optional<FCameraViewInfo> GetCameraViewInfo(UWorld const* World)
	{
		APlayerController const* pController = World ? World->GetFirstPlayerController() : nullptr;
		if (!pController || !pController->PlayerCameraManager)
		{
			return {};
		}
		bool const bOrtho = pController->PlayerCameraManager->IsOrthographic();
		if (bOrtho)
		{
			// TODO_JDE - orthographic camera
			BE_ISSUE("Clipping planes automatic scale not implemented in orthographic view.");
			return {};
		}
		return FCameraViewInfo(
			pController->PlayerCameraManager->GetCameraLocation(),
			pController->PlayerCameraManager->GetActorForwardVector(),
			FMath::Tan(FMath::DegreesToRadians(pController->PlayerCameraManager->GetFOVAngle()) * 0.5),
			UWidgetLayoutLibrary::GetViewportScale(World)
		);
	}
}

UITwinClippingToolImpl::UITwinClippingToolImpl()
{
	// New persistence management based on Scene API.
	Persistence = CreateDefaultSubobject<UITwinClippingPersistence>(TEXT("Persistence"));
	Persistence->SetEffectManager(this);

	Renderer = CreateDefaultSubobject<UITwinClippingRenderer>(TEXT("Renderer"));
	Renderer->SetEffectManager(this);

	Factory = CreateDefaultSubobject<UITwinClippingEffectFactory>(TEXT("Factory"));
	Factory->EffectPropertiesModifiedEvent.AddUniqueDynamic(Renderer.Get(), &UITwinClippingRenderer::OnEffectPropertiesModified);
	Factory->Connect(nullptr, this, Persistence);
}

void UITwinClippingToolImpl::SetEventHub(AITwinClippingEventHub* InEventHub)
{
	EventHub = InEventHub;
	Factory->SetEventHub(InEventHub);
}


void UITwinClippingToolImpl::ClippingModified(
	EITwinClippingPrimitiveType Type, EChangeType change,
	const FString& eventSource, const FString& cutoutSetting)
{
	FFeatureEventProperties properties;
	properties.ChangeType = change;
	properties.FeatureType = EFeatureType::Clipping;

	FString cutout_type;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Plane: cutout_type = TEXT("plane"); break;
	case EITwinClippingPrimitiveType::Box: cutout_type = TEXT("box"); break;
	case EITwinClippingPrimitiveType::Polygon: cutout_type = TEXT("polygon"); break;
	}
	properties.AddProperty(TEXT("cutout_type"), cutout_type);
	if (!eventSource.IsEmpty())
	{
		properties.AddProperty(TEXT("event_source"), eventSource);
	}
	if (!cutoutSetting.IsEmpty())
	{
		properties.AddProperty(TEXT("cutout_setting"), cutoutSetting);
	}

	EventHub->ClippingModifiedEvent.Broadcast(properties);
}

void UITwinClippingToolImpl::OnClippingInstanceAdded(AITwinPopulation* Population,
	EITwinInstantiatedObjectType ObjectType,
	int32 InstanceIndex)
{
	const int32 NumEffectsBefore = NumEffects(Factory->GetEffectType(ObjectType));

	bool const bIsInteractiveCreation = PopulationTool.IsValid()
		&& PopulationTool->IsInteractiveCreationMode();

	EITwinClippingPrimitiveType EffectType = Factory->OnClippingInstanceAdded(Population, ObjectType, InstanceIndex);

	if (EffectType != EITwinClippingPrimitiveType::Count)
	{
		if (Renderer)
		{
			Renderer->OnClippingInstanceArrayResized(EffectType);
		}

		// For interactive creation of a cutout cube, make sure the new cube proxy will be displayed as
		// selected (ie. update splines used for edges).
		if (NumEffects(EffectType) > NumEffectsBefore && bIsInteractiveCreation)
		{
			OnPrimitiveSelectionChanged(EffectType, InstanceIndex);
		}

		// When undoing the deletion of a clipping primitive, we need to make sure the edges will be
		// visible again.
		if (!bIsInteractiveCreation
			&& !Population->IsHiddenInGame())
		{
			// Update the visibility of the edges at once (important for undo/redo).
			GetMutableEffect(EffectType, InstanceIndex).SetEdgeVisibility(true);
		}

		EventHub->EffectListModifiedEvent.Broadcast();
		EventHub->EffectAddedEvent.Broadcast(EffectType, InstanceIndex);

		if (EffectType == EITwinClippingPrimitiveType::Plane
			&& ensure(InstanceIndex < NumEffects(EITwinClippingPrimitiveType::Plane)))
		{
			// Make sure the new plane proxy will not move until the user changes the point of view.
			LastSelectedPlaneIndex = InstanceIndex;
			LastViewInfo = ITwinClippingDetails::GetCameraViewInfo(GetWorld());
			ClippingPlaneInfos[InstanceIndex].UpdateInfluenceBoundingBox(GetWorld());
		}

		if (Persistence)
		{
			// Create new cutout in scene if needed.
			Persistence->OnCutoutAdded(EffectType, InstanceIndex);
		}
	}
}


void UITwinClippingToolImpl::OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos)
{
	EITwinClippingPrimitiveType EffectType = Factory->OnClippingInstancesLoaded(Population, bUpdateEffectInfos);

	if (EffectType != EITwinClippingPrimitiveType::Count)
	{
		Renderer->OnClippingInstanceArrayResized(EffectType);
	}
}


void UITwinClippingToolImpl::OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType,
	const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap)
{
	if (IndicesInDescendingOrder.IsEmpty())
		return;

	EITwinClippingPrimitiveType EffectType = Factory->OnClippingInstancesRemoved(ObjectType, IndicesInDescendingOrder, bUseRemoveAtSwap);
	if (ensure(EffectType != EITwinClippingPrimitiveType::Count))
	{
		// Recreate all effects from remaining instances.
		Renderer->OnClippingInstanceArrayResized(EffectType);

		// After removing a cutout, we should exit isolation mode.
		// See AzDev#2015685 (it also fixes AzDev#2015686, ie. when a cutout creation is aborted).
		SetAllEffectProxiesVisibility(true);
		// Ensure nothing remains selected after deletion (important for cube edges).
		OnPrimitiveSelectionChanged(EffectType, INDEX_NONE);

		EventHub->EffectListModifiedEvent.Broadcast();
	}
}


void UITwinClippingToolImpl::OnItemCreatedInTool(const AITwinInteractiveTool& Tool, bool bTriggeredFromITS)
{
	if (Tool.IsPopulationTool()
		&& ensure(PopulationTool.IsValid() && PopulationTool->HasSelection())
		&& PopulationTool->IsUsedOnCutoutPrimitive())
	{
		BE_ASSERT(PopulationTool->GetSelectedPopulation()->IsClippingPrimitive());
		// Finalize the proxy and gizmo.
		OnPrimitiveSelectionChanged(
			UITwinClippingEffectFactory::GetEffectType(
				PopulationTool->GetSelectedPopulation()->GetObjectType()),
			PopulationTool->GetSelectedInstanceIndex());
	}
}

void UITwinClippingToolImpl::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	const int32 NewEffectIndex = Factory->RegisterCutoutSpline(NewSpline);

	if (NewEffectIndex == INDEX_NONE)
	{
		return;
	}

	if (Renderer)
	{
		// Update tilesets influenced by this new cutout polygon.
		for (ITwin::ModelLink const& Link : NewSpline->GetLinkedModels())
		{
			auto TilesetAccessPtr = ITwin::GetTilesetAccessFromModelLink(Link, GetWorld());
			if (TilesetAccessPtr)
			{
				Renderer->UpdateTileset(*TilesetAccessPtr, EITwinClippingPrimitiveType::Polygon);
			}
		}
	}

	if (EventHub.IsValid())
	{
		EventHub->EffectListModifiedEvent.Broadcast();
		EventHub->EffectAddedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, NewEffectIndex);
	}

	if (Persistence)
	{
		Persistence->OnCutoutAdded(EITwinClippingPrimitiveType::Polygon, NewEffectIndex,
			SplineTool.IsValid() && SplineTool->IsLoadingSpline());
	}

	if (SplineTool.IsValid() && !SplineTool->IsLoadingSpline())
	{
		ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Added);
	}
}

void UITwinClippingToolImpl::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	if (SplineBeingRemoved
		&& SplineBeingRemoved->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		auto const SelectedBefore = GetSelectedEffect();

		if (Factory->DeRegisterCutoutSpline(SplineBeingRemoved, bTriggeredFromITS))
		{
			// After removing a cutout, we should exit isolation mode or else we'll be in an inconsistent state
			// (cutout selection mode in iTS, but some or all cutout proxies hidden in Unreal).
			// See AzDev#2015685
			if (SelectedBefore)
			{
				SetAllEffectProxiesVisibility(true);
			}
		}

		if (!bTriggeredFromITS && !SplineBeingRemoved->IsInteractiveCreationInProgress())
		{
			ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Deleted, TEXT("key_down"));
		}
	}
}

bool UITwinClippingToolImpl::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	// Select the cutout if needed (for undo/redo) - the effect is already selected if this event is
	// triggered from iTS cutout properties page, but not if the event is triggered from the list of cutouts.
	auto const CurrentSelection = GetSelectedEffect();
	bool bEffectIsSelected = CurrentSelection
		&& CurrentSelection->first == Type
		&& CurrentSelection->second == PrimitiveIndex;
	if (!bEffectIsSelected)
	{
		SelectEffect(Type, PrimitiveIndex, false);
	}

	return Factory->RemoveEffect(Type, PrimitiveIndex, bTriggeredFromITS);
}

TWeakObjectPtr<AITwinPopulationTool> UITwinClippingToolImpl::ActivatePopulationTool(
	bool bUpdateTransformationMode /*= true*/)
{
	auto Tool = Super::ActivatePopulationTool(bUpdateTransformationMode);
	if (Tool.IsValid())
	{
		if (bUpdateTransformationMode && TransformationModeOpt)
		{
			Tool->SetTransformationMode(*TransformationModeOpt);
		}
	}
	return Tool;
}

AITwinPopulation const* UITwinClippingToolImpl::GetSelectedPopulation(int32& OutSelectedInstanceIndex) const
{
	AITwinPopulation const* SelectedPopulation = nullptr;
	OutSelectedInstanceIndex = INDEX_NONE;
	if (ensure(PopulationTool.IsValid()))
	{
		SelectedPopulation = PopulationTool->GetSelectedPopulation();
		if (SelectedPopulation)
		{
			OutSelectedInstanceIndex = PopulationTool->GetSelectedInstanceIndex();
		}
	}
	return SelectedPopulation;
}

void UITwinClippingToolImpl::SelectPopulationInstance(AITwinPopulation* Population,
	int32 InstanceIndex,
	EITwinClippingPrimitiveType Type)
{
	bool const bUpdateTransformationMode = (Population != nullptr);

	// Cut-out plane are infinite and thus can not be resized.
	AdjustTransformationModeForPopulation(Population);

	auto const PopTool = ActivatePopulationTool(bUpdateTransformationMode);
	if (PopTool.IsValid())
	{
		PopTool->SetSelectedPopulation(Population);
		PopTool->SetSelectedInstanceIndex(InstanceIndex);
		OnPrimitiveSelectionChanged(Type, InstanceIndex);

		if (bUpdateTransformationMode)
		{
			// Quick fix to refresh the gizmo mode: the call to SelectionChangedEvent below would only
			// refresh the coordinates of the gizmo, but if no instance was selected, and the gizmo
			// was used for the Spline Tool (thus in Move mode), we should ensure we update it to use
			// the transformation mode imposed here.
			// (see ADO#2138730, in particular the video 2138730-1911.mp4).
			EventHub->ActivationEvent.Broadcast(true);
		}
		PopTool->SelectionChangedEvent.Broadcast();
	}
}

void UITwinClippingToolImpl::AdjustTransformationModeForPopulation(const AITwinPopulation* Population)
{
	// Cut-out plane are infinite and thus can not be resized.
	if (Population
		&& Population->GetObjectType() == EITwinInstantiatedObjectType::ClippingPlane
		&& TransformationModeOpt
		&& TransformationModeOpt.value() == ETransformationMode::Scale)
	{
		SetTransformationMode(ETransformationMode::Move);

		// Trigger event to refresh the selection gizmo, typically.
		EventHub->ActivationEvent.Broadcast(true);
	}
}

void UITwinClippingToolImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	ensure(SplineTool.IsValid());
	ITwin::SelectSpline(SplineHelper, INDEX_NONE, World, SplineTool);
}

UITwinClippingToolImpl::FSelectionChangeDetector::FSelectionChangeDetector(UITwinClippingToolImpl& InImpl,
	bool bInManageIsolationMode /*= false*/)
	: Impl(InImpl)
	, PreviousSelection(InImpl.GetSelectedEffect())
	, bManageIsolationMode(bInManageIsolationMode)
{
}

UITwinClippingToolImpl::FSelectionChangeDetector::~FSelectionChangeDetector()
{
	auto const NewSelection = Impl.GetSelectedEffect();
	if (NewSelection)
	{
		if (bManageIsolationMode)
		{
			// Isolation of the selected item, if any.
			if (!PreviousSelection || PreviousSelection->first != NewSelection->first)
			{
				Impl.ShowOnlyProxiesOfType(NewSelection->first, true);
			}
		}

		if (NewSelection != PreviousSelection)
		{
			if (PreviousSelection && PreviousSelection->first != NewSelection->first)
			{
				Impl.OnPrimitiveSelectionChanged(PreviousSelection->first, INDEX_NONE);
			}
			Impl.OnPrimitiveSelectionChanged(NewSelection->first, NewSelection->second);
		}
	}
	else if (PreviousSelection)
	{
		if (bManageIsolationMode)
		{
			// End of isolation mode.
			Impl.SetAllEffectProxiesVisibility(true);
		}

		Impl.OnPrimitiveSelectionChanged(PreviousSelection->first, INDEX_NONE);
	}
}

void UITwinClippingToolImpl::SetTransformationMode(ETransformationMode Mode)
{
	TransformationModeOpt = Mode;

	if (ensure(PopulationTool.IsValid()))
	{
		PopulationTool->SetTransformationMode(Mode);
	}
}

bool UITwinClippingToolImpl::IsRotationMode() const
{
	return TransformationModeOpt && TransformationModeOpt.value() == ETransformationMode::Rotate;
}

void UITwinClippingToolImpl::FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	SetInvertEffect(Type, PrimitiveIndex, !GetInvertEffect(Type, PrimitiveIndex));
}

void UITwinClippingToolImpl::FlipAllEffectsOfType(EITwinClippingPrimitiveType Type)
{
	const int32 NumEff = NumEffects(Type);
	for (int32 i(0); i < NumEff; ++i)
	{
		FlipEffect(Type, i);
	}
}


void UITwinClippingToolImpl::OnSceneLoaded(bool bSuccess)
{
	if (!bSuccess)
		return;

	// When the scene is loaded, we need to update our clipping effects based on the loaded instances,
	// and make sure they are properly applied to the tilesets in the scene.
	Renderer->UpdateAllTilesets();
}


int32 UITwinClippingToolImpl::FindSelectedPolygonIndex() const
{
	if (SplineTool.IsValid() && SplineTool->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		AITwinSplineHelper const* SelectedSpline = SplineTool->GetSelectedSpline();
		if (SelectedSpline)
		{
			return GetCutoutPolygonIndex(SelectedSpline);
		}
	}
	return INDEX_NONE;
}

void UITwinClippingToolImpl::OnSplineMoveStart()
{
	if (!ensure(SplineTool.IsValid()) || SplineTool->GetUsage() != EITwinSplineUsage::MapCutout)
	{
		return;
	}

	ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Modified, TEXT("gizmo"), TEXT("position"));
}

void UITwinClippingToolImpl::OnSplinePointMoveStart()
{
	if (!ensure(SplineTool.IsValid()) || SplineTool->GetUsage() != EITwinSplineUsage::MapCutout)
	{
		return;
	}

	ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Modified, TEXT("gizmo"), TEXT("point_position"));
}

void UITwinClippingToolImpl::OnSplinePointMoved(bool bMovedInITS)
{
	if (!ensure(SplineTool.IsValid()) || SplineTool->GetUsage() != EITwinSplineUsage::MapCutout)
	{
		return;
	}

	EventHub->SplinePointMovedEvent.Broadcast(bMovedInITS);

	// Handle persistence (only if the modified spline corresponds to a cutout effect).
	if (!Persistence)
		return;

	const int32 PolygonIndex = FindSelectedPolygonIndex();
	if (PolygonIndex == INDEX_NONE)
	{
		return;
	}
	// Work with AViz Spline API to get the updated point position, as it was already converted to the
	// right coordinate system (not Unreal...)
	auto const AVizSpline = SplineTool->GetSelectedSpline()->GetAVizSpline();
	if (ensure(AVizSpline))
	{
		if (SplineTool->GetSelectedPointIndex() < 0)
		{
			BE_ASSERT(SplineTool->GetSelectedPointIndex() == INDEX_NONE);
			// If the spline was moved globally, update its transformation in the persistence.
			auto Spline = AVizSpline->GetRAutoLock();
			Persistence->UpdatePolygonTransform(PolygonIndex, Spline->GetTransform());
		}
		else
		{
			const size_t PointIndex = static_cast<size_t>(SplineTool->GetSelectedPointIndex());
			AdvViz::SDK::ISplinePointPtr PointPtr;
			{
				auto Spline = AVizSpline->GetRAutoLock();
				if (ensure(PointIndex < Spline->GetNumberOfPoints()))
				{
					PointPtr = Spline->GetPoint(PointIndex);
				}
			}
			if (PointPtr)
			{
				auto Point = PointPtr->GetAutoLock();
				Persistence->UpdatePolygonPoint(PolygonIndex, PointIndex, Point->GetPosition());
			}
		}
	}
}

void UITwinClippingToolImpl::OnSplinePointAdded()
{
	if (!ensure(SplineTool.IsValid()) || SplineTool->GetUsage() != EITwinSplineUsage::MapCutout)
	{
		return;
	}

	if (!SplineTool->IsInteractiveCreationMode())
	{
		ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Modified, TEXT("mouse_click"), TEXT("point_added"));
	}
	OnSplinePointArrayModified();
}

void UITwinClippingToolImpl::OnSplinePointRemoved()
{
	if (!ensure(SplineTool.IsValid()) || SplineTool->GetUsage() != EITwinSplineUsage::MapCutout)
	{
		return;
	}

	if (!SplineTool->IsInteractiveCreationMode())
	{
		ClippingModified(EITwinClippingPrimitiveType::Polygon, EChangeType::Modified, TEXT("key_down"), TEXT("point_removed"));
	}

	OnSplinePointArrayModified();
}

void UITwinClippingToolImpl::OnSplinePointArrayModified()
{
	// Upon a major modification (add or remove point), we send the full spline again to persistence manager.
	if (Persistence)
	{
		const int32 PolygonIndex = FindSelectedPolygonIndex();
		if (PolygonIndex != INDEX_NONE)
		{
			Persistence->SendToScene(EITwinClippingPrimitiveType::Polygon, PolygonIndex);
		}
	}
}

void UITwinClippingToolImpl::RegisterLoadedEffectsInScene(EITwinClippingPrimitiveType EffectType)
{
	if (Persistence)
	{
		const int32 NumEff = NumEffects(EffectType);
		for (int32 i(0); i < NumEff; ++i)
		{
			if (!GetEffect(EffectType, i).HasSceneLink())
			{
				Persistence->SendToScene(EffectType, i);
			}
		}
	}
}


bool UITwinClippingToolImpl::GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const
{
	if (ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
	{
		return GetEffect(Type, PrimitiveIndex).GetInvertEffect();
	}
	else
	{
		return false;
	}
}

void UITwinClippingToolImpl::SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert)
{
	if (ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
	{
		FITwinClippingInfoBase& PrimitiveInfo = GetMutableEffect(Type, PrimitiveIndex);
		if (PrimitiveInfo.GetInvertEffect() != bInvert)
		{
			PrimitiveInfo.SetInvertEffect(bInvert);

			// Refresh tilesets
			if (Type == EITwinClippingPrimitiveType::Polygon)
			{
				// For cartographic polygons we do it through the general update.
				Renderer->UpdateAllTilesets(Type);
			}
			else
			{
				// For other types we just update one parameter in Material Parameter Collection.
				Renderer->EncodeFlippingInMPC(Type, PrimitiveIndex);
			}

			if (Persistence)
			{
				Persistence->UpdateInversionInfo(Type, PrimitiveIndex);
			}
		}
	}
}


bool UITwinClippingToolImpl::SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	bool bEnterIsolationMode /*= true*/)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	bool bHasSetSelection = false;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(Type);
		if (Population.IsValid())
		{
			SelectPopulationInstance(Population.Get(), PrimitiveIndex, Type);

			bHasSetSelection = (Population->GetSelectedInstanceIndex() == PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.GetSpline().IsValid())
		{
			SelectSpline(PolygonInfo.GetSpline().Get(), GetWorld());

			bHasSetSelection = PolygonInfo.GetSpline()->IsSelected();
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}

	if (bEnterIsolationMode && bHasSetSelection)
	{
		// Isolation mode (AzDev#1967146)
		// Hide all other types. Note that the isolation *inside* the selected type is performed at a lower
		// level:
		// - look for #CUSTOM_FLOAT_OPACITY_INDEX in AITwinPopulation for cube/plane
		// - see UITwinClippingToolImpl::SetSelectedSpline for cutout polygon
		ShowOnlyProxiesOfType(Type, /*bIsolationMode*/true);
	}
	return bHasSetSelection;
}


std::optional<UITwinClippingToolImpl::FEffectIdentifier> UITwinClippingToolImpl::GetSelectedEffect() const
{
	// Recover the selected effect from the population tool or spline tool.

	// First test the population tool:
	int32 InstanceIndex(INDEX_NONE);
	AITwinPopulation const* SelectedPopulation = GetSelectedPopulation(InstanceIndex);
	if (SelectedPopulation)
	{
		if (SelectedPopulation == BoxPopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Box, InstanceIndex);
		if (SelectedPopulation == PlanePopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Plane, InstanceIndex);
	}
	// Then the spline tool:
	AITwinSplineHelper const* SelectedSpline = nullptr;
	if (ensure(SplineTool.IsValid()) && SplineTool->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		SelectedSpline = SplineTool->GetSelectedSpline();
	}
	if (SelectedSpline)
	{
		int32 PolyEffectIndex = GetCutoutPolygonIndex(SelectedSpline);
		if (PolyEffectIndex != INDEX_NONE)
		{
			return std::make_pair(EITwinClippingPrimitiveType::Polygon, PolyEffectIndex);
		}
	}
	return std::nullopt;
}


void UITwinClippingToolImpl::SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	if (ensure(PolygonIndex >= 0 && PolygonIndex < ClippingPolygonInfos.Num())
		&& ClippingPolygonInfos[PolygonIndex].GetSpline().IsValid())
	{
		AITwinSplineHelper* EditedSpline = ClippingPolygonInfos[PolygonIndex].GetSpline().Get();

		// Always prefer using the geo-located geo-reference
		auto&& Geoloc = FITwinGeolocation::Get(*World);

		ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
			? Geoloc->GeoReference.Get() : EditedSpline->GlobeAnchor->ResolveGeoreference();

		if (ensure(PointIndex >= 0)
			&& PointIndex < EditedSpline->GetNumberOfSplinePoints()
			&& ensure(GeoRef != nullptr))
		{
			FVector CurrentLocation = EditedSpline->GetLocationAtSplinePoint(PointIndex);
			FVector CurrentCartographic =
				GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(CurrentLocation);
			// Do not change elevation
			FVector NewUEPosition =
				GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
					FVector(Longitude, Latitude, CurrentCartographic.Z));
			EditedSpline->SetLocationAtSplinePoint(PointIndex, NewUEPosition);

			// If this is the currently selected point (which, most of the time, will be the case), we need
			// to synchronize the gizmo.
			if (ensure(SplineTool.IsValid())
				&& EditedSpline == SplineTool->GetSelectedSpline()
				&& PointIndex == SplineTool->GetSelectedPointIndex())
			{
				SplineTool->SplinePointMovedEvent.Broadcast(true /*bMovedInITS*/);
			}
		}
	}
}


bool UITwinClippingToolImpl::GetEffectTransform(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	UWorld* World = GetWorld();
	if (!World)
		return false;

	auto&& Geoloc = FITwinGeolocation::Get(*World);
	// Always prefer using the geo-located geo-reference
	ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
		? Geoloc->GeoReference.Get() : Geoloc->LocalReference.Get();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(Type);
		if (Population.IsValid())
		{
			OutTransform = Population->GetInstanceTransform(PrimitiveIndex);
		}
		else
		{
			return false;
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.GetSpline().IsValid())
		{
			AITwinSplineHelper const* Polygon = PolygonInfo.GetSpline().Get();
			if (ensure(Polygon->GlobeAnchor))
			{
				GeoRef = Polygon->GlobeAnchor->ResolveGeoreference();
			}
			OutTransform = Polygon->GetTransformForUserInteraction();
		}
		else
		{
			return false;
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, false);
	}

	if (ensure(GeoRef))
	{
		const FVector Cartographic =
			GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(OutTransform.GetLocation());
		OutLatitude = Cartographic.Y;
		OutLongitude = Cartographic.X;
		OutElevation = Cartographic.Z;
	}
	return true;
}



template <typename FTransfoBuilderFunc>
void UITwinClippingToolImpl::TModifyEffectTransformation(FTransfoBuilderFunc const& BuilderFunc,
	EITwinClippingPrimitiveType Type,
	int32 PrimitiveIndex,
	bool bTriggeredFromITS,
	bool bOnlyModifyProxy /*= false*/) const
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	// Always prefer using the geo-located geo-reference
	auto&& Geoloc = FITwinGeolocation::Get(*World);
	ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
		? Geoloc->GeoReference.Get() : Geoloc->LocalReference.Get();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(Type);
		if (Population.IsValid())
		{
			FTransform NewTransform = Population->GetInstanceTransform(PrimitiveIndex);
			BuilderFunc(NewTransform, GeoRef);
			if (bOnlyModifyProxy)
			{
				Population->SetInstanceTransformUEOnly(PrimitiveIndex, NewTransform);
				// Update the splines used to display the edges of the cube or plane.
				Factory->UpdateEdgesFromInstance(Type, PrimitiveIndex);
			}
			else
			{
				Population->SetInstanceTransform(PrimitiveIndex, NewTransform, bTriggeredFromITS);
			}

			if (ensure(PopulationTool.IsValid())
				&& Population.Get() == PopulationTool->GetSelectedPopulation()
				&& PrimitiveIndex == PopulationTool->GetSelectedInstanceIndex())
			{
				PopulationTool->SelectionChangedEvent.Broadcast();
			}
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.GetSpline().IsValid())
		{
			AITwinSplineHelper* EditedSpline = PolygonInfo.GetSpline().Get();
			if (ensure(EditedSpline->GlobeAnchor))
			{
				GeoRef = EditedSpline->GlobeAnchor->ResolveGeoreference();
			}
			FTransform NewTransform = EditedSpline->GetTransformForUserInteraction();
			BuilderFunc(NewTransform, GeoRef);
			EditedSpline->SetTransformFromUserInteraction(NewTransform);

			// If this is the currently selected point (which, most of the time, will be the case), we need
			// to synchronize the gizmo.
			if (ensure(SplineTool.IsValid())
				&& EditedSpline == SplineTool->GetSelectedSpline())
			{
				SplineTool->SplineSelectionEvent.Broadcast();
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
}

void UITwinClippingToolImpl::SetEffectLocation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InLatitude, double InLongitude, double InElevation,
	bool bTriggeredFromITS) const
{
	TModifyEffectTransformation(
		[=](FTransform& NewTransform, ACesiumGeoreference const* GeoRef)
	{
		if (ensure(GeoRef))
		{
			FVector NewUEPosition =
				GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
					FVector(InLongitude, InLatitude, InElevation));
			NewTransform.SetLocation(NewUEPosition);
		}
	}, Type, PrimitiveIndex, bTriggeredFromITS);
}

void UITwinClippingToolImpl::SetEffectRotation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InRotX, double InRotY, double InRotZ,
	bool bTriggeredFromITS)
{
	TModifyEffectTransformation(
		[=](FTransform& NewTransform, ACesiumGeoreference const* GeoRef)
	{
		FVector Rot(InRotX, InRotY, InRotZ);
		NewTransform.SetRotation(FQuat::MakeFromEuler(Rot));
	}, Type, PrimitiveIndex, bTriggeredFromITS);

	if (Type == EITwinClippingPrimitiveType::Plane)
	{
		// Ensure we reset the gizmo proxy to match the new orientation.
		UpdateClippingPlaneTransformFromBoundingBox(PrimitiveIndex, true);
	}
}


namespace ITwinClippingDetails
{

	inline FVector SnapVectorToGrid(FVector const& Vec, double SnapGridSize)
	{
		if (ensure(SnapGridSize > 0))
		{
			return FVector(
				FMath::RoundToFloat(Vec.X / SnapGridSize) * SnapGridSize,
				FMath::RoundToFloat(Vec.Y / SnapGridSize) * SnapGridSize,
				FMath::RoundToFloat(Vec.Z / SnapGridSize) * SnapGridSize);
		}
		else
		{
			return Vec;
		}
	}

	static FVector SnapLocationToGrid(FVector const& WorldPosition,
		FBox const& RefBoundingBox,
		double SnapGridSize)
	{
		if (ensure(SnapGridSize > 0))
		{
			if (RefBoundingBox.IsValid)
			{
				FVector const RelativePos = WorldPosition - RefBoundingBox.Min;
				return RefBoundingBox.Min + FVector(
					FMath::RoundToFloat(RelativePos.X / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(RelativePos.Y / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(RelativePos.Z / SnapGridSize) * SnapGridSize);
			}
			else
			{
				return FVector(
					FMath::RoundToFloat(WorldPosition.X / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(WorldPosition.Y / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(WorldPosition.Z / SnapGridSize) * SnapGridSize);
			}
		}
		else
		{
			return WorldPosition;
		}
	}


	inline double GetSnapGridSize(FBox const& InfluenceBoundingBox)
	{
		if (InfluenceBoundingBox.IsValid)
		{
			return FMath::Max(InfluenceBoundingBox.GetSize().GetMax() * 0.001,
				1000.0);
		}
		else
		{
			// Infinite influence (Google tileset)
			// Let's use a fixed size of 20 meters.
			return 20 * 100.0;
		}
	}

}

std::optional<FVector> UITwinClippingToolImpl::GetPointOnPlaneAtRayIntersection(int32 PlaneIndex,
	FITwinRayTraceInput const& TraceInput,
	bool bClampToInfluenceBounds)
{
	using namespace ITwinClippingDetails;
	if (!ensure(IsValidEffectIndex(EITwinClippingPrimitiveType::Plane, PlaneIndex)))
	{
		return {};
	}
	FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
	FVector PlaneOrientation = PlaneInfo.GetPlaneEquation().PlaneOrientation;
	FVector::FReal PlaneW = PlaneInfo.GetPlaneEquation().PlaneW;

	FPlane const Plane(PlaneOrientation, PlaneW);

	// Try to intersect the infinite plane defined by the cutout with the ray.
	FVector HitPoint;
	double Distance(0.);
	if (ITwin::RayPlaneIntersection(TraceInput.TraceStart, TraceInput.TraceDirection, Plane, HitPoint, Distance))
	{
		std::optional<FVector> ClampedLocationOpt;

		if (bClampToInfluenceBounds)
		{
			// Retrieve the influence bounds of the plane, to limit the re-centering within these bounds.
			FBox InfluenceBoundingBox = PlaneInfo.GetUpToDateInfluenceBoundingBox(GetWorld());
			if (InfluenceBoundingBox.IsValid)
			{
				// Try to shrink the influence box by the current plane box, to avoid snapping on the edge
				// of the plane box instead of the influence bounds when the plane box is almost as big as
				// the influence bounds.
				FVector const InfluenceBoxExtent = InfluenceBoundingBox.GetExtent();

				// We already tested this population validity in #GetPlaneEquationFromUEInstance, so we can
				// safely use it here.
				auto const& Population = GetPopulation(EITwinClippingPrimitiveType::Plane);
				FBox const CurrentPlaneBox = Population->GetInstanceBoundingBox(PlaneIndex);
				FVector PlaneBoxExtent = SnapVectorToGrid(CurrentPlaneBox.GetExtent(), 500.);
				PlaneBoxExtent = FVector::Min(PlaneBoxExtent, 0.8 * InfluenceBoxExtent);
				InfluenceBoundingBox = InfluenceBoundingBox.ExpandBy(-PlaneBoxExtent);
			}

			FVector ClosestPointOnOrInsideBox;
			if (!InfluenceBoundingBox.IsValid || InfluenceBoundingBox.IsInsideOrOn(HitPoint))
			{
				// The hit point is already inside the influence bounds.
				ClosestPointOnOrInsideBox = HitPoint;
			}
			else
			{
				ClosestPointOnOrInsideBox = InfluenceBoundingBox.GetClosestPointTo(HitPoint);
			}

			// Snap position to a fixed grid depending on the influence box.
			const double SnapGridSize = GetSnapGridSize(InfluenceBoundingBox);
			FVector const ClosestPoint = SnapLocationToGrid(ClosestPointOnOrInsideBox, InfluenceBoundingBox, SnapGridSize);

			FVector Dir_Closest = ClosestPoint - TraceInput.TraceStart;
			FVector HitPoint_Closest;
			double Distance_Closest(0.);
			if (Dir_Closest.Normalize()
				&& ITwin::RayPlaneIntersection(TraceInput.TraceStart, Dir_Closest, Plane, HitPoint_Closest, Distance_Closest))
			{
				ClampedLocationOpt = HitPoint_Closest;
			}
		}
		return ClampedLocationOpt.value_or(HitPoint);
	}
	return {};
}

std::optional<FVector> UITwinClippingToolImpl::ProjectPredefinedScreenPositionOnPlane(int32 PlaneIndex,
	bool bClampToInfluenceBounds)
{
	// Build rays starting from predefined positions on the screen, and try to intersect the infinite plane
	// defined by the cutout.
	static const TArray<FVector2d> PredefinedRatios = {
		FVector2d(1. / 2., 2. / 3.), // middle of the bottom half of the screen
		FVector2d(1. / 3., 2. / 3.), // left part of the bottom half of the screen
		FVector2d(2. / 3., 2. / 3.), // right part of the bottom half of the screen
		FVector2d(1. / 2., 1. / 2.), // center of the screen
		FVector2d(1. / 2., 1. / 3.), // middle of the top half of the screen
		FVector2d(1. / 3., 1. / 3.), // left part of the top half of the screen
		FVector2d(2. / 3., 1. / 3.)  // right part of the top half of the screen
	};
	TArray<FITwinRayTraceInput> TraceInputs;
	FITwinTracingHelper::GetRayTraceInputsFromScreenRatios(this,
		PredefinedRatios, TraceInputs);
	std::optional<FVector> FirstValidHit;
	for (FITwinRayTraceInput const& TraceInput : TraceInputs)
	{
		FirstValidHit = GetPointOnPlaneAtRayIntersection(PlaneIndex, TraceInput, bClampToInfluenceBounds);
		if (FirstValidHit)
		{
			break;
		}
	}
	return FirstValidHit;
}

namespace
{
	class FPlaneTransformProxy : public IITwinPopulationInstanceTransformProxy
	{
	public:
		FPlaneTransformProxy(TWeakObjectPtr<AITwinPopulation> InPlanePopulation, int32 InPlaneIndex, FVector const& InOffset)
			: PlanePopulation(InPlanePopulation)
			, PlaneIndex(InPlaneIndex)
			, Offset(InOffset)
		{
		}
		virtual FTransform GetTransform() const override
		{
			FTransform GizmoTsf;
			if (PlanePopulation.IsValid()
				&& ensure(PlaneIndex >= 0 && PlaneIndex < PlanePopulation->GetNumberOfInstances()))
			{
				GizmoTsf = PlanePopulation->GetInstanceTransform(PlaneIndex);
				GizmoTsf.SetLocation(GizmoTsf.GetLocation() + Offset);
			}
			else
			{
				GizmoTsf = FTransform::Identity;
			}
			return GizmoTsf;
		}

		virtual void OnTransformModificationStarted(ETransformationMode TransformationMode) override
		{
			if (TransformationMode == ETransformationMode::Rotate)
			{
				// For the rotation mode, we will move the plane proxy at the gizmo position (there is no
				// handling of both a position and a pivot offset in our instances.
				// This is not ideal visually (as the plane will exit the influence bounds quite abruptly).
				if (PlanePopulation.IsValid()
					&& ensure(PlaneIndex >= 0 && PlaneIndex < PlanePopulation->GetNumberOfInstances()))
				{
					PlanePopulation->SetInstanceTransform(PlaneIndex, GetTransform());
					Offset = FVector::ZeroVector;
				}
			}
		}

		virtual void SetTransform(const FTransform& Transform) override
		{
			if (PlanePopulation.IsValid()
				&& ensure(PlaneIndex >= 0 && PlaneIndex < PlanePopulation->GetNumberOfInstances()))
			{
				FTransform PlaneTsf = Transform;
				PlaneTsf.SetLocation(PlaneTsf.GetLocation() - Offset);
				PlanePopulation->SetInstanceTransform(PlaneIndex, PlaneTsf);
			}
		}

		TWeakObjectPtr<AITwinPopulation> PlanePopulation;
		int32 const PlaneIndex;
		FVector Offset;
	};
}

bool UITwinClippingToolImpl::UpdateClippingPlaneTransformFromBoundingBox(int32 PlaneIndex,
	bool bForceResetGizmoProxy /*= false*/)
{
	using namespace ITwinClippingDetails;

	if (bForceResetGizmoProxy)
	{
		ConfigurePlaneProxyForGizmo(INDEX_NONE);
	}

	if (!ensure(IsValidEffectIndex(EITwinClippingPrimitiveType::Plane, PlaneIndex)))
	{
		return false;
	}
	FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
	FVector PlaneOrientation = PlaneInfo.GetPlaneEquation().PlaneOrientation;
	FVector::FReal PlaneW = PlaneInfo.GetPlaneEquation().PlaneW;

	CesiumGeometry::Plane const Plane(
		glm::dvec3(PlaneOrientation.X, PlaneOrientation.Y, PlaneOrientation.Z),
		-PlaneW
	);

	FBox const& BoundingBox = PlaneInfo.GetUpToDateInfluenceBoundingBox(GetWorld());
	if (!BoundingBox.IsValid)
	{
		return false;
	}
	if (!PlanePopulation.IsValid())
	{
		return false;
	}
	FQuat PlaneRotation = PlanePopulation->GetInstanceTransform(PlaneIndex).GetRotation();
	PlaneRotation.Normalize();
	FVector const EulerAngles = PlaneRotation.Euler();
	double AngleX = FMath::DegreesToRadians(EulerAngles.X);
	double AngleY = FMath::DegreesToRadians(EulerAngles.Y);
	double AngleZ = FMath::DegreesToRadians(EulerAngles.Z);

	FVector const BoxCenter = BoundingBox.GetCenter();
	FVector const BoxSize = BoundingBox.GetSize();

	FVector const AbsOrientation = PlaneOrientation.GetAbs();

	FVector NewScale(1., 1., 0.1);
	if (AbsOrientation.Z >= AbsOrientation.X && AbsOrientation.Z >= AbsOrientation.Y)
	{
		NewScale.X = (BoxSize.X * FMath::Abs(FMath::Cos(AngleZ)))
			+ (BoxSize.Y * FMath::Abs(FMath::Sin(AngleZ)));
		NewScale.Y = (BoxSize.Y * FMath::Abs(FMath::Cos(AngleZ)))
			+ (BoxSize.X * FMath::Abs(FMath::Sin(AngleZ)));
	}
	else if (AbsOrientation.Y >= AbsOrientation.X && AbsOrientation.Y >= AbsOrientation.Z)
	{
		NewScale.X = (BoxSize.X * FMath::Abs(FMath::Cos(AngleY)))
			+ (BoxSize.Z * FMath::Abs(FMath::Sin(AngleY)));
		NewScale.Y = (BoxSize.Z * FMath::Abs(FMath::Cos(AngleY)))
			+ (BoxSize.X * FMath::Abs(FMath::Sin(AngleY)));
	}
	else
	{
		NewScale.X = (BoxSize.Z * FMath::Abs(FMath::Cos(AngleX)))
			+ (BoxSize.Y * FMath::Abs(FMath::Sin(AngleX)));
		NewScale.Y = (BoxSize.Y * FMath::Abs(FMath::Cos(AngleX)))
			+ (BoxSize.Z * FMath::Abs(FMath::Sin(AngleX)));
	}
	NewScale.X *= PLANE_TO_UNREAL;
	NewScale.Y *= PLANE_TO_UNREAL;

	// Move the center of the plane to the intersected point. This does not change the plane equation, so
	// we should not invalidate DB nor recompute the plane effect: only modify the proxy transform.
	glm::dvec3 const ProjCenter = Plane.projectPointOntoPlane(glm::dvec3(BoxCenter.X, BoxCenter.Y, BoxCenter.Z));
	FVector const ProjectedCenter(ProjCenter.x, ProjCenter.y, ProjCenter.z);

	TModifyEffectTransformation(
		[&NewScale, &ProjectedCenter](FTransform& NewTransform, ACesiumGeoreference const* /*GeoRef*/)
	{
		NewTransform.SetLocation(ProjectedCenter);
		NewTransform.SetScale3D(NewScale);
	},
		EITwinClippingPrimitiveType::Plane,
		PlaneIndex,
		false /*bTriggeredFromITS*/,
		true /*bOnlyModifyProxy*/);

	if (PlaneIndex == PlanePopulation->GetSelectedInstanceIndex()
		&& !PlanePopulation->IsBeingInteractivelyTransformed())
	{
		ConfigurePlaneProxyForGizmo(PlaneIndex);
	}
	return true;
}

void UITwinClippingToolImpl::ConfigurePlaneProxyForGizmo(int32 PlaneIndex)
{
	std::optional<FVector> CurrentGizmoPositionOpt;
	if (PlaneTransformProxy)
	{
		CurrentGizmoPositionOpt = PlaneTransformProxy->GetTransform().GetLocation();
	}
	PlaneTransformProxy.Reset();

	FVector VisiblePosition;
	bool bVisibleCenter(false);
	if (PlaneIndex != INDEX_NONE
		&& FindVisiblePointOnPlane(PlaneIndex, CurrentGizmoPositionOpt, VisiblePosition, bVisibleCenter))
	{
		if (!bVisibleCenter)
		{
			// If the center is not visible, we will shift the gizmo position to a visible point.
			ensure(PlanePopulation.IsValid()); // guaranteed by #FindVisiblePointOnPlane
			FVector const Offset = VisiblePosition - PlanePopulation->GetInstanceTransform(PlaneIndex).GetLocation();
			PlaneTransformProxy = MakeShared<FPlaneTransformProxy>(
				PlanePopulation, PlaneIndex, Offset);
		}
	}
	if (PopulationTool.IsValid())
	{
		PopulationTool->SetInstanceTransformProxy(PlaneTransformProxy);
	}
}

void UITwinClippingToolImpl::OnPlaneSelectionChanged(int32 SelectedPlaneIndex)
{
	// First disable previous plane proxy offset.
	ConfigurePlaneProxyForGizmo(INDEX_NONE);

	if (SelectedPlaneIndex != INDEX_NONE)
	{
		// Update the plane proxy position and the proxy offset (if needed).
		UpdateClippingPlaneTransformFromBoundingBox(SelectedPlaneIndex);
	}
	// Update selection flags in plane edge helpers.
	for (int32 PlaneIndex = 0; PlaneIndex < ClippingPlaneInfos.Num(); PlaneIndex++)
	{
		ClippingPlaneInfos[PlaneIndex].SetEdgeSplinesSelected(PlaneIndex == SelectedPlaneIndex);
	}
}

void UITwinClippingToolImpl::OnBoxSelectionChanged(int32 SelectedBoxIndex)
{
	// Update selection flags in box edge helpers.
	for (int32 BoxIndex = 0; BoxIndex < ClippingBoxInfos.Num(); BoxIndex++)
	{
		ClippingBoxInfos[BoxIndex].SetEdgeSplinesSelected(BoxIndex == SelectedBoxIndex);
	}
}

void UITwinClippingToolImpl::OnPrimitiveSelectionChanged(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		OnBoxSelectionChanged(PrimitiveIndex);
		break;
	case EITwinClippingPrimitiveType::Plane:
		OnPlaneSelectionChanged(PrimitiveIndex);
		break;
	case EITwinClippingPrimitiveType::Polygon:
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
}

bool UITwinClippingToolImpl::FindVisiblePointOnPlane(int32 PlaneIndex,
	std::optional<FVector> const& CurrentGizmoPositionOpt,
	FVector& OutPosition,
	bool& bOutIsCenter)
{
	if (!ensure(IsValidEffectIndex(EITwinClippingPrimitiveType::Plane, PlaneIndex)))
	{
		return false;
	}
	auto const& PlaneInfo = GetPlaneEffect(PlaneIndex);
	FVector PlaneOrientation = PlaneInfo.GetPlaneEquation().PlaneOrientation;
	FVector::FReal PlaneW = PlaneInfo.GetPlaneEquation().PlaneW;

	CesiumGeometry::Plane const Plane(
		glm::dvec3(PlaneOrientation.X, PlaneOrientation.Y, PlaneOrientation.Z),
		-PlaneW
	);

	FBox const PlaneBox = PlanePopulation->GetInstanceBoundingBox(PlaneIndex);

	// Get player controller and viewport
	UWorld const* World = GetWorld();
	if (!World)
		return false;
	APlayerController const* PC = World->GetFirstPlayerController();
	if (!PC)
		return false;

	int32 SizeX = 0, SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
		return false;

	// Helper lambda: test if a world point is visible in the camera frustum
	auto IsPointVisible = [&](const FVector& WorldPoint, FIntPoint& Sign2D, FVector2d& ScreenRatio) -> bool
	{
		FVector2D ScreenPos;
		bool bProjected = PC->ProjectWorldLocationToScreen(WorldPoint, ScreenPos, false);
		if (bProjected)
		{
			ScreenRatio.X = ScreenPos.X / SizeX;
			ScreenRatio.Y = ScreenPos.Y / SizeY;
			Sign2D.X = (ScreenPos.X < 0 ? -1 : (ScreenPos.X >= SizeX ? 1 : 0));
			Sign2D.Y = (ScreenPos.Y < 0 ? -1 : (ScreenPos.Y >= SizeY ? 1 : 0));
		}
		else
		{
			Sign2D = FIntPoint(-1, -1);
		}
		return bProjected &&
			ScreenPos.X >= 0 && ScreenPos.X < SizeX &&
			ScreenPos.Y >= 0 && ScreenPos.Y < SizeY;
	};

	// If we had already applied an offset to the plane proxy to make it visible, let's first test if the
	// current gizmo position is still visible, to avoid moving it too frequently, which is disturbing.
	FIntPoint CurrentSign2D(0, 0);
	FVector2d CurrentScreenRatio(0, 0);
	bool bStillValidPosition = CurrentGizmoPositionOpt
		&& IsPointVisible(*CurrentGizmoPositionOpt, CurrentSign2D, CurrentScreenRatio);
	if (bStillValidPosition
		&& CurrentScreenRatio.GetMin() >= 0.2
		&& CurrentScreenRatio.GetMax() <= 0.8)
	{
		bOutIsCenter = false;
		OutPosition = *CurrentGizmoPositionOpt;
		return true;
	}

	auto SelectFinalPosition = [&](const FVector& InProjectedPos, FVector2d const& InScreenRatio) -> FVector
	{
		if (bStillValidPosition)
		{
			// If the current gizmo position is still valid, we will only change it if the new projected
			// position is significantly more centered on the screen than the current position, to avoid
			// too much movements of the gizmo.
			const double CurrentDistanceToCenter = FVector2D(CurrentScreenRatio - FVector2d(0.5, 0.5)).Size();
			const double NewDistanceToCenter = FVector2D(InScreenRatio - FVector2d(0.5, 0.5)).Size();
			if (NewDistanceToCenter < CurrentDistanceToCenter * 0.7)
			{
				return InProjectedPos;
			}
			else
			{
				bOutIsCenter = false;
				return *CurrentGizmoPositionOpt;
			}
		}
		else
		{
			// If the current gizmo position is not valid, we can directly use the new projected position.
			return InProjectedPos;
		}
	};

	TArray<std::pair<FBox, uint32>> BoxesToTest;
	BoxesToTest.Reserve(64);
	BoxesToTest.Add(std::make_pair(PlaneBox, 0));
	bOutIsCenter = true;
	while (BoxesToTest.Num() > 0)
	{
		auto const BoxAndLevel = BoxesToTest.Pop();
		FBox const& Box = BoxAndLevel.first;
		FVector const BoxCenter = Box.GetCenter();
		glm::dvec3 const Projected = Plane.projectPointOntoPlane(glm::dvec3(BoxCenter.X, BoxCenter.Y, BoxCenter.Z));
		FVector const ProjectedCenter(Projected.x, Projected.y, Projected.z);
		FIntPoint SignCenter(0, 0);
		FVector2d ScreenRatio(0, 0);
		if (IsPointVisible(ProjectedCenter, SignCenter, ScreenRatio))
		{
			// The plane center corresponds to the very first tested point, with level 0.
			bOutIsCenter = (BoxAndLevel.second == 0);
			OutPosition = SelectFinalPosition(ProjectedCenter, ScreenRatio);
			return true;
		}
		else
		{
			// Before subdividing the bounding box, try to project the center of the screen on the plane.
			// This will usually work when we are close to the plane.
			if (BoxAndLevel.second == 0)
			{
				auto ProjectedScreenCenter = ProjectPredefinedScreenPositionOnPlane(PlaneIndex, false);
				if (ProjectedScreenCenter
					&& PlaneBox.IsInsideOrOn(*ProjectedScreenCenter))
				{
					bOutIsCenter = false;
					OutPosition = *ProjectedScreenCenter;
					return true;
				}
			}

			if (BoxAndLevel.second >= 4)
			{
				// We already split the box 4 times (so we are testing boxes that are at most 1/16th of the
				// original box), we can consider that there is no visible point in this box to avoid too much
				// iterations.
				continue;
			}

			FVector BoxVertices[8];
			FIntPoint BoxVerticesSign[8];
			bool bHasVisibleCorner = false;
			Box.GetVertices(BoxVertices);
			for (int32 i(0); i < 8; ++i)
			{
				bool bVisible = IsPointVisible(BoxVertices[i], BoxVerticesSign[i], ScreenRatio);
				if (bVisible)
				{
					bHasVisibleCorner = true;
					break;
				}
			}
			if (!bHasVisibleCorner)
			{
				// If all corners are outside of the screen in the same direction, we can consider that there
				// is no visible point in this box, to avoid too much iterations when the plane is almost
				// edge-on.
				bool bAllSameSign = true;
				for (int i(1); i < 8; ++i)
				{
					if (BoxVerticesSign[i] != BoxVerticesSign[0])
					{
						bAllSameSign = false;
						break;
					}
				}
				if (bAllSameSign)
					continue;
			}
			// Subdivide the box in 8 smaller boxes and test them.
			FVector const& Min = Box.Min;
			FVector const& Max = Box.Max;
			FVector const& Mid = BoxCenter;
			uint32 const NextLevel = BoxAndLevel.second + 1;
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Min.X, Min.Y, Min.Z), FVector(Mid.X, Mid.Y, Mid.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Mid.X, Min.Y, Min.Z), FVector(Max.X, Mid.Y, Mid.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Mid.X, Mid.Y, Min.Z), FVector(Max.X, Max.Y, Mid.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Min.X, Mid.Y, Min.Z), FVector(Mid.X, Max.Y, Mid.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Min.X, Min.Y, Mid.Z), FVector(Mid.X, Mid.Y, Max.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Mid.X, Min.Y, Mid.Z), FVector(Max.X, Mid.Y, Max.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Mid.X, Mid.Y, Mid.Z), FVector(Max.X, Max.Y, Max.Z)), NextLevel));
			BoxesToTest.Add(std::make_pair(
				FBox(FVector(Min.X, Mid.Y, Mid.Z), FVector(Mid.X, Max.Y, Max.Z)), NextLevel));
		}
	}
	return false;
}

void UITwinClippingToolImpl::InvalidateBoundingBoxOfClippingPlanes(ITwin::ModelLink const& ModelLink)
{
	const int32 NumPlanes = NumEffects(EITwinClippingPrimitiveType::Plane);
	for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
	{
		FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
		if (PlaneInfo.ShouldInfluenceModel(ModelLink))
		{
			PlaneInfo.InvalidateInfluenceBoundingBox();
		}
	}
}

void UITwinClippingToolImpl::Tick(float DeltaTime)
{
	using namespace ITwinClippingDetails;

	const int32 NumPlanes = NumEffects(EITwinClippingPrimitiveType::Plane);
	if (NumPlanes <= 0)
	{
		return;
	}
	if (!PlanePopulation.IsValid())
	{
		return;
	}

	auto const ViewInfoOpt = GetCameraViewInfo(GetWorld());

	if (PlanePopulation->IsBeingInteractivelyTransformed())
	{
		// Don't update the plane proxy position/scale while the user is transforming it, to avoid conflicts
		// between the automatic update and the user transformation.
		// Also, when the transformation is done, we should not recenter the proxy at once, or this will
		// cause a jump of the gizmo, which can be really disturbing.
		if (!LastSelectedPlaneIndex.has_value())
		{
			LastSelectedPlaneIndex = PlanePopulation->GetSelectedInstanceIndex();
			LastViewInfo = ViewInfoOpt;
		}
		if (IsRotationMode()
			&& ensure(LastSelectedPlaneIndex.value() != INDEX_NONE)
			&& !PlaneTransformProxy)
		{
			// Now that we adapt the scale to the rotation of the plane, let's do it during manual edition
			// as well, to avoid unexpected jumps when the user stops manipulating the gizmo *and* moves the
			// camera.
			UpdateClippingPlaneTransformFromBoundingBox(*LastSelectedPlaneIndex);
		}
		return;
	}
	else if (LastSelectedPlaneIndex.has_value())
	{
		// If nothing has changed since last update, avoid recomputing the gizmo position: it could cycle
		// between 2 positions when the user is looking in a specific direction (parallel to the plane),
		// which can be really disturbing.
		// This is due to the different snapping behavior we use to find the new position of the proxies.
		if (LastSelectedPlaneIndex.value() == PlanePopulation->GetSelectedInstanceIndex()
			&& ViewInfoOpt.has_value() == LastViewInfo.has_value()
			&& (!ViewInfoOpt.has_value() || ViewInfoOpt->Equals(*LastViewInfo))
			&& !ClippingPlaneInfos.ContainsByPredicate([](FITwinClippingPlaneInfo const& Info)
		{
			return Info.NeedsUpdateInfluenceBoundingBox();
		}))
		{
			return;
		}
	}

	for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
	{
		// Do like in iTwin Design Review: size depending on the influence bounding box, and gizmo positioned
		// at a visible place, if possible.
		UpdateClippingPlaneTransformFromBoundingBox(PlaneIndex);
	}

	LastSelectedPlaneIndex = PlanePopulation->GetSelectedInstanceIndex();
	LastViewInfo = ViewInfoOpt;
}

void UITwinClippingToolImpl::ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	FBox FocusBBox;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		//case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(Type);
		if (Population.IsValid())
		{
			FocusBBox = Population->GetInstanceBoundingBox(PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Plane:
	{
		// The position of the (infinite) planes is not relevant (and changes with the camera view).
		// So instead of working on the instance, we will use the influenced bounding box.
		FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PrimitiveIndex];
		FocusBBox = PlaneInfo.GetUpToDateInfluenceBoundingBox(GetWorld());
		if (!FocusBBox.IsValid)
		{
			// If influence has no bounds (case of Google tilesets), we will just zoom on the plane proxy
			// itself, even if it is not really relevant.
			auto const& Population = GetPopulation(Type);
			if (Population.IsValid())
			{
				FocusBBox = Population->GetInstanceBoundingBox(PrimitiveIndex);
			}
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.GetSpline().IsValid())
		{
			// AzDev#1967143 => for a polygon, use overview camera for zoom
			//PolygonInfo.GetSpline()->IncludeInWorldBox(FocusBBox);
			OnOverviewCamera(PolygonInfo.GetSpline().Get());
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
	if (FocusBBox.IsValid)
	{
		UITwinUtilityLibrary::ZoomOn(FocusBBox, GetWorld() /*, MinDistanceToCenter*/);
	}
}


//---------------------------------------------------------------------------------------
// Visibility
//---------------------------------------------------------------------------------------


void UITwinClippingToolImpl::SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame,
	bool bIsolationMode /*= false*/)
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(EffectType);
		if (Population.IsValid())
		{
			Population->SetHiddenInGame(!bVisibleInGame);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetUsage() == EITwinSplineUsage::MapCutout)
			{
				// For isolation mode, we also test the selection status.
				const bool bShowSpline = bVisibleInGame
					&& (!bIsolationMode || (*SplineIter)->IsSelected());
				(*SplineIter)->SetActorHiddenInGame(!bShowSpline);
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}

	if (EffectType == EITwinClippingPrimitiveType::Box
		|| EffectType == EITwinClippingPrimitiveType::Plane)
	{
		// Update the edge visibility
		auto const& Population = GetPopulation(EffectType);
		const int32 SelectedIndex = Population.IsValid() ? Population->GetSelectedInstanceIndex()
			: INDEX_NONE;
		const int32 NumEff = NumEffects(EffectType);
		for (int32 Index = 0; Index < NumEff; Index++)
		{
			const bool bShowEdges = bVisibleInGame
				&& (!bIsolationMode || Index == SelectedIndex);
			GetMutableEffect(EffectType, Index).SetEdgeVisibility(bShowEdges);
		}
	}

	if (EffectType == EITwinClippingPrimitiveType::Plane)
	{
		// Enable Tick for plane proxies, as we need to update their scale and position each frame to keep
		// them visible for the user.
		EventHub->SetActorTickEnabled(bVisibleInGame);
	}
}

bool UITwinClippingToolImpl::IsEffectProxyVisible(EITwinClippingPrimitiveType EffectType) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetPopulation(EffectType);
		return Population.IsValid() && !Population->IsHiddenInGame();
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetUsage() == EITwinSplineUsage::MapCutout
				&& !(*SplineIter)->IsHidden())
			{
				return true;
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}

	return false;
}

void UITwinClippingToolImpl::SetAllEffectProxiesVisibility(bool bVisibleInGame)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		SetEffectVisibility(Type, bVisibleInGame);
	}
}

void UITwinClippingToolImpl::ShowOnlyProxiesOfType(EITwinClippingPrimitiveType SelectedType, bool bIsolationMode)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		SetEffectVisibility(Type, Type == SelectedType, bIsolationMode);
	}
}


//---------------------------------------------------------------------------------------
// Picking & Interaction
//---------------------------------------------------------------------------------------

bool UITwinClippingToolImpl::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	bOutSelectionGizmoNeeded = false;
	UWorld* World = GetWorld();
	if (!World)
		return false;

	std::optional<FSelectionChangeDetector> SelectionChangeDetector;
	SelectionChangeDetector.emplace(*this, true/*bManageIsolationMode*/);

	auto const OldSelection = SelectionChangeDetector->PreviousSelection;

	// Test population then cut-out splines.

	// Note that we can only have one active tool at a time, but we don't want the cutout splines to be
	// hidden just because we temporarily disable the spline tool...
	AITwinSplineTool::FAutomaticVisibilityDisabler AutoVisDisabler;

	if (NumEffects(EITwinClippingPrimitiveType::Box) > 0
		|| NumEffects(EITwinClippingPrimitiveType::Plane) > 0)
	{
		auto const ActiveTool = ActivatePopulationTool();
		if (ActiveTool.IsValid())
		{
			AITwinPopulationTool::FPickingContext RestrictOnClipping(*ActiveTool, true);
			bRelevantAction = ActiveTool->DoMouseClickAction();
			if (bRelevantAction)
			{
				bOutSelectionGizmoNeeded = ActiveTool->HasSelectedPopulation();

				AdjustTransformationModeForPopulation(ActiveTool->GetSelectedPopulation());
			}
		}
	}
	if (!bRelevantAction && NumEffects(EITwinClippingPrimitiveType::Polygon) > 0)
	{
		auto const ActiveTool = ActivateSplineTool();
		if (ActiveTool.IsValid())
		{
			// Quick fix for point selection/insertion: we need to restore the initial selection, if a
			// polygon was selected, as point operations are only allowed on the selected spline (and
			// the selection is lost when the spline tool is disabled through ActivatePopulationTool...)
			if (OldSelection
				&& OldSelection->first == EITwinClippingPrimitiveType::Polygon
				&& IsValidEffectIndex(EITwinClippingPrimitiveType::Polygon, OldSelection->second))
			{
				ActiveTool->SetSelectedSpline(GetCutoutSpline(OldSelection->second));
			}
			bRelevantAction = ActiveTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = ActiveTool->HasSelection();
		}
	}

	// At this point, the selection may have changed. Update the visibility of the proxies accordingly, and
	// call OnPrimitiveSelectionChanged if needed. This is done in the destructor of SelectionChangeDetector.
	SelectionChangeDetector.reset();

	// Notify new selection. If nothing is selected, notify it as well (using -1 as index).
	EventHub->BroadcastSelection();

	return bRelevantAction;
}


void UITwinClippingToolImpl::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline /*= nullptr*/)
{
	TWeakObjectPtr<AITwinSplineTool> ActiveSplineTool = ActivateSplineTool();
	if (ActiveSplineTool.IsValid())
	{
		ActiveSplineTool->OnOverviewCamera(SpecificSpline);
	}
}


//---------------------------------------------------------------------------------------
// Enabled states & Influences
//---------------------------------------------------------------------------------------

bool UITwinClippingToolImpl::IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	if (ensure(Index < NumEffects(EffectType)))
	{
		return GetEffect(EffectType, Index).IsEnabled();
	}
	return false;
}

void UITwinClippingToolImpl::EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled)
{
	if (ensure(Index < NumEffects(EffectType)))
	{
		GetMutableEffect(EffectType, Index).SetEnabled(bInEnabled);
		Renderer->UpdateAllTilesets(EffectType);

		if (Persistence)
		{
			// The enabled state is stored in the base info, so we need to update it in the DB.
			Persistence->UpdateBaseInfo(EffectType, Index);
		}
	}
}

void UITwinClippingToolImpl::EnableAllEffectsOfType(EITwinClippingPrimitiveType Type, bool bInEnabled)
{
	const int32 NumEff = NumEffects(Type);
	for (int32 i(0); i < NumEff; ++i)
	{
		GetMutableEffect(Type, i).SetEnabled(bInEnabled);
	}
	Renderer->UpdateAllTilesets(Type);

	if (Persistence)
	{
		for (int32 i(0); i < NumEff; ++i)
		{
			Persistence->UpdateBaseInfo(Type, i);
		}
	}
}


bool UITwinClippingToolImpl::IsUsingPerLayerTypeInfluence() const
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		const int32 NumEff = NumEffects(Type);
		for (int32 i(0); i < NumEff; ++i)
		{
			if (GetEffect(Type, i).IsUsingPerLayerTypeInfluence())
			{
				return true;
			}
		}
	}
	return false;
}

void UITwinClippingToolImpl::ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		const int32 NumEff = NumEffects(Type);
		for (int32 i(0); i < NumEff; ++i)
		{
			GetMutableEffect(Type, i).ConvertToPerLayerInfluence(InCurrentLayers);
		}
	}
}


bool UITwinClippingToolImpl::ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetEffect(EffectType, EffectIndex).ShouldInfluenceFullModelType(ModelType);
	}
	return false;
}


void UITwinClippingToolImpl::SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType, bool bAll)
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		GetMutableEffect(EffectType, EffectIndex).SetInfluenceFullModelType(ModelType, bAll);
		Renderer->UpdateAllTilesets(EffectType);
	}
}


void UITwinClippingToolImpl::SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier, bool bInfluence)
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		GetMutableEffect(EffectType, EffectIndex).SetInfluenceSpecificModel(ModelIdentifier, bInfluence);
		Renderer->UpdateAllTilesets(EffectType);

		if (Persistence)
		{
			Persistence->UpdateBaseInfo(EffectType, EffectIndex);
		}
	}
}


TSet<FString> UITwinClippingToolImpl::GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
	int32 EffectIndex,
	EITwinModelType LayerType) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetEffect(EffectType, EffectIndex).GetInfluenceInfo(LayerType).SpecificIDs;
	}
	return {};
}


bool UITwinClippingToolImpl::DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetEffect(EffectType, EffectIndex).DoesInfluenceModel(ModelIdentifier);
	}
	return {};
}

