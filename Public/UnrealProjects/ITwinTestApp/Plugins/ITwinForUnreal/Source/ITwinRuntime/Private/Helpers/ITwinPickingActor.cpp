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

	ITwinElementID GetMaterialIDFromHit(FHitResult const& HitResult)
	{
		ITwinElementID PickedMaterial = ITwin::NOT_ELEMENT;
		TMap<FString, FITwinCesiumMetadataValue> const Table1 =
			UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);
		FITwinCesiumMetadataValue const* const MaterialIdFound = Table1.Find(ITwinCesium::Metada::MATERIAL_NAME);
		if (MaterialIdFound != nullptr)
		{
			PickedMaterial = ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				*MaterialIdFound, ITwin::NOT_ELEMENT.value()));
		}
		return PickedMaterial;
	}
}

void AITwinPickingActor::PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition,
												   AITwinIModel* iModel)
{
	if (!iModel)
	{
		return;
	}

	uint32 const MaxUniqueElementsHit = 1;

	ITwinElementID PickedEltID = ITwin::NOT_ELEMENT;
	ITwinElementID PickedMaterial = ITwin::NOT_ELEMENT;

	ITwin::VisitElementsUnderCursor(GetWorld(), MousePosition,
		[this, iModel, &PickedEltID, &PickedMaterial](FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu)
	{
		TMap<FString, FITwinCesiumMetadataValue> const Table =
			UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT);
		FITwinCesiumMetadataValue const* const ElementIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
		ITwinElementID const EltID = (ElementIdFound != nullptr)
			? ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				*ElementIdFound, ITwin::NOT_ELEMENT.value()))
			: ITwin::NOT_ELEMENT;

		if (EltID != ITwin::NOT_ELEMENT)
		{
			FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
			if (IModelInternals.HasElementWithID(EltID)
				&& IModelInternals.OnClickedElement(EltID, HitResult))
			{
				DejaVu.insert(EltID);
				PickedEltID = EltID;
				ElementPickedEvent.Broadcast({});

#if ENABLE_DRAW_DEBUG
				PickedMaterial = ITwin::GetMaterialIDFromHit(HitResult);
#endif
			}
		}

	}, &MaxUniqueElementsHit);

	if (PickedEltID != ITwin::NOT_ELEMENT)
	{
		// convert picked element ID to string.
		ElementId = FString::Printf(TEXT("0x%I64x"), PickedEltID.value());
	}

#if ENABLE_DRAW_DEBUG
	if (PickedMaterial != ITwin::NOT_ELEMENT)
	{
		ElementId += FString::Printf(TEXT(" MaterialID:0x%I64x"), PickedMaterial.value());
	}
#endif
}
