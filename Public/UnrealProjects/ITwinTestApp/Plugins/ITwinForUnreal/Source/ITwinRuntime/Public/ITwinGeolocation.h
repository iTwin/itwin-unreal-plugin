/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGeolocation.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <ITwinFwd.h>

#include <ITwinCesiumGeoreference.h>
#include <UObject/StrongObjectPtr.h>

class AActor;

//! Stores geolocation-related data that can be shared by several iModel and reality data.
class ITWINRUNTIME_API FITwinGeolocation
{
public:
	//! The reference used by assets that have geolocation info.
	TStrongObjectPtr<AITwinCesiumGeoreference> LocatedGeoreference;
	//! The reference used by assets that do not have geolocation info.
	TStrongObjectPtr<AITwinCesiumGeoreference> NonLocatedGeoreference;
	FITwinGeolocation(AActor& Parent);
};
