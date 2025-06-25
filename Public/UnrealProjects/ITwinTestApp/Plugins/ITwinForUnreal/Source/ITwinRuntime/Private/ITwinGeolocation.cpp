/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGeolocation.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinGeolocation.h>
#include <CesiumGeoreference.h>
#include <EngineUtils.h>
#include <Engine/World.h>

void FITwinGeolocation::CheckInit(UWorld& World)
{
	static const TCHAR* GeoRefName = TEXT("iTwinGeolocatedReference");
	// Preempt creation of the default georeference, to prevent Cesium from spawning a new one every when
	// spawning a Tileset (ie before we assign our own...)
	static const TCHAR* LocalRefName = TEXT("iTwinNonGeolocatedReference");
	if (!GeoReference.IsValid() || !LocalReference.IsValid())
	{
		for (TActorIterator<ACesiumGeoreference> GeorefIter(&World); GeorefIter; ++GeorefIter)
		{
			if (!GeoReference.IsValid() && (*GeorefIter)->GetName() == GeoRefName)
				GeoReference = *GeorefIter;
			if (!LocalReference.IsValid() && (*GeorefIter)->GetName() == LocalRefName)
				LocalReference = *GeorefIter;
		}
	}
	if (!GeoReference.IsValid())
	{
		FActorSpawnParameters Params;
		Params.Name = GeoRefName;
		GeoReference = World.SpawnActor<ACesiumGeoreference>(Params);
		GeoReference->SetOriginPlacement(EOriginPlacement::TrueOrigin); // here means "not yet inited"
	#if WITH_EDITOR
		GeoReference->SetActorLabel(GeoRefName);
	#endif
	}
	if (!LocalReference.IsValid())
	{
		FActorSpawnParameters Params;
		Params.Name = LocalRefName;
		LocalReference = World.SpawnActor<ACesiumGeoreference>(Params);
		LocalReference->Tags.Add(FName("DEFAULT_GEOREFERENCE")); // copied from CesiumGeoreference.cpp
		// For non-geolocated iModels, the mesh export service creates a hard-coded fake geolocation
		// by locating the center of the "project extents" at latitude & longitude 0.
		// So for those iModels we use a georeference located here.
		LocalReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
		LocalReference->SetOriginLongitudeLatitudeHeight(FVector::ZeroVector);
	#if WITH_EDITOR
		LocalReference->SetActorLabel(LocalRefName);
	#endif
	}
}

/*static*/ TSharedPtr<FITwinGeolocation> FITwinGeolocation::Get(UWorld& World)
{
	static TSharedPtr<FITwinGeolocation> Instance;
	if (!Instance)
	{
		// can happen if the IModel was created manually, outside of any instance of AITwinDigitalTwin
		Instance = MakeShared<FITwinGeolocation>();
	}
	Instance->CheckInit(World);
	return Instance;
}
