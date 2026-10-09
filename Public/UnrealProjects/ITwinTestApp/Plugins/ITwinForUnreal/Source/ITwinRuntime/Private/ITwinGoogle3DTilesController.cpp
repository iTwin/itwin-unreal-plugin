/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTilesController.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinGoogle3DTilesController.h>

#include <Helpers/WorldSingleton.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <ITwinIModel.h>
#include <ITwinTilesetAccess.h>
#include <ITwinGeolocation.h>
#include <ITwinGoogle3DTileset.h>

#include <EngineUtils.h> // for TActorIterator<>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinScene.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	extern void Gather3DMapTilesets(const UWorld* World, TArray<ACesium3DTileset*>& Out3DMapTilesets);
}


class AITwinGoogle3DTilesController::FImpl
{
public:
	FImpl(AITwinGoogle3DTilesController& InOwner);
	AITwinDecorationHelper* GetPersistenceMgr();

	bool bHasLoadedIModel = false;

private:
	void FindPersistenceMgr();

	AITwinGoogle3DTilesController& Owner;
	AITwinDecorationHelper* PersistenceMgr = nullptr;
};

AITwinGoogle3DTilesController::FImpl::FImpl(AITwinGoogle3DTilesController& InOwner)
	: Owner(InOwner)
{
}

AITwinDecorationHelper* AITwinGoogle3DTilesController::FImpl::GetPersistenceMgr()
{
	if (!PersistenceMgr)
	{
		FindPersistenceMgr();
	}
	return PersistenceMgr;
}

void AITwinGoogle3DTilesController::FImpl::FindPersistenceMgr()
{
	// Look if a helper already exists:
	PersistenceMgr = AITwinDecorationHelper::GetInstance(Owner.GetWorld());
	if (PersistenceMgr)
	{
		PersistenceMgr->OnSceneLoaded.AddDynamic(&Owner, &AITwinGoogle3DTilesController::OnSceneLoaded);
	}
}

/*static*/
AITwinGoogle3DTilesController* AITwinGoogle3DTilesController::Instance = nullptr;

/*static*/ AITwinGoogle3DTilesController* AITwinGoogle3DTilesController::GetInstance(UWorld* World)
{
	if (Instance == nullptr)
	{
		SetInstance(TWorldSingleton<AITwinGoogle3DTilesController>().Get(World));
	}
	return Instance;
}

/*static*/ void AITwinGoogle3DTilesController::SetInstance(AITwinGoogle3DTilesController* InInstance)
{
	Instance = InInstance;
}

AITwinGoogle3DTilesController::AITwinGoogle3DTilesController()
	: Impl(MakePimpl<FImpl>(*this))
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AITwinGoogle3DTilesController::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(false);
}

void AITwinGoogle3DTilesController::BeginDestroy()
{
	if (this == Instance)
	{
		SetInstance(nullptr);
	}
	Super::BeginDestroy();
}

void AITwinGoogle3DTilesController::Toggle3DMap(bool IsToggled)
{
	TArray<ACesium3DTileset*> TilesetActors;
	ITwin::GatherGoogle3DTilesets(GetWorld(), TilesetActors);
	if (!IsToggled)
	{
		for (auto TilesetActor : TilesetActors)
		{
			if (IsValid(TilesetActor))
				TilesetActor->Destroy();
		}
	}
	else
		Toggle3DMapVisibility(true);
}

void AITwinGoogle3DTilesController::Toggle3DMapVisibility(bool IsToggled)
{
	TArray<ACesium3DTileset*> TilesetActors;
	ITwin::GatherGoogle3DTilesets(GetWorld(), TilesetActors);

	if (IsToggled && TilesetActors.IsEmpty())
	{
		const auto Tileset = AddNew3DMap();
		if (ensure(Tileset))
			TilesetActors.Push(Tileset);
	}

	for (auto TilesetActor : TilesetActors)
	{
		TilesetActor->SetActorHiddenInGame(!IsToggled);
	}
}

void AITwinGoogle3DTilesController::Set3DMapQuality(float Quality)
{
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(GetWorld()); Google3DIter; ++Google3DIter)
	{
		(*Google3DIter)->SetTilesetQuality(Quality);
	}
}

void AITwinGoogle3DTilesController::On3DMapLocationSet(double Latitude, double Longitude, double Elevation)
{

}

void AITwinGoogle3DTilesController::Set3DMapLocation(double Latitude, double Longitude, double Elevation)
{
	std::array<double, 3> const latLongHeight = { Latitude, Longitude, Elevation };
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(GetWorld()); Google3DIter; ++Google3DIter)
	{
		(*Google3DIter)->SetGeoLocation(latLongHeight);
	}

	On3DMapLocationSet(Latitude, Longitude, Elevation);
}

AITwinGoogle3DTileset* AITwinGoogle3DTilesController::AddNew3DMap()
{
	// Instantiate a Google 3D Tileset.
	// In Carrot, we activate physics meshes on it, as it can be useful in 3 cases:
	// - orbit around tileset (left click)
	// - population (avoid a full refresh the first time the user enables population on reality data)
	// - cutout (allows a better height for cutout polygons).
	constexpr bool bGeneratePhysicsMeshes = true;
	const auto Tileset = AITwinGoogle3DTileset::MakeInstance(*GetWorld(), bGeneratePhysicsMeshes,
		GetDPIScale());
	if (ensure(Tileset))
	{
		auto GeoRef = Tileset->GetGeoreference();
		if (ensure(GeoRef))
		{
			ensure(GeoRef->GetOriginPlacement() == EOriginPlacement::CartographicOrigin);
			const bool bEnableGeoRefEdition = !Tileset->IsGeoLocationLocked();

			// Update UI
			SetGeolocationInUI(
				GeoRef->GetOriginLatitude(),
				GeoRef->GetOriginLongitude(),
				GeoRef->GetOriginHeight(),
				bEnableGeoRefEdition);
		}
	}
	return Tileset;
}

void AITwinGoogle3DTilesController::OnSceneLoaded(bool bSuccess)
{
	if (!bSuccess)
		return;

	UpdateFromSceneSettings();
}

void AITwinGoogle3DTilesController::UpdateFromSceneSettings()
{
	AITwinDecorationHelper* PersistenceMgr = Impl->GetPersistenceMgr();
	if (PersistenceMgr)
	{
		// Load values from Persistent Scene.
		auto ss = PersistenceMgr->GetSceneSettings();
		Update3DMapUI(ss.displayGoogleTiles, ss.qualityGoogleTiles);

		Toggle3DMapVisibility(ss.displayGoogleTiles);
		Set3DMapQuality(ss.qualityGoogleTiles);
	}
}

std::optional<float> AITwinGoogle3DTilesController::GetQualityFromTileset() const
{
	TArray<ACesium3DTileset*> TilesetActors;
	ITwin::Gather3DMapTilesets(GetWorld(), TilesetActors);
	if (!TilesetActors.IsEmpty())
	{
		return std::make_optional<float>(ITwin::GetTilesetQuality(*TilesetActors[0]));
	}
	return std::nullopt;
}

void AITwinGoogle3DTilesController::UpdateGeolocUIFromTileset(AITwinIModel const* IModel)
{
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(GetWorld()); Google3DIter; ++Google3DIter)
	{
		auto GeoRef = (*Google3DIter)->GetGeoreference();
		if (ensure(GeoRef))
		{
			bool bHasGeoLocatedIModel = (IModel && IModel->GetEcefLocation() != nullptr);
			bool bEnableGeoRefEdition = !bHasGeoLocatedIModel;
			SetGeolocationInUI(
				GeoRef->GetOriginLatitude(),
				GeoRef->GetOriginLongitude(),
				GeoRef->GetOriginHeight(),
				bEnableGeoRefEdition);
			// Stop as soon as UI could be initialized
			return;
		}
	}
}

void AITwinGoogle3DTilesController::OnModelLoaded(AITwinIModel const* IModel)
{
	// Only load 3D map once in case several iModels are loaded
	if (Impl->bHasLoadedIModel)
		return;

	if (IModel && ensure(!IModel->IModelId.IsEmpty()))
	{
		Impl->bHasLoadedIModel = true;
	}

	// Quick fix for old MVP presentations: discard any Google tileset to avoid troubles.
	// Note that we may lose some manual offsets which may have been baked in such levels, but this was just
	// temporary stuff made for Carrot MVP during the YII, and if the final product comes with such concept
	// of presentations, they would be remade from scratch, based on the Scene concept...
	TArray<ACesium3DTileset*> LegacyGoogleTilesets;
	for (TActorIterator<ACesium3DTileset> TilesetIter(GetWorld()); TilesetIter; ++TilesetIter)
	{
		if (ITwin::IsGoogle3DTileset(*TilesetIter)
			&& !(*TilesetIter)->IsA(AITwinGoogle3DTileset::StaticClass()))
		{
			LegacyGoogleTilesets.Push(*TilesetIter);
		}
	}
	for (auto* LegacyGTileset : LegacyGoogleTilesets)
	{
		GetWorld()->DestroyActor(LegacyGTileset);
	}
	AITwinDecorationHelper* PersistenceMgr = Impl->GetPersistenceMgr();
	if (PersistenceMgr)
	{
		// Update geolocation from loaded decoration
		for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(GetWorld()); Google3DIter; ++Google3DIter)
		{
			(*Google3DIter)->OnSceneLoaded(true);
		}

		UpdateFromSceneSettings();
		UpdateGeolocUIFromTileset(IModel);
		OnGoogleTilesLoaded();
	}
	else
	{
		// By default, always create the Google 3D tileset.
		Toggle3DMapVisibility(true);
		auto Quality = GetQualityFromTileset();
		if (Quality)
			Update3DMapUI(true, Quality.value());
		UpdateGeolocUIFromTileset(IModel);
	}
}

bool AITwinGoogle3DTilesController::HasValidGoogleTileset() const
{
	for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(GetWorld()); Google3DIter; ++Google3DIter)
	{
		auto GeoRef = (*Google3DIter)->GetGeoreference();
		if (ensure(GeoRef))
		{
			return true;
		}
	}
	return false;
}

void AITwinGoogle3DTilesController::Save()
{
	AITwinDecorationHelper* PersistenceMgr = Impl->GetPersistenceMgr();
	if (!PersistenceMgr)
		return;

	auto ss = PersistenceMgr->GetSceneSettings();

	std::optional<bool> displayGoogleTilesOpt = DisplayGoogleTiles();
	if (displayGoogleTilesOpt)
		ss.displayGoogleTiles = *displayGoogleTilesOpt;

	// Retrieve latest values from UI, if any, and update the scene settings accordingly.
	std::optional<std::array<double, 3>> geoLocationOpt = GetGeolocationFromUI();
	if (geoLocationOpt && ss.geoLocation != geoLocationOpt.value())
	{
		ss.geoLocation = geoLocationOpt.value();
	}

	if (auto Quality = GetQualityFromTileset())
		ss.qualityGoogleTiles = Quality.value();

	PersistenceMgr->SetSceneSettings(ss);
}

void AITwinGoogle3DTilesController::UpdateAfterGeoLocSet(bool bFromscene)
{
	auto&& Geoloc = FITwinGeolocation::Get(*GetWorld());
	auto GeoRef = Geoloc->GeoReference.Get();

	// Update UI
	SetGeolocationInUI(GeoRef->GetOriginLatitude(), GeoRef->GetOriginLongitude(), GeoRef->GetOriginHeight(), bFromscene);

	// save new geoloc to scenePersistence
	if (!bFromscene)
	{
		Save();
	}
}

#if WITH_TESTS
void AITwinGoogle3DTilesController::SetElevationTestMode(FString const& ServerUrl, const FString& InGoogleElevationKey)
{
#if WITH_EDITORONLY_DATA
	GoogleElevationKey = InGoogleElevationKey;
#endif
	AITwinGoogle3DTileset::SetElevationtKey(TCHAR_TO_ANSI(*InGoogleElevationKey));
	AITwinGoogle3DTileset::SetElevationTestURL(ServerUrl);
}
#endif // WITH_TESTS


#if WITH_EDITOR

void AITwinGoogle3DTilesController::PostEditChangeProperty(struct FPropertyChangedEvent& e)
{
	UE_LOG(LogITwin, Display, TEXT("AITwinGoogle3DTilesController::PostEditChangeProperty()"));
	Super::PostEditChangeProperty(e);

	FName const PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinGoogle3DTilesController, GoogleElevationKey))
	{
		if (!GoogleElevationKey.IsEmpty())
		{
			// Save the key in the project settings, so that it is available for all levels.
			AITwinGoogle3DTileset::SetElevationtKey(TCHAR_TO_ANSI(*GoogleElevationKey));
		}
	}
}

#endif // WITH_EDITOR
