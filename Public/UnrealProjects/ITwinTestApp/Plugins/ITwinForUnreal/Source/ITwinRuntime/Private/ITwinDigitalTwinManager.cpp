/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwinManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinDigitalTwinManager.h"

#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinTilesetAccess.h>
#include <ITwinWebServices/ITwinWebServices.h>

#include <Components/DirectionalLightComponent.h>
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

	// Some operations require to first fetch a valid authorization token
	enum class EOperationUponAuth : uint8
	{
		None,
		Update,
		LoadDecoration
	};
	EOperationUponAuth PendingOperation = EOperationUponAuth::None;
};

AITwinDigitalTwinManager::FImpl::FImpl(AITwinDigitalTwinManager& InOwner)
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


AITwinDigitalTwinManager::AITwinDigitalTwinManager()
	: Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	//PrimaryActorTick.bCanEverTick = true;
}

void AITwinDigitalTwinManager::ResetITwin()
{
	IModelsMap.Empty();
	RealityDataMap.Empty();
	LoadedObjects.Empty();

	const auto ChildrenCopy = Children;
	for (auto& Child : ChildrenCopy)
		GetWorld()->DestroyActor(Child);
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

void AITwinDigitalTwinManager::Init(FString const& InITwinId, FString const& InDisplayName)
{
	ITwinId = InITwinId;
	DisplayName = InDisplayName;
	{
		BeUtils::WLock WLock(Impl->ITwinInfoMutex);
		Impl->ITwinInfoRequestStatus = FImpl::EITwinRequestStatus::NotStarted;
		Impl->bHasLoggedGeolocInfo = false;
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
			IModel->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
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

void AITwinDigitalTwinManager::LoadIModel(FIModelInfo Info, EITwinLoadContext LoadContext)
{
	ensureMsgf(LoadContext != EITwinLoadContext::Unknown, TEXT("always specify a context!"));

	BE_LOGV("ITwinAdvViz", "ITwinManager: Loading iModel " << TCHAR_TO_ANSI(*Info.DisplayName)
		<< " with ID " << TCHAR_TO_ANSI(*Info.Id));

	FLoadingScope LoadingScope(*this);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	auto IModel = GetWorld()->SpawnActor<AITwinIModel>(SpawnParams);
	LoadedObjects.Add(Info.Id, IModel);
	// Automatic + latest = necessary for StartExport to be called when no export available
	IModel->LoadingMethod = ELoadingMethod::LM_Automatic;
	IModel->ChangesetId = TEXT("latest");
	IModel->OnIModelLoaded.AddDynamic(this, &AITwinDigitalTwinManager::OnIModelLoaded);
	IModel->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
#if WITH_EDITOR
	IModel->SetActorLabel(Info.DisplayName);
#endif
	IModel->ServerConnection = ServerConnection;
	IModel->IModelId = Info.Id;
	IModel->ITwinId = ITwinId;
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

void AITwinDigitalTwinManager::LoadRealityData(FITwinRealityData3DInfo Info, EITwinLoadContext /*LoadContext*/)
{
	BE_LOGV("ITwinAdvViz", "ITwinManager: Loading RealityData " << TCHAR_TO_ANSI(*Info.DisplayName)
		<< " with ID " << TCHAR_TO_ANSI(*Info.Id));

	FLoadingScope LoadingScope(*this);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	auto RealityData = GetWorld()->SpawnActor<AITwinRealityData>(SpawnParams);
	LoadedObjects.Add(Info.Id, RealityData);
#if WITH_EDITOR
	RealityData->SetActorLabel(Info.DisplayName);
#endif
	RealityData->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
	RealityData->ServerConnection = ServerConnection;
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
		RealityData->OnRealityData3DInfoRetrieved(/*bSuccess*/true, Info);
	}
}

void AITwinDigitalTwinManager::OnRealityDataInfoLoaded(bool bSuccess, FString StringId)
{
	AITwinRealityData* AsRealityData = nullptr;
	if (bSuccess && LoadedObjects.Contains(StringId)
		&& (AsRealityData = Cast<AITwinRealityData>(LoadedObjects[StringId])) != nullptr)
	{
		if (ActiveModelId.IsEmpty())
			ActiveModelId = StringId;
		// Now that RealityData information is known, we can broadcast the event (to handle geo-location,
		// typically).
		CompletedLoadIds.Add(StringId);
		ComponentLoadedEvent.Broadcast(LoadedObjects[StringId]);
	}
}

void AITwinDigitalTwinManager::LoadComponent(FString const& StringId, EITwinLoadContext LoadContext)
{
	//no need to load anything if it's already (being) loaded
	if (IsComponentLoaded(StringId) || IsComponentBeingLoaded(StringId))
		return;
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
		AsIModel->GetAttachedRealityDataIds().Then([this](const auto& RealityDataIdsFuture)
			{
				for (const auto& RealityDataId: RealityDataIdsFuture.Get())
				{
					LoadComponent(RealityDataId, EITwinLoadContext::Unknown);
				}
			});
		ComponentLoadedEvent.Broadcast(LoadedObjects[StringId]);
	}
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

void AITwinDigitalTwinManager::SetIsLoadingScene(bool bIsLoading)
{
	// Broadcast the change of state if any.
	FLoadingScope InfoChangedScope(*this);

	bIsLoadingScene = bIsLoading;
}

void AITwinDigitalTwinManager::OnSceneLoadingStartStop(bool bStart)
{
	SetIsLoadingScene(bStart);
}

void AITwinDigitalTwinManager::RemoveComponent(FString const& StringId)
{
	if (!LoadedObjects.Contains(StringId))
		return;

	auto Comp = *(LoadedObjects.Find(StringId));
	LoadedObjects.Remove(StringId);
	CompletedLoadIds.Remove(StringId);
	if (auto iModel = Cast<AITwinIModel>(Comp))
	{
		if (ActiveModelId == StringId)
		{
			ActiveModelId = {};
			for (auto const& [_, Info] : IModelsMap)
			{
				if (LoadedObjects.Contains(Info.Id))
				{
					ActiveModelId = Info.Id;
					break;
				}
			}
		}
		Synchro4DSchedules.Remove(StringId);
		iModel->Destroy();
		ComponentRemovedEvent.Broadcast(StringId, EITwinModelType::IModel);
	}
	else if (auto RealityData = Cast<AITwinRealityData>(Comp))
	{
		RealityData->Destroy();
		ComponentRemovedEvent.Broadcast(StringId, EITwinModelType::RealityData);
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
		}
		if (LoadedObjects.Num() != CompletedLoadIds.Num())
		{
			BE_LOGI("ITwinAdvViz", "Models loaded: " << CompletedLoadIds.Num() << "/" << LoadedObjects.Num());
		}
	}

	return !PendingLoadIds.IsEmpty() || LoadedObjects.Num() != CompletedLoadIds.Num();
}

TSet<FString> AITwinDigitalTwinManager::GetLoadedIModels(bool bOnlyCountFullyLoadedModels) const
{
	TSet<FString> LoadedIModels;
	for (auto const& [StringId, _] : LoadedObjects)
	{
		if (IsIModel(StringId))
		{
			if (!bOnlyCountFullyLoadedModels || CompletedLoadIds.Contains(StringId))
				LoadedIModels.Add(StringId);
		}
	}
	return LoadedIModels;
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, ServerConnection) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, ITwinId))
	{
		ResetITwin();
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwinManager, bAutoLoadAllComponents))
	{
		SetAutoLoadAllComponents(bAutoLoadAllComponents);
	}
}
#endif // WITH_EDITOR
