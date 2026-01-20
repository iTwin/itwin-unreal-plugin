/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGeolocation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinGeolocation.h>
#include <CesiumGeoreference.h>
#include <EngineUtils.h>
#include <Engine/World.h>
#include <Kismet/GameplayStatics.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>
#include <Decoration/ITwinDecorationHelper.h>

std::function<FVector(bool& bRequestInProgress, bool& bHasRelevantElevation)> FITwinGeolocation::GetDefaultGeoRefFct;

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
		bNeedElevationEvaluation = false;
	#if WITH_EDITOR
		GeoReference->SetActorLabel(GeoRefName);
	#endif
		if (GeoReference->GetEllipsoid()->GetRadii().GetAbsMax() < 2.0)
		{
			BE_LOGE("ITwinAdvViz", "Corrupted ellipsoid (WGS84 asset missing?)");
		}

		if (GetDefaultGeoRefFct)
		{
			bool bDefaultGeoRefInProgress = false;
			bool bHasRelevantElevation = false;
			FVector VLongLat = GetDefaultGeoRefFct(bDefaultGeoRefInProgress, bHasRelevantElevation);
			ensureMsgf(!bDefaultGeoRefInProgress, TEXT("iTwin geo-ref request still in progress"));
			if (VLongLat.X != 0. || VLongLat.Y != 0.)
			{
				bCanBypassCurrentLocation = true;
				bNeedElevationEvaluation = !bHasRelevantElevation;
				GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
				GeoReference->SetOriginLongitudeLatitudeHeight(VLongLat);
				// update decoration geo-reference
				AITwinDecorationHelper* DecoHelper = Cast<AITwinDecorationHelper>(UGameplayStatics::GetActorOfClass(&World, AITwinDecorationHelper::StaticClass()));
				if (DecoHelper)
				{
					FVector latLongHeight(VLongLat.Y, VLongLat.X, VLongLat.Z);
					DecoHelper->SetDecoGeoreference(latLongHeight);
				}
			}
		}
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

/*static*/ bool FITwinGeolocation::IsDefaultGeoRefRequestInProgress()
{
	if (GetDefaultGeoRefFct)
	{
		bool bDefaultGeoRefInProgress = false;
		bool bHasRelevantElevation = false;
		FVector VLongLat = GetDefaultGeoRefFct(bDefaultGeoRefInProgress, bHasRelevantElevation);
		return bDefaultGeoRefInProgress;
	}
	else
	{
		return false;
	}
}
