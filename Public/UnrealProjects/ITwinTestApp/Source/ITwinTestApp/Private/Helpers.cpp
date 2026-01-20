/*--------------------------------------------------------------------------------------+
|
|     $Source: Helpers.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Helpers.h>
#include <Kismet/GameplayStatics.h>
#include <Helpers/ITwinPickingActor.h>
#include <ITwinIModel.h>

void UHelpers::PickMouseElements(const UObject* WorldContextObject, bool& bValid, FString& ElementId)
{
	PickUnderCursorWithOptions(WorldContextObject, bValid, ElementId, {});
}

void UHelpers::PickUnderCursorWithOptions(const UObject* WorldContextObject, bool& bValid, FString& ElementId,
	FITwinPickingOptions const& Options)
{
	bValid = false;
	auto* const PickingActor = (AITwinPickingActor*)UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinPickingActor::StaticClass());
	auto* const IModel = (AITwinIModel*)UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinIModel::StaticClass());
	if (IModel)
	{
		FVector2D MousePosition; FHitResult HitResult;
		PickingActor->PickUnderCursorWithOptions(ElementId, MousePosition, IModel, HitResult, Options);
		bValid = ElementId != "0x0";
	}
}