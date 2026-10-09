/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinClippingTool.h>
#include <Clipping/ITwinClippingToolImpl.h>

#include <CesiumGlobeAnchorComponent.h>

#include <Clipping/ITwinClippingEffectFactory.h>
#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingInfoBase.inl>
#include <Clipping/ITwinClippingPersistence.h>
#include <Clipping/ITwinClippingRenderer.h>

#include <Decoration/ITwinDecorationHelper.h>
#include <Helpers/ITwinConsoleCommandUtils.inl>
#include <Helpers/ITwinTracingHelper.h>
#include <Helpers/WorldSingleton.h>
#include <ITwinGeolocation.h>
#include <IncludeCesium3DTileset.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinFeatureChange.h>
#include <Math/UEMathConversion.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

// UE headers=
#include <DrawDebugHelpers.h>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

// Between iTwin Engage LA and GA, we made an attempt to let the user pick the rotation center for cutout
// planes, but we abandoned the idea (lack of feedback for the user, and possibility to get the same with
// the recenter icon, in a much clearer way...)
// Blame here to recover the removed code, if you want to give it another try...)
//#define HAS_ROTATION_CENTER_PICKING_MODE() 0

// Tentative we made to automatically recenter and resize the cutting planes.
// Abandoned as it was not intuitive for the user (blame here if you want to try again...)
//#define AUTOMATIC_CUTTING_PLANE_RECENTERING() 0


// ======================================================================================
//	                             AITwinClippingTool
// ======================================================================================

AITwinClippingTool::AITwinClippingTool()
{
	Impl = CreateDefaultSubobject<UITwinClippingToolImpl>(TEXT("Impl"));
	Impl->SetEventHub(this);

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AITwinClippingTool::ConnectPopulationTool(AITwinPopulationTool* PopulationTool)
{
	Impl->SetPopulationTool(PopulationTool);
	if (PopulationTool)
	{
		PopulationTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnItemCreationAbortedInTool);

		PopulationTool->InteractiveCreationCompletedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnItemCreatedInTool);
	}
}

void AITwinClippingTool::OnSceneLoaded(bool bSuccess)
{
	Impl->OnSceneLoaded(bSuccess);
}

void AITwinClippingTool::ConnectSplineTool(AITwinSplineTool* SplineTool)
{
	Impl->SetSplineTool(SplineTool);
	if (SplineTool)
	{
		SplineTool->SplineAddedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplineHelperAdded);
		SplineTool->SplineBeforeRemovedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplineHelperRemoved);
		SplineTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnItemCreationAbortedInTool);
		SplineTool->SplineSelectedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnCutoutPolygonSelected);
		SplineTool->SplinePointMovedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplinePointMoved);
		SplineTool->SplinePointRemovedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplinePointRemoved);
		SplineTool->SplinePointAddedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplinePointAdded);
		SplineTool->SplinePointMovingStartedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplinePointMoveStart);
		SplineTool->SplineMovingStartedEvent.AddUniqueDynamic(Impl.Get(), &UITwinClippingToolImpl::OnSplineMoveStart);
	}
}

void AITwinClippingTool::ConnectPersistenceManager(AITwinDecorationHelper* DecorationHelper)
{
	if (!DecorationHelper)
	{
		return;
	}
	DecorationHelper->OnSceneLoaded.AddUniqueDynamic(this, &AITwinClippingTool::OnSceneLoaded);

	// New persistence management based on Scene API.
	if (ensure(Impl->Persistence))
	{
		Impl->Persistence->Connect(DecorationHelper);
	}
}

void AITwinClippingTool::RegisterTileset(FITwinTilesetAccess const& TilesetAccess)
{
	// A new model appeared: it needs a clipping group id before its CPD is filled below.
	Impl->Renderer->RegisterTileset(TilesetAccess);

	// If some cutting planes are meant to influence this tileset, invalidate their bounding box (used for
	// automatic recentering).
	Impl->InvalidateBoundingBoxOfClippingPlanes(TilesetAccess.GetModelLink());

	// Also make sure those bounding boxes are invalidated as soon as the tileset is transformed.
	ACesium3DTileset* TilesetPtr = TilesetAccess.GetMutableTileset();
	if (TilesetPtr && TilesetPtr->GetRootComponent())
	{
		TilesetPtr->GetRootComponent()->TransformUpdated.AddLambda(
			[this, ModelLink = TilesetAccess.GetModelLink()](USceneComponent*, EUpdateTransformFlags, ETeleportType)
		{
			// Invalidate bounding box of all clipping plane effects influencing this tileset.
			Impl->InvalidateBoundingBoxOfClippingPlanes(ModelLink);
		});
	}
}

void AITwinClippingTool::OnModelRemoved(const ITwin::ModelLink& ModelIdentifier)
{
	Impl->InvalidateBoundingBoxOfClippingPlanes(ModelIdentifier);
}

bool AITwinClippingTool::StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type)
{
	// Abort current effect creation, if any.
	AbortInteractiveCreation(/*bTriggeredFromITS*/true);

	// Make sure we hide all effect proxies (only the new item will be visible).
	Impl->HideAllEffectProxies();

	if (Impl->Factory->StartInteractiveEffectCreation(Type))
	{
		// Ensure the new instance will be visible.
		Impl->ShowOnlyProxiesOfType(Type, false);

		return true;
	}
	return false;
}


void AITwinClippingTool::OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex)
{
	Impl->OnClippingInstanceAdded(Population, ObjectType, InstanceIndex);
}

void AITwinClippingTool::OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex, bool bTriggeredFromITS)
{
	const EITwinClippingPrimitiveType ModifiedType =
		Impl->Factory->OnClippingInstanceModified(ObjectType, InstanceIndex);

	if (ModifiedType != EITwinClippingPrimitiveType::Count)
	{
		EffectModifiedEvent.Broadcast(ModifiedType, InstanceIndex, bTriggeredFromITS);
	}
}

void AITwinClippingTool::BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices)
{
	Impl->Factory->BeforeRemoveClippingInstances(ObjectType, InstanceIndices);
}

void AITwinClippingTool::OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType,
	const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap)
{
	Impl->OnClippingInstancesRemoved(ObjectType, IndicesInDescendingOrder, bUseRemoveAtSwap);
}

bool AITwinClippingTool::AllowLoadingLegacyInstances() const
{
	// We may create cutout effects from instances loaded from the Decoration Service (DS), except if some
	// effects were loaded from the Scene API (SC).
	// Note that we do *not* remove the instances from the DS after converting them to SC, for 3
	// reasons:
	// - avoid having the handle error cases between the 2 save paths
	// - allow users from the LA-7 and below to load scene re-saved in LA-8 with the original cutouts
	// - make it easier to debug cutout conversion.
	bool bLoadDSCutouts = true;
	if (Impl->Persistence)
	{
		bLoadDSCutouts = !Impl->Persistence->DoesLoadedSceneContainCutouts();
	}
	return bLoadDSCutouts;
}

void AITwinClippingTool::OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos)
{
	BE_ASSERT(AllowLoadingLegacyInstances() || !bUpdateEffectInfos);
	Impl->OnClippingInstancesLoaded(Population, bUpdateEffectInfos);
}

void AITwinClippingTool::OnLoadComplete()
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		Impl->RegisterLoadedEffectsInScene(Type);
	}
}

void AITwinClippingTool::OnItemCreationAbortedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS)
{
	if (Tool && Tool->IsUsedOnCutoutPrimitive())
	{
		InteractiveCreationAbortedEvent.Broadcast(bTriggeredFromITS);
	}
}

void AITwinClippingTool::OnItemCreatedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS)
{
	if (Tool && Tool->IsUsedOnCutoutPrimitive())
	{
		Impl->OnItemCreatedInTool(*Tool, bTriggeredFromITS);
	}
}


int32 AITwinClippingTool::NumEffects(EITwinClippingPrimitiveType Type) const
{
	return Impl->NumEffects(Type);
}

FITwinClippingInfoBase& AITwinClippingTool::GetMutableEffect(EITwinClippingPrimitiveType Type, int32 Index)
{
	return Impl->GetMutableEffect(Type, Index);
}

const FITwinClippingInfoBase& AITwinClippingTool::GetEffect(EITwinClippingPrimitiveType Type, int32 Index) const
{
	return Impl->GetEffect(Type, Index);
}


namespace
{
	struct [[nodiscard]] FPopulationToolAutoRestore
	{
		FPopulationToolAutoRestore(UITwinClippingToolImpl* InCutoutToolImpl,
			bool bAutoRestore = true)
			: PopulationTool(InCutoutToolImpl->GetPopulationTool())
			, CutoutToolImpl(InCutoutToolImpl)
		{
			if (bAutoRestore
				&& PopulationTool.IsValid()
				&& PopulationTool->IsEnabled()
				&& !PopulationTool->IsUsedOnCutoutPrimitive())
			{
				PopToolStateRecord = PopulationTool->MakeStateRecord();
				if (PopulationTool->HasSelection())
					PopToolSelectionRecord = PopulationTool->MakeSelectionRecord();
			}
		}

		~FPopulationToolAutoRestore()
		{
			if (PopToolStateRecord && PopulationTool.IsValid())
			{
				// Restore the Population Tool to its previous state (3D Objects).
				PopulationTool->RestoreState(*PopToolStateRecord);

				if (PopToolSelectionRecord
					&& PopulationTool->RestoreSelection(*PopToolSelectionRecord))
				{
					PopulationTool->SelectionChangedEvent.Broadcast();
				}

				// Make sure we hide all cutout proxies, since we are *not* in cutout selection mode!
				if (CutoutToolImpl.IsValid())
					CutoutToolImpl->HideAllEffectProxies();
			}
		}

	private:
		TWeakObjectPtr<AITwinPopulationTool> const& PopulationTool;
		TWeakObjectPtr<UITwinClippingToolImpl> const CutoutToolImpl;
		TUniquePtr<AITwinInteractiveTool::IActiveStateRecord> PopToolStateRecord;
		TUniquePtr<AITwinInteractiveTool::ISelectionRecord> PopToolSelectionRecord;
	};

}

bool AITwinClippingTool::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	// Removing an effect from the list panel should not deactivate the 3D Objects tool.
	// See AzDev#2030933 for more details.
	FPopulationToolAutoRestore AutoRestore(Impl.Get(), /*bAutoRestore=*/bTriggeredFromITS);

	const bool bRemoved = Impl->RemoveEffect(Type, PrimitiveIndex, bTriggeredFromITS);
	if (bRemoved)
	{
		RemoveEffectCompletedEvent.Broadcast();
	}
	return bRemoved;
}

bool AITwinClippingTool::GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const
{
	return Impl->GetInvertEffect(Type, PrimitiveIndex);
}

void AITwinClippingTool::SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert)
{
	Impl->SetInvertEffect(Type, PrimitiveIndex, bInvert);
}

void AITwinClippingTool::FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	Impl->FlipEffect(Type, PrimitiveIndex);
}

void AITwinClippingTool::FlipAllEffectsOfType(EITwinClippingPrimitiveType Type)
{
	Impl->FlipAllEffectsOfType(Type);
}

bool AITwinClippingTool::SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	bool bEnterIsolationMode /*= true*/)
{
	return Impl->SelectEffect(Type, PrimitiveIndex, bEnterIsolationMode);
}

std::optional<AITwinClippingTool::FEffectIdentifier> AITwinClippingTool::GetSelectedEffect() const
{
	return Impl->GetSelectedEffect();
}

void AITwinClippingTool::DeSelectAll(bool bExitIsolationMode /*= true*/)
{
	auto CurrentSelection = GetSelectedEffect();
	if (CurrentSelection)
	{
		BE_ASSERT(Impl->IsEffectProxyVisible(CurrentSelection->first), "selected but invisible?");
		if (CurrentSelection->first == EITwinClippingPrimitiveType::Box
			|| CurrentSelection->first == EITwinClippingPrimitiveType::Plane)
		{
			Impl->SelectPopulationInstance(nullptr, INDEX_NONE, CurrentSelection->first);
		}
		else
		{
			Impl->SelectSpline(nullptr, GetWorld());
		}
		if (bExitIsolationMode)
		{
			// Restore visibility of proxies.
			Impl->SetAllEffectProxiesVisibility(true);
		}
	}
	BroadcastSelection();
}

int32 AITwinClippingTool::GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const
{
	UWorld* World = GetWorld();
	if (!World)
		return INDEX_NONE;

	auto const CurrentSelection = GetSelectedEffect();
	if (CurrentSelection && CurrentSelection->first == EITwinClippingPrimitiveType::Polygon)
	{
		auto const& SplineTool = Impl->GetSplineTool();
		if (ensure(SplineTool.IsValid()) && SplineTool->HasSelectedPoint())
		{
			AITwinSplineHelper const* SelectedSpline = SplineTool->GetSelectedSpline();

			// Always prefer using the geo-located geo-reference
			auto&& Geoloc = FITwinGeolocation::Get(*World);

			ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
				? Geoloc->GeoReference.Get() : SelectedSpline->GlobeAnchor->ResolveGeoreference();
			if (ensure(GeoRef))
			{
				const FTransform Transform = SplineTool->GetSelectionTransform();
				const int32 PointIndex = SplineTool->GetSelectedPointIndex();
				const FVector Cartographic =
					GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(Transform.GetLocation());
				OutLatitude = Cartographic.Y;
				OutLongitude = Cartographic.X;
				return PointIndex;
			}
		}
	}
	return INDEX_NONE;
}

void AITwinClippingTool::SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude)
{
	Impl->SetPolygonPointLocation(PolygonIndex, PointIndex, Latitude, Longitude);
}

bool AITwinClippingTool::GetEffectTransform(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const
{
	return Impl->GetEffectTransform(Type, PrimitiveIndex, OutTransform, OutLatitude, OutLongitude, OutElevation);
}

void AITwinClippingTool::SetEffectLocation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InLatitude, double InLongitude, double InElevation,
	bool bTriggeredFromITS)
{
	Impl->SetEffectLocation(Type, PrimitiveIndex, InLatitude, InLongitude, InElevation, bTriggeredFromITS);
}

void AITwinClippingTool::SetEffectRotation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InRotX, double InRotY, double InRotZ,
	bool bTriggeredFromITS)
{
	Impl->SetEffectRotation(Type, PrimitiveIndex, InRotX, InRotY, InRotZ, bTriggeredFromITS);
}

void AITwinClippingTool::ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	Impl->ZoomOnEffect(Type, PrimitiveIndex);
}


void AITwinClippingTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->Tick(DeltaTime);
}

void AITwinClippingTool::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects a cutout from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedEffect();
		if (CurrentSelection)
			Impl->ShowOnlyProxiesOfType(CurrentSelection->first, /*bIsolationMode*/true);
		else
			Impl->SetAllEffectProxiesVisibility(true);
	}
	else
	{
		Impl->HideAllEffectProxies();
	}
}

bool AITwinClippingTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	return Impl->DoMouseClickPicking(bOutSelectionGizmoNeeded);
}

void AITwinClippingTool::BroadcastSelection()
{
	// Notify new selection. If nothing is selected, notify it as well (using -1 as index).
	auto const NewSelection = GetSelectedEffect();
	if (NewSelection)
	{
		EffectSelectedEvent.Broadcast(NewSelection->first, NewSelection->second);
	}
	else
	{
		EffectSelectedEvent.Broadcast(
			static_cast<EITwinClippingPrimitiveType>(0), -1);
	}
}

void AITwinClippingTool::OnCutoutPolygonSelected()
{
	BroadcastSelection();
}

void AITwinClippingTool::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline /*= nullptr*/)
{
	Impl->OnOverviewCamera(SpecificSpline);
}

void AITwinClippingTool::SetOverviewCamera()
{
	OnOverviewCamera(nullptr);
}

void AITwinClippingTool::SetTransformationMode(ETransformationMode Mode)
{
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedOnCutoutPrimitive())
	{
		if (ActiveTool->IsPopulationTool())
		{
			Impl->SetTransformationMode(Mode);

			// Trigger event to refresh the selection gizmo, typically.
			ActivationEvent.Broadcast(true);
		}
	}
}


const UITwinClippingRenderer* AITwinClippingTool::GetRenderer() const
{
	return Impl->Renderer.Get();
}

#if WITH_EDITOR

void AITwinClippingTool::ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate)
{
	Impl->Renderer->ActivateEffects(Type, Level, bActivate);
}
void AITwinClippingTool::ActivateEffectsAllLevels(EITwinClippingPrimitiveType Type, bool bActivate)
{
	ActivateEffects(Type, EITwinClippingEffectLevel::Tileset,	bActivate);
	ActivateEffects(Type, EITwinClippingEffectLevel::Shader,	bActivate);
}

#endif // WITH_EDITOR


bool AITwinClippingTool::IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return Impl->IsEffectEnabled(EffectType, Index);
}

void AITwinClippingTool::EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled)
{
	Impl->EnableEffect(EffectType, Index, bInEnabled);
}

void AITwinClippingTool::EnableAllEffects(bool bInEnabled)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		Impl->EnableAllEffectsOfType(Type, bInEnabled);
	}
}

bool AITwinClippingTool::ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	return Impl->ShouldEffectInfluenceModel(EffectType, EffectIndex, ModelIdentifier);
}

bool AITwinClippingTool::IsUsingPerLayerTypeInfluence() const
{
	return Impl->IsUsingPerLayerTypeInfluence();
}

void AITwinClippingTool::ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers)
{
	Impl->ConvertToPerLayerInfluence(InCurrentLayers);
}

bool AITwinClippingTool::ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType) const
{
	return Impl->ShouldEffectInfluenceFullModelType(EffectType, EffectIndex, ModelType);
}

void AITwinClippingTool::SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType, bool bAll)
{
	Impl->SetEffectInfluenceFullModelType(EffectType, EffectIndex, ModelType, bAll);
}

void AITwinClippingTool::SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier, bool bInfluence)
{
	Impl->SetEffectInfluenceModel(EffectType, EffectIndex, ModelIdentifier, bInfluence);
}

TSet<FString> AITwinClippingTool::GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
	int32 EffectIndex,
	EITwinModelType LayerType) const
{
	return Impl->GetInfluencedSpecificModels(EffectType, EffectIndex, LayerType);
}

bool AITwinClippingTool::DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	return Impl->DoesEffectInfluenceModel(EffectType, EffectIndex, ModelIdentifier);
}

AdvViz::SDK::RefID AITwinClippingTool::GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const
{
	return Impl->GetEffectId(EffectType, EffectIndex);
}

int32 AITwinClippingTool::GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const
{
	return Impl->GetEffectIndex(EffectType, RefID);
}


void AITwinClippingTool::AbortInteractiveCreation(bool bTriggeredFromITS)
{
	// Disabling the active tool will trigger a selection change that will be handled by the selection
	// change detector below.
	UITwinClippingToolImpl::FSelectionChangeDetector SelectionSynchronizer(*Impl);

	// Abort current effect creation, if any.
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedOnCutoutPrimitive())
	{
		if (ActiveTool->IsInteractiveCreationMode())
			ActiveTool->AbortInteractiveCreation(bTriggeredFromITS);
		ActiveTool->SetEnabled(false);
		ActiveTool->SetUsedOnCutoutPrimitive(false);
	}
}

void AITwinClippingTool::Deactivate()
{
	// Abort current effect creation, if any.
	AbortInteractiveCreation(/*bTriggeredFromITS*/true);

	// Deselect all, without changing the visibility (since we will hide all below...)
	DeSelectAll(/*bExitIsolationMode*/false);

	// Trigger event to refresh the selection gizmo, typically.
	ActivationEvent.Broadcast(false);

	Impl->HideAllEffectProxies();
}

bool AITwinClippingTool::HasEffectListListener() const
{
	return EffectListModifiedEvent.IsBound();
}


#if ENABLE_DRAW_DEBUG

// Console command to flip all clipping effects
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFlipClippingEffects(
	TEXT("cmd.ITwinFlipClippingEffects"),
	TEXT("Flip all clipping effects."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	std::optional<EITwinClippingPrimitiveType> SingleType = std::nullopt;
	if (Args.Num() >= 1)
	{
		SingleType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	}
	auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
	if (ensure(ClippingActor))
	{
		if (SingleType)
		{
			ClippingActor->FlipAllEffectsOfType(*SingleType);
		}
		else
		{
			for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
			{
				ClippingActor->FlipAllEffectsOfType(Type);
			}
		}
	}
}));


// Console command to activate/deactivate all clipping effects
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinActivateClippingEffects(
	TEXT("cmd.ITwinActivateClippingEffects"),
	TEXT("Activate/deactivate all clipping effects."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
#if WITH_EDITOR
	if (Args.Num() != 3)
	{
		UE_LOG(LogITwin, Error, TEXT("Need exactly 3 args: <box|plane> <shader|tileset> <0|1>"));
		return;
	}
	auto EffectType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	auto EffectLevel = ITwin::GetEnumFromCmdArg<EITwinClippingEffectLevel>(Args, 1);
	auto const ActivateOpt = ITwin::ToggleFromCmdArg(Args, 2);

	if (EffectLevel && ActivateOpt)
	{
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
		if (ensure(ClippingActor))
		{
			ClippingActor->ActivateEffects(*EffectType, *EffectLevel, *ActivateOpt);
		}
	}
#else
	UE_LOG(LogITwin, Error, TEXT("ActivateEffects is not available in game"));
#endif
}));


// Console command to activate/deactivate clipping effects to a category of models.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinActivatePerModelClippingEffects(
	TEXT("cmd.ITwinActivatePerModelClippingEffects"),
	TEXT("Activate/deactivate all clipping effects to a given model or model category."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() < 3)
	{
		UE_LOG(LogITwin, Error, TEXT("Expects 3 to 5 args: <box|plane|polygon> <IModel|RealityData|GlobalMapLayer> <0|1> [EffectIndex] [SingleModelId]"));
		return;
	}
	auto EffectType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	auto ModelType = ITwin::GetEnumFromCmdArg<EITwinModelType>(Args, 1);
	auto const ActivateOpt = ITwin::ToggleFromCmdArg(Args, 2);
	int32 EffectIndex = -1;
	FString SingleModelId;
	if (Args.Num() > 3)
	{
		EffectIndex = FCString::Strtoi(*Args[3], nullptr, /*base*/10);
	}
	if (Args.Num() > 4)
	{
		SingleModelId = Args[4];
		SingleModelId.TrimStartAndEndInline();
	}
	if (EffectType && ModelType && ActivateOpt)
	{
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
		if (ensure(ClippingActor))
		{
			auto ChangeClippingInfluenceForEffect = [&](EITwinClippingPrimitiveType Type, int32 Index,
														EITwinModelType InModelType)
			{
				if (SingleModelId.IsEmpty())
				{
					ClippingActor->SetEffectInfluenceFullModelType(Type, Index, InModelType, *ActivateOpt);
				}
				else
				{
					ClippingActor->SetEffectInfluenceFullModelType(Type, Index, InModelType, false);
					ClippingActor->SetEffectInfluenceModel(Type, Index, std::make_pair(InModelType, SingleModelId), *ActivateOpt);
				}
			};

			if (EffectIndex == -1)
			{
				// Apply to all clipping effects
				const int32 NumEffects = ClippingActor->NumEffects(*EffectType);
				for (int32 i(0); i < NumEffects; ++i)
				{
					ChangeClippingInfluenceForEffect(*EffectType, i, *ModelType);
				}
			}
			else
			{
				ChangeClippingInfluenceForEffect(*EffectType, EffectIndex, *ModelType);
			}
		}
	}
}));

#endif // ENABLE_DRAW_DEBUG
