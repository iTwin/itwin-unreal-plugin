/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/ITwinDecorationHelper.h>
#include <Engine/StaticMesh.h>
#include <Decoration/DecorationAsyncIOHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>

#include <Engine/Engine.h>
#include <Containers/Ticker.h>
#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/GameViewportClient.h>

#include <ITwinCesium3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>
#include <Population/ITwinPopulation.h>

#include <Misc/MessageDialog.h>
#include <Materials/MaterialInstanceDynamic.h>

#include <Async/Async.h>
#include <Misc/MessageDialog.h>
#include <atomic>
#include <chrono>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/MaterialPersistence.h"
#	include "SDK/Core/ITwinAPI/ITwinAuthManager.h"
#	include <BeHeaders/Compil/CleanUpGuard.h>
#   include "SDK/Core/Visualization/ScenePersistence.h"
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	AITwinIModel* GetIModelByID(FString const& IModelID, const UWorld* World)
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			if ((**IModelIter).IModelId == IModelID)
				return *IModelIter;
		}
		return nullptr;
	}

	inline AITwinDecorationHelper* GetDecorationHelper(FITwinLoadInfo const& Info, UWorld const* World)
	{
		if (!World)
		{
			BE_ISSUE("no world given");
			return nullptr;
		}
		// For now, decoration is defined at the iTwin level. Look if a helper already exists for the given
		// iTwin:
		for (TActorIterator<AITwinDecorationHelper> DecoIter(World); DecoIter; ++DecoIter)
		{
			if ((**DecoIter).GetLoadedITwinInfo().ITwinId == Info.ITwinId)
			{
				return *DecoIter;
			}
		}
		return nullptr;
	}

	bool ShouldLoadDecoration(FITwinLoadInfo const& Info, UWorld const* World)
	{
		if (Info.ITwinId.IsEmpty())
		{
			// We cannot load a decoration without the iTwin ID...
			return false;
		}

		// Test if the iTwin scope is sufficient to access the decoration service.
		static bool bHasDecoScope = false;
		static bool bHasCheckedScope = false;
		if (!bHasCheckedScope)
		{
			UITwinDecorationServiceSettings const* DecoSettings = GetDefault<UITwinDecorationServiceSettings>();
			if (ensure(DecoSettings) && !DecoSettings->ExtraITwinScope.IsEmpty())
			{
				bHasDecoScope = SDK::Core::ITwinAuthManager::HasScope(
					TCHAR_TO_UTF8(*DecoSettings->ExtraITwinScope));
			}
			bHasCheckedScope = true;
		}
		if (!bHasDecoScope)
		{
			return false;
		}

		// If a decoration helper already exists for this iTwin, consider that the loading is already in
		// progress, or will be started from another path.
		AITwinDecorationHelper const* DecoHelper = GetDecorationHelper(Info, World);
		return(DecoHelper == nullptr);
	}

	void LoadDecoration(FITwinLoadInfo const& Info, UWorld* World)
	{
		if (!World)
		{
			BE_ISSUE("no world given");
			return;
		}
		AITwinDecorationHelper* DecoHelper = GetDecorationHelper(Info, World);
		if (DecoHelper == nullptr)
		{
			// Instantiate the decoration helper now:
			DecoHelper = World->SpawnActor<AITwinDecorationHelper>();
			DecoHelper->SetLoadedITwinInfo(Info);
		}
		DecoHelper->LoadDecoration();
	}

	void SaveDecoration(FITwinLoadInfo const& Info, UWorld const* World)
	{
		AITwinDecorationHelper* DecoHelper = GetDecorationHelper(Info, World);
		if (DecoHelper)
		{
			DecoHelper->SaveDecoration(false /*bPromptUser*/);
		}
	}
}

/* -------------------------- class AITwinDecorationHelper::FImpl ---------------------------*/

/// Hold implementation details for asynchronous tasks regarding the decoration service.
class AITwinDecorationHelper::FImpl
{
public:
	enum class EAsyncTask : uint8_t
	{
		None,
		LoadMaterials,
		LoadPopulations,
		SaveDecoration,
		LoadScenes
	};

	FImpl(AITwinDecorationHelper& InOwner);
	~FImpl();

	EAsyncTask GetAsyncTask() const { return CurrentAsyncTask.load(); }
	bool IsRunningAsyncTask(EAsyncTask TaskType) const;

	void StartLoadingDecoration(const UObject* WorldContextObject);

	struct SaveRequestOptions
	{
		bool bUponExit = false;
		bool bUponCustomMaterialsDeletion = false;
		bool bPromptUser = true;
	};
	void SaveDecoration(SaveRequestOptions const& opts);

	// Ask confirmation if the task is taking too long - return true if the user confirmed the abortion.
	bool ShouldAbort();

private:
	void AsyncLoadMaterials();
	void AsyncLoadScene();
	void ResetTicker();

	// Some tasks such as custom material loading would preferably be waited for (to avoid an additional
	// re-tuning), but this should not penalize the launching of the application.
	// Use SecondsToWaitBeforeAbort for this purpose.
	template <typename TaskFunc>
	void StartAsyncTask(EAsyncTask TaskType, TaskFunc&& TaskToRun, FString const& InConfirmAbortMsg = {});

	enum class ETaskExitStatus : uint8_t
	{
		Completed,
		Aborted
	};
	void OnAsyncTaskDone_GameThread(ETaskExitStatus TaskExitStatus, bool bSuccess);

private:
	std::atomic<EAsyncTask> CurrentAsyncTask = EAsyncTask::None;
	std::shared_ptr<std::atomic_bool> IsThisValid = std::make_shared<std::atomic_bool>(true);
	std::atomic_bool CurrentAsyncTaskDone = false;
	std::atomic_bool CurrentAsyncTaskResult = false;
	FTSTicker::FDelegateHandle TickerDelegate;
	std::chrono::system_clock::time_point NextConfirmTime = std::chrono::system_clock::now();
	FString ConfirmAbortMsg;
	int ConfirmOccurrences = 0;
	bool bIsDisplayingConfirmMsg = false;
	AITwinDecorationHelper& Owner;
	bool bIsDeletingCustomMaterials = false;
};


AITwinDecorationHelper::FImpl::FImpl(AITwinDecorationHelper& InOwner)
	: Owner(InOwner)
{
}

AITwinDecorationHelper::FImpl::~FImpl()
{
	ResetTicker();
	*IsThisValid = false;
}

void AITwinDecorationHelper::FImpl::ResetTicker()
{
	if (TickerDelegate.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerDelegate);
		TickerDelegate.Reset();
	}
}

bool AITwinDecorationHelper::FImpl::ShouldAbort()
{
	if (!ConfirmAbortMsg.IsEmpty())
	{
		if (std::chrono::system_clock::now() > NextConfirmTime && !bIsDisplayingConfirmMsg)
		{
			Be::CleanUpGuard restoreGuard([this]
			{
				bIsDisplayingConfirmMsg = false;
			});
			bIsDisplayingConfirmMsg = true;

			if (FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
				FText::FromString(ConfirmAbortMsg),
				FText::FromString("")) == EAppReturnType::Yes)
			{
				CurrentAsyncTask = EAsyncTask::None;
				return true;
			}
			ConfirmOccurrences++;
			NextConfirmTime = std::chrono::system_clock::now() + std::chrono::seconds(ConfirmOccurrences * 30);
		}
	}
	return false;
}

bool AITwinDecorationHelper::FImpl::IsRunningAsyncTask(EAsyncTask TaskType) const
{
	return GetAsyncTask() == TaskType && !CurrentAsyncTaskDone;
}

template <typename TaskFunc>
void AITwinDecorationHelper::FImpl::StartAsyncTask(EAsyncTask TaskType, TaskFunc&& InTaskToRun,
	FString const& InConfirmAbortMsg /*= {}*/)
{
	if (CurrentAsyncTask == TaskType)
	{
		// A same operation is already in progress (it can be triggered at any time by the user through a
		// shortcut). Do not start several tasks...
		return;
	}
	ensureMsgf(CurrentAsyncTask == EAsyncTask::None, TEXT("Do not nest different async tasks"));

	ResetTicker();

	CurrentAsyncTask = TaskType;
	CurrentAsyncTaskDone = false;

	// NB: NextConfirmTime and ConfirmOccurrences are only relevant when a confirmation message is provided.
	// Currently, only when saving the decoration.
	NextConfirmTime = std::chrono::system_clock::now() + std::chrono::seconds(30);
	ConfirmOccurrences = 0;
	ConfirmAbortMsg = InConfirmAbortMsg;

	AsyncTask(ENamedThreads::Type::AnyBackgroundThreadNormalTask,
		[TaskToRun = Forward<TaskFunc>(InTaskToRun),
		this,
		IsValidLambda = this->IsThisValid]()
	{
		bool const bResult = TaskToRun();
		if (*IsValidLambda)
		{
			this->CurrentAsyncTaskResult = bResult;
			this->CurrentAsyncTaskDone = true;
		}
	});

	TickerDelegate = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[this,
			IsValidLambda = this->IsThisValid](float Delta) -> bool
	{
		if (!*IsValidLambda)
			return false;

		if (this->CurrentAsyncTaskDone)
		{
			OnAsyncTaskDone_GameThread(ETaskExitStatus::Completed, this->CurrentAsyncTaskResult.load());
			return false;
		}
		// Propose to abort if the task is taking too long
		if (this->ShouldAbort())
		{
			OnAsyncTaskDone_GameThread(ETaskExitStatus::Aborted, false);
			return false;
		}
		return true;
	}), 1.f /* tick once per second*/);
}

void AITwinDecorationHelper::FImpl::OnAsyncTaskDone_GameThread(ETaskExitStatus TaskExitStatus, bool bSuccess)
{
	EAsyncTask const TaskJustFinished = CurrentAsyncTask.load();
	CurrentAsyncTask = EAsyncTask::None;

	switch (TaskJustFinished)
	{
	case EAsyncTask::LoadMaterials:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			Owner.OnCustomMaterialsLoaded_GameThread(bSuccess);

			// After loading materials, start loading populations
			// Access token has to be retrieved in game thread.
			std::string const AccessToken = Owner.GetDecorationAccessToken();
			// The communication with the service is done in a separate thread.
			// Then the actual loading of populations in the game engine is performed in game thread.
			// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
			auto DecoIO = Owner.GetDecorationAsyncIOHelper();
			StartAsyncTask(EAsyncTask::LoadPopulations,
				[DecoIO, AccessToken]() { return DecoIO->LoadPopulationsFromServer(AccessToken); });
		}
		break;

	case EAsyncTask::LoadPopulations:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			Owner.LoadPopulationsInGame(bSuccess);
		}
		break;

	case EAsyncTask::LoadScenes:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			Owner.OnSceneLoad_GameThread(bSuccess);
			if (bSuccess)
			{
				// prevent save flags from being set during update of the UI
				Owner.decorationIO->scene->SetShoudlSave(false);

			}
			// Start the asynchronous loading of materials, then populations.
			AsyncLoadMaterials();
		}
		break;

	case EAsyncTask::SaveDecoration:
		{
			Owner.OnDecorationSaved_GameThread(bSuccess, bIsDeletingCustomMaterials);

			bIsDeletingCustomMaterials = false;
		}
		break;

	case EAsyncTask::None:
		break;
	}
}

namespace ITwinMsg
{
	static const FString LongDecoServerResponseTime = TEXT("The decoration service is taking a longer time to complete.\n");
	static const FString ConfirmAbortLoadDeco = TEXT("\nDo you want to load your model without any population/material customization?\n");
	static const FString ConfirmAbortSaveDeco = TEXT("\nDo you want to abort saving the modifications you made to your population/materials?\n");

	inline FString GetConfirmAbortLoadMsg() {
		return LongDecoServerResponseTime + ConfirmAbortLoadDeco;
	}
	inline FString GetConfirmAbortSaveMsg() {
		return LongDecoServerResponseTime + ConfirmAbortSaveDeco;
	}
}

void AITwinDecorationHelper::FImpl::StartLoadingDecoration(const UObject* WorldContextObject)
{
	auto DecoIO = Owner.GetDecorationAsyncIOHelper();
	DecoIO->InitDecorationService(WorldContextObject);

	// Detect the case of baked presentations: disable persistence of iModel offset & geo-location in this
	// case (avoid confusion as those levels already contain baked offsets & geo-location).
	auto const& PersitenceMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (ensure(PersitenceMngr))
	{
		TArray<AActor*> TilesetActors;
		UGameplayStatics::GetAllActorsOfClass(WorldContextObject, AITwinCesium3DTileset::StaticClass(), TilesetActors);
		if (TilesetActors.Num() > 0)
		{
			PersitenceMngr->EnableOffsetAndGeoLocation(false);
		}
	}

	// Start the asynchronous loading of Scene then, materials, then populations.
	AsyncLoadScene();
}

void AITwinDecorationHelper::FImpl::AsyncLoadMaterials()
{
	// Access token has to be retrieved in game thread.
	std::string const AccessToken = Owner.GetDecorationAccessToken();

	// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
	auto DecoIO = Owner.GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::LoadMaterials,
		[DecoIO, AccessToken]() { return DecoIO->LoadCustomMaterials(AccessToken); });
}
void AITwinDecorationHelper::FImpl::AsyncLoadScene()
{
	// Access token has to be retrieved in game thread.
	std::string const AccessToken = Owner.GetDecorationAccessToken();

	// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
	auto DecoIO = Owner.GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::LoadScenes,
		[DecoIO, AccessToken]() { return DecoIO->LoadSceneFromServer(AccessToken); });

}

void AITwinDecorationHelper::FImpl::SaveDecoration(SaveRequestOptions const& opts)
{
	if (!Owner.ShouldSaveDecoration(opts.bPromptUser))
		return;

	bIsDeletingCustomMaterials = opts.bUponCustomMaterialsDeletion;

	// Access token has to be retrieved in game thread.
	std::string const AccessToken = Owner.GetDecorationAccessToken();
	auto DecoIO = Owner.GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::SaveDecoration,
		[DecoIO, AccessToken]() {  
			 bool err1 = DecoIO->SaveDecorationToServer(AccessToken);
			 bool err2 = DecoIO->SaveSceneToServer(AccessToken);
			 return err1 && err2;
		},
		ITwinMsg::GetConfirmAbortSaveMsg());

	if (opts.bUponExit)
	{
		// Here we must wait until the saving is done or aborted by user (if we let the level end, the
		// saving operation may not be terminated, and thus, potentially lost...)
		// Note that no ticker will work at this stage, so we test termination in a basic loop:
		int ElapsedSec = 0;
		while (IsRunningAsyncTask(FImpl::EAsyncTask::SaveDecoration)
			&& !ShouldAbort()
			&& ElapsedSec < 300)
		{
			FPlatformProcess::Sleep(1.f);
			ElapsedSec++;
		}
	}
}


/* --------------------------- class AITwinDecorationHelper ---------------------------------*/

AITwinDecorationHelper::AITwinDecorationHelper()
	: Impl(MakePimpl<FImpl>(*this))
{
	decorationIO = std::make_shared<FDecorationAsyncIOHelper>();
}

void AITwinDecorationHelper::SetLoadedITwinInfo(FITwinLoadInfo InLoadedITwinInfo)
{
	decorationIO->SetLoadedITwinInfo(InLoadedITwinInfo);

	// Initialize decoration service asap (important for presentations, typically: the material persistence
	// manager should be instantiated *before* the IModel starts to load the tileset...)
	InitDecorationService();
}

FITwinLoadInfo AITwinDecorationHelper::GetLoadedITwinInfo() const
{
	return decorationIO->GetLoadedITwinInfo();
}


std::string AITwinDecorationHelper::GetDecorationAccessToken() const
{
	// We test AITwinServerConnection here, considering that there is only one active instance in most cases.
	// This should be improved if we allow mixing QA and Prod iTwins in a same session, which is
	// theoretically already possible in the plugin... TODO_JDE
	FString Token;
	AITwinServerConnection* serverConnection = Cast<AITwinServerConnection>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AITwinServerConnection::StaticClass()));
	if (serverConnection && serverConnection->HasAccessToken())
	{
		Token = serverConnection->GetAccessToken();
	}
	return TCHAR_TO_UTF8(*Token);
}

void AITwinDecorationHelper::InitDecorationService()
{
	decorationIO->InitDecorationService(GetWorld());
}

std::shared_ptr<FDecorationAsyncIOHelper> AITwinDecorationHelper::GetDecorationAsyncIOHelper() const
{
	checkSlow(decorationIO->IsInitialized());
	return decorationIO;
}

void AITwinDecorationHelper::LoadPopulationsInGame(bool bHasLoadedPopulations)
{
	checkSlow(IsInGameThread());
	auto& instancesManager(decorationIO->instancesManager);
	if (!instancesManager)
		return;

	// For now there is only one group of instances
	const SDK::Core::SharedInstGroupVect& instGroups = instancesManager->GetInstancesGroups();
	if (!instGroups.empty())
	{
		decorationIO->instancesGroup = instGroups[0];
	}

	if (!(GEngine && GEngine->GameViewport))
	{
		BE_LOGW("ITwinDecoration", "Populations cannot be loaded in Editor");
		return;
	}

	// Add a population for each object reference
	std::vector<std::string> objReferences = instancesManager->GetObjectReferences();
	for (const auto& objRef : objReferences)
	{
		AITwinPopulation* population = GetOrCreatePopulation(FString(objRef.c_str()));
		if (population)
		{
			population->UpdateInstancesFromSDKCoreToUE();
		}
	}
	bPopulationEnabled = true;

	OnPopulationsLoaded.Broadcast(true);
}

bool AITwinDecorationHelper::HasITwinID() const
{
	return !decorationIO->LoadedITwinInfo.ITwinId.IsEmpty();
}

void AITwinDecorationHelper::LoadDecoration()
{
	if (!ensure(HasITwinID()))
	{
		return;
	}
	// This will start the asynchronous loading of materials, populations...
	Impl->StartLoadingDecoration(GetWorld());
}

void AITwinDecorationHelper::OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials)
{
	checkSlow(IsInGameThread());

	// Materials were now loaded from the decoration service. If the tileset has already been loaded, we
	// may have to re-tune & refresh it depending on custom material definitions.
	if (bHasLoadedMaterials && decorationIO->materialPersistenceMngr)
	{
		// We may have loaded material definitions for several iModel.
		std::vector<std::string> iModelIds;
		decorationIO->materialPersistenceMngr->ListIModelsWithMaterialSettings(iModelIds);
		for (std::string const& imodelid : iModelIds)
		{
			AITwinIModel* IModel = ITwin::GetIModelByID(UTF8_TO_TCHAR(imodelid.c_str()), GetWorld());
			if (IModel)
			{
				IModel->DetectCustomizedMaterials();

				// We now also retrieve the potential iModel offset and geo-location for the Google tileset
				// from some specific materials (quick & dirtiest thing ever...)

				std::array<double, 3> posOffset = { 0., 0., 0. };
				std::array<double, 3> rotOffset = { 0., 0., 0. };
				if (decorationIO->materialPersistenceMngr->GetModelOffset(imodelid, posOffset, rotOffset))
				{
					FVector Pos((float)posOffset[0], (float)posOffset[1], (float)posOffset[2]);
					FVector Rot((float)rotOffset[0], (float)rotOffset[1], (float)rotOffset[2]);
					IModel->SetModelOffset(Pos, Rot, AITwinIModel::EOffsetContext::Reload);
				}
			}
		}
		// Also update the Google tileset if needed (note that it can be instantiated *before* the materials
		// are loaded asynchronously...
		OnMaterialsLoaded.Broadcast(true);
	}

	bMaterialEditionEnabled = true;
}

bool AITwinDecorationHelper::ShouldSaveDecoration(bool bPromptUser /*= true*/) const
{
	if (!HasITwinID() || !decorationIO->decoration)
	{
		return false;
	}
	std::string const accessToken = GetDecorationAccessToken();
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}

	bool const saveInstances = decorationIO->instancesManager
		&& decorationIO->instancesManager->HasInstancesToSave();
	bool const saveMaterials = decorationIO->materialPersistenceMngr
		&& decorationIO->materialPersistenceMngr->NeedUpdateDB();
	bool const saveScenes = decorationIO->scene->ShoudlSave();
	if (!saveInstances && !saveMaterials && !saveScenes)
		return false;

	if (bPromptUser &&
		FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
			FText::FromString("Do you want to save the decoration?"),
			FText::FromString("")) != EAppReturnType::Yes)
	{
		return false;
	}
	return true;
}

void AITwinDecorationHelper::SaveDecoration(bool bPromptUser /*= true*/)
{
	Impl->SaveDecoration(FImpl::SaveRequestOptions{ .bPromptUser = bPromptUser });
}

void AITwinDecorationHelper::SaveDecorationOnExit()
{
	Impl->SaveDecoration(FImpl::SaveRequestOptions{ .bUponExit = true });
}

void AITwinDecorationHelper::OnDecorationSaved_GameThread(bool bSaved, bool bHasResetMaterials)
{
	OnDecorationSaved.Broadcast(bSaved);

	if (bSaved && bHasResetMaterials)
	{
		// Now that material definitions have been reset, update the iModel
		AITwinIModel* IModel = ITwin::GetIModelByID(GetLoadedITwinInfo().IModelId, GetWorld());
		if (IModel)
		{
			IModel->ReloadCustomizedMaterials();
		}
	}
}

void AITwinDecorationHelper::OnSceneLoad_GameThread(bool bSuccess)
{
	OnnSceneLoaded.Broadcast(bSuccess);
}

AITwinPopulation* AITwinDecorationHelper::GetOrCreatePopulation(FString assetPath)
{
	TArray<AActor*> populations;
    UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), AITwinPopulation::StaticClass(), populations);

	std::string stdAssetPath = TCHAR_TO_UTF8(*assetPath);

    for (AActor* actor : populations)
    {
		AITwinPopulation* pop = Cast<AITwinPopulation>(actor);
		if (pop->GetObjectRef() == stdAssetPath)
		{
			return pop;
		}
	}
	// Spawn a new actor with a deferred call in order to be able
	// to set the static mesh before BeginPlay is called.
	FTransform spawnTransform;
	AActor* newActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
		this, AITwinPopulation::StaticClass(), spawnTransform,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	AITwinPopulation* population = Cast<AITwinPopulation>(newActor);

	if (!population)
	{
		return nullptr;
	}

	population->InitFoliageMeshComponent();

	UStaticMesh* mesh = LoadObject<UStaticMesh>(nullptr, *assetPath);

	if (mesh)
	{
		population->mesh = mesh;
		for (int32 i = 0; i < mesh->GetStaticMaterials().Num(); ++i)
		{
			population->meshComp->SetMaterial(i, mesh->GetMaterial(i));
		}
	}

	UGameplayStatics::FinishSpawningActor(newActor, spawnTransform);

	population->SetInstancesManager(decorationIO->instancesManager);
	population->SetInstancesGroup(decorationIO->instancesGroup);
	population->SetObjectRef(stdAssetPath);

	return population;
}

SDK::Core::ITwinAtmosphereSettings AITwinDecorationHelper::GetAtmosphereSettings() const
{
	return decorationIO->scene->GetAtmosphere();
}

void AITwinDecorationHelper::SetAtmosphereSettings(const SDK::Core::ITwinAtmosphereSettings& as) const
{
	if (decorationIO && decorationIO->scene)
		 decorationIO->scene->SetAtmosphere(as);
}

//std::shared_ptr<SDK::Core::IScenePersistence> AITwinDecorationHelper::GetScene() const
//{
//	if (decorationIO)
//		return decorationIO->scene;
//	else return std::shared_ptr<SDK::Core::IScenePersistence>();
//
//}

void AITwinDecorationHelper::BeginPlay()
{
	// Add callback to propose to save upon closing
	if (ensure(GEngine && GEngine->GameViewport))
	{
		GEngine->GameViewport->OnCloseRequested().AddUObject(this, &AITwinDecorationHelper::OnCloseRequested);
	}
}

void AITwinDecorationHelper::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	decorationIO->RequestStop();
}


void AITwinDecorationHelper::BeforeCloseLevel()
{
	SaveDecorationOnExit();
}

void AITwinDecorationHelper::OnCloseRequested(FViewport*)
{
	BeforeCloseLevel();
}

void AITwinDecorationHelper::DeleteAllCustomMaterials()
{
	if (!IsMaterialEditionEnabled())
		return;
	if (!(decorationIO && decorationIO->materialPersistenceMngr))
		return;
	auto const LoadedInfo = GetLoadedITwinInfo();
	if (LoadedInfo.IModelId.IsEmpty())
		return;

	if (FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
		FText::FromString(
			"Are you sure you want to reset all material definitions to default for current model?" \
			"\n\nBeware it will have an impact to all users sharing this iModel, and that it cannot be undone!"),
		FText::FromString("")) == EAppReturnType::Yes)
	{
		const std::string imodelId = TCHAR_TO_UTF8(*LoadedInfo.IModelId);
		decorationIO->materialPersistenceMngr->RequestDeleteIModelMaterialsInDB(imodelId);

		// Propose to save at once (with a specific flag set to perform refresh at the end).
		Impl->SaveDecoration(FImpl::SaveRequestOptions{ .bUponCustomMaterialsDeletion = true });
	}
}

#if WITH_EDITOR

// Console command to reset all custom materials in current iModel
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinResetCustomMaterialDefinitions(
	TEXT("cmd.ITwin_ResetCustomMaterialDefinitions"),
	TEXT("Reset all custom material definitions for current iModel."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TActorIterator<AITwinDecorationHelper> DecoHelperIter(World); DecoHelperIter; ++DecoHelperIter)
	{
		(**DecoHelperIter).DeleteAllCustomMaterials();
	}
})
);

#endif // WITH_EDITOR
