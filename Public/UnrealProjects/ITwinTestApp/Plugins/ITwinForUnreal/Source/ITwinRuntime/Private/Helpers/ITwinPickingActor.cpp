/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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

	std::optional<uint64_t> GetMaterialIDFromHit(FHitResult const& HitResult)
	{
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
				if (doPickElement)
				{
					DejaVu.insert(EltID);
					PickedEltID = EltID;
					ElementPickedEvent.Broadcast({});

					if (Options.bSelectMaterial)
					{
						PickedMaterial = ITwin::GetMaterialIDFromHit(HitResult);
						bHasTestedMaterial = true;
					}
				}
			}
		}
		if (Options.bSelectMaterial && !bHasTestedMaterial)
		{
			// Some primitive parts may not be assigned any ElementID but still have a valid ITwin material.
			auto MatOpt = ITwin::GetMaterialIDFromHit(HitResult);
			if (MatOpt && !PickedMaterial)
			{
				PickedMaterial = MatOpt;
			}
		}

	}, &MaxUniqueElementsHit);

	if (PickedEltID != ITwin::NOT_ELEMENT)
	{
		// convert picked element ID to string.
		ElementId = FString::Printf(TEXT("0x%I64x"), PickedEltID.value());

#if ENABLE_DRAW_DEBUG
		if (PickedMaterial)
		{
			ElementId += FString::Printf(TEXT(" [MatID: %llu (%s)]"),
				*PickedMaterial, *iModel->GetMaterialName(*PickedMaterial));
		}
#endif
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
