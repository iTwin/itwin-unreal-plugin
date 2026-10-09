/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingEdMode.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_EDITOR

#include "Helpers/ITwinPickingEdMode.h"
#include "Helpers/ITwinPickingActor.h"
#include "Helpers/ITwinPickingOptions.h"
#include "Helpers/WorldSingleton.h"

#include <EditorViewportClient.h>
#include <Engine/World.h>

const FEditorModeID FITwinPickingEdMode::EM_ITwinPicking(TEXT("EM_ITwinPicking"));

bool FITwinPickingEdMode::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport,
	FKey InKey, EInputEvent InEvent)
{
	if (InKey == EKeys::LeftMouseButton && InEvent == IE_Pressed)
	{
		UWorld* World = InViewportClient ? InViewportClient->GetWorld() : nullptr;
		if (World)
		{
			// Same singleton actor used by the gameplay-side picking helper (see Helpers.cpp).
			auto PickingActor = TWorldSingleton<AITwinPickingActor>().Get(World);
			if (PickingActor)
			{
				FString ElementId;
				FVector2D MousePosition;
				FHitResult HitResult;
				bool const bAddKey = InViewportClient ? InViewportClient->IsCtrlPressed() : false;
				bool const bMatKey = InViewportClient ? InViewportClient->IsShiftPressed() : false;
				PickingActor->PickUnderCursorWithOptions(ElementId, MousePosition,
					/*ThisIModelOnly =*/nullptr, HitResult,
					FITwinPickingOptions{
						.bAdditiveSelection			= bAddKey && !bMatKey,
						.bSelectMaterial			= bMatKey && !bAddKey,
						.bHighlightSelectedMaterial = bMatKey && !bAddKey,
					});
				// Consuming the click here prevents the default actor-selection click handling.
				// Return false instead if you want both behaviors (default selection + iModel picking).
				return true;
			}
		}
	}
	return FEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

#endif // WITH_EDITOR
