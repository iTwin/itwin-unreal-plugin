/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesGeneration.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

#include <ITwinSynchro4DSchedules.h>

inline const TCHAR* ToString(EITwinSchedulesGeneration Generation)
{
	switch (Generation)
	{
	case EITwinSchedulesGeneration::Legacy: return TEXT("Legacy");
	case EITwinSchedulesGeneration::NextGen: return TEXT("NextGen");
	case EITwinSchedulesGeneration::Unknown: break;
	}
	return TEXT("Unknown");
}

inline bool FromString(FString const& Str, EITwinSchedulesGeneration& Generation)
{
	if (Str == TEXT("Legacy"))
		Generation = EITwinSchedulesGeneration::Legacy;
	else if (Str == TEXT("NextGen"))
		Generation = EITwinSchedulesGeneration::NextGen;
	else if (Str == TEXT("Unknown"))
		Generation = EITwinSchedulesGeneration::Unknown;
	else
		return false;
	return true;
}
