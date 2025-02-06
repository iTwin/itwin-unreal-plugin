/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

#include <AnimTimeline/ITwinTimelineActor.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinServerConnection.h>
#include <Population/ITwinPopulation.h>

#include <Misc/MessageDialog.h>
#include <Materials/MaterialInstanceDynamic.h>

#include <Async/Async.h>
#include <atomic>
#include <chrono>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/MaterialPersistence.h"
#	include "SDK/Core/Visualization/Timeline.h"
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
			bHasDecoScope = SDK::Core::ITwinAuthManager::HasScope(ITWIN_DECORATIONS_SCOPE);
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

	std::string ConvertToStdString(const FString& fstring)
	{
		return (const char*)StringCast<UTF8CHAR>(*fstring).Get();
	}

	static ITwinSceneInfo LinkToSceneInfo(const SDK::Core::Link& l)
	{
		using namespace SDK::Core;

		ITwinSceneInfo s;
		if(l.HasQuality())
			s.Quality = l.GetQuality();
		if (l.HasVisibility())
			s.Visibility = l.GetVisibility();
		FMatrix dstMat;
		FVector dstPos;
		if (l.HasTransform())
		{
			s.Offset = FTransform();
			const dmat3x4& srcMat = l.GetTransform();
			for (unsigned i = 0; i < 3; ++i)
			{
				for (unsigned j = 0; j < 3; ++j)
				{
					dstMat.M[j][i] = ColRow3x4(srcMat, i, j);
				}
			}
			dstPos.X = ColRow3x4(srcMat, 0, 3);
			dstPos.Y = ColRow3x4(srcMat, 1, 3);
			dstPos.Z = ColRow3x4(srcMat, 2, 3);
			s.Offset->SetFromMatrix(dstMat);
			s.Offset->SetTranslation(dstPos);
		}
		return s;
	}

	static void SceneToLink(const ITwinSceneInfo& si, std::shared_ptr<SDK::Core::Link> l)
	{
		if (si.Visibility.has_value())
			l->SetVisibility(*si.Visibility);
		if (si.Quality.has_value())
			l->SetQuality(*si.Quality);
		if (si.Offset.has_value())
		{
			std::array<double, 12> dstTransform;
			FMatrix srcMat = si.Offset->ToMatrixWithScale();
			for (int32 i = 0; i < 3; ++i)
			{
				for (int32 j = 0; j < 3; ++j)
				{
					dstTransform[i*4+j] = srcMat.M[j][i];
				}
			}
			FVector srcPos = si.Offset->GetTranslation();
			dstTransform[3] = srcPos.X;
			dstTransform[7] = srcPos.Y;
			dstTransform[11] = srcPos.Z;
			l->SetTransform(dstTransform);
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

	bool IsPopulationEnabled() const { return bPopulationEnabled; }
	bool IsMaterialEditionEnabled() const { return bMaterialEditionEnabled; }

	EAsyncTask GetAsyncTask() const { return CurrentAsyncTask.load(); }
	bool IsRunningAsyncTask(EAsyncTask TaskType) const;
	bool IsRunningAsyncLoadTask() const;

	// Initialize the connection with the decoration service (if needed). This will not try trigger any
	// communication with the server.
	void InitDecorationService();

	void SetLoadedITwinInfo(FITwinLoadInfo const& LoadedSceneInfo);
	FITwinLoadInfo const& GetLoadedITwinInfo() const;
	bool HasITwinID() const;

	std::string GetDecorationAccessToken() const;

	void StartLoadingDecoration(const UObject* WorldContextObject);


	bool ShouldSaveDecoration(bool bPromptUser) const;

	struct SaveRequestOptions
	{
		bool bUponExit = false;
		bool bUponCustomMaterialsDeletion = false;
		bool bPromptUser = true;
	};
	void SaveDecoration(SaveRequestOptions const& opts);

	std::shared_ptr<SDK::Core::ITimeline> GetTimeline() const;

	void DeleteAllCustomMaterials();

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

	// This will share all data with DecorationIO.
	std::shared_ptr<FDecorationAsyncIOHelper> GetDecorationAsyncIOHelper() const;

	void LoadPopulationsInGame(bool bHasLoadedPopulations);

	void OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials);

	void OnDecorationSaved_GameThread(bool bSuccess, bool bHasResetMaterials);

	void OnSceneLoad_GameThread(bool bSuccess);


public:
	// For loading and saving
	std::shared_ptr<FDecorationAsyncIOHelper> DecorationIO;

	EITwinDecorationClientMode ClientMode = EITwinDecorationClientMode::Unknown;

private:
	// Initially, both Population and Material edition are disabled, until we have loaded the corresponding
	// information (which can be empty of course) from the decoration service.
	bool bPopulationEnabled = false;
	bool bMaterialEditionEnabled = false;

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
	DecorationIO = std::make_shared<FDecorationAsyncIOHelper>();
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

bool AITwinDecorationHelper::FImpl::IsRunningAsyncLoadTask() const
{
	auto const CurTask = GetAsyncTask();
	return (CurTask == EAsyncTask::LoadScenes
		|| CurTask == EAsyncTask::LoadMaterials
		|| CurTask == EAsyncTask::LoadPopulations)
		&& !CurrentAsyncTaskDone;
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
			OnCustomMaterialsLoaded_GameThread(bSuccess);

			// After loading materials, start loading populations
			// Access token has to be retrieved in game thread.
			std::string const AccessToken = GetDecorationAccessToken();
			// The communication with the service is done in a separate thread.
			// Then the actual loading of populations in the game engine is performed in game thread.
			// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
			auto DecoIO = GetDecorationAsyncIOHelper();
			StartAsyncTask(EAsyncTask::LoadPopulations,
				[DecoIO, AccessToken]() { return DecoIO->LoadPopulationsFromServer(AccessToken); });
		}
		break;

	case EAsyncTask::LoadPopulations:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			LoadPopulationsInGame(bSuccess);

			// The loading of decoration is now done.
			Owner.OnDecorationLoaded.Broadcast();
		}
		break;

	case EAsyncTask::LoadScenes:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			OnSceneLoad_GameThread(bSuccess);
			if (bSuccess)
			{
				// prevent save flags from being set during update of the UI
				DecorationIO->scene->SetShoudlSave(false);
			}
			// Start the asynchronous loading of materials, then populations.
			AsyncLoadMaterials();

			// Finish timeline initialization
			AITwinTimelineActor* tla = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinTimelineActor::StaticClass());
			if (tla)
				tla->OnLoad();
		}
		break;

	case EAsyncTask::SaveDecoration:
		{
			OnDecorationSaved_GameThread(bSuccess, bIsDeletingCustomMaterials);

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

void AITwinDecorationHelper::FImpl::InitDecorationService()
{
	DecorationIO->InitDecorationService(Owner.GetWorld());
}

void AITwinDecorationHelper::FImpl::SetLoadedITwinInfo(FITwinLoadInfo const& LoadedSceneInfo)
{
	DecorationIO->SetLoadedITwinInfo(LoadedSceneInfo);

	// Initialize decoration service asap (important for presentations, typically: the material persistence
	// manager should be instantiated *before* the IModel starts to load the tileset...)
	InitDecorationService();
}

FITwinLoadInfo const& AITwinDecorationHelper::FImpl::GetLoadedITwinInfo() const
{
	return DecorationIO->GetLoadedITwinInfo();
}

bool AITwinDecorationHelper::FImpl::HasITwinID() const
{
	return !DecorationIO->LoadedITwinInfo.ITwinId.IsEmpty();
}

std::shared_ptr<FDecorationAsyncIOHelper> AITwinDecorationHelper::FImpl::GetDecorationAsyncIOHelper() const
{
	checkSlow(DecorationIO->IsInitialized());
	return DecorationIO;
}

void AITwinDecorationHelper::FImpl::StartLoadingDecoration(const UObject* WorldContextObject)
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	DecoIO->InitDecorationService(WorldContextObject);

	// Start the asynchronous loading of Scene then, materials, then populations.
	AsyncLoadScene();
}

void AITwinDecorationHelper::FImpl::AsyncLoadMaterials()
{
	// Access token has to be retrieved in game thread.
	std::string const AccessToken = GetDecorationAccessToken();

	// Gather all iModels in the scene (must be done in game thread.
	TMap<FString, TWeakObjectPtr<AITwinIModel>> idToIModel;
	for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
	{
		idToIModel.Emplace((**IModelIter).IModelId, *IModelIter);
	}

	// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
	auto DecoIO = GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::LoadMaterials,
		[DecoIO, AccessToken, idToIModel]() { return DecoIO->LoadCustomMaterials(AccessToken, idToIModel); });
}

void AITwinDecorationHelper::FImpl::AsyncLoadScene()
{
	// Access token has to be retrieved in game thread.
	std::string const AccessToken = GetDecorationAccessToken();

	AITwinTimelineActor* timelineActor = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinTimelineActor::StaticClass());

	// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
	auto DecoIO = GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::LoadScenes,
		[DecoIO, AccessToken, timelineActor]() {
			std::shared_ptr<SDK::Core::ITimeline> timeline;
			auto ret = DecoIO->LoadSceneFromServer(AccessToken, timeline);
			if (ret && timeline && timelineActor)
			{
				timelineActor->SetTimelineSDK(timeline);
			}
			return ret;
		});

}

std::shared_ptr<SDK::Core::ITimeline> AITwinDecorationHelper::FImpl::GetTimeline() const
{
	TStrongObjectPtr<AITwinTimelineActor> timelineActor;
	timelineActor.Reset((AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinTimelineActor::StaticClass()));

	BE_ASSERT(timelineActor.IsValid() || ClientMode != EITwinDecorationClientMode::AdvVizApp,
		"in advanced visualization mode, we should have a valid timeline");
	if (timelineActor.IsValid())
	{
		return timelineActor->GetTimelineSDK();
	}
	return {};
}

void AITwinDecorationHelper::FImpl::SaveDecoration(SaveRequestOptions const& opts)
{
	if (!ShouldSaveDecoration(opts.bPromptUser))
		return;

	bIsDeletingCustomMaterials = opts.bUponCustomMaterialsDeletion;

	std::shared_ptr<SDK::Core::ITimeline> timeline(GetTimeline());

	// Access token has to be retrieved in game thread.
	std::string const AccessToken = GetDecorationAccessToken();
	auto DecoIO = GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::SaveDecoration,
		[DecoIO, AccessToken, timeline]() {
			 bool err1 = DecoIO->SaveDecorationToServer(AccessToken);
			 bool err2 = DecoIO->SaveSceneToServer(AccessToken, timeline);
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

std::string AITwinDecorationHelper::FImpl::GetDecorationAccessToken() const
{
	// We test AITwinServerConnection here, considering that there is only one active instance in most cases.
	// This should be improved if we allow mixing QA and Prod iTwins in a same session, which is
	// theoretically already possible in the plugin... TODO_JDE
	std::string AccessToken;
	AITwinServerConnection* serverConnection = Cast<AITwinServerConnection>(
		UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinServerConnection::StaticClass()));
	if (serverConnection && serverConnection->HasAccessToken())
	{
		serverConnection->GetAccessTokenStdString(AccessToken);
	}
	return AccessToken;
}


/* --------------------------- class AITwinDecorationHelper ---------------------------------*/

AITwinDecorationHelper::AITwinDecorationHelper()
	: Impl(MakePimpl<FImpl>(*this))
{
}

bool AITwinDecorationHelper::IsPopulationEnabled() const
{
	return Impl->IsPopulationEnabled();
}

bool AITwinDecorationHelper::IsMaterialEditionEnabled() const
{
	return Impl->IsMaterialEditionEnabled();
}


void AITwinDecorationHelper::SetDecorationClientMode(EITwinDecorationClientMode ClientMode)
{
	Impl->ClientMode = ClientMode;
}

EITwinDecorationClientMode AITwinDecorationHelper::GetDecorationClientMode() const
{
	return Impl->ClientMode;
}

void AITwinDecorationHelper::SetLoadedITwinInfo(FITwinLoadInfo InLoadedITwinInfo)
{
	Impl->SetLoadedITwinInfo(InLoadedITwinInfo);
}

FITwinLoadInfo AITwinDecorationHelper::GetLoadedITwinInfo() const
{
	return Impl->GetLoadedITwinInfo();
}

void AITwinDecorationHelper::FImpl::LoadPopulationsInGame(bool bHasLoadedPopulations)
{
	checkSlow(IsInGameThread());
	auto& instancesManager(DecorationIO->instancesManager);
	if (!instancesManager)
		return;

	// For now there is only one group of instances
	const SDK::Core::SharedInstGroupVect& instGroups = instancesManager->GetInstancesGroups();
	if (!instGroups.empty())
	{
		DecorationIO->instancesGroup = instGroups[0];
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
		AITwinPopulation* population = Owner.GetOrCreatePopulation(FString(objRef.c_str()));
		if (population)
		{
			population->UpdateInstancesFromSDKCoreToUE();
		}
	}
	bPopulationEnabled = true;

	Owner.OnPopulationsLoaded.Broadcast(true);
}

void AITwinDecorationHelper::LoadDecoration()
{
	if (!ensure(Impl->HasITwinID()))
	{
		return;
	}
	// This will start the asynchronous loading of materials, populations...
	Impl->StartLoadingDecoration(GetWorld());
}

bool AITwinDecorationHelper::IsLoadingDecoration() const
{
	return Impl->IsRunningAsyncLoadTask();
}

void AITwinDecorationHelper::FImpl::OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials)
{
	checkSlow(IsInGameThread());

	// Materials were now loaded from the decoration service. If the tileset has already been loaded, we
	// may have to re-tune & refresh it depending on custom material definitions.
	if (bHasLoadedMaterials && DecorationIO->materialPersistenceMngr)
	{
		const UWorld* World = Owner.GetWorld();
		// We may have loaded material definitions for several iModel.
		std::vector<std::string> iModelIds;
		DecorationIO->materialPersistenceMngr->ListIModelsWithMaterialSettings(iModelIds);
		for (std::string const& imodelid : iModelIds)
		{
			AITwinIModel* IModel = ITwin::GetIModelByID(UTF8_TO_TCHAR(imodelid.c_str()), World);
			if (IModel)
			{
				IModel->DetectCustomizedMaterials();
			}
		}
		// Also update the Google tileset if needed (note that it can be instantiated *before* the materials
		// are loaded asynchronously...)
		Owner.OnMaterialsLoaded.Broadcast(true);
	}

	bMaterialEditionEnabled = true;
}

bool AITwinDecorationHelper::FImpl::ShouldSaveDecoration(bool bPromptUser) const
{
	if (!HasITwinID() || !DecorationIO->decoration)
	{
		return false;
	}
	std::string const accessToken = GetDecorationAccessToken();
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}

	bool const saveInstances = DecorationIO->instancesManager
		&& DecorationIO->instancesManager->HasInstancesToSave();
	bool const saveMaterials = DecorationIO->materialPersistenceMngr
		&& DecorationIO->materialPersistenceMngr->NeedUpdateDB();
	bool const saveScenes = DecorationIO->scene->ShouldSave();
	bool const saveTimeline = GetTimeline() && GetTimeline()->ShouldSave();
	

	if (!saveInstances && !saveMaterials && !saveScenes && !saveTimeline)
		return false;

	if (bPromptUser &&
		FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
			FText::FromString("Do you want to save the scene?"),
			FText::FromString("")) != EAppReturnType::Yes)
	{
		return false;
	}
	return true;
}

bool AITwinDecorationHelper::ShouldSaveDecoration(bool bPromptUser /*= true*/) const
{
	return Impl->ShouldSaveDecoration(bPromptUser);
}

void AITwinDecorationHelper::SaveDecoration(bool bPromptUser /*= true*/)
{
	Impl->SaveDecoration(FImpl::SaveRequestOptions{ .bPromptUser = bPromptUser });
}

void AITwinDecorationHelper::SaveDecorationOnExit()
{
	Impl->SaveDecoration(FImpl::SaveRequestOptions{ .bUponExit = true });
}

void AITwinDecorationHelper::FImpl::OnDecorationSaved_GameThread(bool bSaved, bool bHasResetMaterials)
{
	Owner.OnDecorationSaved.Broadcast(bSaved);

	if (bSaved && bHasResetMaterials)
	{
		// Now that material definitions have been reset, update the iModel
		AITwinIModel* IModel = ITwin::GetIModelByID(GetLoadedITwinInfo().IModelId, Owner.GetWorld());
		if (IModel)
		{
			IModel->ReloadCustomizedMaterials();
		}
	}
}
void AITwinDecorationHelper::OnIModelLoaded(bool bSuccess, FString StringId)
{
	for (TActorIterator<AITwinIModel> IModelIter(GetWorld()); IModelIter; ++IModelIter)
	{
		//find imodel
		if (IModelIter->IModelId == StringId)
		{
			//find link
			auto key = std::make_pair(EITwinModelType::IModel, IModelIter->IModelId);
			auto iter = Impl->DecorationIO->links.find(key);
			if (iter != Impl->DecorationIO->links.end())
			{
				auto si = ITwin::LinkToSceneInfo(*iter->second);
				if (si.Offset.has_value())
				{
					IModelIter->SetActorTransform(*si.Offset, false, nullptr, ETeleportType::TeleportPhysics);
					IModelIter->OnIModelOffsetChanged();
				}
				if (si.Visibility.has_value())
					IModelIter->HideTileset(!*si.Visibility);
				if (si.Quality.has_value())
					IModelIter->SetTilesetQuality(*si.Quality);
			}
		}
	}
}
	
void AITwinDecorationHelper::OnRealityDatalLoaded(bool bSuccess, FString StringId)
{
	for (TActorIterator<AITwinRealityData> IReallIter(GetWorld()); IReallIter; ++IReallIter)
	{
		//find RealityData
		if (IReallIter->RealityDataId == StringId)
		{
			//find link
			auto key = std::make_pair(EITwinModelType::RealityData, IReallIter->RealityDataId);
			auto iter = Impl->DecorationIO->links.find(key);
			if (iter != Impl->DecorationIO->links.end())
			{
				auto si = ITwin::LinkToSceneInfo(*iter->second);
				if (si.Offset.has_value())
					IReallIter->SetActorTransform(*si.Offset, true);
				if (si.Visibility.has_value())
					IReallIter->HideTileset(!*si.Visibility);
				if (si.Quality.has_value())
					IReallIter->SetTilesetQuality(*si.Quality);
			}
		}
	}
}

void AITwinDecorationHelper::FImpl::OnSceneLoad_GameThread(bool bSuccess)
{
	for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
	{
		auto key = std::make_pair(EITwinModelType::IModel, IModelIter->IModelId);
		auto iter = DecorationIO->links.find(key);
		if (iter != DecorationIO->links.end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			if (si.Offset.has_value())
				IModelIter->SetActorTransform(*si.Offset, true);
			if (si.Visibility.has_value())
				IModelIter->HideTileset(!*si.Visibility);
			if (si.Quality.has_value())
				IModelIter->SetTilesetQuality(*si.Quality);

			IModelIter->OnIModelLoaded.AddDynamic(&Owner, &AITwinDecorationHelper::OnIModelLoaded);

		}
	}
	for (TActorIterator<AITwinRealityData> IReallIter(Owner.GetWorld()); IReallIter; ++IReallIter)
	{
		auto key = std::make_pair(EITwinModelType::RealityData, IReallIter->RealityDataId);
		auto iter = DecorationIO->links.find(key);
		if (iter != DecorationIO->links.end())
		{
			if (iter->second->HasTransform())
			{
				auto si = ITwin::LinkToSceneInfo(*iter->second);
				if (si.Offset.has_value())
					IReallIter->SetActorTransform(*si.Offset, true);
				if (si.Visibility.has_value())
					IReallIter->HideTileset(!*si.Visibility);
				if (si.Quality.has_value())
					IReallIter->SetTilesetQuality(*si.Quality);
				//IReallIter->OnRealityDatalLoaded.AddDynamic(&Owner, &AITwinDecorationHelper::OnRealityDatalLoaded);
			}
		}
	}

	Owner.OnSceneLoaded.Broadcast(bSuccess);
}

AITwinPopulation* AITwinDecorationHelper::GetPopulation(FString assetPath) const
{
	TArray<AActor*> populations;
    UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), AITwinPopulation::StaticClass(), populations);

	std::string stdAssetPath = ITwin::ConvertToStdString(assetPath);

    for (AActor* actor : populations)
    {
		AITwinPopulation* pop = Cast<AITwinPopulation>(actor);
		if (pop->GetObjectRef() == stdAssetPath)
		{
			return pop;
		}
	}

	return nullptr;
}

AITwinPopulation* AITwinDecorationHelper::GetOrCreatePopulation(FString assetPath) const
{
	AITwinPopulation* population = GetPopulation(assetPath);

	if (population)
	{
		return population;
	}

	// Spawn a new actor with a deferred call in order to be able
	// to set the static mesh before BeginPlay is called.
	FTransform spawnTransform;
	AActor* newActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
		this, AITwinPopulation::StaticClass(), spawnTransform,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	population = Cast<AITwinPopulation>(newActor);

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

	population->SetInstancesManager(Impl->DecorationIO->instancesManager);
	population->SetInstancesGroup(Impl->DecorationIO->instancesGroup);
	population->SetObjectRef(ITwin::ConvertToStdString(assetPath));

	return population;
}

int32 AITwinDecorationHelper::GetPopulationInstanceCount(FString assetPath) const
{
	AITwinPopulation* population = GetPopulation(assetPath);

	if (population)
	{
		return population->GetNumberOfInstances();
	}

	return 0;
}

SDK::Core::ITwinAtmosphereSettings AITwinDecorationHelper::GetAtmosphereSettings() const
{
	return Impl->DecorationIO->scene->GetAtmosphere();
}

void AITwinDecorationHelper::SetAtmosphereSettings(const SDK::Core::ITwinAtmosphereSettings& as) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
		Impl->DecorationIO->scene->SetAtmosphere(as);
}
SDK::Core::ITwinSceneSettings AITwinDecorationHelper::GetSceneSettings() const
{
	return Impl->DecorationIO->scene->GetSceneSettings();
}

void AITwinDecorationHelper::SetSceneSettings(const SDK::Core::ITwinSceneSettings& as) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
		Impl->DecorationIO->scene->SetSceneSettings(as);
}


ITwinSceneInfo AITwinDecorationHelper::GetSceneInfo(EITwinModelType ct, const FString& id) const
{
	auto key = std::make_pair(ct, id);
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(key);
		if (iter != Impl->DecorationIO->links.end())
			return ITwin::LinkToSceneInfo(*iter->second);

	}
	return ITwinSceneInfo();
}

void AITwinDecorationHelper::SetSceneInfo(EITwinModelType ct, const FString& id, const ITwinSceneInfo& si) const
{

	auto key = std::make_pair(ct, id);
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(key);
		std::shared_ptr<SDK::Core::Link> sp;

		if (iter == Impl->DecorationIO->links.end())
			sp = Impl->DecorationIO->CreateLink(ct, id);
		else
			sp = iter->second;

		if (sp)
		{
			ITwin::SceneToLink(si, sp);
		}

	}
}


std::vector<std::pair<EITwinModelType, FString>> AITwinDecorationHelper::GetLinkedElements() const
{
	std::vector<std::pair<EITwinModelType, FString>>  res;
	for (auto link : Impl->DecorationIO->links)
	{
		res.push_back(link.first);
	}
	return res;
}

//std::shared_ptr<SDK::Core::IScenePersistence> AITwinDecorationHelper::GetScene() const
//{
//	if (DecorationIO)
//		return DecorationIO->scene;
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

	Impl->DecorationIO->RequestStop();
}


void AITwinDecorationHelper::BeforeCloseLevel()
{
	SaveDecorationOnExit();
}

void AITwinDecorationHelper::OnCloseRequested(FViewport*)
{
	BeforeCloseLevel();
}

void AITwinDecorationHelper::FImpl::DeleteAllCustomMaterials()

{
	if (!IsMaterialEditionEnabled())
		return;
	if (!(DecorationIO && DecorationIO->materialPersistenceMngr))
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
		DecorationIO->materialPersistenceMngr->RequestDeleteIModelMaterialsInDB(imodelId);

		// Propose to save at once (with a specific flag set to perform refresh at the end).
		SaveDecoration(SaveRequestOptions{ .bUponCustomMaterialsDeletion = true });
	}
}

void AITwinDecorationHelper::DeleteAllCustomMaterials()
{
	Impl->DeleteAllCustomMaterials();
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

AITwinDecorationHelper::SaveLocker::SaveLocker()
{

}

AITwinDecorationHelper::SaveLocker::~SaveLocker()
{

}

class AITwinDecorationHelper::SaveLockerImpl : public AITwinDecorationHelper::SaveLocker
{
public:
	SaveLockerImpl(AITwinDecorationHelper* __this)
		:_this(__this)
	{
		_this->Lock(this);
	}
	AITwinDecorationHelper* _this;
	~SaveLockerImpl()
	{
		_this->Unlock(this);
	}

	bool sceneStatus;
	bool timelineStatus;
	std::map< std::pair<EITwinModelType, FString>, bool > linksStatus;

};
std::shared_ptr<AITwinDecorationHelper::SaveLocker> AITwinDecorationHelper::LockSave()
{
	return std::shared_ptr<AITwinDecorationHelper::SaveLocker>(new SaveLockerImpl(this));
}
void AITwinDecorationHelper::Lock(SaveLockerImpl* saver)
{
	saver->sceneStatus = Impl->DecorationIO->scene && Impl->DecorationIO->scene->ShouldSave();
	for (auto link : Impl->DecorationIO->links)
	{
		saver->linksStatus[link.first] = link.second->ShouldSave();
	}
	saver->timelineStatus = Impl->GetTimeline() && Impl->GetTimeline()->ShouldSave();
}

void AITwinDecorationHelper::Unlock(SaveLockerImpl* saver )
{
	if (Impl->DecorationIO->scene)
	{
		Impl->DecorationIO->scene->SetShoudlSave(saver->sceneStatus);
	}
	for (auto link : Impl->DecorationIO->links)
	{
		if (saver->linksStatus.find(link.first) == saver->linksStatus.end())
		{
			link.second->SetShouldSave(false);
		}
		else
		{
			link.second->SetShouldSave(saver->linksStatus[link.first]);
		}
	}
	if (Impl->GetTimeline())
		Impl->GetTimeline()->SetShouldSave(saver->timelineStatus);
}

void AITwinDecorationHelper::DeleteLoadedScene()
{

	if (FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
		FText::FromString("Do you want to delete the current scene?"),
		FText::FromString("")) != EAppReturnType::Yes)
	{
		return;
	}
	std::string const accessToken = Impl->GetDecorationAccessToken();

	Impl->DecorationIO->scene->Delete(accessToken);
}
