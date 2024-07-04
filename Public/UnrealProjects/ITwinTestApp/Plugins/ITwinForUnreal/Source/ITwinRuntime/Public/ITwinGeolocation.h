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
	//! We can use a raw pointer here because the actor is referenced by the Parent passed in the ctor.
	//! Actually, using a TStrongObjectPtr would prevent the owning ULevel from being garbage collected,
	//! causing a fatal error in debug builds eg. when creating/loading another level.
	AITwinCesiumGeoreference* LocatedGeoreference = nullptr;
	//! The reference used by assets that do not have geolocation info.
	//! See comment on LocatedGeoreference for why we use a raw pointer.
	AITwinCesiumGeoreference* NonLocatedGeoreference = nullptr;
	FITwinGeolocation(AActor& Parent);
};
