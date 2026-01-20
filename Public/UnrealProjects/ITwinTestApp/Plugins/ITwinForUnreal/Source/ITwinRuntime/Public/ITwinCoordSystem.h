/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCoordSystem.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

#include <ITwinCoordSystem.generated.h>


UENUM(BlueprintType)
enum class EITwinCoordSystem : uint8
{
	ITwin,
	UE UMETA(DisplayName = "Unreal")
};
