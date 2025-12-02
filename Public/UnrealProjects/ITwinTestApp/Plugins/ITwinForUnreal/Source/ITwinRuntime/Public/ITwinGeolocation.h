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
#include <CesiumGeoreference.h>

#include <Templates/SharedPointer.h>

#include <functional>

class ACesiumGeoreference;
class UWorld;

//! Stores geolocation-related data that can be shared by several iModel and reality data.
class ITWINRUNTIME_API FITwinGeolocation
{
	void CheckInit(UWorld& World);
public:
	//! The reference used by assets that have geolocation info.
	//! Note: Former use of a TStrongObjectPtr would prevent the owning ULevel from being garbage collected,
	//! causing a fatal error in debug builds eg. when creating/loading another level.
	TWeakObjectPtr<ACesiumGeoreference> GeoReference;
	//! The reference used by assets that do not have geolocation info.
	//! See comment on GeoReference for why we use a raw pointer.
	TWeakObjectPtr<ACesiumGeoreference> LocalReference;
	//! When GeoReference already uses CartographicOrigin as OriginPlacement and this flag is true, it means
	//! the longitude/latitude/Z position used are to be considered mere defaults that should not prevent
	//! a model from setting its own geolocation, which is probably more suitable. Typically used by tilesets
	//! with a worldwide coverage like Google 3D to set some default location for viewing, but not prevent
	//! the actual iModel to be loaded from setting the "right" location.
	bool bCanBypassCurrentLocation = false;
	//! When the default GeoReference is loaded from the iTwin information and not from Ecef location defined
	//! at iModel level, the elevation needs to be evaluated from another request.
	bool bNeedElevationEvaluation = false;

	static TSharedPtr<FITwinGeolocation> Get(UWorld& World);

	//! Returns true if the default geo-ref retrieval (for current iTwin) request is still in progress, and
	//! thus the actual loading of tilesets should be delayed.
	static bool IsDefaultGeoRefRequestInProgress();

	static std::function<FVector(bool& bRequestInProgress, bool& bHasRelevantElevation)> GetDefaultGeoRefFct;
};
