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

}

void AITwinPickingActor::PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition, AITwinIModel* iModel)
{
	if (!iModel)
	{
		return;
	}

	uint32 const MaxUniqueElementsHit = 1;

	ITwinElementID PickedEltID = ITwin::NOT_ELEMENT;

	ITwin::VisitElementsUnderCursor(GetWorld(), MousePosition,
		[this, iModel, &PickedEltID](FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu)
	{
		TMap<FString, FITwinCesiumMetadataValue> const Table =
			UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(HitResult);
		FITwinCesiumMetadataValue const* const ElementIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
		ITwinElementID const EltID = (ElementIdFound != nullptr)
			? ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				*ElementIdFound, ITwin::NOT_ELEMENT.value()))
			: ITwin::NOT_ELEMENT;

		if (EltID != ITwin::NOT_ELEMENT)
		{
			FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
			if (IModelInternals.HasElementWithID(EltID))
			{
				DejaVu.insert(EltID);

				PickedEltID = EltID;
				IModelInternals.OnClickedElement(EltID, HitResult);

				ElementPickedEvent.Broadcast({});
			}
		}

	}, &MaxUniqueElementsHit);

	if (PickedEltID != ITwin::NOT_ELEMENT)
	{
		// convert picked element ID to string.
		ElementId = FString::Printf(TEXT("0x%I64x"), PickedEltID.value());
	}

	// Update selection highlight
	FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
	IModelInternals.SelectElement(PickedEltID);

	if (IModelInternals.SceneMapping.NeedUpdateSelectionHighlights())
	{
		// Make sure the textures are updated in materials (if needed), upon next frame.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[iModel](float Delta) -> bool
		{
			GetInternals(*iModel).SceneMapping.UpdateSelectionHighlights();
			return false;
		}));
	}
}
