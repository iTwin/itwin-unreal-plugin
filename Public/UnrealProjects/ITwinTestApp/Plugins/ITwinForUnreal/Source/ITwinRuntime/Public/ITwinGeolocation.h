/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGeolocation.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <ITwinFwd.h>

#include <Templates/SharedPointer.h>

class AITwinCesiumGeoreference;
class UWorld;

//! Stores geolocation-related data that can be shared by several iModel and reality data.
class ITWINRUNTIME_API FITwinGeolocation
{
	void CheckInit(UWorld& World);
public:
	//! The reference used by assets that have geolocation info.
	//! Note: Former use of a TStrongObjectPtr would prevent the owning ULevel from being garbage collected,
	//! causing a fatal error in debug builds eg. when creating/loading another level.
	TWeakObjectPtr<AITwinCesiumGeoreference> GeoReference;
	//! The reference used by assets that do not have geolocation info.
	//! See comment on GeoReference for why we use a raw pointer.
	TWeakObjectPtr<AITwinCesiumGeoreference> LocalReference;

	static TSharedPtr<FITwinGeolocation> Get(UWorld& World);
};
