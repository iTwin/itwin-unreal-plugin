/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/ITwinDecorationHelper.h>

#include <Decoration/DecorationWaitableLoadEvent.h>
#include <Decoration/DecorationAsyncIOHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>

#include "HttpModule.h"
#include "HttpManager.h"

#include <Engine/Engine.h>
#include <Containers/Ticker.h>
#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/GameViewportClient.h>
#include <Engine/StaticMesh.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerStart.h>
#include <AnimTimeline/ITwinTimelineActor.h>
#include <ITwinGeolocation.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinServerConnection.h>
#include <Material/ITwinMaterialLibrary.h>
#include <Population/ITwinKeyframePath.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationWithPathExt.h>
#include <Population/ITwinAnimPathManager.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

#include <Misc/MessageDialog.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Async/Async.h>
#include <atomic>
#include <chrono>
#include <Content/ITwinContentManager.h>
#include <Math/UEMathConversion.h>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/MaterialPersistence.h"
#	include "SDK/Core/Visualization/Timeline.h"
#	include "SDK/Core/ITwinAPI/ITwinAuthManager.h"
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#   include "SDK/Core/Visualization/SplinesManager.h"
#   include "SDK/Core/Visualization/KeyframeAnimator.h"
#	include <numbers>
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

	AITwinRealityData* GetRealityDataByID(FString const& RealityDataId, const UWorld* World)
	{
		for (TActorIterator<AITwinRealityData> IReallIter(World); IReallIter; ++IReallIter)
		{
			if (IReallIter->RealityDataId == RealityDataId)
				return *IReallIter;
		}
		return nullptr;
	}

	TUniquePtr<FITwinTilesetAccess> GetGoogleTilesetAccess(const UWorld* World)
	{
		for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(World); Google3DIter; ++Google3DIter)
		{
			return (*Google3DIter)->MakeTilesetAccess();
		}
		return {};
	}

	TUniquePtr<FITwinTilesetAccess> GetTilesetAccess(ModelLink const& ModelKey, const UWorld* World)
	{
		switch (ModelKey.first)
		{
		case EITwinModelType::IModel:
		{
			auto* Model = GetIModelByID(ModelKey.second, World);
			if (Model)
				return Model->MakeTilesetAccess();
			break;
		}
		case EITwinModelType::RealityData:
		{
			auto* Model = GetRealityDataByID(ModelKey.second, World);
			if (Model)
				return Model->MakeTilesetAccess();
			break;
		}
		case EITwinModelType::GlobalMapLayer:
		{
			return GetGoogleTilesetAccess(World);
		}
		BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
		case EITwinModelType::AnimationKeyframe:
		case EITwinModelType::Scene:
		case EITwinModelType::Invalid: );
		}
		return {};
	}

	std::set<ModelLink> GetSplineModelLinks(AdvViz::SDK::SharedSpline const& Spline)
	{
		std::set<ModelLink> Links;
		if (Spline)
		{
			for (auto const& ModelLink : Spline->GetLinkedModels())
			{
				Links.insert(std::make_pair(
					ITwin::StrToModelType(ModelLink.modelType),
					UTF8_TO_TCHAR(ModelLink.modelId.c_str())
				));
			}
		}
		return Links;
	}

	int32 GetLinkedTilesets(
		AITwinSplineTool::TilesetAccessArray& OutArray,
		AdvViz::SDK::SharedSpline const& Spline,
		const UWorld* World)
	{
		OutArray.Reset();

		std::set<ModelLink> const Links = GetSplineModelLinks(Spline);
		for (auto const& Key : Links)
		{
			// Spline is linked to specific model(s)
			TUniquePtr<FITwinTilesetAccess> LinkedTileset = GetTilesetAccess(Key, World);
			if (LinkedTileset && !LinkedTileset->HasTileset())
			{
				LinkedTileset.Reset();
			}
			if (LinkedTileset)
			{
				OutArray.Add(std::move(LinkedTileset));
			}
		}
		return OutArray.Num();
	}

	AdvViz::SDK::SharedSplineVect GetLinkedSplines(AdvViz::SDK::ISplinesManager const& SplinesManager,
		ModelLink const& Key)
	{
		AdvViz::SDK::SharedSplineVect LinkedSplines;
		for (auto const& Spline : SplinesManager.GetSplines())
		{
			if (GetSplineModelLinks(Spline).contains(Key))
				LinkedSplines.push_back(Spline);
		}
		return LinkedSplines;
	}

	inline AITwinDecorationHelper* GetDecorationHelper(FString const& ITwinId, UWorld const* World)
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
			if ((**DecoIter).GetLoadedITwinId() == ITwinId)
			{
				return *DecoIter;
			}
		}
		return nullptr;
	}

	bool ShouldLoadScene(FString const& ITwinId, UWorld const* World)
	{
		if (ITwinId.IsEmpty())
		{
			// We cannot load a decoration without the iTwin ID...
			return false;
		}

		// Test if the iTwin scope is sufficient to access the decoration service.
		static bool bHasDecoScope = false;
		static bool bHasCheckedScope = false;
		if (!bHasCheckedScope)
		{
			bHasDecoScope = AdvViz::SDK::ITwinAuthManager::HasScope(ITWIN_DECORATIONS_SCOPE);
			bHasCheckedScope = true;
		}
		if (!bHasDecoScope)
		{
			return false;
		}

		// If a decoration helper already exists for this iTwin, consider that the loading is already in
		// progress, or will be started from another path.
		AITwinDecorationHelper const* DecoHelper = GetDecorationHelper(ITwinId, World);
		return(DecoHelper == nullptr);
	}

	void LoadScene(const FString & ITwinId, UWorld* World)
	{
		if (!World)
		{
			BE_ISSUE("no world given");
			return;
		}
		AITwinDecorationHelper* DecoHelper = GetDecorationHelper(ITwinId, World);
		if (DecoHelper == nullptr)
		{
			// Instantiate the decoration helper now:
			DecoHelper = World->SpawnActor<AITwinDecorationHelper>();
			DecoHelper->SetLoadedITwinId(ITwinId);
		}
		DecoHelper->LoadScene();
	}

	ITWINRUNTIME_API void LoadIModelDecorationMaterials(AITwinIModel& IModel, UWorld* World)
	{
		AITwinDecorationHelper* DecoHelper = GetDecorationHelper(IModel.ITwinId, World);
		if (DecoHelper)
		{
			DecoHelper->LoadIModelMaterials(IModel);
		}
		else
		{
			LoadScene(IModel.ITwinId, World);
		}
	}

	void SaveScene(FString const& ITwinId, UWorld const* World)
	{
		AITwinDecorationHelper* DecoHelper = GetDecorationHelper(ITwinId, World);
		if (DecoHelper)
		{
			DecoHelper->SaveScene(false /*bPromptUser*/);
		}
	}

	std::string ConvertToStdString(const FString& fstring)
	{
		return (const char*)StringCast<UTF8CHAR>(*fstring).Get();
	}

	static ITwinSceneInfo LinkToSceneInfo(const AdvViz::SDK::ILink& l)
	{
		using namespace AdvViz::SDK;

		ITwinSceneInfo s;
		if(l.HasQuality())
			s.Quality = l.GetQuality();
		if (l.HasVisibility())
			s.Visibility = l.GetVisibility();
		FMatrix dstMat(FMatrix::Identity);
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

	static void SceneToLink(const ITwinSceneInfo& si, std::shared_ptr<AdvViz::SDK::ILink> l)
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
					if(srcMat.M[j][i] == -0.0)
						dstTransform[i * 4 + j] = 0.0;
					else
						dstTransform[i*4+j] = srcMat.M[j][i];
				}
			}
			FVector srcPos = si.Offset->GetTranslation();
			dstTransform[3] = srcPos.X;
			dstTransform[7] = srcPos.Y;
			dstTransform[11] = srcPos.Z;
			l->SetTransform(dstTransform);
		}
		l->Delete(false); // cancel delete
	}

	// The scene loader thread should wait for iTwin geo-location request
	class FITwinGeolocInfoEvent : public FDecorationWaitableLoadEvent
	{
	public:
		virtual bool ShouldWait() const override {
			return FITwinGeolocation::IsDefaultGeoRefRequestInProgress();
		}
		virtual std::string Describe() const override {
			return "iTwin geo-location";
		}
	};
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
	std::map< ITwin::ModelLink, bool > linksStatus;

};

/* -------------------------- class AITwinDecorationHelper::FImpl ---------------------------*/

/// Hold implementation details for asynchronous tasks regarding the decoration service.
class AITwinDecorationHelper::FImpl
{
public:
	enum class EAsyncTask : uint8_t
	{
		None = 0,

		LOAD_TASK_START,
		LoadScenes = LOAD_TASK_START,
		LoadMaterials,
		LoadSplines,
		LoadPathAnimations,
		LoadPopulations,
		LoadAnnotations,

		LOAD_TASK_END,

		SaveDecoration,
	};

	enum class EAsyncContext : uint8_t
	{
		None = 0,
		Load,
		Save
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

	void SetLoadedITwinId(FString const& ITwinId);
	FString GetLoadedITwinId() const;
	bool HasITwinID() const;


	void StartLoadingDecoration(UWorld* WorldContextObject);
	void StartLoadingIModelMaterials(AITwinIModel& IModel);

	bool ShouldSaveScene(bool bPromptUser) const;

	struct SaveRequestOptions
	{
		bool bUponExit = false;
		bool bUponCustomMaterialsDeletion = false;
		bool bPromptUser = true;
	};
	void SaveScene(SaveRequestOptions const& opts);

	void DeleteAllCustomMaterials();

	size_t LoadSplinesLinkedToModel(ITwin::ModelLink const& Key, FITwinTilesetAccess& TilesetAccess);

	// Ask confirmation if the task is taking too long - return true if the user confirmed the abortion.
	bool ShouldAbort();

private:
	void SetCurrentTask(EAsyncTask NewTask, bool bUpdateContext = true);
	void AsyncLoadMaterials(TMap<FString, TWeakObjectPtr<AITwinIModel>> const& IModelMap, bool bForSpecificModels);
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

	std::function<bool()> GetAsyncFunctor(EAsyncTask AsyncTask);

	void LoadPopulationsInGame(bool bHasLoadedPopulations);
	void DissociateAnimation(const std::string& animId);
	void LoadSplinesInGame(bool bHasLoadedSplines);
	bool LoadSplineIfAllLinkedModelsReady(
		AdvViz::SDK::SharedSpline const& AdvVizSpline,
		AITwinSplineTool* SplineTool,
		const UWorld* World);
	void LoadAnnotationsInGame(bool bHasLoadedSplines);
	void LoadPathAnimationsInGame(bool bHasLoadePathAnimations);

	void OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials);

	void OnDecorationSaved_GameThread(bool bSuccess, bool bHasResetMaterials);

	void OnSceneLoad_GameThread(bool bSuccess);

	void PreSaveCameras();
	void LoadCameras();

public:
	// For loading and saving
	std::shared_ptr<FDecorationAsyncIOHelper> DecorationIO;

	EITwinDecorationClientMode ClientMode = EITwinDecorationClientMode::Unknown;

	std::weak_ptr<SaveLocker> saveLocker;
private:
	// Initially, both Population and Material edition are disabled, until we have loaded the corresponding
	// information (which can be empty of course) from the decoration service.
	bool bPopulationEnabled = false;
	bool bMaterialEditionEnabled = false;

	std::atomic<EAsyncTask> CurrentAsyncTask = EAsyncTask::None;
	std::shared_ptr<std::atomic_bool> IsThisValid = std::make_shared<std::atomic_bool>(true);
	std::atomic_bool CurrentAsyncTaskDone = false;
	std::atomic_bool CurrentAsyncTaskResult = false;

	std::atomic<EAsyncContext> CurrentContext = EAsyncContext::None;

	FTSTicker::FDelegateHandle TickerDelegate;
	std::chrono::system_clock::time_point NextConfirmTime = std::chrono::system_clock::now();
	FString ConfirmAbortMsg;
	int ConfirmOccurrences = 0;
	bool bIsDisplayingConfirmMsg = false;
	AITwinDecorationHelper& Owner;
	bool bIsDeletingCustomMaterials = false;
	TMap<FString, TWeakObjectPtr<AITwinIModel>> PendingIModelsForMaterials;
	bool bLoadingMaterialsForSpecificModels = false;
	std::set<std::string> SpecificIModelsForMaterialLoading;
	std::set<ITwin::ModelLink> ModelsWithLoadedSplines;
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
				SetCurrentTask(EAsyncTask::None);
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
	return (CurTask >= EAsyncTask::LOAD_TASK_START
		&& CurTask < EAsyncTask::LOAD_TASK_END)
		&& !CurrentAsyncTaskDone;
}

void AITwinDecorationHelper::FImpl::SetCurrentTask(EAsyncTask TaskType, bool bUpdateContext /*= true*/)
{
	CurrentAsyncTask = TaskType;

	if (bUpdateContext)
	{
		// Deduce current context from the task type.
		EAsyncContext NewContext = EAsyncContext::None;
		if (TaskType >= EAsyncTask::LOAD_TASK_START
			&& TaskType < EAsyncTask::LOAD_TASK_END)
		{
			NewContext = EAsyncContext::Load;
		}
		else if (TaskType == EAsyncTask::SaveDecoration)
		{
			NewContext = EAsyncContext::Save;
		}
		if (CurrentContext.load() != NewContext)
		{
			CurrentContext = NewContext;
		}
	}
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

	SetCurrentTask(TaskType);
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

std::function<bool()> AITwinDecorationHelper::FImpl::GetAsyncFunctor(EAsyncTask AsyncTask)
{
	// Share all data for use in the lambda (the game mode may be deleted while the lambda is
	// executed).
	auto DecoIO = GetDecorationAsyncIOHelper();

	switch (AsyncTask)
	{
	case EAsyncTask::LoadScenes:
	{
		// TODO_LC move edition of actor in Game-thread part? (could be destroyed when the lambda is
		// executed)
		// Ghislain: this is also a pb because SetTimelineSDK replaces the existing ITimelineClip shared_ptr,
		// which leads to releasing UMovieSceneTrack strong ptr, which should be done in Game thread or GC thread.
		// This happens in Carrot PIE when opening an itwin then trying to open a different one with 'O' shortcut
		AITwinTimelineActor* timelineActor = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(
			Owner.GetWorld(), AITwinTimelineActor::StaticClass());
		return [DecoIO, timelineActor]() {
			auto ret = DecoIO->LoadSceneFromServer();
			if (DecoIO->scene && DecoIO->scene->GetTimeline() && timelineActor)
			{
				timelineActor->SetTimelineSDK(DecoIO->scene->GetTimeline());
			}
			// ***** Synchronization with itwin requests *****
			// If the loading of the scene is very fast (typically if it fails very soon), it may happen
			// that the iTwin Manager has not even finished its requests to retrieve the available models in
			// the selected iTwin and its geo-reference, which could introduce some randomness (typically if
			// the iTwin contains only one model: in such case, we should normally automatically load the
			// latter; but this can work only if the iTwin Manager has finished its requests.
			DecoIO->WaitForExternalLoadEvents(60);
			return ret;
		};
	}
	case EAsyncTask::LoadMaterials:
	{
		TMap<FString, TWeakObjectPtr<AITwinIModel>> IdToIModel;
		for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
		{
			IdToIModel.Emplace((**IModelIter).IModelId, *IModelIter);
		}
		return [DecoIO, IdToIModel]() { return DecoIO->LoadCustomMaterials(IdToIModel); };
	}
	case EAsyncTask::LoadPopulations:
	{
		return [DecoIO]() { return DecoIO->LoadPopulationsFromServer(); };
	}
	case EAsyncTask::LoadSplines:
	{
		return [DecoIO]() { return DecoIO->LoadSplinesFromServer(); };
	}
	case EAsyncTask::LoadAnnotations:
	{
		return [DecoIO]() { return DecoIO->LoadAnnotationsFromServer(); };
	}
	case EAsyncTask::LoadPathAnimations:
	{
		return [DecoIO]() { return DecoIO->LoadPathAnimationFromServer(); };
	}
	case EAsyncTask::SaveDecoration:
	{
		return [DecoIO]() {
			bool err1 = DecoIO->SaveDecorationToServer();
			bool err2 = DecoIO->SaveSceneToServer();
			return err1 && err2;
		};
	}

	// Other tasks are not valid tasks
	default:
	case EAsyncTask::LOAD_TASK_END:
		break;
	}
	BE_ISSUE("invalid async task", (uint8_t)AsyncTask);
	return []() { return false; };
}

void AITwinDecorationHelper::FImpl::OnAsyncTaskDone_GameThread(ETaskExitStatus TaskExitStatus, bool bSuccess)
{
	ensure(IsInGameThread());
	ensure(CurrentContext.load() != EAsyncContext::None);

	EAsyncTask const TaskJustFinished = CurrentAsyncTask.load();
	bool const bMaterialsForSpecificModels = bLoadingMaterialsForSpecificModels;

	SetCurrentTask(EAsyncTask::None, false /*updateContext*/);

	switch (TaskJustFinished)
	{
	case EAsyncTask::LoadScenes:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			OnSceneLoad_GameThread(bSuccess);
			if (bSuccess)
			{
				// prevent save flags from being set during update of the UI
				DecorationIO->scene->SetShouldSave(false);
			}
			// Finish timeline initialization
			AITwinTimelineActor* tla = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinTimelineActor::StaticClass());
			if (tla)
				tla->OnLoad();
		}
		else if (TaskExitStatus == ETaskExitStatus::Aborted)
		{
			Owner.OnSceneLoadingStartStop.Broadcast(false);
		}
		break;

	case EAsyncTask::LoadMaterials:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			OnCustomMaterialsLoaded_GameThread(bSuccess);
		}
		break;

	case EAsyncTask::LoadPopulations:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			LoadPopulationsInGame(bSuccess);
		}
		break;

	case EAsyncTask::LoadSplines:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			LoadSplinesInGame(bSuccess);
		}
		break;

	case EAsyncTask::LoadAnnotations:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			LoadAnnotationsInGame(bSuccess);
		}
		break;

	case EAsyncTask::LoadPathAnimations:
		if (TaskExitStatus == ETaskExitStatus::Completed)
		{
			LoadPathAnimationsInGame(bSuccess);
		}
		break;

	case EAsyncTask::SaveDecoration:
		{
			OnDecorationSaved_GameThread(bSuccess, bIsDeletingCustomMaterials);

			bIsDeletingCustomMaterials = false;
		}
		break;

	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(
	case EAsyncTask::LOAD_TASK_END:)
	case EAsyncTask::None:
		break;
	}

	// If we are in the loading phase, jump to the next step (the order is defined by the enum).
	EAsyncTask NextLoadTask = EAsyncTask::None;
	if (TaskExitStatus == ETaskExitStatus::Completed
		&& TaskJustFinished >= EAsyncTask::LOAD_TASK_START
		&& TaskJustFinished < EAsyncTask::LOAD_TASK_END
		&& !bMaterialsForSpecificModels)
	{
		NextLoadTask = static_cast<EAsyncTask>(
			static_cast<uint8_t>(TaskJustFinished) + 1);
		if (NextLoadTask == EAsyncTask::LOAD_TASK_END)
		{
			// The loading of decoration is now done.
			Owner.OnDecorationLoaded.Broadcast();
			NextLoadTask = EAsyncTask::None;
		}
		else
		{
			StartAsyncTask(NextLoadTask, GetAsyncFunctor(NextLoadTask));
		}
	}

	if (NextLoadTask == EAsyncTask::None)
	{
		CurrentContext = EAsyncContext::None;

		// Process pending load material task, if any.
		if (!PendingIModelsForMaterials.IsEmpty())
		{
			AsyncLoadMaterials(PendingIModelsForMaterials, true);
			PendingIModelsForMaterials.Empty();
		}
	}
}

namespace ITwinMsg
{
	static const FString LongITwinServicesResponseTime = TEXT("The iTwin services are taking a longer time to complete.\n");
	static const FString LongDecoServerResponseTime = TEXT("The decoration service is taking a longer time to complete.\n");
	static const FString ConfirmAbortLoadDeco = TEXT("\nDo you want to load your model without any population/material customization?\n");
	static const FString ConfirmAbortSaveDeco = TEXT("\nDo you want to abort saving the modifications you made to your population/materials?\n");

	inline FString GetConfirmAbortLoadMsg() {
		return LongITwinServicesResponseTime + ConfirmAbortLoadDeco;
	}
	inline FString GetConfirmAbortSaveMsg() {
		return LongDecoServerResponseTime + ConfirmAbortSaveDeco;
	}
}

void AITwinDecorationHelper::FImpl::InitDecorationService()
{
	DecorationIO->InitDecorationService(Owner.GetWorld());
}

void AITwinDecorationHelper::FImpl::SetLoadedITwinId(FString const& LoadedITwinId)
{
	DecorationIO->SetLoadedITwinId(LoadedITwinId);

	// Initialize decoration service asap (important for presentations, typically: the material persistence
	// manager should be instantiated *before* the IModel starts to load the tileset...)
	InitDecorationService();
}

FString AITwinDecorationHelper::FImpl::GetLoadedITwinId() const
{
	return DecorationIO->GetLoadedITwinId();
}

bool AITwinDecorationHelper::FImpl::HasITwinID() const
{
	return !DecorationIO->LoadedITwinId.IsEmpty();
}

std::shared_ptr<FDecorationAsyncIOHelper> AITwinDecorationHelper::FImpl::GetDecorationAsyncIOHelper() const
{
	checkSlow(DecorationIO->IsInitialized());
	return DecorationIO;
}

void AITwinDecorationHelper::FImpl::StartLoadingDecoration(UWorld* WorldContextObject)
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	DecoIO->InitDecorationService(WorldContextObject);

	// Start the asynchronous loading of Scene then, materials, then populations.
	AsyncLoadScene();
}

void AITwinDecorationHelper::FImpl::StartLoadingIModelMaterials(AITwinIModel& IModel)
{
	ensure(IsInGameThread());

	TMap<FString, TWeakObjectPtr<AITwinIModel>> IdToIModel;
	IdToIModel.Emplace(IModel.IModelId, &IModel);

	bool const bAlreadyRunningTask = (CurrentContext.load() != EAsyncContext::None);
	if (bAlreadyRunningTask)
	{
		// We are already loading decoration data => postpone the loading of this model's materials.
		PendingIModelsForMaterials.Append(IdToIModel);
	}
	else
	{
		AsyncLoadMaterials(IdToIModel, true);
	}
}

void AITwinDecorationHelper::FImpl::AsyncLoadMaterials(TMap<FString, TWeakObjectPtr<AITwinIModel>> const& IdToIModel,
	bool bForSpecificModels)
{
	bLoadingMaterialsForSpecificModels = bForSpecificModels;
	SpecificIModelsForMaterialLoading.clear();
	if (bForSpecificModels)
	{
		for (auto const& [StrId, _] : IdToIModel)
		{
			SpecificIModelsForMaterialLoading.insert(TCHAR_TO_UTF8(*StrId));
		}
	}
	// Share all data for use in the lambda (the game mode may be deleted while the lambda is executed).
	auto DecoIO = GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::LoadMaterials,
		[DecoIO, IdToIModel, specificModels = SpecificIModelsForMaterialLoading]()
		{
			return DecoIO->LoadCustomMaterials(IdToIModel, specificModels);
		}
	);
}

void AITwinDecorationHelper::FImpl::AsyncLoadScene()
{
	StartAsyncTask(EAsyncTask::LoadScenes,
		GetAsyncFunctor(EAsyncTask::LoadScenes),
		ITwinMsg::GetConfirmAbortLoadMsg());
	Owner.OnSceneLoadingStartStop.Broadcast(true);
}
void AITwinDecorationHelper::FImpl::SaveScene(SaveRequestOptions const& opts)
{
	if (!ShouldSaveScene(opts.bPromptUser))
		return;

	bIsDeletingCustomMaterials = opts.bUponCustomMaterialsDeletion;

	PreSaveCameras();

	auto DecoIO = GetDecorationAsyncIOHelper();
	StartAsyncTask(EAsyncTask::SaveDecoration,
		GetAsyncFunctor(EAsyncTask::SaveDecoration),
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
			FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::FullFlush);
			FPlatformProcess::Sleep(1.f);
			ElapsedSec++;
		}
		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Shutdown);
	}
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

void AITwinDecorationHelper::SetLoadedITwinId(FString ITwinId)
{
	Impl->SetLoadedITwinId(ITwinId);
}

FString AITwinDecorationHelper::GetLoadedITwinId() const
{
	return Impl->GetLoadedITwinId();
}

void AITwinDecorationHelper::SetLoadedSceneId(FString InLoadedSceneId, bool inNewsScene /*= false*/)
{
	Impl->DecorationIO->SetLoadedSceneId(InLoadedSceneId, inNewsScene);
}

void AITwinDecorationHelper::FImpl::DissociateAnimation(const std::string& animId)
{
	auto& instancesManager(DecorationIO->instancesManager_);
	if (!instancesManager)
		return;

	auto gp = instancesManager->GetInstancesGroupByName(animId);
	if (gp && gp->GetId().HasDBIdentifier())
	{
		instancesManager->RemoveGroupInstances(gp->GetId());
		instancesManager->RemoveInstancesGroup(gp);
		instancesManager->SaveDataOnServer(DecorationIO->decoration->GetId());
	}
}

void AITwinDecorationHelper::FImpl::LoadPopulationsInGame(bool bHasLoadedPopulations)
{
	checkSlow(IsInGameThread());
	auto& instancesManager(DecorationIO->instancesManager_);
	if (!instancesManager)
		return;

	if (!(GEngine && GEngine->GameViewport))
	{
		BE_LOGW("ITwinDecoration", "Populations cannot be loaded in Editor");
		return;
	}

	//D-O-NOTC
	// code to remove instances associate to an animation, used to re-associate them with code below.
	//for (auto& it : DecorationIO->animationKeyframes)
	//	DissociateAnimation((std::string)it.first);

	using namespace AdvViz::SDK;

	const char* cars[] = {
		"/Game/CarrotLibrary/Vehicles/Audi_A4",
		"/Game/CarrotLibrary/Vehicles/Chevrolet_Impala",
		"/Game/CarrotLibrary/Vehicles/Mercedes_SL",
		"/Game/CarrotLibrary/Vehicles/Volvo_V70",
	};

	// Associate animation set to group
	bool bSaveDataOnServer = false;
	for (auto& it : DecorationIO->animationKeyframes)
	{
		auto gp = instancesManager->GetInstancesGroupByName((std::string)it.first);
		if (!gp) // we go there only if animation is not already associated
		{
			gp = std::shared_ptr<IInstancesGroup>(IInstancesGroup::New());
			gp->SetName((std::string)it.first);
			gp->SetType("animKeyframe");
			instancesManager->AddInstancesGroup(gp);

			//Save each group to have a valid id;
			// We should save only group.
			BE_ASSERT(DecorationIO->decoration);
			if (DecorationIO->decoration) 
				instancesManager->SaveDataOnServer(DecorationIO->decoration->GetId()); // Temporary, we need a valid groupid, LC TODO: Check if we should we save the association now?

			auto lockanimationKeyframe(it.second->GetAutoLock());
			auto& animationKeyframe = lockanimationKeyframe.Get();
			for (auto &infoId: animationKeyframe.GetAnimationKeyframeInfoIds())
			{ 
				auto animationKeyframeInfoPtr = animationKeyframe.GetAnimationKeyframeInfo(infoId);
				if (animationKeyframeInfoPtr)
				{
					auto lockanimationKeyframeInfo(animationKeyframeInfoPtr->GetAutoLock());
					auto& animationKeyframeInfo = lockanimationKeyframeInfo.Get();
					std::string objectRef = "/Game/CarrotLibrary/Characters/Architect";
					FVector colorShift(0., 0., 0.);
					for (auto& tag : animationKeyframeInfo.GetTags())
					{
						if (tag == "car")
						{
							int32 index = FMath::RandRange((int32)0, (int32)(std::size(cars) - 1));
							objectRef = cars[index];
							colorShift = AITwinPopulation::GetRandomColorShift(EITwinInstantiatedObjectType::Vehicle);
							break;
						}
						if (tag == "character")
						{
							objectRef = "/Game/CarrotLibrary/Characters/Architect";
							break;
						}
					}
					auto inst = instancesManager->AddInstance(objectRef, gp->GetId());
					inst->SetShouldSave(true);
					inst->SetName("inst");
					inst->SetObjectRef(objectRef);
					inst->SetAnimId((std::string)infoId);
					AdvViz::SDK::float3 cs;
					AdvViz::SDK::Copy(colorShift, cs);
					inst->SetColorShift(cs);
				}
			}
		}
		AITwinKeyframePath* keyframePath = Owner.CreateKeyframePath();
		BE_ASSERT(keyframePath != nullptr);
		auto keyframeAnimator = std::shared_ptr<IKeyframeAnimator>(IKeyframeAnimator::New());
		keyframeAnimator->SetAnimation(it.second);
		keyframePath->SetKeyframeAnimator(keyframeAnimator);
		keyframeAnimator->SetInstanceManager(instancesManager);
		keyframeAnimator->AssociateInstances(gp);
		bSaveDataOnServer = true;
	}

	BE_ASSERT(DecorationIO->decoration);
	if (bSaveDataOnServer && DecorationIO->decoration) // to save latest instances
		instancesManager->SaveDataOnServer(DecorationIO->decoration->GetId()); // Temporary, TODO: Check if Should we save the association now?


	// Add a population for each object reference
	auto const objReferences = instancesManager->GetObjectReferences();
	for (const auto& objRef : objReferences)
	{
		AITwinPopulation* population = Owner.CreatePopulation(FString(objRef.first.c_str()), objRef.second);
		if (population)
		{
			auto gp = instancesManager->GetInstancesGroup(objRef.second);
			if (gp && gp->GetType() == "animKeyframe")
			{
				auto keyfAnim = DecorationIO->animationKeyframes.find(IAnimationKeyframe::Id(gp->GetName()));
				if (keyfAnim != DecorationIO->animationKeyframes.end()) {
					std::shared_ptr<FITwinPopulationWithPathExt> animExt = std::make_shared<FITwinPopulationWithPathExt>();
					animExt->population_ = population;
					population->AddExtension(animExt);
				}
				else {
					BE_LOGW("keyframeAnim", "animation keyframe: " << gp->GetName() << " not found");
				}
			}
			population->UpdateInstancesFromAVizToUE();
		}

		auto& pathAnimator(DecorationIO->pathAnimator);
		if (pathAnimator)
		{
			const AdvViz::SDK::SharedInstVect& instances = instancesManager->GetInstancesByObjectRef(objRef.first.c_str(), objRef.second);
			for (size_t i = 0; i < instances.size(); ++i)
			{
				AdvViz::SDK::IInstance* inst = instances[i].get();
				if (inst->GetAnimPathId())
				{
					auto AnimPathInfo = pathAnimator->GetAnimationPathInfo(inst->GetAnimPathId().value());
					std::shared_ptr<InstanceWithSplinePathExt> animPathExt = std::make_shared<InstanceWithSplinePathExt>(AnimPathInfo, population, i);
					inst->AddExtension(animPathExt);
					AnimPathInfo->AddExtension(animPathExt);
				}
			}
		}
	}

	bPopulationEnabled = true;

	Owner.OnPopulationsLoaded.Broadcast(true);
}

bool AITwinDecorationHelper::FImpl::LoadSplineIfAllLinkedModelsReady(
	AdvViz::SDK::SharedSpline const& AdvVizSpline,
	AITwinSplineTool* SplineTool,
	const UWorld* World)
{
	AITwinSplineTool::TilesetAccessArray LinkedTilesets;

	// Splines linked to specific models can be loaded now, but only if the corresponding 3D tilesets
	// have all been created (in general, it won't be the case...)
	ITwin::GetLinkedTilesets(LinkedTilesets, AdvVizSpline, World);
	if (LinkedTilesets.Num() < (int32)AdvVizSpline->GetLinkedModels().size())
	{
		// This spline will be loaded later, once all linked tilesets are ready.
		return false;
	}

	std::set<ITwin::ModelLink> LoadedKeys;
	for (auto const& LinkedTileset : LinkedTilesets)
	{
		LoadedKeys.insert(LinkedTileset->GetDecorationKey());
	}

	if (!SplineTool->LoadSpline(AdvVizSpline, std::move(LinkedTilesets)))
	{
		return false;
	}

	// Avoid loading the same splines again.
	ModelsWithLoadedSplines.insert(LoadedKeys.begin(), LoadedKeys.end());

	return true;
}

void AITwinDecorationHelper::FImpl::LoadSplinesInGame(bool bHasLoadedSplines)
{
	checkSlow(IsInGameThread());
	auto& splinesManager(DecorationIO->splinesManager);
	if (!splinesManager)
		return;

	if (!(GEngine && GEngine->GameViewport))
	{
		BE_LOGW("ITwinDecoration", "Splines cannot be loaded in Editor");
		return;
	}

	const UWorld* World = Owner.GetWorld();

	AITwinSplineTool* splineTool =
		(AITwinSplineTool*)UGameplayStatics::GetActorOfClass(World, AITwinSplineTool::StaticClass());

	if (!splineTool)
	{
		BE_LOGW("ITwinDecoration", "Splines can't be loaded because there is no SplineTool actor.");
		return;
	}

	for (auto const& splinePtr : splinesManager->GetSplines())
	{
		LoadSplineIfAllLinkedModelsReady(splinePtr, splineTool, World);
	}

	Owner.OnSplinesLoaded.Broadcast(true);
}

void AITwinDecorationHelper::FImpl::LoadAnnotationsInGame(bool bHasLoadedAnnoations)
{
	checkSlow(IsInGameThread());
	auto& annotationsManager(DecorationIO->annotationsManager);
	if (!annotationsManager)
		return;

	if (!(GEngine && GEngine->GameViewport))
	{
		BE_LOGW("ITwinDecoration", "Annotations cannot be loaded in Editor");
		return;
	}

	Owner.OnAnnotationsLoaded.Broadcast(true);
}

void AITwinDecorationHelper::FImpl::LoadPathAnimationsInGame(bool bHasLoadePathAnimations)
{
}

void AITwinDecorationHelper::InitContentManager()
{
	if (!iTwinContentManager)
	{
		iTwinContentManager = NewObject<UITwinContentManager>();

		// Temporary path, should be replaced by component center download path.
		// TODO_MACOS
		iTwinContentManager->SetContentRootPath(TEXT("C:\\ProgramData\\Bentley\\iTwinEngage\\Content"));
	}

	FITwinMaterialLibrary::InitPaths(*this);
}

void AITwinDecorationHelper::LoadScene()
{
	if (!ensure(Impl->HasITwinID()))
	{
		return;
	}

	// The scene loader thread should wait for iTwin geo-location request
	RegisterWaitableLoadEvent(std::make_unique<ITwin::FITwinGeolocInfoEvent>());

	InitContentManager();

	// This will start the asynchronous loading of materials, populations...
	Impl->StartLoadingDecoration(GetWorld());
}

FString AITwinDecorationHelper::GetContentRootPath() const
{
	if (ensure(iTwinContentManager))
	{
		return iTwinContentManager->GetContentRootPath();
	}
	return {};
}

bool AITwinDecorationHelper::IsLoadingScene() const
{
	return Impl->IsRunningAsyncLoadTask();
}

void AITwinDecorationHelper::RegisterWaitableLoadEvent(std::unique_ptr<FDecorationWaitableLoadEvent>&& LoadEventPtr)
{
	Impl->DecorationIO->RegisterWaitableLoadEvent(std::move(LoadEventPtr));
}

void AITwinDecorationHelper::LoadIModelMaterials(AITwinIModel& IModel)
{
	if (!ensure(Impl->HasITwinID() && Impl->GetLoadedITwinId() == IModel.ITwinId))
	{
		return;
	}
	Impl->StartLoadingIModelMaterials(IModel);
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
			if (bLoadingMaterialsForSpecificModels
				&& !SpecificIModelsForMaterialLoading.contains(imodelid))
			{
				continue;
			}
			AITwinIModel* IModel = ITwin::GetIModelByID(UTF8_TO_TCHAR(imodelid.c_str()), World);
			if (IModel)
			{
				IModel->DetectCustomizedMaterials();
			}
		}
		// Notify any registered client.
		Owner.OnMaterialsLoaded.Broadcast(true);
	}

	bLoadingMaterialsForSpecificModels = false;

	bMaterialEditionEnabled = true;
}

bool AITwinDecorationHelper::FImpl::ShouldSaveScene(bool bPromptUser) const
{
	if (!HasITwinID() || !DecorationIO->decoration)
	{
		return false;
	}

	bool const saveInstances = DecorationIO->instancesManager_
		&& DecorationIO->instancesManager_->HasInstancesToSave();
	bool const saveMaterials = DecorationIO->materialPersistenceMngr
		&& DecorationIO->materialPersistenceMngr->NeedUpdateDB();
	bool const saveScenes = DecorationIO->scene->ShouldSave();
	bool const saveTimeline = DecorationIO->scene->GetTimeline() && DecorationIO->scene->GetTimeline()->ShouldSave();
	bool const saveSplines = DecorationIO->splinesManager && DecorationIO->splinesManager->HasSplinesToSave();
	bool const saveAnnotations = DecorationIO->annotationsManager && DecorationIO->annotationsManager->HasAnnotationToSave();

	if (!saveInstances && !saveMaterials && !saveScenes && !saveTimeline && !saveSplines && !saveAnnotations)
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

bool AITwinDecorationHelper::ShouldSaveScene(bool bPromptUser /*= true*/) const
{
	return Impl->ShouldSaveScene(bPromptUser);
}

void AITwinDecorationHelper::SaveScene(bool bPromptUser /*= true*/)
{
	Impl->SaveScene(FImpl::SaveRequestOptions{ .bPromptUser = bPromptUser });
}

void AITwinDecorationHelper::SaveSceneOnExit(bool bPromptUser /*= true*/)
{
	Impl->SaveScene(FImpl::SaveRequestOptions{ .bUponExit = true, .bPromptUser = bPromptUser });
}

void AITwinDecorationHelper::FImpl::OnDecorationSaved_GameThread(bool bSaved, bool bHasResetMaterials)
{
	Owner.OnSceneSaved.Broadcast(bSaved);

	// Now that material definitions have been reset, update the iModels
	if (bSaved && bHasResetMaterials)
	{
		for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
		{
			IModelIter->ReloadCustomizedMaterials();
		}
	}
}

size_t AITwinDecorationHelper::FImpl::LoadSplinesLinkedToModel(ITwin::ModelLink const& Key,
	FITwinTilesetAccess& TilesetAccess)
{
	if (ModelsWithLoadedSplines.find(Key) != ModelsWithLoadedSplines.end())
	{
		// Already done.
		return 0;
	}
	if (!TilesetAccess.HasTileset())
	{
		BE_LOGW("ITwinDecoration", "Linked splines can't be loaded (no tileset yet).");
		return 0;
	}
	if (!DecorationIO->splinesManager)
	{
		return 0;
	}
	// Find linked splines
	auto const LinkedSplines = ITwin::GetLinkedSplines(*DecorationIO->splinesManager, Key);
	if (LinkedSplines.empty())
	{
		return 0;
	}
	const UWorld* World = Owner.GetWorld();
	AITwinSplineTool* SplineTool =
		(AITwinSplineTool*)UGameplayStatics::GetActorOfClass(World, AITwinSplineTool::StaticClass());
	if (!SplineTool)
	{
		BE_LOGW("ITwinDecoration", "Linked splines can't be loaded because there is no SplineTool actor.");
		return 0;
	}

	size_t LoadedSplines = 0;
	for (auto const& Spline : LinkedSplines)
	{
		if (LoadSplineIfAllLinkedModelsReady(Spline, SplineTool, World))
			LoadedSplines++;
	}
	return LoadedSplines;
}

void AITwinDecorationHelper::OnIModelLoaded(bool bSuccess, FString StringId)
{
	// Find model
	AITwinIModel* Model = ITwin::GetIModelByID(StringId, GetWorld());
	if (Model)
	{
		TUniquePtr<FITwinTilesetAccess> TilesetAccess = Model->MakeTilesetAccess();

		// Find link
		auto key = std::make_pair(EITwinModelType::IModel, Model->IModelId);
		auto iter = Impl->DecorationIO->links.find(key);
		if (iter != Impl->DecorationIO->links.end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			TilesetAccess->ApplyLoadedInfo(si, true);
		}
		// Load linked splines if needed
		Impl->LoadSplinesLinkedToModel(key, *TilesetAccess);
	}
}
	
void AITwinDecorationHelper::OnRealityDataLoaded(bool bSuccess, FString StringId)
{
	// Find RealityData
	AITwinRealityData* RealityData = ITwin::GetRealityDataByID(StringId, GetWorld());
	if (RealityData)
	{
		TUniquePtr<FITwinTilesetAccess> TilesetAccess = RealityData->MakeTilesetAccess();

		// Find link
		auto key = std::make_pair(EITwinModelType::RealityData, RealityData->RealityDataId);
		auto iter = Impl->DecorationIO->links.find(key);
		if (iter != Impl->DecorationIO->links.end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			TilesetAccess->ApplyLoadedInfo(si, true);
		}
		// Load linked splines if needed
		Impl->LoadSplinesLinkedToModel(key, *TilesetAccess);
	}
}

void AITwinDecorationHelper::FImpl::OnSceneLoad_GameThread(bool bSuccess)
{
	 // Must be called *before* the loops below, as it will actually instantiate the iModels/RealityDatas if
	 // needed...
	Owner.OnSceneLoaded.Broadcast(bSuccess);

	// Note that visibility will *not* be restored at this point, because it requires a tileset, and both
	// iModels and reality-data were just created, and thus do not have the tileset yet.
	// Hence the need to bind OnIModelLoaded/OnRealityDatalLoaded and re-apply the scene information to the
	// model (via its tileset access) in the corresponding callback.

	for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
	{
		auto key = std::make_pair(EITwinModelType::IModel, IModelIter->IModelId);
		auto iter = DecorationIO->links.find(key);
		if (iter != DecorationIO->links.end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			IModelIter->MakeTilesetAccess()->ApplyLoadedInfo(si, false);
			IModelIter->OnIModelLoaded.AddUniqueDynamic(&Owner, &AITwinDecorationHelper::OnIModelLoaded);
		}
	}
	for (TActorIterator<AITwinRealityData> IReallIter(Owner.GetWorld()); IReallIter; ++IReallIter)
	{
		auto key = std::make_pair(EITwinModelType::RealityData, IReallIter->RealityDataId);
		auto iter = DecorationIO->links.find(key);
		if (iter != DecorationIO->links.end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			IReallIter->MakeTilesetAccess()->ApplyLoadedInfo(si, false);
			IReallIter->OnRealityDataLoaded.AddUniqueDynamic(&Owner, &AITwinDecorationHelper::OnRealityDataLoaded);
		}
	}

	LoadCameras();
}

AITwinPopulation* AITwinDecorationHelper::GetPopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const
{
	TArray<AActor*> populations;
    UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), AITwinPopulation::StaticClass(), populations);

	std::string stdAssetPath = ITwin::ConvertToStdString(assetPath);

    for (AActor* actor : populations)
    {
		AITwinPopulation* pop = Cast<AITwinPopulation>(actor);
		if (pop->GetObjectRef() == stdAssetPath && pop->GetInstanceGroupId() == groupId)
		{
			return pop;
		}
	}

	return nullptr;
}

AITwinKeyframePath* AITwinDecorationHelper::CreateKeyframePath() const
{
	return GetWorld()->SpawnActor<AITwinKeyframePath>();
}

AITwinPopulation* AITwinDecorationHelper::CreatePopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const
{
	std::shared_ptr<AdvViz::SDK::IInstancesGroup> gp =
		Impl->DecorationIO->instancesManager_->GetInstancesGroup(groupId);
	if (!gp)
	{
		BE_ISSUE("invalid group ID", groupId.ID(), groupId.GetDBIdentifier());
		return nullptr;
	}

	if (iTwinContentManager)
		iTwinContentManager->DownloadFromAssetPath(assetPath);

	AITwinPopulation* population = AITwinPopulation::CreatePopulation(this, assetPath,
		Impl->DecorationIO->instancesManager_, gp);

	return population;
}

AITwinPopulation* AITwinDecorationHelper::GetOrCreatePopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const
{
	AITwinPopulation* population = GetPopulation(assetPath, groupId);
	if (population)
	{
		return population;
	}

	return CreatePopulation(assetPath, groupId);
}

AdvViz::SDK::RefID AITwinDecorationHelper::GetStaticInstancesGroupId() const
{
	if (Impl->DecorationIO && Impl->DecorationIO->staticInstancesGroup)
	{
		return Impl->DecorationIO->staticInstancesGroup->GetId();
	}
	BE_ISSUE("no group to hold static instances");
	return AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinDecorationHelper::GetInstancesGroupIdForSpline(AITwinSplineHelper const& Spline) const
{
	if (!(Impl->DecorationIO && Impl->DecorationIO->instancesManager_))
	{
		BE_ISSUE("no instance manager");
		return AdvViz::SDK::RefID::Invalid();
	}
	auto& instancesManager(*Impl->DecorationIO->instancesManager_);
	if (!Spline.GetAVizSpline())
	{
		BE_ISSUE("no core spline");
		return AdvViz::SDK::RefID::Invalid();
	}
	std::shared_ptr<AdvViz::SDK::IInstancesGroup> gp =
		instancesManager.GetInstancesGroupBySplineID(Spline.GetAVizSpline()->GetId());
	if (!gp)
	{
		// No group for this spline yet: create it now.
		gp.reset(AdvViz::SDK::IInstancesGroup::New());
		gp->SetName(TCHAR_TO_UTF8(*Spline.GetActorNameOrLabel()));
		gp->SetType("spline");
		gp->SetLinkedSplineId(Spline.GetAVizSpline()->GetId());
		instancesManager.AddInstancesGroup(gp);
	}
	return gp->GetId();
}

int32 AITwinDecorationHelper::GetPopulationInstanceCount(FString assetPath, const AdvViz::SDK::RefID& groupId) const
{
	return Impl->DecorationIO->instancesManager_->GetInstanceCountByObjectRef(ITwin::ConvertToStdString(assetPath), groupId);
}

AdvViz::SDK::ITwinAtmosphereSettings AITwinDecorationHelper::GetAtmosphereSettings() const
{
	return Impl->DecorationIO->scene->GetAtmosphere();
}

void AITwinDecorationHelper::SetAtmosphereSettings(const AdvViz::SDK::ITwinAtmosphereSettings& as) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
		Impl->DecorationIO->scene->SetAtmosphere(as);
}
AdvViz::SDK::ITwinSceneSettings AITwinDecorationHelper::GetSceneSettings() const
{
	// [Julot] I add a crash in Editor, when starting PIE *after* having instantiated an iModel manually in
	// the level (which is not a relevant workflow for Carrot, but could perfectly happen in the plugin, when
	// decoration is fully supported there.
	if (ensure(Impl->DecorationIO && Impl->DecorationIO->scene))
	{
		return Impl->DecorationIO->scene->GetSceneSettings();
	}
	else
	{
		return {};
	}
}

void AITwinDecorationHelper::SetSceneSettings(const AdvViz::SDK::ITwinSceneSettings& as) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
		Impl->DecorationIO->scene->SetSceneSettings(as);
}


ITwinSceneInfo AITwinDecorationHelper::GetSceneInfo(const ModelIdentifier& Key) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(Key);
		if (iter != Impl->DecorationIO->links.end())
			return ITwin::LinkToSceneInfo(*iter->second);

	}
	return ITwinSceneInfo();
}

void AITwinDecorationHelper::SetSceneInfo(const ModelIdentifier& Key, const ITwinSceneInfo& si) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(Key);
		std::shared_ptr<AdvViz::SDK::ILink> sp;

		if (iter == Impl->DecorationIO->links.end())
			sp = Impl->DecorationIO->CreateLink(Key);
		else
			sp = iter->second;

		if (sp)
		{
			ITwin::SceneToLink(si, sp);
		}
	}
}


void AITwinDecorationHelper::CreateLinkIfNeeded(EITwinModelType ct, const FString& id) const
{
	auto const Key = std::make_pair(ct, id);
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(Key);
		if (iter == Impl->DecorationIO->links.end())
		{
			auto sp = Impl->DecorationIO->CreateLink(Key);
			ITwin::SceneToLink(ITwinSceneInfo(), sp);
		}
	}
}

std::vector<ITwin::ModelLink> AITwinDecorationHelper::GetLinkedElements() const
{
	std::vector<ITwin::ModelLink>  res;
	for (auto link : Impl->DecorationIO->links)
	{
		res.push_back(link.first);
	}
	return res;
}



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

void AITwinDecorationHelper::OnCloseRequested(FViewport*)
{
	if (!bOverrideOnSceneClose_)
		SaveSceneOnExit();
}

void AITwinDecorationHelper::FImpl::DeleteAllCustomMaterials()
{
	if (!IsMaterialEditionEnabled())
		return;
	if (!(DecorationIO && DecorationIO->materialPersistenceMngr))
		return;
	if (GetLoadedITwinId().IsEmpty())
		return;

	if (FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
		FText::FromString(
			"Are you sure you want to reset all material definitions to default for current model?" \
			"\n\nBeware it will have an impact to all users sharing this iModel, and that it cannot be undone!"),
		FText::FromString("")) == EAppReturnType::Yes)
	{
		for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
		{
			const std::string imodelId = TCHAR_TO_ANSI(*IModelIter->IModelId);
			DecorationIO->materialPersistenceMngr->RequestDeleteIModelMaterialsInDB(imodelId);
		}
		// Propose to save at once (with a specific flag set to perform refresh at the end).
		SaveScene(SaveRequestOptions{ .bUponCustomMaterialsDeletion = true });
	}
}

void AITwinDecorationHelper::FImpl::PreSaveCameras()
{
	if (!DecorationIO || !DecorationIO->scene)
		return;
	auto* PlayerController = Owner.GetWorld()->GetFirstPlayerController();

	if (PlayerController)
	{
		ITwinSceneInfo si;
		si.Offset = ScreenUtils::GetCurrentViewTransform(Owner.GetWorld());
		std::shared_ptr<AdvViz::SDK::ILink> clink;
		for (auto link : DecorationIO->scene->GetLinks())
		{
			if (link->GetType() == "camera" && link->GetRef() == "Main Camera")
			{
				clink = link;
				break;
			}
		}
		if (!clink)
		{
			clink = DecorationIO->scene->MakeLink();
			DecorationIO->scene->AddLink(clink);
			clink->SetType("camera");
			clink->SetRef("Main Camera");
		}
		ITwin::SceneToLink(si, clink);
	}
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(Owner.GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
	if (!PlayerStarts.IsEmpty())
	{

		ITwinSceneInfo si;
		si.Offset = PlayerStarts[0]->GetActorTransform();
		std::shared_ptr<AdvViz::SDK::ILink> clink;
		for (auto link : DecorationIO->scene->GetLinks())
		{
			if (link->GetType() == "camera" && link->GetRef() =="Home Camera") 
			{
				clink = link;
				break;
			}
		}
		if (!clink)
		{
			clink = DecorationIO->scene->MakeLink();
			DecorationIO->scene->AddLink(clink);
			clink->SetType("camera");
			clink->SetRef("Home Camera");
			ITwin::SceneToLink(si, clink); //save only if not exist ( otherwise it is already set )
		}
	}
}

void AITwinDecorationHelper::FImpl::LoadCameras()
{
	bool homeC = false;
	bool mainC = false;
	std::shared_ptr<AdvViz::SDK::ILink> clink;
	for (auto link : DecorationIO->scene->GetLinks())
	{
		if (link->GetType() == "camera" && link->GetRef() == "Home Camera")
		{
			auto si = ITwin::LinkToSceneInfo(*link);
			if (si.Offset.has_value())
			{
				homeC = true;
			}
		}
		if (link->GetType() == "camera" && link->GetRef() == "Main Camera")
		{
			auto si = ITwin::LinkToSceneInfo(*link);
			if (si.Offset.has_value())
			{
				ScreenUtils::SetCurrentView(Owner.GetWorld(),*si.Offset);
				mainC = true;
			}
		}
	}
	if (homeC && !mainC)
	{
		ScreenUtils::SetCurrentView(Owner.GetWorld(), Owner.GetHomeCamera());
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


std::shared_ptr<AITwinDecorationHelper::SaveLocker> AITwinDecorationHelper::LockSave()
{
	if (auto locker = Impl->saveLocker.lock())
		return locker;
	else
	{
	 auto res =	std::shared_ptr<AITwinDecorationHelper::SaveLocker>(new SaveLockerImpl(this));
	 Impl->saveLocker = res;
	 return res;
	}
}

bool AITwinDecorationHelper::IsSaveLocked()
{
	return !Impl->saveLocker.expired();
}

void AITwinDecorationHelper::SetHomeCamera(const FTransform& ft)
{
	ITwinSceneInfo si;
	si.Offset = ft;
	std::shared_ptr<AdvViz::SDK::ILink> clink;
	for (auto link : Impl->DecorationIO->scene->GetLinks())
	{
		if (link->GetType() == "camera" && link->GetRef() == "Home Camera")
		{
			clink = link;
			break;
		}
	}
	if (!clink)
	{
		clink = Impl->DecorationIO->scene->MakeLink();
		Impl->DecorationIO->scene->AddLink(clink);
		clink->SetType("camera");
		clink->SetRef("Home Camera");
	}
	ITwin::SceneToLink(si, clink);
}

FTransform AITwinDecorationHelper::GetHomeCamera() const
{
	for (auto link : Impl->DecorationIO->scene->GetLinks())
	{
		if (link->GetType() == "camera" && link->GetRef() == "Home Camera")
		{
			auto si = ITwin::LinkToSceneInfo(*link);
			if (si.Offset.has_value())
			{
				return *si.Offset;
			}
		}
	}
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
	if (!PlayerStarts.IsEmpty())
	{
		return PlayerStarts[0]->GetActorTransform();
	}
	BE_ASSERT(false);
	return FTransform::Identity;
}

FString AITwinDecorationHelper::GetSceneID() const
{
	
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
		return FString(Impl->DecorationIO->scene->GetId().c_str());
	else
		return FString();
}

void AITwinDecorationHelper::InitDecorationService()
{
	InitContentManager();
	Impl->InitDecorationService();
}

AdvViz::expected<std::vector<std::shared_ptr<AdvViz::SDK::IScenePersistence>>, int> AITwinDecorationHelper::GetITwinScenes(const FString& itwinid)
{
	return Impl->DecorationIO->GetITwinScenes(itwinid);
}

std::shared_ptr<AdvViz::SDK::IAnnotationsManager> AITwinDecorationHelper::GetAnnotationManager() const
{
	return Impl->DecorationIO->annotationsManager;
}

std::string AITwinDecorationHelper::ExportHDRIAsJson(AdvViz::SDK::ITwinHDRISettings const& hdri) const
{
	return Impl->DecorationIO->scene->ExportHDRIAsJson(hdri);
}

bool AITwinDecorationHelper::ConvertHDRIJsonFileToKeyValueMap(std::string assetPath, AdvViz::SDK::KeyValueStringMap& keyValueMap) const
{
	return Impl->DecorationIO->scene->ConvertHDRIJsonFileToKeyValueMap(assetPath, keyValueMap);
}

void AITwinDecorationHelper::Lock(SaveLockerImpl* saver)
{
	saver->sceneStatus = Impl->DecorationIO->scene && Impl->DecorationIO->scene->ShouldSave();
	for (auto link : Impl->DecorationIO->links)
	{
		saver->linksStatus[link.first] = link.second->ShouldSave();
	}
	saver->timelineStatus = Impl->DecorationIO->scene->GetTimeline() && Impl->DecorationIO->scene->GetTimeline()->ShouldSave();
}

void AITwinDecorationHelper::Unlock(SaveLockerImpl* saver )
{
	if (Impl->DecorationIO->scene)
	{
		Impl->DecorationIO->scene->SetShouldSave(saver->sceneStatus);
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
	if (Impl->DecorationIO->scene->GetTimeline())
		Impl->DecorationIO->scene->GetTimeline()->SetShouldSave(saver->timelineStatus);
}

bool AITwinDecorationHelper::DeleteLoadedScene()
{

	if (FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::YesNo,
		FText::FromString("Do you want to delete the current scene? (It will close the scene)"),
		FText::FromString("")) != EAppReturnType::Yes)
	{
		return false;
	}
	Impl->DecorationIO->scene->Delete();
	return true;
}

void AITwinDecorationHelper::RemoveComponent(EITwinModelType ct, const FString& id) const
{
	auto key = std::make_pair(ct, id);

	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		auto iter = Impl->DecorationIO->links.find(key);
		if (iter == Impl->DecorationIO->links.end())
			return;
		else
			iter->second->Delete();
	}
}

void AITwinDecorationHelper::ConnectSplineToolToSplinesManager(AITwinSplineTool* splineTool)
{
	splineTool->SetSplinesManager(Impl->DecorationIO->GetSplinesManager());
}

void AITwinDecorationHelper::ConnectPathAnimator(AITwinAnimPathManager* manager)
{
	manager->SetPathAnimator(Impl->DecorationIO->GetPathAnimator());
}

void AITwinDecorationHelper::SetDecoGeoreference(const FVector& latLongHeight)
{
	Impl->DecorationIO->SetDecoGeoreference(latLongHeight);
}

AdvViz::expected<void, std::string> AITwinDecorationHelper::InitDecoGeoreference()
{ 
	return Impl->DecorationIO->InitDecoGeoreference();
}
