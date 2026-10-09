/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinViewProjectionState.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Helpers/ITwinViewProjectionState.h"

#include <Blueprint/WidgetLayoutLibrary.h>
#include <Camera/PlayerCameraManager.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>

namespace ITwin
{
	bool GetViewProjectionState(const APlayerController* PC, FITwinViewProjectionState& OutState)
	{
		if (!PC)
			return false;

		PC->GetPlayerViewPoint(OutState.ViewLocation, OutState.ViewRotation);

		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
		OutState.ViewportSize = FVector2D(ViewportSizeX, ViewportSizeY);

		OutState.ViewportScale = 1.0f;
		if (const UWorld* World = PC->GetWorld())
		{
			OutState.ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(World), 1e-2f);
		}

		OutState.FOV = 90.0f;
		OutState.bIsOrthographic = false;
		OutState.OrthoWidth = 0.0f;

		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			OutState.FOV = CameraManager->GetFOVAngle();
			OutState.bIsOrthographic = CameraManager->IsOrthographic();
			if (OutState.bIsOrthographic)
			{
				OutState.OrthoWidth = CameraManager->GetOrthoWidth();
			}
		}

		return true;
	}

	bool GetViewProjectionState(const UWorld* World, FITwinViewProjectionState& OutState)
	{
		return GetViewProjectionState(World ? World->GetFirstPlayerController() : nullptr, OutState);
	}
}
