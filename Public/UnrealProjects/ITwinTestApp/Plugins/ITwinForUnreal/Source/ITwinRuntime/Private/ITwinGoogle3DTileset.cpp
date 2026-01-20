/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTileset.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinGoogle3DTileset.h>

#include <ITwinGeolocation.h>
#include <ITwinSetupMaterials.h>
#include <ITwinTilesetAccess.h>
#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>
#include <Decoration/ITwinDecorationHelper.h>

#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h> // for TActorIterator<>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinScene.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Network/http.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>


#define GOOGLE_3D_TILESET_URL TEXT("https://tile.googleapis.com/v1/3dtiles/root.json?key=")


namespace
{
	//! Used if the user provides his own Google API key.
	static FString DefaultGoogle3DTilesetKey;

	//! Used when the access token is retrieved from iTwin API.
	static AdvViz::SDK::ITwinGoogleCuratedContentAccess ITwinGoogleAccess;

	inline bool HasITwinGoogleAccess()
	{
		return !ITwinGoogleAccess.url.empty() && !ITwinGoogleAccess.accessToken.empty();
	}
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


class AITwinGoogle3DTileset::FTilesetAccess : public FITwinTilesetAccess
{
public:
	FTilesetAccess(AITwinGoogle3DTileset* InGoogleTileset);
	virtual TUniquePtr<FITwinTilesetAccess> Clone() const override;

	virtual ITwin::ModelDecorationIdentifier GetDecorationKey() const override;
	virtual AITwinDecorationHelper* GetDecorationHelper() const override;
	virtual const ACesium3DTileset* GetTileset() const override;
	virtual ACesium3DTileset* GetMutableTileset() const override;

private:
	TWeakObjectPtr<AITwinGoogle3DTileset> GoogleTileset;
};


class AITwinGoogle3DTileset::FImpl
{
public:
	AITwinGoogle3DTileset& Owner;
	AITwinDecorationHelper* PersistenceMgr = nullptr;
	bool bHasLoadedGeoLocationFromDeco = false;
	bool bEnableGeoRefEdition = true; // Geo-location can be imposed by outside - when the loaded imodels/reality-data are geo-located
	TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper> ClippingHelper;

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
		// update decoration geo-reference
		AITwinDecorationHelper* DecoHelper = Cast<AITwinDecorationHelper>(UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinDecorationHelper::StaticClass()));
		if (DecoHelper)
			DecoHelper->SetDecoGeoreference(FVector(latLongHeight[0], latLongHeight[1], latLongHeight[2]));
	}

	Geoloc->GeoReference->SetOriginLatitude(latLongHeight[0]);
	Geoloc->GeoReference->SetOriginLongitude(latLongHeight[1]);
	Geoloc->GeoReference->SetOriginHeight(latLongHeight[2]);
	Geoloc->bNeedElevationEvaluation = false;

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
void AITwinGoogle3DTileset::SetContentAccess(AdvViz::SDK::ITwinGoogleCuratedContentAccess const& ContentAccess, UWorld* World)
{
	if (ITwinGoogleAccess.accessToken != ContentAccess.accessToken
		|| ITwinGoogleAccess.url != ContentAccess.url)
	{
		ITwinGoogleAccess = ContentAccess;

		const FString NewToken = ANSI_TO_TCHAR(ContentAccess.accessToken.c_str());
		BE_ASSERT(NewToken == FString(UTF8_TO_TCHAR(ContentAccess.accessToken.c_str())),
			"expecting ascii token", ContentAccess.accessToken);
		const FString NewUrl = ANSI_TO_TCHAR(ContentAccess.url.c_str());
		const TMap<FString, FString> RequestHeaders =
		{
			{ TEXT("Authorization"), TEXT("Bearer ") + NewToken }
		};

		// Update all Google tilesets already instantiated.
		for (TActorIterator<AITwinGoogle3DTileset> GooIter(World); GooIter; ++GooIter)
		{
			AITwinGoogle3DTileset* Tileset = *GooIter;
			Tileset->SetRequestHeaders(RequestHeaders);
			Tileset->SetUrl(NewUrl);
		}
	}
}

/*static*/
std::string AITwinGoogle3DTileset::ElevationtKey;

/*static*/ void AITwinGoogle3DTileset::SetElevationtKey(std::string const& GoogleElevationKey)
{
	ElevationtKey = GoogleElevationKey;
}

/*static*/
bool AITwinGoogle3DTileset::RequestElevationtAtGeolocation(AdvViz::SDK::ITwinGeolocationInfo const& GeolocationInfo,
	std::function<void(std::optional<double> const& elevationOpt)>&& InCallback)
{
	using namespace AdvViz::SDK;
	if (ElevationtKey.empty())
		return false;

	BE_LOGI("ITwinAdvViz", "Requesting elevation at ["
		<< GeolocationInfo.latitude << ", " << GeolocationInfo.longitude << "]");

	static std::shared_ptr<Http> g_GoogleHttp;
	if (!g_GoogleHttp)
	{
		g_GoogleHttp = std::shared_ptr<Http>(Http::New());
		g_GoogleHttp->SetBaseUrl("https://maps.googleapis.com/maps/api");
		g_GoogleHttp->SetExecuteAsyncCallbackInGameThread(true); // mandatory here! (callbacks will access world/actors)
	}
	struct SElevationInfo
	{
		double elevation = -1.0;
		double resolution = -1.0;
	};
	struct SElevationResults
	{
		std::vector<SElevationInfo> results;
		std::string status;
	};
	using SharedElevationResults = Tools::TSharedLockableDataPtr<SElevationResults>;
	SharedElevationResults dataOut = std::make_shared<Tools::RWLockablePtrObject<SElevationResults>>(
		new SElevationResults());

	const std::string relativeUrl = fmt::format("elevation/json?locations={}%2C{}&key={}",
		GeolocationInfo.latitude, GeolocationInfo.longitude, ElevationtKey);

	g_GoogleHttp->AsyncGetJson<SElevationResults>(dataOut,
		[Callback = std::move(InCallback)](long status, SharedElevationResults ElevationResultsPtr)
	{
		auto lock(ElevationResultsPtr->GetRAutoLock());
		const SElevationResults& ElevationResults = lock.Get();

		std::optional<double> elevation;
		if (status >= 200 && status < 300 && !ElevationResults.results.empty())
		{
			elevation = ElevationResults.results[0].elevation;
		}
		Callback(elevation);
	},
		relativeUrl);
	return true;
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
				Geoloc->bNeedElevationEvaluation = false;
			}
		}
	}

	// Make use of our own materials (important for packaged version!)
	ITwin::SetupMaterials(FTilesetAccess(Tileset));

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
		else if (HasITwinGoogleAccess())
		{
			SetRequestHeaders(
			{
				{
					TEXT("Authorization"),
					FString(TEXT("Bearer ")) + ANSI_TO_TCHAR(ITwinGoogleAccess.accessToken.c_str())
				}
			});
			SetUrl(ANSI_TO_TCHAR(ITwinGoogleAccess.url.c_str()));
		}

		ShowCreditsOnScreen = true;

		// Quick fix for EAP: add Google logo to the left
		UserCredit = TEXT("<img alt=\"Google\" src=\"https://assets.ion.cesium.com/google-credit.png\" style=\"vertical-align:-5px\">");
		bHighPriorityUserCredit = true;
	}
}

AITwinGoogle3DTileset::~AITwinGoogle3DTileset()
{
	Impl->ClippingHelper.Reset();
}

void AITwinGoogle3DTileset::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Impl->ClippingHelper.Reset();
	Super::EndPlay(EndPlayReason);
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

UITwinClippingCustomPrimitiveDataHelper* AITwinGoogle3DTileset::GetClippingHelper() const
{
	return Impl->ClippingHelper.Get();
}

bool AITwinGoogle3DTileset::MakeClippingHelper()
{
	Impl->ClippingHelper =
		TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper>(NewObject<UITwinClippingCustomPrimitiveDataHelper>(this));
	Impl->ClippingHelper->SetModelIdentifier(
		std::make_pair(EITwinModelType::GlobalMapLayer, FString()));

	// Connect mesh creation callback
	this->SetLifecycleEventReceiver(Impl->ClippingHelper.Get());

	return true;
}


AITwinGoogle3DTileset::FTilesetAccess::FTilesetAccess(AITwinGoogle3DTileset* InGoogleTileset)
	: FITwinTilesetAccess(InGoogleTileset)
	, GoogleTileset(InGoogleTileset)
{

}

TUniquePtr<FITwinTilesetAccess> AITwinGoogle3DTileset::FTilesetAccess::Clone() const
{
	return MakeUnique<FTilesetAccess>(GoogleTileset.Get());
}

ITwin::ModelDecorationIdentifier AITwinGoogle3DTileset::FTilesetAccess::GetDecorationKey() const
{
	return std::make_pair(EITwinModelType::GlobalMapLayer, FString());
}

AITwinDecorationHelper* AITwinGoogle3DTileset::FTilesetAccess::GetDecorationHelper() const
{
	// For now, the offset and quality settings of the Google tileset are saved at hand in a custom
	// way, not as a standard layer in the scene.
	ensureMsgf(false, TEXT("persistence of Google tileset settings is handled apart"));
	return nullptr;
}

const ACesium3DTileset* AITwinGoogle3DTileset::FTilesetAccess::GetTileset() const
{
	return GoogleTileset.Get();
}
ACesium3DTileset* AITwinGoogle3DTileset::FTilesetAccess::GetMutableTileset() const
{
	return GoogleTileset.Get();
}

TUniquePtr<FITwinTilesetAccess> AITwinGoogle3DTileset::MakeTilesetAccess()
{
	return MakeUnique<FTilesetAccess>(this);
}
