/*--------------------------------------------------------------------------------------+
|
|     $Source: Helpers.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Helpers.h>
#include <Kismet/GameplayStatics.h>
#include <Helpers/ITwinPickingActor.h>
#include <ITwinIModel.h>

void UHelpers::PickMouseElements(const UObject* WorldContextObject, bool& bValid, FString& ElementId)
{
	bValid = false;
	auto* const PickingActor = (AITwinPickingActor*)UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinPickingActor::StaticClass());
	auto* const IModel = (AITwinIModel*)UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinIModel::StaticClass());
	if (IModel)
	{
		FVector2D MousePosition;
		PickingActor->PickObjectAtMousePosition(ElementId, MousePosition, IModel);
		bValid = ElementId != "0x0";
	}
}
