/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTileset.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinGoogle3DTileset.h>

#include <ITwinGeolocation.h>
#include <ITwinSetupMaterials.h>
#include <ITwinTilesetAccess.h>

#include <Decoration/ITwinDecorationHelper.h>

#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h> // for TActorIterator<>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinScene.h>
#include <Compil/AfterNonUnrealIncludes.h>


#define GOOGLE_3D_TILESET_URL TEXT("https://tile.googleapis.com/v1/3dtiles/root.json?key=")


namespace
{
	static FString DefaultGoogle3DTilesetKey;
}


namespace ITwin
{
	/// Works with both legacy tilesets (saved in presentations for Carrot MVP) and new ones.
	bool IsGoogle3DTileset(ACesium3DTileset const* Tileset)
	{
		return (Tileset != nullptr)
			&&
			(	(Cast<AITwinGoogle3DTileset>(Tileset) != nullptr)
			||	Tileset->GetUrl().StartsWith(GOOGLE_3D_TILESET_URL));
	}

	/// Get current list of Google Maps 3D tilesets.
	void GatherGoogle3DTilesets(const UObject* WorldContextObject,
		TArray<ACesium3DTileset*>& Out3DMapTilesets)
	{
		Out3DMapTilesets.Empty();
		TArray<AActor*> TilesetActors;
		UGameplayStatics::GetAllActorsOfClass(WorldContextObject, ACesium3DTileset::StaticClass(), TilesetActors);
		for (auto Actor : TilesetActors)
		{
			ACesium3DTileset* TilesetActor = Cast<ACesium3DTileset>(Actor);
			if (TilesetActor && IsGoogle3DTileset(TilesetActor))
			{
				Out3DMapTilesets.Push(TilesetActor);
			}
		}
	}
}

class AITwinGoogle3DTileset::FImpl
{
public:
	AITwinGoogle3DTileset& Owner;
	AITwinDecorationHelper* PersistenceMgr = nullptr;
	bool bHasLoadedGeoLocationFromDeco = false;
	bool bEnableGeoRefEdition = true; // Geo-location can be imposed by outside - when the loaded imodels/reality-data are geo-located


	FImpl(AITwinGoogle3DTileset& InOwner)
		: Owner(InOwner)
	{
	}

	// Persistence
	void FindPersistenceMgr();
	void OnSceneLoaded(bool bSuccess);
	bool LoadGeoLocationFromDeco(std::array<double, 3> const& latLongHeight);
	void SetGeoLocation(std::array<double, 3> const& latLongHeight);
};

void AITwinGoogle3DTileset::FImpl::FindPersistenceMgr()
{
	//Look if a helper already exists:
	for (TActorIterator<AITwinDecorationHelper> DecoIter(Owner.GetWorld()); DecoIter; ++DecoIter)
	{
		PersistenceMgr = *DecoIter;
	}
	//if (PersistenceMgr)
	//{
	//	PersistenceMgr->OnSceneLoaded.AddDynamic(&Owner, &AITwinGoogle3DTileset::OnSceneLoaded);
	//}
}

void AITwinGoogle3DTileset::FImpl::OnSceneLoaded(bool bSuccess)
{
	if (!PersistenceMgr)
	{
		FindPersistenceMgr();
	}
	if (bSuccess && PersistenceMgr && !bHasLoadedGeoLocationFromDeco)
	{
		// Load values from Persistent Scene.
		auto const ss = PersistenceMgr->GetSceneSettings();
		if (ss.geoLocation.has_value())
		{
			bHasLoadedGeoLocationFromDeco = LoadGeoLocationFromDeco(*ss.geoLocation);
		}
	}
}

bool AITwinGoogle3DTileset::FImpl::LoadGeoLocationFromDeco(std::array<double, 3> const& latLongHeight)
{
	auto GeoRef = Owner.GetGeoreference();
	if (!ensure(GeoRef))
	{
		return false;
	}
	GeoRef->SetOriginLatitude(latLongHeight[0]);
	GeoRef->SetOriginLongitude(latLongHeight[1]);
	GeoRef->SetOriginHeight(latLongHeight[2]);
	return true;
}

void AITwinGoogle3DTileset::FImpl::SetGeoLocation(std::array<double, 3> const& latLongHeight)
{
	auto&& Geoloc = FITwinGeolocation::Get(*Owner.GetWorld());
	if (Geoloc->GeoReference->GetOriginPlacement() == EOriginPlacement::TrueOrigin)
	{
		// First time we initialize the common geo-reference
		Geoloc->GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
	}

	Geoloc->GeoReference->SetOriginLatitude(latLongHeight[0]);
	Geoloc->GeoReference->SetOriginLongitude(latLongHeight[1]);
	Geoloc->GeoReference->SetOriginHeight(latLongHeight[2]);

	// Manage persistence
	if (!PersistenceMgr)
		FindPersistenceMgr();
	if (PersistenceMgr)
	{
		auto ss = PersistenceMgr->GetSceneSettings();
		if (ss.geoLocation != latLongHeight)
		{
			ss.geoLocation = latLongHeight;
			PersistenceMgr->SetSceneSettings(ss);
		}
	}
}


/*static*/
void AITwinGoogle3DTileset::SetDefaultKey(FString const& DefaultGoogleKey, UWorld* World /*= nullptr*/)
{
	DefaultGoogle3DTilesetKey = DefaultGoogleKey;

	if (World && !DefaultGoogleKey.IsEmpty())
	{
		// Update all Google tilesets already instantiated
		for (TActorIterator<AITwinGoogle3DTileset> GooIter(World); GooIter; ++GooIter)
		{
			AITwinGoogle3DTileset* Tileset = *GooIter;
			if (Tileset->GetUrl().IsEmpty())
			{
				Tileset->GoogleKey = DefaultGoogleKey;
				Tileset->SetUrl(FString(GOOGLE_3D_TILESET_URL) + DefaultGoogleKey);
			}
		}
	}
}

/*static*/
AITwinGoogle3DTileset* AITwinGoogle3DTileset::MakeInstance(UWorld& World, bool bGeneratePhysicsMeshes /*= false*/)
{
	// Instantiate a Google 3D Tileset

	// *before* SpawnActor otherwise Cesium will create its own default georef
	auto&& Geoloc = FITwinGeolocation::Get(World);
	const auto Tileset = World.SpawnActor<AITwinGoogle3DTileset>();
#if WITH_EDITOR
	Tileset->SetActorLabel(TEXT("Google 3D tileset"));
#endif
	Tileset->SetCreatePhysicsMeshes(bGeneratePhysicsMeshes);

	// Decrease the default quality to avoid consuming too much (AzDev #1533278)
	ITwin::SetTilesetQuality(*Tileset, 0.30f);

	// Always use the *true* geo-reference for Google 3D Maps.
	Tileset->SetGeoreference(Geoloc->GeoReference.Get());

	auto GeoRef = Tileset->GetGeoreference();
	if (ensure(GeoRef))
	{
		// GeoRef is a singleton potentially shared by many iModels / reality-data tilesets.
		// Its placement is initially set to TrueOrigin, and only becomes 'CartographicOrigin' when
		// an iModel or reality-data is truly geo-referenced, so if its placement is currently
		// 'CartographicOrigin', we can be sure that something truly geo-referenced was loaded in the
		// scene.
		// This test remains correct when we load a different model from Carrot's startup panel,
		// because in such case, the singleton itself is recreated (see FITwinGeolocation::CheckInit
		// for details).
		if (GeoRef->GetOriginPlacement() == EOriginPlacement::CartographicOrigin)
		{
			// The scene already contains a truly geo-located item
			// => fill the edit fields with the latter, and forbid their edition.
			Tileset->LockGeoLocation(true);
		}
		else
		{
			Geoloc->bCanBypassCurrentLocation = true;
			GeoRef->SetOriginPlacement(EOriginPlacement::CartographicOrigin);

			// We now have the possibility to reload user customizations from the decoration service
			// (temporary solution for the YII, again...)
			// Note that this will *not* have any impact on presentations, in which the Google
			// tileset is baked, and thus, not re-created here...
			Tileset->Impl->OnSceneLoaded(true);

			if (!Tileset->Impl->bHasLoadedGeoLocationFromDeco)
			{
				// By default, use Exton's coordinates
				GeoRef->SetOriginLatitude(40.0325817);
				GeoRef->SetOriginLongitude(-75.6274583);
				GeoRef->SetOriginHeight(94.0);
			}
		}
	}

	// Instantiate a UCesiumPolygonRasterOverlay component, which can then be populated with polygons to
	// enable cutout (ACesiumCartographicPolygon)
	ITwin::InitCutoutOverlay(*Tileset);

	return Tileset;
}


AITwinGoogle3DTileset::AITwinGoogle3DTileset()
	: Super()
	, Impl(MakePimpl<FImpl>(*this))
{
	GoogleKey = DefaultGoogle3DTilesetKey;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SetTilesetSource(ETilesetSource::FromUrl);

		if (!GoogleKey.IsEmpty())
		{
			SetUrl(FString(GOOGLE_3D_TILESET_URL) + GoogleKey);
		}

		ShowCreditsOnScreen = true;

		// Quick fix for EAP: add Google logo to the left
		UserCredit = TEXT("<img alt=\"Google\" src=\"https://assets.ion.cesium.com/google-credit.png\" style=\"vertical-align:-5px\">");
		bHighPriorityUserCredit = true;

		// Make use of our own materials (important for packaged version!)
		ITwin::SetupMaterials(*this);
	}
}

void AITwinGoogle3DTileset::SetActorHiddenInGame(bool bNewHidden)
{
	Super::SetActorHiddenInGame(bNewHidden);

	if (Impl->PersistenceMgr)
	{
		const bool bShow = !bNewHidden;
		auto ss = Impl->PersistenceMgr->GetSceneSettings();
		if (ss.displayGoogleTiles != bShow)
		{
			ss.displayGoogleTiles = bShow;
			Impl->PersistenceMgr->SetSceneSettings(ss);
		}
	}
}

void AITwinGoogle3DTileset::SetTilesetQuality(float Value)
{
	ITwin::SetTilesetQuality(*this, Value);

	if (Impl->PersistenceMgr)
	{
		auto ss = Impl->PersistenceMgr->GetSceneSettings();
		if (fabs(ss.qualityGoogleTiles - Value) > 1e-5)
		{
			ss.qualityGoogleTiles = Value;
			Impl->PersistenceMgr->SetSceneSettings(ss);
		}
	}
}

void AITwinGoogle3DTileset::SetGeoLocation(std::array<double, 3> const& latLongHeight)
{
	ensureMsgf(!IsGeoLocationLocked(), TEXT("geo-location is locked!"));
	Impl->SetGeoLocation(latLongHeight);
}

void AITwinGoogle3DTileset::LockGeoLocation(bool bLockEdition)
{
	Impl->bEnableGeoRefEdition = !bLockEdition;
}

bool AITwinGoogle3DTileset::IsGeoLocationLocked() const
{
	return !Impl->bEnableGeoRefEdition;
}

#if WITH_EDITOR
void AITwinGoogle3DTileset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	// The Google Key is unique to the user.
	FName const PropertyName = (Event.Property != nullptr) ? Event.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinGoogle3DTileset, GoogleKey)
		&& !GoogleKey.IsEmpty())
	{
		AITwinGoogle3DTileset::SetDefaultKey(GoogleKey, GetWorld());
	}
}
#endif // WITH_EDITOR


void AITwinGoogle3DTileset::OnSceneLoaded(bool bSuccess)
{
	Impl->OnSceneLoaded(bSuccess);
}

