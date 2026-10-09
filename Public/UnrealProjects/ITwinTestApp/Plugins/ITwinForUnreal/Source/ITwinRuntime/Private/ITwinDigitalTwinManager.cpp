/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwinManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinDigitalTwinManager.h"

#include <Decoration/ITwinDecorationHelper.h>
#include <ITwinGeolocation.h>
#include <ITwinGoogle3DTilesController.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinTilesetAccess.h>
#include <ITwinWebServices/ITwinWebServices.h>

#include <CesiumWgs84Ellipsoid.h>
#include <Components/DirectionalLightComponent.h>
#include <Engine/Engine.h>
#include <Engine/World.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Misc/RWLock.h>
#	include <SDK/Core/ITwinAPI/ITwinTypes.h>
#	include <SDK/Core/Tools/Log.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	extern void LoadIModelDecorationMaterials(AITwinIModel& IModel, UWorld* World);

	extern void LoadScene(FString const& ITwinID, UWorld* World);
	extern void SaveScene(FString const& ITwinID, UWorld const* World);
}


class AITwinDigitalTwinManager::FLoadingScope::FImpl
{
public:
	FImpl(AITwinDigitalTwinManager& InMngr)
		: Mngr(InMngr)
		, LoadedComponentsAtStart(InMngr.LoadedObjects.Num())
		, bIsLoadingSceneAtStart(InMngr.bIsLoadingScene)
	{
		++Mngr.NumLoadingScopes;
	}

	~FImpl()
	{
		if (ensure(Mngr.NumLoadingScopes > 0))
		{
			--Mngr.NumLoadingScopes;
		}

		// Only broadcast when no scopes are nested anymore
		if (Mngr.NumLoadingScopes == 0)
		{
			// If a modification is detected, broadcast the info received event
			if (LoadedComponentsAtStart != Mngr.LoadedObjects.Num()
				|| bIsLoadingSceneAtStart != Mngr.bIsLoadingScene)
			{
				Mngr.ComponentInfoReceivedEvent.Broadcast();
			}
		}
	}

private:
	AITwinDigitalTwinManager& Mngr;
	const int32 LoadedComponentsAtStart;
	const bool bIsLoadingSceneAtStart;
};

AITwinDigitalTwinManager::FLoadingScope::FLoadingScope(AITwinDigitalTwinManager& InMngr)
	: Impl(MakePimpl<FImpl>(InMngr))
{
}

AITwinDigitalTwinManager::FLoadingScope::~FLoadingScope()
{
}

class AITwinDigitalTwinManager::FImpl
{
public:
	FImpl(AITwinDigitalTwinManager& InOwner);
	bool HasFinishedRetrievingAllComponentInfos() const;

	// Called with LoadedObject as nullptr when loading a scene.
	void HandleGeolocation(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId);
	void OnGeoLocationSet(bool bFromScene, bool bFromElevationRequest = false);
	void RequestGeoLocElevationIfNeeded(bool bFromITwinInfo);

	AITwinDecorationHelper* FindDecorationHelper() const;

	bool ShouldSaveGeoRef(ACesiumGeoreference const& GeoRef) const;
	void OnGeoRefSaved(ACesiumGeoreference const& GeoRef);

public:
	AITwinDigitalTwinManager& Owner;
	// iTwin info
	std::shared_mutex ITwinInfoMutex;
	enum class EITwinRequestStatus : uint8_t
	{
		NotStarted,
		InProgress,
		Done
	};
	EITwinRequestStatus ITwinInfoRequestStatus = EITwinRequestStatus::NotStarted;
	bool bHasLoggedGeolocInfo = false;
	std::optional<AdvViz::SDK::ITwinGeolocationInfo> GeolocInfo;

	EITwinRequestStatus GetIModelsRequestStatus = EITwinRequestStatus::NotStarted;
	EITwinRequestStatus GetRealityDataRequestStatus = EITwinRequestStatus::NotStarted;
	int32 NumRealityDataRequests = 0;
	int32 NumRealityDataRequestsDone = 0;

	//! Whether we have already loaded a layer (iModel or RealityData) in the scene. Once an existing scene
	//! has been loaded, we also consider it is true.
	bool bHasLoadedLayer = false;
	// True when we actually got a georeference
	bool bGeoLocationSet = false;

	FVector LastLatLongHeightSaved = FVector::ZeroVector;


	// Some operations require to first fetch a valid authorization token
	enum class EOperationUponAuth : uint8
	{
		None,
		Update,
		LoadDecoration
	};
	EOperationUponAuth PendingOperation = EOperationUponAuth::None;
	TMap<FString, FString> IModelIdToLoadableLayerNameMap;
	TMap<FString, FString> RealityDataIdToLoadableLayerNameMap;
	TSet<FString> LoadableLayerNames;

	//! Unlike PendingLoadIds, ComponentLoadContextMap will persist after the component is loaded: it can be
	//! used to know in which context the component was loaded (to know if a model was loaded as part of a
	//! scene or individually).
	TMap<FString, EITwinLoadContext> ComponentLoadContextMap;
};

AITwinDigitalTwinManager::FImpl::FImpl(AITwinDigitalTwinManager& InOwner)
	: Owner(InOwner)
{
	if (!InOwner.HasAnyFlags(RF_ClassDefaultObject))
	{
		// If the loaded iTwin does have a geo-location, it should be used as default one.
		TWeakObjectPtr<AITwinDigitalTwinManager> OwnerPtr(&InOwner);
		FITwinGeolocation::GetDefaultGeoRefFct = [OwnerPtr, this](bool& bRequestInProgress, bool& bHasRelevantElevation)
		{
			bRequestInProgress = false;
			// The iTwin geo-location does not provide with elevation. This will have to be evaluated through
			// another mean (using Google elevation api).
			bHasRelevantElevation = false;
			FVector VLongLat(0., 0., 0.);
			if (OwnerPtr.IsValid())
			{
				BeUtils::RLock RLock(ITwinInfoMutex);
				bRequestInProgress = ITwinInfoRequestStatus == EITwinRequestStatus::InProgress;
				if (GeolocInfo)
				{
					VLongLat = FVector(GeolocInfo->longitude, GeolocInfo->latitude, 0.0f);
				}
				// Only log iTwin geo-ref result once per iTwin.
				if (ITwinInfoRequestStatus == EITwinRequestStatus::Done
					&& !bHasLoggedGeolocInfo)
				{
					if (GeolocInfo)
					{
						BE_LOGI("ITwinAdvViz", "iTwin latitude: " << GeolocInfo->latitude << ", longitude: " << GeolocInfo->longitude);
					}
					else
					{
						BE_LOGI("ITwinAdvViz", "No latitude longitude found in iTwin data");
					}
					bHasLoggedGeolocInfo = true;
				}
			}
			return VLongLat;
		};
	}
}

bool AITwinDigitalTwinManager::FImpl::HasFinishedRetrievingAllComponentInfos() const
{
	ensure(NumRealityDataRequestsDone <= NumRealityDataRequests);
	return GetIModelsRequestStatus == EITwinRequestStatus::Done
		&& GetRealityDataRequestStatus == EITwinRequestStatus::Done
		&& NumRealityDataRequestsDone == NumRealityDataRequests;
}


AITwinDecorationHelper* AITwinDigitalTwinManager::FImpl::FindDecorationHelper() const
{
	if (Owner.ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "Unspecified ITwinId - cannot fetch decoration helper");
		return nullptr;
	}
	return AITwinDecorationHelper::FindByITwinID(Owner.ITwinId, Owner.GetWorld());
}

bool AITwinDigitalTwinManager::FImpl::ShouldSaveGeoRef(ACesiumGeoreference const& GeoRef) const
{
	const FVector LatLongHeight(
		GeoRef.GetOriginLatitude(),
		GeoRef.GetOriginLongitude(),
		GeoRef.GetOriginHeight());
	return !LastLatLongHeightSaved.Equals(LatLongHeight, UE_SMALL_NUMBER);
}

void AITwinDigitalTwinManager::FImpl::OnGeoRefSaved(ACesiumGeoreference const& GeoRef)
{
	LastLatLongHeightSaved = FVector(
		GeoRef.GetOriginLatitude(),
		GeoRef.GetOriginLongitude(),
		GeoRef.GetOriginHeight());
}


namespace
{
	double ComputeDistanceMeters(const FCartographicProps& Props1, const FCartographicProps& Props2)
	{
		// Convert each cartographic position to ECEF
		FVector ECEF1 = UCesiumWgs84Ellipsoid::LongitudeLatitudeHeightToEarthCenteredEarthFixed(
			FVector(Props1.Longitude, Props1.Latitude, Props1.Height)
		);
		FVector ECEF2 = UCesiumWgs84Ellipsoid::LongitudeLatitudeHeightToEarthCenteredEarthFixed(
			FVector(Props2.Longitude, Props2.Latitude, Props2.Height)
		);
		// Compute Euclidean distance in ECEF space (straight-line distance through Earth)
		return FVector::Distance(ECEF1, ECEF2);
	}
}

void AITwinDigitalTwinManager::FImpl::HandleGeolocation(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId)
{
	auto&& Geoloc = FITwinGeolocation::Get(*Owner.GetWorld());
	// Note: we can't rely on Geoloc->GeoReference->GetOriginPlacement being still TrueOrigin, because the
	// Google 3D Tileset has actually set it to CartographicOrigin with the default (Exton's) long/lat, right
	// from the start (see AMainLevelScript::OnWorldBeginPlay), but the first /other/ geolocated tileset
	// loaded must change it to some more suitable location.
	if (bGeoLocationSet)
	{
		ensure(!Geoloc->bCanBypassCurrentLocation);

		// When adding an individual layer to the scene, detect cases with very distant geo-locations and
		// warn the user about if in this case, as it can lead to very weird camera behavior.
		// AzDev#2024778.
		if (Owner.GetComponentLoadContext(LayerId) == EITwinLoadContext::Single)
		{
			auto TilesetAccess = Owner.GetTilesetAccess(ModelType, LayerId);
			std::optional<FCartographicProps> GeoProps = TilesetAccess
				? TilesetAccess->GetNativeGeoreference()
				: std::nullopt;
			if (GeoProps)
			{
				FCartographicProps CurrentGeoProps;
				CurrentGeoProps.Latitude = Geoloc->GeoReference->GetOriginLatitude();
				CurrentGeoProps.Longitude = Geoloc->GeoReference->GetOriginLongitude();
				CurrentGeoProps.Height = Geoloc->GeoReference->GetOriginHeight();
				const double DistanceMeters = ComputeDistanceMeters(CurrentGeoProps, *GeoProps);
				Owner.GeoLocationGapInMetersEvent.Broadcast(DistanceMeters, LayerId);
			}
		}
		return;
	}
	if (ModelType == EITwinModelType::IModel)
	{
		const auto IModel = Cast<AITwinIModel const>(LoadedObject);
		if (ensure(IModel) && IModel->GetEcefLocation() && IModel->GetProjectExtents())
		{
			// Geoloc->GeoReference has been set up in MakeTileset, no need to repeat here
			OnGeoLocationSet(false);
		}
	}
	else if (ModelType == EITwinModelType::RealityData)
	{
		auto iRealData = Cast<AITwinRealityData>(LoadedObject);
		if (ensure(iRealData) && iRealData->IsGeolocated())
		{
			iRealData->UseAsGeolocation();
			OnGeoLocationSet(false);
			// geo-location retrieved from reality-data usually lacks elevation information.
			RequestGeoLocElevationIfNeeded(/*bFromITwinInfo*/false);
		}
	}

	if (LoadedObject == nullptr)
	{
		BE_ASSERT(ModelType == EITwinModelType::GlobalMapLayer || ModelType == EITwinModelType::Invalid);
		AITwinDecorationHelper* DecoHelper = FindDecorationHelper();
		if (ensure(DecoHelper))
		{
			auto const ss = DecoHelper->GetSceneSettings();
			if (ss.geoLocation.has_value())
			{
				Geoloc->GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
				Geoloc->GeoReference->SetOriginLatitude((*ss.geoLocation)[0]);
				Geoloc->GeoReference->SetOriginLongitude((*ss.geoLocation)[1]);
				Geoloc->GeoReference->SetOriginHeight((*ss.geoLocation)[2]);
				OnGeoLocationSet(true);
				FVector latLongHeight((*ss.geoLocation)[0], (*ss.geoLocation)[1], (*ss.geoLocation)[2]);
				// update decoration geo-reference transform
				DecoHelper->SetDecoGeoreference(latLongHeight);
			}
		}
		if (!bGeoLocationSet && Geoloc->bNeedElevationEvaluation)
		{
			// When the geo-location is determined by iTwin information, we lack the elevation, so we make
			// a request to Google elevation API to get a better elevation than 0.0
			RequestGeoLocElevationIfNeeded(/*bFromITwinInfo*/true);
		}
	}

	if (!bGeoLocationSet
		&& !Geoloc->bNeedElevationEvaluation
		&& LoadedObject
		&& ShouldSaveGeoRef(*Geoloc->GeoReference))
	{
		// If the user creates a scene using the default geo-location (Exton) with at least one model,
		// it is important to save the geo-location so that the scene is not totally messed up in the future,
		// in case one of the models or the itwin has a modification of its geo-location:
		AITwinGoogle3DTilesController* GoogleTilesController = AITwinGoogle3DTilesController::GetInstance(
			Owner.GetWorld());
		if (GoogleTilesController
			&& GoogleTilesController->HasValidGoogleTileset()
			&& GoogleTilesController->DisplayGoogleTiles().value_or(false))
		{
			GoogleTilesController->UpdateAfterGeoLocSet(true);
			GoogleTilesController->Save();
			OnGeoRefSaved(*Geoloc->GeoReference);
		}
	}
}

void AITwinDigitalTwinManager::FImpl::OnGeoLocationSet(bool bFromScene, bool bFromElevationRequest /*= false*/)
{
	UWorld* World = Owner.GetWorld();
	if (!ensure(World))
		return;
	auto&& Geoloc = FITwinGeolocation::Get(*World);
	if (!ensure(Geoloc->GeoReference.Get()))
		return;

	// Note about elevation request: it should not alter the flags / geo-location set nor
	// bCanBypassCurrentLocation, as those request are just
	if (!bFromElevationRequest)
	{
		bGeoLocationSet = true;
		Geoloc->bCanBypassCurrentLocation = false;
	}

	Owner.GeoLocationSetEvent.Broadcast(bFromScene, bFromElevationRequest);
}

void AITwinDigitalTwinManager::FImpl::RequestGeoLocElevationIfNeeded(bool bFromITwinInfo)
{
	UWorld* World = Owner.GetWorld();
	if (!ensure(World))
		return;
	auto&& Geoloc = FITwinGeolocation::Get(*World);
	if (Geoloc->bNeedElevationEvaluation && ensure(Geoloc->GeoReference.Get()))
	{
		ensure(fabs(Geoloc->GeoReference->GetOriginHeight()) < 1e-8);

		const AdvViz::SDK::ITwinGeolocationInfo Geolocation =
		{
			.latitude = Geoloc->GeoReference->GetOriginLatitude(),
			.longitude = Geoloc->GeoReference->GetOriginLongitude()
		};

		AITwinGoogle3DTileset::RequestElevationtAtGeolocation(Geolocation,
			[bFromITwinInfo, Geolocation,
			this, ThisOwner = TWeakObjectPtr<AITwinDigitalTwinManager>(&Owner)]
			(std::optional<double> const& elevationOpt)
		{
			if (ThisOwner.IsValid() && elevationOpt)
			{
				auto&& Geoloc = FITwinGeolocation::Get(*ThisOwner->GetWorld());
				// Before changing the elevation, check that the coordinates used for this request still
				// match the current ones (the geo-reference could have been imposed by an iModel after we
				// started the request...)
				if (ensure(Geoloc->GeoReference.Get())
					&& Geoloc->bNeedElevationEvaluation
					&& fabs(Geolocation.latitude - Geoloc->GeoReference->GetOriginLatitude()) < 1e-8
					&& fabs(Geolocation.longitude - Geoloc->GeoReference->GetOriginLongitude()) < 1e-8)
				{
					Geoloc->GeoReference->SetOriginHeight(*elevationOpt);
					Geoloc->bNeedElevationEvaluation = false;
					OnGeoLocationSet(bFromITwinInfo, /*bFromElevationRequest*/true);

					const FVector latLongHeight(Geolocation.latitude, Geolocation.longitude, *elevationOpt);
					// update decoration geo-reference transform
					AITwinDecorationHelper* DecoHelper = FindDecorationHelper();
					if (DecoHelper)
						DecoHelper->SetDecoGeoreference(latLongHeight);
				}
			}
		});
	}
}


AITwinDigitalTwinManager::AITwinDigitalTwinManager()
	: Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));

#if WITH_EDITOR
	// In Editor, outside of PIE, delegates are not called, so we use Tick to check for load status updates
	// instead.
	PrimaryActorTick.bCanEverTick = true;
#endif
}

void AITwinDigitalTwinManager::ResetITwin()
{
	IModelsMap.Empty();
	IModelLoadStatusMap.Empty();
	Impl->IModelIdToLoadableLayerNameMap.Empty();
	RealityDataMap.Empty();
	RealityDataLoadStatusMap.Empty();
	Impl->RealityDataIdToLoadableLayerNameMap.Empty();
	LoadedObjects.Empty();

	const auto ChildrenCopy = Children;
	for (auto& Child : ChildrenCopy)
		if (AActor* ChildActor = Child.Get())
			GetWorld()->DestroyActor(ChildActor);
	Children.Empty();
}

void AITwinDigitalTwinManager::Destroyed()
{
	Super::Destroyed();

	ResetITwin();
}

UDirectionalLightComponent * AITwinDigitalTwinManager::GetSkyLight() const
{
	return SkyLight;
}

void AITwinDigitalTwinManager::SetSkyLight(UDirectionalLightComponent* InSkyLight)
{
	SkyLight = InSkyLight;
}

void AITwinDigitalTwinManager::UpdateITwin()
{
	if (ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "Unspecified ITwinId - cannot be updated");
		return;
	}
	if (!Children.IsEmpty() || !IModelsMap.IsEmpty() || !RealityDataMap.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "Some components have already been loaded: please call ResetITwin before updating iTwin");
		return;
	}
	if (Impl->PendingOperation != FImpl::EOperationUponAuth::None)
	{
		BE_LOGW("ITwinAPI", "An update operation is already pending - cannot stack updates");
		return;
	}
	Init(ITwinId, DisplayName);
}

void AITwinDigitalTwinManager::Init(FString const& InITwinId, FString const& InDisplayName,
									bool bNoWebServicesForTesting/*=false*/)
{
	ITwinId = InITwinId;
	DisplayName = InDisplayName;
	{
		BeUtils::WLock WLock(Impl->ITwinInfoMutex);
		Impl->ITwinInfoRequestStatus =
			bNoWebServicesForTesting ? FImpl::EITwinRequestStatus::Done : FImpl::EITwinRequestStatus::NotStarted;
		Impl->bHasLoggedGeolocInfo = false;
	}
	if (bNoWebServicesForTesting)
	{
		return;
	}
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		// No authorization yet: postpone the actual update (see UpdateOnSuccessfulAuthorization)
		Impl->PendingOperation = FImpl::EOperationUponAuth::Update;
		return;
	}
	RequestData();
}

void AITwinDigitalTwinManager::RequestData()
{
	BE_LOGI("ITwinAdvViz", "Requesting iTwin data");
	// make a request to get the display name and optional iTwin's geo-location
	WebServices->GetITwinInfo(ITwinId);
	{
		BeUtils::WLock WLock(Impl->ITwinInfoMutex);
		Impl->ITwinInfoRequestStatus = FImpl::EITwinRequestStatus::InProgress;
	}
	// fetch iModels
	Impl->GetIModelsRequestStatus = FImpl::EITwinRequestStatus::InProgress;
	WebServices->GetiTwiniModels(ITwinId);
	// fetch reality data
	Impl->GetRealityDataRequestStatus = FImpl::EITwinRequestStatus::InProgress;
	WebServices->GetRealityData(ITwinId);
}

void AITwinDigitalTwinManager::UpdateOnSuccessfulAuthorization()
{
	switch (Impl->PendingOperation)
	{
	case FImpl::EOperationUponAuth::Update:
		if (ensure(IModelsMap.IsEmpty() && RealityDataMap.IsEmpty()))
		{
			RequestData();
		}
		break;
	case FImpl::EOperationUponAuth::LoadDecoration:
		LoadDecoration();
		break;
	case FImpl::EOperationUponAuth::None:
		break;
	}
	Impl->PendingOperation = FImpl::EOperationUponAuth::None;
}

const TCHAR* AITwinDigitalTwinManager::GetObserverName() const
{
	return TEXT("ITwinDigitalTwinManager");
}

void AITwinDigitalTwinManager::OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& Info)
{
	{
		BeUtils::WLock WLock(Impl->ITwinInfoMutex);

		ensureMsgf(ITwinId == ANSI_TO_TCHAR(Info.id.c_str()),
			TEXT("mismatch in iTwin ID (%s vs %s)"), *ITwinId, ANSI_TO_TCHAR(Info.id.c_str()));

		if (bSuccess)
		{
			ensureMsgf(ITwinId == ANSI_TO_TCHAR(Info.id.c_str()),
				TEXT("mismatch in iTwin ID (%s vs %s)"), *ITwinId, ANSI_TO_TCHAR(Info.id.c_str()));
			DisplayName = UTF8_TO_TCHAR(Info.displayName.value_or(std::u8string()).c_str());

			// Store latitude & longitude registered at the iTwin level.
			if (Info.latitude && Info.longitude)
			{
				Impl->GeolocInfo.emplace(AdvViz::SDK::ITwinGeolocationInfo
					{
						.latitude = *Info.latitude,
						.longitude = *Info.longitude
					});
			}
			else
			{
				Impl->GeolocInfo.reset();
			}
		}
		Impl->ITwinInfoRequestStatus = FImpl::EITwinRequestStatus::Done;
	}
	ITwinInfoReceivedEvent.Broadcast();
}

bool AITwinDigitalTwinManager::HasRetrievedITwinInfo() const
{
	BeUtils::RLock RLock(Impl->ITwinInfoMutex);
	return Impl->ITwinInfoRequestStatus == FImpl::EITwinRequestStatus::Done;
}

void AITwinDigitalTwinManager::OnIModelsRetrieved(bool bSuccess, FIModelInfos const& IModelInfos)
{
	if (bSuccess)
	{
		BE_LOGV("ITwinAdvViz", "ITwinManager: Found "
			<< IModelInfos.iModels.Num() << " iModels in " << TCHAR_TO_ANSI(*DisplayName));

		for (FIModelInfo const& IModelInfo : IModelInfos.iModels)
		{
			IModelsMap.FindOrAdd(IModelInfo.Id) = IModelInfo;

			// Also create a helper which will allow to easily load this iModel from the Editor.
			TObjectPtr<UITwinLoadableIModel> LoadableIModel = NewObject<UITwinLoadableIModel>(this,
				MakeUniqueObjectName(this, UITwinLoadableIModel::StaticClass(), *IModelInfo.DisplayName),
				RF_Transient);
			LoadableIModel->Owner = this;
			LoadableIModel->Info = IModelInfo;

			// MakeUniqueObjectName appends "_0" to the name even if it's not already used, so we try to
			// remove it if possible.
			const FString LoadableUniqueName = Impl->LoadableLayerNames.Contains(IModelInfo.DisplayName)
				? LoadableIModel->GetName()
				: IModelInfo.DisplayName;
			Impl->LoadableLayerNames.Add(LoadableUniqueName);
			FITwinLoadableLayerHelper& LoadableIModelHelper = IModelLoadStatusMap.FindOrAdd(LoadableUniqueName);
			LoadableIModelHelper.LoadableLayer = LoadableIModel;

			// Store the mapping between layer ID and loadable layer name, to be able to find the
			// corresponding loadable layer when only the layer ID is known.
			// (See #SetLoadStatus for example).
			Impl->IModelIdToLoadableLayerNameMap.FindOrAdd(IModelInfo.Id) = LoadableUniqueName;

			if (bAutoLoadAllComponents)
			{
				PendingLoadIds.FindOrAdd(IModelInfo.Id, EITwinLoadContext::Single);
			}
			EITwinLoadContext const* PendingLoadCtx = PendingLoadIds.Find(IModelInfo.Id);
			if (PendingLoadCtx != nullptr)
			{
				LoadIModel(IModelInfo, *PendingLoadCtx);
				PendingLoadIds.Remove(IModelInfo.Id);
			}
		}
		ComponentInfoReceivedEvent.Broadcast();
	}
	Impl->GetIModelsRequestStatus = FImpl::EITwinRequestStatus::Done;
	OnComponentInfoRetrieved();
}

void AITwinDigitalTwinManager::OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& RealityDataInfos)
{
	Impl->NumRealityDataRequests = Impl->NumRealityDataRequestsDone = 0;
	if (bSuccess)
	{
		BE_LOGV("ITwinAdvViz", "ITwinManager: Found "
			<< RealityDataInfos.Infos.Num() << " Reality Data objects in " << TCHAR_TO_ANSI(*DisplayName));

		for (FITwinRealityDataInfo const& ReaDataInfo : RealityDataInfos.Infos)
		{
			if (WebServices && !ReaDataInfo.Id.IsEmpty() && !ITwinId.IsEmpty())
			{
				Impl->NumRealityDataRequests++;
				WebServices->GetRealityData3DInfo(ITwinId, ReaDataInfo.Id);
			}
		}
	}
	Impl->GetRealityDataRequestStatus = FImpl::EITwinRequestStatus::Done;
	OnComponentInfoRetrieved();
}

void AITwinDigitalTwinManager::OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) 
{
	if (bSuccess)
	{
		RealityDataMap.FindOrAdd(Info.Id) = Info;

		// Also create a helper which will allow to easily load this iModel from the Editor.
		TObjectPtr<UITwinLoadableRealityData> LoadableRealityData;
		LoadableRealityData = NewObject<UITwinLoadableRealityData>(this,
			MakeUniqueObjectName(this, UITwinLoadableRealityData::StaticClass(), *Info.DisplayName),
			RF_Transient);
		LoadableRealityData->Owner = this;
		LoadableRealityData->Info = Info;

		// Same remark as for iModels regarding the name (see #OnIModelsRetrieved).
		const FString LoadableUniqueName = Impl->LoadableLayerNames.Contains(Info.DisplayName)
			? LoadableRealityData->GetName()
			: Info.DisplayName;
		Impl->LoadableLayerNames.Add(LoadableUniqueName);
		FITwinLoadableLayerHelper& LoadableRealityDataHelper = RealityDataLoadStatusMap.FindOrAdd(LoadableUniqueName);
		LoadableRealityDataHelper.LoadableLayer = LoadableRealityData;

		// Store the mapping between layer ID and loadable layer name, to be able to find the
		// corresponding loadable layer when only the layer ID is known.
		// (See #SetLoadStatus for example).
		Impl->RealityDataIdToLoadableLayerNameMap.FindOrAdd(Info.Id) = LoadableUniqueName;

		if (bAutoLoadAllComponents)
		{
			PendingLoadIds.FindOrAdd(Info.Id, EITwinLoadContext::Single);
		}
		EITwinLoadContext const* PendingLoadCtx = PendingLoadIds.Find(Info.Id);
		if (PendingLoadCtx != nullptr)
		{
			LoadRealityData(Info, *PendingLoadCtx);
			PendingLoadIds.Remove(Info.Id);
		}
		ComponentInfoReceivedEvent.Broadcast();
	}
	Impl->NumRealityDataRequestsDone++;
	OnComponentInfoRetrieved();
}

void AITwinDigitalTwinManager::OnComponentInfoRetrieved()
{
	if (Impl->HasFinishedRetrievingAllComponentInfos())
	{
		// Important: clean the list of pending models: if some are still referenced when all requests have
		// been processed (which can happen if the iTwin services do not answer, or if the scene loaded from
		// the decoration service or SceneAPI contains references to obsoletes models), they will never be
		// loaded in current session...
		if (!PendingLoadIds.IsEmpty())
		{
			BE_LOGW("ITwinAdvViz", PendingLoadIds.Num() << " model(s) not currently available in the iTwin");
			PendingLoadIds.Reset();
		}
		ComponentInfoRetrievalDoneEvent.Broadcast();
	}
}

void AITwinDigitalTwinManager::Add(FITwinLoadInfo const& Info, AITwinIModel* IModel)
{
	BE_LOGV("ITwinAdvViz", "ITwinManager: Adding existing iModel " << TCHAR_TO_ANSI(*Info.IModelDisplayName)
		<< " with ID " << TCHAR_TO_ANSI(*Info.IModelId));
	LoadedObjects.Add(Info.IModelId, IModel);
	IModel->OnIModelLoaded.AddDynamic(this, &AITwinDigitalTwinManager::OnIModelLoaded);
	IModel->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
}

void AITwinDigitalTwinManager::SetupSpawnedLayer(AITwinServiceActor* NewLayer)
{
	NewLayer->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);

	NewLayer->ServerConnection = ServerConnection;

#if WITH_TESTS
	// In test mode, we need to set the server URL to all spawned layers.
	FString ServerUrl;
	if (IsInTestMode(ServerUrl))
	{
		NewLayer->SetTestMode(ServerUrl);
	}
#endif
}

void AITwinDigitalTwinManager::Load(FITwinLoadInfo const& Info, EITwinLoadContext LoadContext)
{
	switch (Info.ModelType)
	{
		case EITwinModelType::IModel:
		{
			BE_LOGV("ITwinAdvViz", "ITwinManager: Loading iModel " << TCHAR_TO_ANSI(*Info.IModelDisplayName)
				<< " with ID " << TCHAR_TO_ANSI(*Info.IModelId));
			auto IModel = GetWorld()->SpawnActor<AITwinIModel>();
			LoadedObjects.Add(Info.IModelId, IModel);
			// Automatic + latest = necessary for StartExport to be called when no export available
			IModel->LoadingMethod = ELoadingMethod::LM_Automatic;
			IModel->ChangesetId = TEXT("latest");
			IModel->OnIModelLoaded.AddDynamic(this, &AITwinDigitalTwinManager::OnIModelLoaded);
			SetupSpawnedLayer(IModel);
			IModel->SetModelLoadInfo(Info);
			IModel->LoadModel(Info.ExportId);

			if (LoadContext == EITwinLoadContext::Single)
			{
				// When loading an individual model to an existing scene, we should load the custom materials
				// which may have been created for the latter in the current decoration scene.
				ITwin::LoadIModelDecorationMaterials(*IModel, GetWorld());
			}
			break;
		}
		case EITwinModelType::RealityData:
			LoadComponent(Info.RealityDataId, LoadContext);
			break;
	}
}

void AITwinDigitalTwinManager::SetLoadStatus(EITwinModelType ModelType, FString const& StringId, EITwinLayerLoadStatus NewStatus)
{
	// Update load status (useful in Editor).

	TMap<FString, FITwinLoadableLayerHelper>& LoadStatusMap = (ModelType == EITwinModelType::IModel)
		? IModelLoadStatusMap
		: RealityDataLoadStatusMap;

	TMap<FString, FString> const& IdToLoadableLayerNameMap = (ModelType == EITwinModelType::IModel)
		? Impl->IModelIdToLoadableLayerNameMap
		: Impl->RealityDataIdToLoadableLayerNameMap;

	const FString* LoadableLayerNamePtr = IdToLoadableLayerNameMap.Find(StringId);
	if (ensure(LoadableLayerNamePtr))
	{
		FITwinLoadableLayerHelper* LoadStatusHelper = LoadStatusMap.Find(*LoadableLayerNamePtr);
		if (ensure(LoadStatusHelper))
		{
			LoadStatusHelper->LoadStatus = NewStatus;

			if (NewStatus != EITwinLayerLoadStatus::InProgress)
			{
				LoadStatusHelper->bLoad = (NewStatus == EITwinLayerLoadStatus::Complete);
			}
		}
	}
}

#if WITH_EDITOR
namespace
{
	template <typename TLayer>
	std::optional<EITwinLayerLoadStatus> TGetITwinLayerLoadStatus(TLayer const& Layer)
	{
		if (Layer.HasLoadedTileset())
		{
			return EITwinLayerLoadStatus::Complete;
		}
		else if (Layer.HasTilesetLoadFailure())
		{
			return EITwinLayerLoadStatus::Failed;
		}
		return std::nullopt;
	}
}
#endif // WITH_EDITOR

bool AITwinDigitalTwinManager::ShouldTickIfViewportsOnly() const
{
#if WITH_EDITOR
	return true;
#else
	return Super::ShouldTickIfViewportsOnly();
#endif
}

void AITwinDigitalTwinManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if WITH_EDITOR

	auto const UpdateLoadStatus = [this](TMap<FString, FITwinLoadableLayerHelper> const& StatusMap)
	{
		// Find which iModel should be loaded/unloaded:
		for (auto const& [_, LoadableLayerHelper] : StatusMap)
		{
			if (LoadableLayerHelper.LoadableLayer
				&& LoadableLayerHelper.LoadStatus == EITwinLayerLoadStatus::InProgress)
			{
				// In progress: check if the layer has finished loading or not, and update the status accordingly.
				std::optional<EITwinLayerLoadStatus> NewStatus;

				EITwinModelType const ModelType = LoadableLayerHelper.LoadableLayer->GetModelType();
				FString const& LayerId = LoadableLayerHelper.LoadableLayer->GetLayerId();
				if (ModelType == EITwinModelType::IModel)
				{
					AITwinIModel const* IModel = GetIModel(LayerId);
					if (IModel)
					{
						NewStatus = TGetITwinLayerLoadStatus<AITwinIModel>(*IModel);
					}
				}
				else if (ModelType == EITwinModelType::RealityData)
				{
					AITwinRealityData const* RealityData = GetRealityData(LayerId);
					if (RealityData)
					{
						NewStatus = TGetITwinLayerLoadStatus<AITwinRealityData>(*RealityData);
					}
				}
				if (NewStatus && *NewStatus != EITwinLayerLoadStatus::InProgress)
				{
					SetLoadStatus(ModelType, LayerId, *NewStatus);

					if (*NewStatus == EITwinLayerLoadStatus::Complete)
					{
						// Outside of PIE, delegates are not called, so we execute the corresponding callback
						// here to ensure the same operations are dones as in game/PIE.
						if (ModelType == EITwinModelType::IModel)
						{
							OnIModelLoaded(true, LayerId);
						}
						else if (ModelType == EITwinModelType::RealityData)
						{
							OnRealityDataInfoLoaded(true, LayerId);
						}
					}
				}
			}
		}
	};
	UpdateLoadStatus(IModelLoadStatusMap);
	UpdateLoadStatus(RealityDataLoadStatusMap);

#endif // WITH_EDITOR
}


void AITwinDigitalTwinManager::LoadIModel(FIModelInfo const& Info, EITwinLoadContext LoadContext)
{
	ensureMsgf(LoadContext != EITwinLoadContext::Unknown, TEXT("always specify a context!"));

	BE_LOGV("ITwinAdvViz", "ITwinManager: Loading iModel " << TCHAR_TO_ANSI(*Info.DisplayName)
		<< " with ID " << TCHAR_TO_ANSI(*Info.Id));

	FLoadingScope LoadingScope(*this);

	SetLoadStatus(EITwinModelType::IModel, Info.Id, EITwinLayerLoadStatus::InProgress);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	auto IModel = GetWorld()->SpawnActor<AITwinIModel>(SpawnParams);
	LoadedObjects.Add(Info.Id, IModel);
	// Automatic + latest = necessary for StartExport to be called when no export available
	IModel->LoadingMethod = ELoadingMethod::LM_Automatic;
	IModel->ChangesetId = TEXT("latest");
	IModel->OnIModelLoaded.AddDynamic(this, &AITwinDigitalTwinManager::OnIModelLoaded);
	SetupSpawnedLayer(IModel);
	IModel->IModelId = Info.Id;
	IModel->ITwinId = ITwinId;
#if WITH_EDITOR
	IModel->SetActorLabel(Info.DisplayName);
#endif
	OnLoadIModelEvent.Broadcast();

	ConnectLoadedIModelToUI(IModel);

	IModel->UpdateIModel();

	if (LoadContext == EITwinLoadContext::Single)
	{
		// When loading an individual model to an existing scene, we should load the custom materials which
		// may have been created for the latter in the current decoration scene.
		ITwin::LoadIModelDecorationMaterials(*IModel, GetWorld());
	}
}

void AITwinDigitalTwinManager::ConnectLoadedIModelToUI(AITwinIModel* /*IModel*/) const
{

}

void AITwinDigitalTwinManager::LoadRealityData(FITwinRealityData3DInfo const& Info, EITwinLoadContext /*LoadContext*/)
{
	BE_LOGV("ITwinAdvViz", "ITwinManager: Loading RealityData " << TCHAR_TO_ANSI(*Info.DisplayName)
		<< " with ID " << TCHAR_TO_ANSI(*Info.Id));

	FLoadingScope LoadingScope(*this);

	SetLoadStatus(EITwinModelType::RealityData, Info.Id, EITwinLayerLoadStatus::InProgress);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	auto RealityData = GetWorld()->SpawnActor<AITwinRealityData>(SpawnParams);
	LoadedObjects.Add(Info.Id, RealityData);
#if WITH_EDITOR
	RealityData->SetActorLabel(Info.DisplayName);
#endif
	SetupSpawnedLayer(RealityData);
	RealityData->RealityDataId = Info.Id;
	RealityData->ITwinId = ITwinId;
	RealityData->OnRealityDataInfoLoaded.AddDynamic(this, &AITwinDigitalTwinManager::OnRealityDataInfoLoaded);

	if (Info.MeshUrl.IsEmpty())
	{
		RealityData->UpdateRealityData();
	}
	else
	{
		// No need to repeat the same request: directly fills the reality data information:
		RealityData->SetRealityData3DInfo(Info);
	}
}

void AITwinDigitalTwinManager::OnRealityDataInfoLoaded(bool bSuccess, FString StringId)
{
	// Update load status (useful in Editor).
	SetLoadStatus(EITwinModelType::RealityData, StringId,
		bSuccess ? EITwinLayerLoadStatus::Complete : EITwinLayerLoadStatus::Failed);

	AITwinRealityData* AsRealityData = nullptr;
	if (bSuccess && LoadedObjects.Contains(StringId)
		&& (AsRealityData = Cast<AITwinRealityData>(LoadedObjects[StringId])) != nullptr)
	{
		if (ActiveModelId.IsEmpty())
			ActiveModelId = StringId;
		// Now that RealityData information is known, we can broadcast the event (to handle geo-location,
		// typically).
		CompletedLoadIds.Add(StringId);
		OnComponentLoaded(LoadedObjects[StringId], EITwinModelType::RealityData, StringId);
	}
}

void AITwinDigitalTwinManager::LoadComponent(FString const& StringId, EITwinLoadContext LoadContext)
{
	//no need to load anything if it's already (being) loaded
	if (IsComponentLoaded(StringId) || IsComponentBeingLoaded(StringId))
		return;

	Impl->ComponentLoadContextMap.FindOrAdd(StringId, LoadContext);

	// First look in iModels map:
	const FIModelInfo* IModelInfo = IModelsMap.Find(StringId);
	if (IModelInfo != nullptr)
	{
		ensure(IModelInfo->Id == StringId);
		LoadIModel(*IModelInfo, LoadContext);
		return;
	}
	// Then look in reality data:
	const FITwinRealityData3DInfo* RealDataInfo = RealityDataMap.Find(StringId);
	if (RealDataInfo != nullptr)
	{
		ensure(RealDataInfo->Id == StringId);
		LoadRealityData(*RealDataInfo, LoadContext);
		return;
	}

	if (Impl->HasFinishedRetrievingAllComponentInfos())
	{
		// If we have already loaded all information on iModels/RealityData and the given ID does not match
		// any, no need to register the latter as pending, as it will never become available in current
		// session.
		BE_LOGW("ITwinAdvViz", "No model available in iTwin matches id " << TCHAR_TO_ANSI(*StringId));
		return;
	}
	PendingLoadIds.Emplace(StringId, LoadContext);
}

void AITwinDigitalTwinManager::OnIModelLoaded(bool bSuccess, FString StringId)
{
	AITwinIModel* AsIModel = nullptr;

	// Update load status (useful in Editor).
	SetLoadStatus(EITwinModelType::IModel, StringId,
		bSuccess ? EITwinLayerLoadStatus::Complete : EITwinLayerLoadStatus::Failed);

	if (bSuccess && LoadedObjects.Contains(StringId)
		&& (AsIModel = Cast<AITwinIModel>(LoadedObjects[StringId])) != nullptr)
	{
		if (ActiveModelId.IsEmpty())
			ActiveModelId = StringId;
		if (IsValid(AsIModel->Synchro4DSchedules))
			Synchro4DSchedules.Add(StringId, AsIModel->Synchro4DSchedules);
		CompletedLoadIds.Add(StringId);
		AsIModel->SetLightForForcedShadowUpdate(SkyLight);
		// Load reality data attached to the iModel (same behavior as Design Review).
		if (AsIModel->GetWebServices()) // none when unit testing
			AsIModel->GetAttachedRealityDataIds().Then([this, StringId](const auto& RealityDataIdsFuture)
			{
				for (const auto& RealityDataId: RealityDataIdsFuture.Get())
				{
					LoadComponent(RealityDataId, EITwinLoadContext::Unknown);
				}
				NumProcessedIModelAttachmentRequests++;
			});
		OnComponentLoaded(LoadedObjects[StringId], EITwinModelType::IModel, StringId);
	}
}

void AITwinDigitalTwinManager::OnComponentLoaded(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId)
{
	std::shared_ptr<AITwinDecorationHelper::SaveLocker> SaveLocker;

	AITwinDecorationHelper* DecoHelper = Impl->FindDecorationHelper();

	if (DecoHelper)
	{
		if (!Impl->bHasLoadedLayer)
		{
			// In case of first load, lock the save so that it does not trigger a save question.
			SaveLocker = DecoHelper->LockSave();
		}
		DecoHelper->CreateLinkIfNeeded(ModelType, LayerId);
	}

	if (ModelType == EITwinModelType::IModel)
	{
		if (GetActiveIModel() == nullptr)
			SetActiveIModel(LayerId);
	}

	Impl->HandleGeolocation(LoadedObject, ModelType, LayerId);

	ComponentLoadedEvent.Broadcast(LoadedObject, ModelType, LayerId);
}

bool AITwinDigitalTwinManager::IsGeoLocationSet() const
{
	return Impl->bGeoLocationSet;
}

FString AITwinDigitalTwinManager::GetComponentName(FString const& StringId) const
{
	const FIModelInfo* IModelInfo = IModelsMap.Find(StringId);
	if (IModelInfo != nullptr)
	{
		ensure(IModelInfo->Id == StringId);
		return IModelInfo->DisplayName;
	}
	const FITwinRealityData3DInfo* RealDataInfo = RealityDataMap.Find(StringId);
	if (RealDataInfo != nullptr)
	{
		ensure(RealDataInfo->Id == StringId);
		return RealDataInfo->DisplayName;
	}
	return FString();
}

AITwinRealityData* AITwinDigitalTwinManager::GetRealityData(FString const& StringId) const
{
	return !StringId.IsEmpty() && LoadedObjects.Contains(StringId) ? Cast<AITwinRealityData>(LoadedObjects[StringId]) : nullptr;
}

AITwinIModel* AITwinDigitalTwinManager::GetIModel(FString const& StringId) const
{
	return !StringId.IsEmpty() && LoadedObjects.Contains(StringId) ? Cast<AITwinIModel>(LoadedObjects[StringId]) : nullptr;
}

TUniquePtr<FITwinTilesetAccess> AITwinDigitalTwinManager::GetTilesetAccess(EITwinModelType ModelType, FString const& StringId) const
{
	switch (ModelType)
	{
	case EITwinModelType::IModel:
		if (AITwinIModel* IModel = GetIModel(StringId))
		{
			return IModel->MakeTilesetAccess();
		}
		break;

	case EITwinModelType::RealityData:
		if (AITwinRealityData* RealityData = GetRealityData(StringId))
		{
			return RealityData->MakeTilesetAccess();
		}
		break;

	default:
		BE_ISSUE("Model type not handled by iTwin Manager: ", static_cast<int>(ModelType));
		break;
	}
	return {};
}

TUniquePtr<FITwinTilesetAccess> AITwinDigitalTwinManager::GetTilesetAccessFromId(FString const& StringId) const
{
	if (AITwinIModel* IModel = GetIModel(StringId))
	{
		return IModel->MakeTilesetAccess();
	}
	else if (AITwinRealityData* RealityData = GetRealityData(StringId))
	{
		return RealityData->MakeTilesetAccess();
	}
	return {};
}

bool AITwinDigitalTwinManager::IsComponentLoaded(FString const& StringId) const
{
	return !StringId.IsEmpty() && LoadedObjects.Contains(StringId);
}

bool AITwinDigitalTwinManager::AreComponentSavedViewsLoaded(FString const& StringId) const
{
	AITwinIModel* IModel = GetIModel(StringId);
	return IModel? IModel->AreSavedViewsLoaded() : false;
}

bool AITwinDigitalTwinManager::IsComponentBeingLoaded(FString const& StringId) const
{
	return !StringId.IsEmpty() && LoadedObjects.Contains(StringId) && !CompletedLoadIds.Contains(StringId);
}

EITwinLoadContext AITwinDigitalTwinManager::GetComponentLoadContext(FString const& StringId) const
{
	if (const EITwinLoadContext* LoadContext = Impl->ComponentLoadContextMap.Find(StringId))
	{
		return *LoadContext;
	}
	return EITwinLoadContext::Unknown;
}

void AITwinDigitalTwinManager::SetIsLoadingScene(bool bIsLoading)
{
	// Broadcast the change of state if any.
	FLoadingScope InfoChangedScope(*this);

	bIsLoadingScene = bIsLoading;
}

void AITwinDigitalTwinManager::RemoveComponent(FString const& StringId)
{
	if (!LoadedObjects.Contains(StringId))
		return;

	auto Comp = *(LoadedObjects.Find(StringId));
	if (Comp)
	{
		auto Type = Cast<AITwinIModel>(Comp) ? EITwinModelType::IModel
			: (Cast<AITwinRealityData>(Comp) ? EITwinModelType::RealityData : EITwinModelType::Invalid);
		if (EITwinModelType::Invalid != Type)
			ComponentWillBeRemovedEvent.Broadcast(Comp, Type);
	}
	LoadedObjects.Remove(StringId);
	CompletedLoadIds.Remove(StringId);
	if (auto iModel = Cast<AITwinIModel>(Comp))
	{
		if (ActiveModelId == StringId)
		{
			// If the removed model was active, reset the active model. If any other model is loaded, set the
			// first one as active.
			ActiveModelId = {};
			for (auto const& [IModelId, _] : IModelsMap)
			{
				if (LoadedObjects.Contains(IModelId))
				{
					ensure(IModelId != StringId); // We have just removed StringId from LoadedObjects...
					ActiveModelId = IModelId;
					break;
				}
			}
		}
		Synchro4DSchedules.Remove(StringId);
		// My unit test world has none (see MainPanelTests.cpp), which led to a warning here: I tried to add a context
		// but it led to other errors, so I just skip destruction for the U.T.
		if (nullptr != GEngine->GetWorldContextFromWorld(GetWorld()))
			iModel->Destroy();
		ComponentRemovedEvent.Broadcast(StringId, EITwinModelType::IModel);

		SetLoadStatus(EITwinModelType::IModel, StringId, EITwinLayerLoadStatus::NotStarted);
	}
	else if (auto RealityData = Cast<AITwinRealityData>(Comp))
	{
		RealityData->Destroy();
		ComponentRemovedEvent.Broadcast(StringId, EITwinModelType::RealityData);

		SetLoadStatus(EITwinModelType::RealityData, StringId, EITwinLayerLoadStatus::NotStarted);
	}
}

bool AITwinDigitalTwinManager::IsIModel(FString const& StringId) const
{
	return IModelsMap.Find(StringId) != nullptr;
}

bool AITwinDigitalTwinManager::IsRealityData(FString const& StringId) const
{
	return RealityDataMap.Find(StringId) != nullptr;
}

bool AITwinDigitalTwinManager::HasLoadingPending(bool bLogState /*= false*/) const
{
	if (bLogState)
	{
		if (!PendingLoadIds.IsEmpty())
		{
			BE_LOGI("ITwinAdvViz", "iTwin components still pending: " << PendingLoadIds.Num());
			for(const auto& PendingId : PendingLoadIds)
			{
				BE_LOGI("ITwinAdvViz", " - " << TCHAR_TO_UTF8(*PendingId.Key));
			}
		}
		if (LoadedObjects.Num() != CompletedLoadIds.Num())
		{
			BE_LOGI("ITwinAdvViz", "Models loaded: " << CompletedLoadIds.Num() << "/" << LoadedObjects.Num());
			BE_LOGI("ITwinAdvViz", "Loaded :");
			for (const auto& PendingId : LoadedObjects)
			{
				BE_LOGI("ITwinAdvViz", " - " << TCHAR_TO_UTF8(*PendingId.Key));
			}
			BE_LOGI("ITwinAdvViz", "Completed :");
			for (const auto& PendingId : CompletedLoadIds)
			{
				BE_LOGI("ITwinAdvViz", " - " << TCHAR_TO_UTF8(*PendingId));
			}
		}
	}

	return !PendingLoadIds.IsEmpty() || LoadedObjects.Num() != CompletedLoadIds.Num();
}

TSet<FString> AITwinDigitalTwinManager::GetLoadedLayers(EITwinModelType LayerType, bool bOnlyCountFullyLoadedLayers) const
{
	TSet<FString> LoadedLayers;

	auto const IsOfLayerType = [this](FString const& StringId, EITwinModelType LayerType) -> bool
	{
		switch (LayerType)
		{
		case EITwinModelType::IModel:
			return IsIModel(StringId);
		case EITwinModelType::RealityData:
			return IsRealityData(StringId);
		default:
			BE_ISSUE("unhandled layer type", static_cast<int>(LayerType));
			return false;
		}
	};

	for (auto const& [StringId, _] : LoadedObjects)
	{
		if (IsOfLayerType(StringId, LayerType))
		{
			if (!bOnlyCountFullyLoadedLayers || CompletedLoadIds.Contains(StringId))
				LoadedLayers.Add(StringId);
		}
	}
	return LoadedLayers;
}

void AITwinDigitalTwinManager::OnSceneLoaded(bool bSuccess)
{
	FLoadingScope LoadingScope(*this);

	SetIsLoadingScene(false);

	// We load the scene first so no geolocation should be set before
	if (ensure(!Impl->bGeoLocationSet))
	{
		Impl->HandleGeolocation(nullptr, EITwinModelType::GlobalMapLayer, ITwin::GetGoogleLayerId());
	}

	if (bSuccess)
	{
		Impl->bHasLoadedLayer = true;

		AITwinDecorationHelper* DecoHelper = Impl->FindDecorationHelper();
		if (ensureMsgf(DecoHelper, TEXT("Scene loaded, but no Decoration Helper?")))
		{
			for (auto link : DecoHelper->GetLinkedElements())
			{
				if (link.second.IsEmpty())
					continue;
				// Load iModel / reality data attached to this scene.
				this->LoadComponent(link.second, EITwinLoadContext::Scene);
			}
		}
	}

	SceneLoadedEvent.Broadcast(bSuccess);
}

void AITwinDigitalTwinManager::SetAutoLoadAllComponents(bool bInAutoLoadAllComponents)
{
	bAutoLoadAllComponents = bInAutoLoadAllComponents;
	if (bAutoLoadAllComponents)
	{
		// If the content of the iTwin is already known, load all missing components.
		// Note that #LoadComponent already tests if the model is loaded:
		for (auto const& [StringId, _] : IModelsMap)
			LoadComponent(StringId, EITwinLoadContext::Single);
		for (auto const& [StringId, _] : RealityDataMap)
			LoadComponent(StringId, EITwinLoadContext::Single);
	}
}

void AITwinDigitalTwinManager::LoadDecoration()
{
	if (ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinID is required to load decoration");
		return;
	}

	if (Impl->PendingOperation != FImpl::EOperationUponAuth::None)
	{
		BE_LOGW("ITwinAPI", "An update operation is already pending - cannot stack updates");
		return;
	}

	// If no access token has been retrieved yet, make sure we request one.
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::LoadDecoration;
		return;
	}
	ITwin::LoadScene(ITwinId, GetWorld());
}

void AITwinDigitalTwinManager::SaveDecoration()
{
	ITwin::SaveScene(ITwinId, GetWorld());
}

#if WITH_EDITOR
void AITwinDigitalTwinManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName const PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	FName const MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, ServerConnection) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, ITwinId))
	{
		ResetITwin();
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, bAutoLoadAllComponents))
	{
		SetAutoLoadAllComponents(bAutoLoadAllComponents);
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, ITwinId)
		&& !ITwinId.IsEmpty()
		&& !bAutoLoadAllComponents)
	{
		UpdateITwin();
	}

	auto const ModifyLoadStatus = [this](TMap<FString, FITwinLoadableLayerHelper> const& StatusMap)
	{
		// Find which iModel should be loaded/unloaded:
		for (auto const& [_, LoadableLayerHelper] : StatusMap)
		{
			if (LoadableLayerHelper.LoadableLayer)
			{
				if (LoadableLayerHelper.bLoad && LoadableLayerHelper.LoadStatus != EITwinLayerLoadStatus::Complete)
				{
					LoadableLayerHelper.LoadableLayer->Load();
				}
				else if (!LoadableLayerHelper.bLoad && LoadableLayerHelper.LoadStatus == EITwinLayerLoadStatus::Complete)
				{
					LoadableLayerHelper.LoadableLayer->Remove();
				}
			}
		}
	};
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, IModelLoadStatusMap)
		&& PropertyName == GET_MEMBER_NAME_CHECKED(FITwinLoadableLayerHelper, bLoad))
	{
		// Find which iModel should be loaded/unloaded:
		ModifyLoadStatus(IModelLoadStatusMap);
	}
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, RealityDataLoadStatusMap)
		&& PropertyName == GET_MEMBER_NAME_CHECKED(FITwinLoadableLayerHelper, bLoad))
	{
		// Find which reality data should be loaded/unloaded:
		ModifyLoadStatus(RealityDataLoadStatusMap);
	}
}
#endif // WITH_EDITOR
