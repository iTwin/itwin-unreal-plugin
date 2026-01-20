/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Helpers/ITwinPickingActor.h"

#include "Helpers/ITwinTracingHelper.h"
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <CesiumMetadataPickingBlueprintLibrary.h>
#include <CesiumMetadataValue.h>
#include <ITwinMetadataConstants.h>

#include <Containers/Ticker.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h>

#include <optional>
#include <unordered_set>

namespace ITwin
{
	std::optional<uint64_t> GetMaterialIDFromHit(FHitResult const& HitResult, AITwinIModel& IModel)
	{
		// When visualizing ML-based material predictions, we ignore the material IDs present in source
		// meta-data, and replace them with custom material IDs depending on the ML inference
		// => just test the material ID that we may have baked in the mesh.
		if (IModel.VisualizeMaterialMLPrediction() && HitResult.Component.IsValid())
		{
			FITwinIModelInternals const& IModelInternals = GetInternals(IModel);
			auto const Found = IModelInternals.SceneMapping.FindOwningTileSLOW(HitResult.Component.Get());
			if (auto* pMeshWrapper = Found.second)
			{
				return pMeshWrapper->GetITwinMaterialIDOpt();
			}
		}
		// General case: test meta-data produced by the Mesh Export Service.
		TMap<FString, FCesiumMetadataValue> const Table1 =
			UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);
		FCesiumMetadataValue const* const MaterialIdFound = Table1.Find(ITwinCesium::Metada::MATERIAL_NAME);
		if (MaterialIdFound != nullptr)
		{
			return FCesiumMetadataValueAccess::GetUnsignedInteger64(
				*MaterialIdFound, ITwin::NOT_ELEMENT.value());
		}
		return std::nullopt;
	}
}

void AITwinPickingActor::PickUnderCursorWithOptions(FPickingResult& OutPickingResult,
	AITwinIModel* PickedIModel, FITwinPickingOptions const& Options)
{
	FString& ElementId(OutPickingResult.ElementId);
	FHitResult& VisibleHit(OutPickingResult.HitResult);
	OutPickingResult.MaterialId.Reset();

	std::optional<uint32> const MaxUniqueElementsHit = 1;// if changing this, see DejaVu.insert(ITwin::NOT_ELEMENT) below
	VisibleHit.Reset();
	ITwinElementID PickedEltID = ITwin::NOT_ELEMENT;
	std::optional<uint64_t> PickedMaterial;
	AITwinIModel* PickedMaterialIModel = nullptr;
	std::optional<float> CustomTraceExtentInMeters;
	std::optional<FVector2D> CustomMousePosition;
	if (Options.CustomTraceExtentInMeters > 0.f)
	{
		CustomTraceExtentInMeters = Options.CustomTraceExtentInMeters;
	}
	if (Options.CustomMousePosition)
	{
		CustomMousePosition.emplace(*Options.CustomMousePosition);
	}

	FITwinTracingHelper TracingHelper;
	if (Options.ActorsToIgnore.Num() > 0)
		TracingHelper.AddIgnoredActors(Options.ActorsToIgnore);
	if (Options.ComponentsToIgnore.Num() > 0)
		TracingHelper.AddIgnoredComponents(Options.ComponentsToIgnore);
	TracingHelper.VisitElementsUnderCursor(GetWorld(),
		OutPickingResult.MousePosition, OutPickingResult.TraceStart, OutPickingResult.TraceEnd,
		[this, PickedIModel, &Options, &PickedEltID, &PickedMaterial, &PickedMaterialIModel, &VisibleHit, &TracingHelper]
		(FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu)
	{
		AITwinIModel* iModel = PickedIModel;
		// If passed, use it as a filter, otherwise, set it.
		// Using GetOwner() because the hit actor is actually the cesium tileset
		AActor* HitTilesetOwner = nullptr;
		if (HitResult.HasValidHitObjectHandle())
			if (AActor* HitTileset = HitResult.GetActor())
				HitTilesetOwner = HitTileset->GetOwner(); // may be null or sth else than an iModel of course
		if (iModel && iModel != HitTilesetOwner)
			return;
		if (!iModel)
			iModel = Cast<AITwinIModel>(HitTilesetOwner);

		if (iModel)
		{
			ITwinElementID EltID = ITwin::NOT_ELEMENT;
			if (TracingHelper.PickVisibleElement(HitResult, *iModel, EltID, Options.bSelectElement))
			{
				DejaVu.insert(EltID);
				PickedEltID = EltID;
				if (!VisibleHit.HasValidHitObjectHandle())
					VisibleHit = HitResult;
				ElementPickedEvent.Broadcast({});//why not pass PickedEltID?! TODO_GCO: 3DFT oldy, remove

				if (Options.bSelectMaterial)
				{
					PickedMaterial = ITwin::GetMaterialIDFromHit(HitResult, *iModel);
					PickedMaterialIModel = iModel;
				}
			}
		}
		else if (!VisibleHit.HasValidHitObjectHandle())
		{
			VisibleHit = HitResult;
			// This is to avoid looping for more hits uselessly - ok as long as MaxUniqueElementsHit is 1...
			DejaVu.insert(ITwin::NOT_ELEMENT);
		}
		if (Options.bSelectMaterial && !PickedMaterialIModel && iModel)
		{
			// Some primitive parts may not be assigned any ElementID but still have a valid ITwin material.
			auto MatOpt = ITwin::GetMaterialIDFromHit(HitResult, *iModel);
			if (MatOpt && !PickedMaterial)
			{
				PickedMaterial = MatOpt;
				PickedMaterialIModel = iModel;
			}
		}

	}, MaxUniqueElementsHit, CustomTraceExtentInMeters, CustomMousePosition);
	
	static FString LastPickedIModelId = FString("");
	if (!PickedIModel && VisibleHit.HasValidHitObjectHandle())
		PickedIModel = Cast<AITwinIModel>(VisibleHit.GetActor()->GetOwner());
	if (Options.bSelectElement)
	{
		// remove highlights from all iModels except the one (possibly) selected
		for (TActorIterator<AITwinIModel> Iter(GetWorld()); Iter; ++Iter)
		{
			if ((*Iter) != PickedIModel)
				DeSelect(*Iter);
		}
		if (PickedEltID != ITwin::NOT_ELEMENT)
		{
			// convert picked element ID to string.
			ElementId = ITwin::ToString(PickedEltID);
			OnElemPicked.Broadcast(ElementId, PickedIModel->IModelId);
			LastPickedIModelId = PickedIModel->IModelId;
		}
		else
		{
			if (PickedIModel)
				DeSelect(PickedIModel);
			OnElemPicked.Broadcast("", LastPickedIModelId);
		}
	}

	OutPickingResult.PickedMaterialIModel = nullptr;
	if (PickedMaterial && ensure(PickedMaterialIModel))
	{

#if ENABLE_DRAW_DEBUG
		ElementId += FString::Printf(TEXT(" [MatID: %llu (%s)]"),
			*PickedMaterial, *PickedMaterialIModel->GetMaterialName(*PickedMaterial));
#endif

		if (Options.bHighlightSelectedMaterial)
		{
			// Highlight the selected material (in all tiles of the iModel).
			PickedMaterialIModel->HighlightMaterial(*PickedMaterial);
		}
		if (Options.bBroadcastMaterialSelection)
		{
			OnMaterialPicked.Broadcast(*PickedMaterial, PickedMaterialIModel->IModelId);
		}
		OutPickingResult.MaterialId = *PickedMaterial;
		OutPickingResult.PickedMaterialIModel = PickedMaterialIModel;
	}
}

void AITwinPickingActor::PickUnderCursorWithOptions(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* PickedIModel, FHitResult& VisibleHit, FITwinPickingOptions const& Options)
{
	FPickingResult PickingResult;
	PickUnderCursorWithOptions(PickingResult, PickedIModel, Options);

	ElementId = MoveTemp(PickingResult.ElementId);
	MousePosition = MoveTemp(PickingResult.MousePosition);
	VisibleHit = MoveTemp(PickingResult.HitResult);
}

void AITwinPickingActor::PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* iModel, FHitResult& VisibleHit)
{
	PickUnderCursorWithOptions(ElementId, MousePosition, iModel, VisibleHit,
		FITwinPickingOptions{ .bSelectElement = false, .bSelectMaterial = false });
}

void AITwinPickingActor::DeSelect(AITwinIModel* iModel)
{
	if (ensure(iModel))
	{
		iModel->DeSelectAll();
	}
}
