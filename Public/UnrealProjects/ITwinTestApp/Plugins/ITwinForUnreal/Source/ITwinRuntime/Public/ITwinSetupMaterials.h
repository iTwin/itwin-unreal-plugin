/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSetupMaterials.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

class FITwinTilesetAccess;
class UITwinSynchro4DSchedules;

namespace ITwin
{
	ITWINRUNTIME_API void SetupMaterials(FITwinTilesetAccess const& TilesetAccess,
										 UITwinSynchro4DSchedules* SchedulesComp = nullptr);
}
