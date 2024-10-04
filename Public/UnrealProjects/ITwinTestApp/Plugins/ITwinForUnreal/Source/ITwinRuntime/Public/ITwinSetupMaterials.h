/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSetupMaterials.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

class AITwinCesium3DTileset;

namespace ITwin
{
	ITWINRUNTIME_API void SetupMaterials(AITwinCesium3DTileset& Tileset, UObject& MaterialOwner);
}
