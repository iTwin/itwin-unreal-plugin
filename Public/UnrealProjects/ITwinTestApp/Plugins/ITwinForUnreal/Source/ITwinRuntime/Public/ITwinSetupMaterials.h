/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSetupMaterials.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

class ACesium3DTileset;
class UITwinSynchro4DSchedules;

namespace ITwin
{
	ITWINRUNTIME_API void SetupMaterials(ACesium3DTileset& Tileset,
										 UITwinSynchro4DSchedules* SchedulesComp = nullptr);
}
