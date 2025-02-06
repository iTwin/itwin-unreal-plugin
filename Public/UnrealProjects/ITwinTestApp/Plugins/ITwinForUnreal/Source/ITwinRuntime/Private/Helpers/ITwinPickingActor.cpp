/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Helpers/ITwinPickingActor.h"

#include <Engine/HitResult.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumMetadataValue.h>
#include <ITwinMetadataConstants.h>

#include <Containers/Ticker.h>
#include <DrawDebugHelpers.h>

#include <optional>
#include <unordered_set>

// Sets default values
AITwinPickingActor::AITwinPickingActor()
{

}

// Called when the game starts or when spawned
void AITwinPickingActor::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AITwinPickingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


/// Picking


namespace ITwin
{
	ITwinElementID VisitElementsUnderCursor(UWorld const* World,
		FVector2D& MousePosition,
		std::function<void(FHitResult const&, std::unordered_set<ITwinElementID>&)> const& HitResultHandler,
		uint32 const* pMaxUniqueElementsHit);

	std::optional<uint64_t> GetMaterialIDFromHit(FHitResult const& HitResult, AITwinIModel& IModel)
	{
		// When visualizing ML-based material predictions, we ignore the material IDs present in source
		// meta-data, and replace them with custom material IDs depending on the ML inference
		// => just test the material ID that we may have baked in the mesh.
		if (IModel.VisualizeMaterialMLPrediction() && HitResult.Component.IsValid())
		{
			FITwinIModelInternals const& IModelInternals = GetInternals(IModel);
			auto const* pMeshWrapper = IModelInternals.SceneMapping.FindGlTFMeshWrapper(HitResult.Component.Get());
			if (pMeshWrapper)
			{
				return pMeshWrapper->GetITwinMaterialIDOpt();
			}
		}
		// General case: test meta-data produced by the Mesh Export Service.
		TMap<FString, FITwinCesiumMetadataValue> const Table1 =
			UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);
		FITwinCesiumMetadataValue const* const MaterialIdFound = Table1.Find(ITwinCesium::Metada::MATERIAL_NAME);
		if (MaterialIdFound != nullptr)
		{
			return UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				*MaterialIdFound, ITwin::NOT_ELEMENT.value());
		}
		return std::nullopt;
	}
}

void AITwinPickingActor::PickUnderCursorWithOptions(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* iModel, FITwinPickingOptions const& Options)
{
	if (!iModel)
	{
		return;
	}
	uint32 const MaxUniqueElementsHit = 1;

	ITwinElementID PickedEltID = ITwin::NOT_ELEMENT;
	std::optional<uint64_t> PickedMaterial;

	ITwin::VisitElementsUnderCursor(GetWorld(), MousePosition,
		[this, iModel, &Options, &PickedEltID, &PickedMaterial]
		(FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu)
	{
		TMap<FString, FITwinCesiumMetadataValue> const Table =
			UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT);
		FITwinCesiumMetadataValue const* const ElementIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
		ITwinElementID const EltID = (ElementIdFound != nullptr)
			? ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				*ElementIdFound, ITwin::NOT_ELEMENT.value()))
			: ITwin::NOT_ELEMENT;

		bool bHasTestedMaterial = false;

		if (EltID != ITwin::NOT_ELEMENT)
		{
			FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
			if (IModelInternals.HasElementWithID(EltID))
			{
				bool doPickElement = true;
				if (Options.bSelectElement)
				{
					// TODO_JDE clarify this (currently, OnClickedElement does select the element, with some
					// visual feedback and element information in the logs...)
					// This is disabled in Carrot MVP, where only the selection of material matters.
					doPickElement = IModelInternals.OnClickedElement(EltID, HitResult);
				}
				if (doPickElement && !IModelInternals.IsElementHiddenInSavedView(EltID))
				{
					DejaVu.insert(EltID);
					PickedEltID = EltID;
					ElementPickedEvent.Broadcast({});

					if (Options.bSelectMaterial)
					{
						PickedMaterial = ITwin::GetMaterialIDFromHit(HitResult, *iModel);
						bHasTestedMaterial = true;
					}
				}
			}
		}
		if (Options.bSelectMaterial && !bHasTestedMaterial)
		{
			// Some primitive parts may not be assigned any ElementID but still have a valid ITwin material.
			auto MatOpt = ITwin::GetMaterialIDFromHit(HitResult, *iModel);
			if (MatOpt && !PickedMaterial)
			{
				PickedMaterial = MatOpt;
			}
		}

	}, &MaxUniqueElementsHit);

	if (PickedEltID != ITwin::NOT_ELEMENT)
	{
		// convert picked element ID to string.
		ElementId = ITwin::ToString(PickedEltID);
		OnElemPicked.Broadcast(ElementId);
#if ENABLE_DRAW_DEBUG
		if (PickedMaterial)
		{
			ElementId += FString::Printf(TEXT(" [MatID: %llu (%s)]"),
				*PickedMaterial, *iModel->GetMaterialName(*PickedMaterial));
		}
#endif
	}
	//remove highlight if we don't click on the imodel
	else
	{
		DeSelect(iModel);
		OnElemPicked.Broadcast("");
	}
	if (PickedMaterial)
	{
		OnMaterialPicked.Broadcast(*PickedMaterial, iModel->IModelId);
	}
}

void AITwinPickingActor::PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* iModel)
{
	PickUnderCursorWithOptions(ElementId, MousePosition, iModel, {});
}

void AITwinPickingActor::DeSelect(AITwinIModel* iModel)
{
	if (ensure(iModel))
	{
		iModel->DeSelectElements();
	}
}
