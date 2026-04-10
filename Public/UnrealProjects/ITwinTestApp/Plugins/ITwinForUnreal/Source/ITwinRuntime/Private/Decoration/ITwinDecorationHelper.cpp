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
#include <ITwinDigitalTwinManager.h>
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
#	include "SDK/Core/Tools/DelayedCall.h"
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#   include "SDK/Core/Visualization/SplinesManager.h"
#   include "SDK/Core/Visualization/KeyframeAnimator.h"
#   include "SDK/Core/Visualization/ScenePersistenceAPI.h"
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

	std::set<ModelLink> GetSplineModelLinks(AdvViz::SDK::ISplinePtr const& Spline)
	{
		std::set<ModelLink> Links;
		if (Spline)
		{
			auto spline = Spline->GetRAutoLock();
			for (auto const& ModelLink : spline->GetLinkedModels())
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
		AdvViz::SDK::ISplinePtr const& Spline,
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

	AdvViz::SDK::ISplinePtrVect GetLinkedSplines(AdvViz::SDK::ISplinesManager const& SplinesManager,
		ModelLink const& Key)
	{
		AdvViz::SDK::ISplinePtrVect LinkedSplines;
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

	inline AITwinDigitalTwinManager* GetDigitalITwinManagerByID(FString const& ITwinId, UWorld const* World)
	{
		if (!World)
		{
			BE_ISSUE("no world given");
			return nullptr;
		}
		// For now, decoration is defined at the iTwin level. Look if a helper already exists for the given
		// iTwin:
		for (TActorIterator<AITwinDigitalTwinManager> Iter(World); Iter; ++Iter)
		{
			if ((**Iter).GetITwinId() == ITwinId)
			{
				return *Iter;
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
			if (auto* ITwinManager = GetDigitalITwinManagerByID(ITwinId, World))
			{
				DecoHelper->OnSceneLoadingStartStop.AddDynamic(ITwinManager, &AITwinDigitalTwinManager::OnSceneLoadingStartStop);
			}
			DecoHelper->SetLoadedITwinId(ITwinId);
		}
		DecoHelper->LoadScene();
	}

	void LoadIModelDecorationMaterials(AITwinIModel& IModel, UWorld* World)
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
	SaveLockerImpl(AITwinDecorationHelper* InOwner)
		: Owner(InOwner)
	{
		Owner->Lock(*this);
	}

	~SaveLockerImpl()
	{
		if (ensure(Owner.IsValid()))
		{
			Owner->Unlock(*this);
		}
	}

	TWeakObjectPtr<AITwinDecorationHelper> Owner;
	bool sceneStatus;
	bool timelineStatus;
	std::map< ITwin::ModelLink, bool > linksStatus;

};

/* -------------------------- class AITwinDecorationHelper::FImpl ---------------------------*/

/// Hold implementation details for asynchronous tasks regarding the decoration service.
class AITwinDecorationHelper::FImpl
{
public:
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

	bool IsLoadingScene() const { return RemainingLoadingSceneTasks > 0; }
	bool IsSavingScene() const { return bIsSavingScene; }

	void InitDecorationService();
	void SetLoadedITwinId(FString const& LoadedITwinId);
	FString GetLoadedITwinId() const;
	bool HasITwinID() const;

	void StartLoadingDecoration(UWorld* WorldContextObject);
	void StartLoadingIModelMaterials(AITwinIModel& IModel);

	bool ShouldSaveScene() const;
	void SaveScene(FSaveRequestOptions const& opts);

	// Ask confirmation if the task is taking too long - return true if the user confirmed the abortion.
	bool ShouldAbort();

	void DeleteAllCustomMaterials();
	size_t LoadSplinesLinkedToModel(ITwin::ModelLink const& Key, FITwinTilesetAccess& TilesetAccess);
	void LoadPopulationsInGame(bool bHasLoadedPopulations);
	void CreateOrRefreshPopulationInGame(FString const& assetPath, AdvViz::SDK::RefID const& groupId);

private:
	void AsyncLoadScene();
	void LoadMaterialsStep();
	void LoadSplinesStep();
	void LoadPathAnimationsStep();
	void LoadPopulationsStep();
	void LoadAnnotationsStep();
	void AsyncLoadMaterials(TMap<FString, TWeakObjectPtr<AITwinIModel>> && IModelMap, bool bForSpecificModels);
	void ResetTicker();
	
	std::shared_ptr<FDecorationAsyncIOHelper> GetDecorationAsyncIOHelper() const;

	void DissociateAnimation(const std::string& animId);
	void CreateKeyframeAnimPopulation();
	void LoadSplinesInGame(bool bHasLoadedSplines);
	bool LoadSplineIfAllLinkedModelsReady(
		AdvViz::SDK::ISplinePtr const& AdvVizSpline,
		AITwinSplineTool* SplineTool,
		const UWorld* World);
	void LoadAnnotationsInGame(bool bHasLoadedAnnoations);
	void LoadPathAnimationsInGame(bool bHasLoadePathAnimations);

	void OnCustomMaterialsLoaded_GameThread(bool bHasLoadedMaterials);
	void OnDecorationSaved_GameThread(bool bSuccess, bool bHasResetMaterials);
	void OnSceneLoad_GameThread(bool bSuccess);

	void PreSaveCameras();
	void LoadCameras();

	void FinishedALoadingTask()
	{
		BE_ASSERT(RemainingLoadingSceneTasks > 0);
		if (RemainingLoadingSceneTasks.fetch_sub(1) == 1) //note:fetch_sub return old value
		{	// All tasks complete
			CurrentContext = EAsyncContext::None;
			Owner.OnDecorationLoaded.Broadcast();
		}
	}

public:
	std::shared_ptr<FDecorationAsyncIOHelper> DecorationIO;
	EITwinDecorationClientMode ClientMode = EITwinDecorationClientMode::Unknown;
	std::weak_ptr<SaveLocker> saveLocker;
	bool ReadOnly = false;

private:
	bool bPopulationEnabled = false;
	bool bMaterialEditionEnabled = false;
	
	std::atomic_int RemainingLoadingSceneTasks = 0;
	std::atomic_bool bIsSavingScene = false;
	std::shared_ptr<std::atomic_bool> IsThisValid = std::make_shared<std::atomic_bool>(true);

	std::atomic<EAsyncContext> CurrentContext = EAsyncContext::None;

	FTSTicker::FDelegateHandle TickerDelegate;
	std::chrono::system_clock::time_point NextConfirmTime = std::chrono::system_clock::now();
	FString ConfirmAbortMsg;
	int ConfirmOccurrences = 0;
	bool bIsDisplayingConfirmMsg = false;
	AITwinDecorationHelper& Owner;
	bool bIsDeletingCustomMaterials = false;
	std::function<void()> OnSceneSavedCallback = {}; // used when iTS requests Unreal to close.
	TMap<FString, TWeakObjectPtr<AITwinIModel>> PendingIModelsForMaterials;
	bool bLoadingMaterialsForSpecificModels = false;
	std::set<std::string> SpecificIModelsForMaterialLoading;
	std::set<ITwin::ModelLink> ModelsWithLoadedSplines;

};


// The error can be voluntarily left empty sometimes (on new scene with no decoration, typically)
// => it should not appear as an error in the logs.
#define BE_LOG_LOAD_UNEXP(contentType, exp) {										\
	auto const StrError = exp.error();												\
	if (StrError.empty()) {															\
		BE_LOGI("ITwinDecoration", "No " << contentType << " loaded from server");	\
	}																				\
	else {																			\
		BE_LOGE("ITwinDecoration", "Failed to load " << contentType << " from server: " << StrError);\
	}																				\
}


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
	ConfirmAbortMsg.Empty();
	ConfirmOccurrences = 0;
	bIsDisplayingConfirmMsg = false;
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

	// Start the asynchronous loading of materials, populations...
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
		AsyncLoadMaterials(std::move(IdToIModel), true);
	}
}

void AITwinDecorationHelper::FImpl::AsyncLoadScene()
{
	if (RemainingLoadingSceneTasks > 0)
	{
		BE_LOGW("ITwinDecoration", "Scene loading already in progress");
		return;
	}

	Owner.OnSceneLoadingStartStop.Broadcast(true);

	RemainingLoadingSceneTasks = 1;
	CurrentContext = EAsyncContext::Load;
	ResetTicker();

	auto DecoIO = GetDecorationAsyncIOHelper();
	auto IsValidLambda = IsThisValid;

	// Capture timeline actor for LoadScenes task
	AITwinTimelineActor* timelineActor = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(
		Owner.GetWorld(), AITwinTimelineActor::StaticClass());
	const bool bHasTimeline = (timelineActor != nullptr);

	TWeakObjectPtr<AITwinTimelineActor> timelineActorPtr(timelineActor);
	// Note: do not use a strong pointer, it may still be ignored by the GC (eg. when closing PIE...),
	// and those used in this file were responsible for a leak when closing PIE (yes!!), maybe some
	// async task was still on hold but I have no idea which, the ref chain didn't tell...
	// Just passing pOwner=&Owner and testing IsValid(pOwner) would have been equivalent and cleaner,
	// but refactoring was faster keeping the smart pointer semantics instead of converting to raw ptrs.
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);

	// Start LoadScenes on background thread
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, timelineActorPtr, bHasTimeline, ownerPtr]()
	{
		if ((bHasTimeline && !timelineActorPtr.IsValid()) || !ownerPtr.IsValid())
			return;
		DecoIO->AsyncLoadScene([DecoIO, timelineActorPtr, bHasTimeline, ownerPtr](AdvViz::expected<bool, std::string> const& exp)
		{
			if ((bHasTimeline && !timelineActorPtr.IsValid()) || !ownerPtr.IsValid())
				return;
			auto& This(ownerPtr.Get()->Impl);
			if (!exp)
			{
				BE_LOG_LOAD_UNEXP("scene", exp);
				This->FinishedALoadingTask();
				return;
			}
			bool bLoadSuccess = *exp;
			auto timelineActor = timelineActorPtr.Pin();
			if (DecoIO->scene && DecoIO->scene->GetTimeline() && timelineActor)
			{
				timelineActor->SetTimelineSDK(DecoIO->scene->GetTimeline());
			}
			DecoIO->WaitForExternalLoadEvents(60);
			// Chain to game thread for post-processing
			AsyncTask(ENamedThreads::GameThread,
				[ownerPtr, bLoadSuccess]()
				{
					if (!ownerPtr.IsValid())
						return;
					auto& This(ownerPtr.Get()->Impl);
					ON_SCOPE_EXIT{ This->FinishedALoadingTask(); };
					This->OnSceneLoad_GameThread(bLoadSuccess);
					if (bLoadSuccess)
						This->DecorationIO->scene->SetShouldSave(false);

					AITwinTimelineActor* tla = (AITwinTimelineActor*)UGameplayStatics::GetActorOfClass(
						This->Owner.GetWorld(), AITwinTimelineActor::StaticClass());
					if (tla)
						tla->OnLoad();

					// load in parallel:

					// Chain to LoadMaterials
					This->LoadMaterialsStep();
					// Chain to LoadPopulations
					This->LoadPopulationsStep(); // will also load Splines & Animations

					// Chain to LoadAnnotations
					This->LoadAnnotationsStep();
				});
			});
	});
}


void AITwinDecorationHelper::FImpl::LoadMaterialsStep()
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	TMap<FString, TWeakObjectPtr<AITwinIModel>> IdToIModel;
	for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
	{
		IdToIModel.Emplace((**IModelIter).IModelId, *IModelIter);
	}
	RemainingLoadingSceneTasks++;
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, IdToIModel = std::move(IdToIModel), ownerPtr]() mutable
		{
			if (!ownerPtr.IsValid())
				return;
			DecoIO->AsyncLoadMaterials(std::move(IdToIModel),
				[ownerPtr](AdvViz::expected<void, std::string> const& exp)
			{
				auto& This(ownerPtr.Get()->Impl);
				if (!exp)
				{
					BE_LOG_LOAD_UNEXP("custom materials", exp);
					This->FinishedALoadingTask();
					return;
				}
				AsyncTask(ENamedThreads::GameThread,
					[ownerPtr]()
					{
						if (!ownerPtr.IsValid())
							return;
						auto& This(ownerPtr.Get()->Impl);
						ON_SCOPE_EXIT{ This->FinishedALoadingTask(); };
						This->OnCustomMaterialsLoaded_GameThread(true);
					});
			});
		});
}

void AITwinDecorationHelper::FImpl::LoadSplinesStep()
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	RemainingLoadingSceneTasks++;
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, ownerPtr]()
		{
			if (!ownerPtr.IsValid())
				return;
			DecoIO->AsyncLoadSplines(
				[ownerPtr](AdvViz::expected<void, std::string> const& exp)
				{
					if (!ownerPtr.IsValid())
						return;
					auto& This(ownerPtr.Get()->Impl);
					if (!exp)
					{
						BE_LOG_LOAD_UNEXP("splines", exp);
						This->FinishedALoadingTask();
						return;
					}
					// Chain to LoadPathAnimations
					This->LoadPathAnimationsStep();

					AsyncTask(ENamedThreads::GameThread,
						[ownerPtr]()
						{
							if (!ownerPtr.IsValid())
								return;
							auto& This(ownerPtr.Get()->Impl);
							ON_SCOPE_EXIT{ This->FinishedALoadingTask(); };
							This->LoadSplinesInGame(true);
						});
				}
			);
		});
}

void AITwinDecorationHelper::FImpl::LoadPathAnimationsStep()
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	RemainingLoadingSceneTasks++;
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, ownerPtr]()
		{
			if (!ownerPtr.IsValid())
				return;
			DecoIO->AsyncLoadPathAnimations(
				[ownerPtr](AdvViz::expected<void, std::string> const& exp)
			{
				if (!ownerPtr.IsValid())
					return;
				if (!exp)
				{
					BE_LOG_LOAD_UNEXP("path animations", exp);
					auto& This(ownerPtr.Get()->Impl);
					This->FinishedALoadingTask();
					return;
				}
				AsyncTask(ENamedThreads::GameThread,
					[ownerPtr]()
					{
						if (!ownerPtr.IsValid())
							return;
						auto& This(ownerPtr.Get()->Impl);
						ON_SCOPE_EXIT{ This->FinishedALoadingTask(); };
						This->LoadPathAnimationsInGame(true);
					});
			});
		});
}

void AITwinDecorationHelper::FImpl::LoadPopulationsStep()
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);
	RemainingLoadingSceneTasks++;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, ownerPtr]()
	{
		if (!ownerPtr.IsValid())
			return;
		DecoIO->AsyncLoadPopulations(
			[ownerPtr](AdvViz::expected<void, std::string> const& result)
			{
				if (!ownerPtr.IsValid())
					return;
				auto& This(ownerPtr.Get()->Impl);

				// Start loading splines (even though the populations failed to load: remember that most of
				// splines are unrelated to populations...)
				This->LoadSplinesStep();

				// Be careful with new scenes, where the population will fail to load, but we should still
				// enable future population creation => we will distinguish a "real" failure case from a
				// "regular" one (see boolean bHasRealErrror below), and only return in case of a real error.
				// (AzDev#1993945).
				bool bHasLoadedPopulations = false;
				if (!result)
				{
					BE_LOG_LOAD_UNEXP("populations", result);
					bool const bHasRealErrror = !result.error().empty();
					if (bHasRealErrror)
					{
						// In case of real load error, population will remain disabled as before.
						This->FinishedALoadingTask();
						return;
					}
				}
				else
				{
					BE_LOGI("ITwinDecoration", "Populations loaded successfully from server");
					bHasLoadedPopulations = true;
				}
				AsyncTask(ENamedThreads::GameThread,
					[ownerPtr, bHasLoadedPopulations]()
					{
						if (!ownerPtr.IsValid())
							return;
						auto& This(ownerPtr.Get()->Impl);
						This->LoadPopulationsInGame(bHasLoadedPopulations);
						This->FinishedALoadingTask();
					});
			}
		);
	});
}

void AITwinDecorationHelper::FImpl::LoadAnnotationsStep()
{
	auto DecoIO = GetDecorationAsyncIOHelper();
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);

	RemainingLoadingSceneTasks++;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, ownerPtr]()
		{
			if (!ownerPtr.IsValid())
				return;
			DecoIO->AsyncLoadAnnotations(
				[ownerPtr](AdvViz::expected<void, std::string> const&)
				{
					if (!ownerPtr.IsValid())
						return;
					AsyncTask(ENamedThreads::GameThread,
						[ownerPtr]()
						{
							if (!ownerPtr.IsValid())
								return;
							auto& This(ownerPtr.Get()->Impl);
							ON_SCOPE_EXIT{This->FinishedALoadingTask();};
							This->LoadAnnotationsInGame(true);
						});
				}
			);
		});
}

// Simplify AsyncLoadMaterials:

void AITwinDecorationHelper::FImpl::AsyncLoadMaterials(
	TMap<FString, TWeakObjectPtr<AITwinIModel>> && IdToIModel,
	bool bForSpecificModels)
{
	if (IsLoadingScene())
	{
		// Already loading, materials will be loaded as part of the chain
		return;
	}

	bLoadingMaterialsForSpecificModels = bForSpecificModels;
	SpecificIModelsForMaterialLoading.clear();
	if (bForSpecificModels)
	{
		for (auto const& [StrId, _] : IdToIModel)
		{
			SpecificIModelsForMaterialLoading.insert(TCHAR_TO_UTF8(*StrId));
		}
	}

	auto DecoIO = GetDecorationAsyncIOHelper();
	TWeakObjectPtr<AITwinDecorationHelper> ownerPtr(&Owner);
	RemainingLoadingSceneTasks++;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[DecoIO, IdToIModel = std::move(IdToIModel), specificModels = SpecificIModelsForMaterialLoading, ownerPtr]() mutable
	{
		if (!ownerPtr.IsValid())
			return;
		DecoIO->AsyncLoadMaterials(
			std::move(IdToIModel),
			[ownerPtr](AdvViz::expected<void, std::string> const& exp)
		{
			if (!ownerPtr.IsValid())
				return;
			auto& This(ownerPtr.Get()->Impl);
			if (!exp)
			{
				BE_LOG_LOAD_UNEXP("custom materials", exp);
				This->FinishedALoadingTask();
				return;
			}
			AsyncTask(ENamedThreads::GameThread,
				[ownerPtr]()
			{
				if (!ownerPtr.IsValid())
					return;
				auto& This(ownerPtr.Get()->Impl);
				ON_SCOPE_EXIT{ This->FinishedALoadingTask(); };

				This->OnCustomMaterialsLoaded_GameThread(true);
				This->CurrentContext = EAsyncContext::None;

				// Process pending material tasks
				if (!This->PendingIModelsForMaterials.IsEmpty())
				{
					This->AsyncLoadMaterials(std::move(This->PendingIModelsForMaterials), true);
					This->PendingIModelsForMaterials = {};
				}
			});
		},
			specificModels);
	});
}

// Simplify SaveScene:

namespace ITwinMsg
{
	static const FString LongITwinServicesResponseTime = TEXT("The iTwin services are taking a longer time to complete.\n");
	static const FString LongDecoServerResponseTime = TEXT("The decoration service is taking a longer time to complete.\n");
	static const FString ConfirmAbortLoadDeco = TEXT("\nDo you want to load your model without any population/material customization?\n");
	static const FString ConfirmAbortSaveDeco = TEXT("\nDo you want to abort saving the modifications you made to your population/materials?\n");

	//inline FString GetConfirmAbortLoadMsg() {
	//	return LongITwinServicesResponseTime + ConfirmAbortLoadDeco;
	//}
	inline FString GetConfirmAbortSaveMsg() {
		return LongDecoServerResponseTime + ConfirmAbortSaveDeco;
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
				return true;
			}
			ConfirmOccurrences++;
			NextConfirmTime = std::chrono::system_clock::now() + std::chrono::seconds(ConfirmOccurrences * 30);
		}
	}
	return false;
}

void AITwinDecorationHelper::FImpl::SaveScene(FSaveRequestOptions const& opts)
{
	if (ReadOnly)
		return;
	if (!ShouldSaveScene())
		return;

	if (bIsSavingScene)
	{
		BE_LOGW("ITwinDecoration", "Scene saving already in progress");
		return;
	}
	bIsSavingScene = true;
	bIsDeletingCustomMaterials = opts.bUponCustomMaterialsDeletion;
	OnSceneSavedCallback = opts.OnSceneSavedCallback;
	CurrentContext = EAsyncContext::Save;

	PreSaveCameras();

	ResetTicker();
	NextConfirmTime = std::chrono::system_clock::now() + std::chrono::seconds(30);
	ConfirmOccurrences = 0;
	ConfirmAbortMsg = ITwinMsg::GetConfirmAbortSaveMsg();

	// For save/auto-save, requests are build in the game thread to avoid threading issues if the user
	// modifies some data being written. Then requests are run in an asynchronous way (using the AsyncXXX
	// versions of AdvViz::SDK::Http)
	auto DecoIO = GetDecorationAsyncIOHelper();
	const bool bSomethingToSave = DecoIO &&
		DecoIO->AsyncSave(
			[this,
			IsValidLambda = this->IsThisValid](bool bResult)
	{
		if (!*IsValidLambda)
			return;
		OnDecorationSaved_GameThread(bResult, this->bIsDeletingCustomMaterials);
	});

	if (bSomethingToSave)
	{
		TickerDelegate = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[this,
				IsValidLambda = this->IsThisValid](float Delta) -> bool
		{
			if (!*IsValidLambda)
				return false;

			if (!this->bIsSavingScene)
				return false;

			// Propose to abort if the task is taking too long
			if (this->ShouldAbort())
			{
				OnDecorationSaved_GameThread(false, false);
				return false;
			}
			return true;
		}), 1.f /* tick once per second*/);
	}
	else
	{
		this->bIsDeletingCustomMaterials = false;
		this->bIsSavingScene = false;
		this->CurrentContext = EAsyncContext::None;
	}

	if (opts.bUponExit && !opts.OnSceneSavedCallback && bIsSavingScene)
	{
		// Wait for save to complete
		int ElapsedSec = 0;
		while (bIsSavingScene && ElapsedSec < 300)
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
	if (!iTwinContentManager)
	{
		InitContentManager();
	}
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

	auto gpPtr = instancesManager->GetInstancesGroupByName(animId);
	if (gpPtr)
	{
		auto gp = gpPtr->GetRAutoLock();
		if (gp->GetId().HasDBIdentifier())
		{
			instancesManager->RemoveGroupInstances(gp->GetId());
			instancesManager->RemoveInstancesGroup(gpPtr);
		}
	}
}

void AITwinDecorationHelper::FImpl::CreateKeyframeAnimPopulation()
{
	using namespace AdvViz::SDK;
	auto& instancesManager(DecorationIO->instancesManager_);
	if (!instancesManager)
		return;
	using namespace AdvViz::SDK;

	//D-O-NOTC
	// code to remove instances associate to an animation, used to re-associate them with code below.
	//for (auto& it : DecorationIO->animationKeyframes)
	//	DissociateAnimation((std::string)it.first);

	// TODO_LC adapt those paths to the final content based on the Component Center?
	const char* cars[] = {
		"/Game/CarrotLibrary/Vehicles/Audi_A4",
		"/Game/CarrotLibrary/Vehicles/Chevrolet_Impala",
		"/Game/CarrotLibrary/Vehicles/Mercedes_SL",
		"/Game/CarrotLibrary/Vehicles/Volvo_V70",
	};

	auto animationKeyframes = DecorationIO->animationKeyframesPtr->GetRAutoLock();
	// Associate animation set to group
	for (auto& it : *animationKeyframes)
	{
		auto gpPtr = instancesManager->GetInstancesGroupByName((std::string)it.first);
		if (!gpPtr) // we go there only if animation is not already associated
		{
			auto gp = IInstancesGroup::New();
			gp->SetName((std::string)it.first);
			gp->SetType("animKeyframe");
			gpPtr = Tools::MakeSharedLockableDataPtr<>(gp);
			instancesManager->AddInstancesGroup(gpPtr);
			auto lockanimationKeyframe(it.second->GetAutoLock());
			auto& animationKeyframe = lockanimationKeyframe.Get();
			for (auto& infoId : animationKeyframe.GetAnimationKeyframeInfoIds())
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
					auto instPtr = instancesManager->AddInstance(objectRef, gp->GetId());
					auto inst = instPtr->GetAutoLock();
					inst->SetShouldSave(true);
					inst->SetName("inst");
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
		keyframeAnimator->AssociateInstances(gpPtr);
	}
}

void AITwinDecorationHelper::FImpl::CreateOrRefreshPopulationInGame(
	FString const& assetPath,
	AdvViz::SDK::RefID const& groupId)
{
	auto& instancesManager(DecorationIO->instancesManager_);
	if (!instancesManager)
		return;

	AITwinPopulation* population = Owner.GetOrCreatePopulation(assetPath, groupId);
	if (!population)
	{
		return;
	}

	auto gpPtr = instancesManager->GetInstancesGroup(groupId);
	if (gpPtr)
	{
		auto gp = gpPtr->GetRAutoLock();
		if (gp->GetType() == "animKeyframe")
		{
			auto animationKeyframes2 = DecorationIO->animationKeyframesPtr->GetRAutoLock();
			auto keyfAnim = animationKeyframes2->find(AdvViz::SDK::IAnimationKeyframe::Id(gp->GetName()));
			if (keyfAnim != animationKeyframes2->end())
			{
				std::shared_ptr<FITwinPopulationWithPathExt> animExt = std::make_shared<FITwinPopulationWithPathExt>();
				animExt->population_ = population;
				population->AddExtension(animExt);
			}
			else
			{
				BE_LOGW("keyframeAnim", "animation keyframe: " << gp->GetName() << " not found");
			}
		}
	}

	auto& pathAnimator(DecorationIO->pathAnimator);
	if (pathAnimator)
	{
		const AdvViz::SDK::SharedInstVect& instances =
			instancesManager->GetInstancesByObjectRef(ITwin::ConvertToStdString(assetPath), groupId);
		for (size_t i = 0; i < instances.size(); ++i)
		{
			AdvViz::SDK::IInstancePtr instPtr = instances[i];
			auto inst = instPtr->GetAutoLock();
			if (inst->GetAnimPathId())
			{
				auto AnimPathInfoPtr = pathAnimator->GetAnimationPathInfo(inst->GetAnimPathId().value());
				if (!AnimPathInfoPtr)
					continue;
				auto AnimPathInfo = AnimPathInfoPtr->GetAutoLock();
				std::shared_ptr<InstanceWithSplinePathExt> animPathExt = std::make_shared<InstanceWithSplinePathExt>(AnimPathInfoPtr, population, i);
				inst->AddExtension(animPathExt);
				AnimPathInfo->AddExtension(animPathExt);
			}
		}
	}

	population->UpdateInstancesFromAVizToUE();
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

	
	CreateKeyframeAnimPopulation();


	using namespace AdvViz::SDK;
	// Add a population for each object reference
	auto const objReferences = instancesManager->GetObjectReferences();
	for (const auto& objRef : objReferences)
	{
		CreateOrRefreshPopulationInGame(FString(objRef.first.c_str()), objRef.second);
	}

	bPopulationEnabled = true;

	Owner.OnPopulationsLoaded.Broadcast(true);
}

bool AITwinDecorationHelper::FImpl::LoadSplineIfAllLinkedModelsReady(
	AdvViz::SDK::ISplinePtr const& AdvVizSpline,
	AITwinSplineTool* SplineTool,
	const UWorld* World)
{
	AITwinSplineTool::TilesetAccessArray LinkedTilesets;

	// Splines linked to specific models can be loaded now, but only if the corresponding 3D tilesets are
	// have all been created (in general, it won't be the case...)
	ITwin::GetLinkedTilesets(LinkedTilesets, AdvVizSpline, World);
	auto spline = AdvVizSpline->GetRAutoLock();
	if (LinkedTilesets.Num() < (int32)spline->GetLinkedModels().size())
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


/*static*/ std::optional<bool> AITwinDecorationHelper::bHasComponentCenterOpt;

/*static*/ void AITwinDecorationHelper::SetUseComponentCenter(bool bComponentCenter)
{
	bHasComponentCenterOpt = bComponentCenter;
}

bool AITwinDecorationHelper::UseComponentCenter()
{
	return bHasComponentCenterOpt.value_or(false);
}

void AITwinDecorationHelper::InitContentManager()
{
	if (!iTwinContentManager)
	{
		iTwinContentManager = NewObject<UITwinContentManager>();

		// Temporary path, should be replaced by component center download path.
		// TODO_MACOS
		iTwinContentManager->SetContentRootPath(TEXT("C:\\ProgramData\\Bentley\\iTwinEngage\\ContentUE_5.6"));
		iTwinContentManager->DecorationHelperPtr = this;
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
	return Impl->IsLoadingScene();
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

bool AITwinDecorationHelper::FImpl::ShouldSaveScene() const
{
	if (!HasITwinID() || !DecorationIO->decoration || ReadOnly)
	{
		return false;
	}

	bool const saveInstances = DecorationIO->instancesManager_
		&& DecorationIO->instancesManager_->HasInstancesToSave();
	bool const saveMaterials = DecorationIO->materialPersistenceMngr
		&& DecorationIO->materialPersistenceMngr->NeedUpdateDB();
	bool const saveScenes = DecorationIO->scene
		&& DecorationIO->scene->ShouldSave();
	bool const saveTimeline = DecorationIO->scene
		&& DecorationIO->scene->GetTimeline()
		&& DecorationIO->scene->GetTimeline()->HasSomethingToSave();
	bool const saveSplines = DecorationIO->splinesManager
		&& DecorationIO->splinesManager->HasSplinesToSave();
	bool const saveAnnotations = DecorationIO->annotationsManager
		&& DecorationIO->annotationsManager->HasAnnotationToSave();

	if (!saveInstances && !saveMaterials && !saveScenes && !saveTimeline && !saveSplines && !saveAnnotations)
		return false;
	return true;
}

bool AITwinDecorationHelper::ShouldSaveScene() const
{
	return Impl->ShouldSaveScene();
}

void AITwinDecorationHelper::SaveScene(bool bPromptUser /*= true*/)
{
	SaveSceneWithOptions(FSaveRequestOptions{ .bPromptUser = bPromptUser });
}

void AITwinDecorationHelper::SaveSceneOnExit(bool bPromptUser /*= true*/)
{
	SaveSceneWithOptions(FSaveRequestOptions{ .bPromptUser = bPromptUser, .bUponExit = true });
}

void AITwinDecorationHelper::SaveSceneWithOptions(FSaveRequestOptions const& Options)
{
	Impl->SaveScene(Options);
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
	// Execute custom save callback, if any (used when iTwin Studio requests closing Unreal)
	if (OnSceneSavedCallback)
	{
		OnSceneSavedCallback();
		OnSceneSavedCallback = {};
	}

	this->bIsDeletingCustomMaterials = false;
	this->bIsSavingScene = false;
	this->CurrentContext = EAsyncContext::None;
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
	if (Model
		// Invalid case, eg. when opening a Level in itE_Editor then running PIE... will probably crash later
		&& ensure(Impl->DecorationIO->IsInitialized()))
	{
		auto const Key = std::make_pair(EITwinModelType::IModel, Model->IModelId);

		TUniquePtr<FITwinTilesetAccess> TilesetAccess = Model->MakeTilesetAccess();
		TWeakObjectPtr<AITwinDecorationHelper> SThis(this);
		// Find link

		auto linkslock = SThis->Impl->DecorationIO->links.GetRAutoLock();
		auto iter = linkslock->find(Key);
		if (iter != linkslock->end())
		{
			FDecorationAsyncIOHelper::LinkSharedPtr linkPtr = iter->second;
			AsyncTask(ENamedThreads::GameThread,
				[SThis, linkPtr, Key]()
				{
					if (!SThis.IsValid())
						return;
					AITwinIModel* Model = ITwin::GetIModelByID(Key.second, SThis->GetWorld());
					if (!Model)
						return;
					TUniquePtr<FITwinTilesetAccess> TilesetAccess = Model->MakeTilesetAccess();


					auto si = ITwin::LinkToSceneInfo(*linkPtr);
					TilesetAccess->ApplyLoadedInfo(si, true);
				});
		}


		// Load linked splines if needed
		SThis->Impl->LoadSplinesLinkedToModel(Key, *TilesetAccess);
	}
}
	
void AITwinDecorationHelper::OnRealityDataLoaded(bool bSuccess, FString StringId)
{
	// Find RealityData
	AITwinRealityData* RealityData = ITwin::GetRealityDataByID(StringId, GetWorld());
	if (RealityData)
	{
		auto const Key = std::make_pair(EITwinModelType::RealityData, RealityData->RealityDataId);

		TUniquePtr<FITwinTilesetAccess> TilesetAccess = RealityData->MakeTilesetAccess();
		TWeakObjectPtr<AITwinDecorationHelper> SThis(this);
		// Find link
		auto linkslock = SThis->Impl->DecorationIO->links.GetRAutoLock();
		auto iter = linkslock->find(Key);
		if (iter != linkslock->end())
		{
			FDecorationAsyncIOHelper::LinkSharedPtr linkPtr = iter->second;
			AsyncTask(ENamedThreads::GameThread,
				[SThis, linkPtr, Key]() {
					if (!SThis.IsValid())
						return;
					AITwinRealityData* RealityData = ITwin::GetRealityDataByID(Key.second, SThis->GetWorld());
					if (!RealityData)
						return;
					TUniquePtr<FITwinTilesetAccess> TilesetAccess = RealityData->MakeTilesetAccess();

					auto si = ITwin::LinkToSceneInfo(*linkPtr);
					TilesetAccess->ApplyLoadedInfo(si, true);
				});
		}
		// Load linked splines if needed
		SThis->Impl->LoadSplinesLinkedToModel(Key, *TilesetAccess);
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
	auto linkslock = DecorationIO->links.GetRAutoLock();
	for (TActorIterator<AITwinIModel> IModelIter(Owner.GetWorld()); IModelIter; ++IModelIter)
	{
		auto key = std::make_pair(EITwinModelType::IModel, IModelIter->IModelId);
		auto iter = linkslock->find(key);
		if (iter != linkslock->end())
		{
			auto si = ITwin::LinkToSceneInfo(*iter->second);
			IModelIter->MakeTilesetAccess()->ApplyLoadedInfo(si, false);
			IModelIter->OnIModelLoaded.AddUniqueDynamic(&Owner, &AITwinDecorationHelper::OnIModelLoaded);
		}
	}
	for (TActorIterator<AITwinRealityData> IReallIter(Owner.GetWorld()); IReallIter; ++IReallIter)
	{
		auto key = std::make_pair(EITwinModelType::RealityData, IReallIter->RealityDataId);
		auto iter = linkslock->find(key);
		if (iter != linkslock->end())
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

bool AITwinDecorationHelper::MountPak(const std::string& file, const std::string& id) const
{
	if (!iTwinContentManager)
		return false;
	iTwinContentManager->MountPak(UTF8_TO_TCHAR(file.c_str()), UTF8_TO_TCHAR(id.c_str()));
	return true;
}

bool AITwinDecorationHelper::IsComponentDownloadPending(const FString& componentId) const
{
	if (!iTwinContentManager)
		return false;
	return !iTwinContentManager->ShouldDownloadComponent(componentId).IsEmpty();
}

AITwinPopulation* AITwinDecorationHelper::CreatePopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const
{
	AdvViz::SDK::IInstancesGroupPtr gpPtr =
		Impl->DecorationIO->instancesManager_->GetInstancesGroup(groupId);
	if (!gpPtr)
	{
		BE_ISSUE("invalid group ID", groupId.ID(), groupId.GetDBIdentifier());
		return nullptr;
	}
	FString realAssetPath = assetPath;
	if (iTwinContentManager)
	{
		FString componentId = iTwinContentManager->ShouldDownloadComponent(assetPath);
		if (!componentId.IsEmpty())
		{
			if (!iTwinContentManager->DownloadedComponent(componentId))
			{
				BE_LOGE("ContentHelper", "Failed to request component download: " << TCHAR_TO_UTF8(*componentId));
				return nullptr;
			}

			// Population creation must wait until the corresponding component is mounted.
			TWeakObjectPtr<AITwinDecorationHelper> weakOwner(const_cast<AITwinDecorationHelper*>(this));
			std::string const delayedCallId =
				"RetryPopulation_"
				+ std::string(TCHAR_TO_UTF8(*componentId))
				+ "_"
				+ std::to_string(groupId.ID());

			AdvViz::SDK::UniqueDelayedCall(delayedCallId,
				[weakOwner, assetPath, groupId]() -> AdvViz::SDK::DelayedCall::EReturnedValue
				{
					if (!weakOwner.IsValid() || !weakOwner->iTwinContentManager)
					{
						return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
					}

					if (!weakOwner->iTwinContentManager->ShouldDownloadComponent(assetPath).IsEmpty())
					{
						return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
					}

					weakOwner->Impl->CreateOrRefreshPopulationInGame(assetPath, groupId);

					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
				},
				0.25f);

			return nullptr;
		}
		realAssetPath = iTwinContentManager->SanitizePath(assetPath);
		iTwinContentManager->DownloadFromAssetPath(realAssetPath);
	}

	AITwinPopulation* population = AITwinPopulation::CreatePopulation(this, realAssetPath,
		Impl->DecorationIO->instancesManager_, gpPtr);
	if (population && realAssetPath != assetPath)
	{
		population->SetObjectRef(TCHAR_TO_UTF8(*assetPath));
	}
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
		auto gp = Impl->DecorationIO->staticInstancesGroup->GetRAutoLock();
		return gp->GetId();
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
	auto spline = Spline.GetAVizSpline()->GetRAutoLock();
	AdvViz::SDK::IInstancesGroupPtr gpPtr =
		instancesManager.GetInstancesGroupBySplineID(spline->GetId());
	if (!gpPtr)
	{
		// No group for this spline yet: create it now.
		auto gp = AdvViz::SDK::IInstancesGroup::New();
		gp->SetName(TCHAR_TO_UTF8(*Spline.GetActorNameOrLabel()));
		gp->SetType("spline");
		gp->SetLinkedSplineId(spline->GetId());
		gpPtr = AdvViz::SDK::Tools::MakeSharedLockableDataPtr(gp);
		instancesManager.AddInstancesGroup(gpPtr);
		return gp->GetId();
	}
	else
	{
		auto gp = gpPtr->GetRAutoLock();
		return gp->GetId();	
	}
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

void AITwinDecorationHelper::GetSceneInfoThen(const ModelIdentifier& Key,
	std::function<void(ITwinSceneInfo)>&& InCallback) const
{
	ITwinSceneInfo SceneInfo;
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		{
			auto linkslock = Impl->DecorationIO->links.GetRAutoLock();
			auto iter = linkslock->find(Key);
			if (iter != linkslock->end())
				SceneInfo = ITwin::LinkToSceneInfo(*iter->second);
		}
	}
	InCallback(SceneInfo);
}

std::shared_ptr<AdvViz::SDK::ILink> AITwinDecorationHelper::GetRawSceneInfoSynchronous(const ModelIdentifier& Key) const
{
	auto linkslock = this->Impl->DecorationIO->links.GetRAutoLock();
	auto iter = linkslock->find(Key);
	if (iter != linkslock->end())
		return iter->second;
	return std::shared_ptr<AdvViz::SDK::ILink>();
}

void AITwinDecorationHelper::SetSceneInfo(const ModelIdentifier& Key, const ITwinSceneInfo& si) const
{
	if (Impl->DecorationIO && Impl->DecorationIO->scene)
	{
		TWeakObjectPtr<AITwinDecorationHelper> SThis(const_cast<AITwinDecorationHelper*>(this));
		auto linkslock = SThis->Impl->DecorationIO->links.GetRAutoLock();
		auto iter = linkslock->find(Key);
		std::shared_ptr<AdvViz::SDK::ILink> sp;
		if (iter == linkslock->end())
			sp = SThis->Impl->DecorationIO->CreateLink(Key);
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
		Impl->DecorationIO->CreateLinkIfNeeded(Key,
			[](FDecorationAsyncIOHelper::LinkSharedPtr sp)
		{
			ITwin::SceneToLink(ITwinSceneInfo(), sp);
		});
	}
}

std::vector<ITwin::ModelLink> AITwinDecorationHelper::GetLinkedElements() const
{
	auto linkslock = Impl->DecorationIO->links.GetRAutoLock();
	std::vector<ITwin::ModelLink> res;
	for (auto link : *linkslock)
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
		SaveScene(FSaveRequestOptions{ .bUponCustomMaterialsDeletion = true });
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
	{
		return locker;
	}
	else
	{
		auto res = std::shared_ptr<AITwinDecorationHelper::SaveLocker>(new SaveLockerImpl(this));
		Impl->saveLocker = res;
		return res;
	}
}

bool AITwinDecorationHelper::IsSaveLocked() const
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

AdvViz::expected<AdvViz::SDK::ScenePtrVector, AdvViz::SDK::HttpError> AITwinDecorationHelper::GetITwinScenes(const FString& itwinid)
{
	return Impl->DecorationIO->GetITwinScenes(itwinid);
}

void AITwinDecorationHelper::AsyncGetITwinSceneInfos(const FString& itwinid,
	std::function<void(AdvViz::expected<FTwinSceneBasicInfos, AdvViz::SDK::HttpError> const&)>&& InCallback,
	bool bExecuteCallbackInGameThread)
{
	Impl->DecorationIO->AsyncGetITwinSceneInfos(itwinid,
		[Callback = std::move(InCallback)](AdvViz::expected<AdvViz::SDK::SceneInfoVec, AdvViz::SDK::HttpError> const& ret)
	{
		if (ret)
		{
			// Convert to Unreal types.
			AdvViz::SDK::SceneInfoVec const& AVizSceneInfos(*ret);
			FTwinSceneBasicInfos Infos;
			Infos.Scenes.Reserve(AVizSceneInfos.size());
			Algo::Transform(AVizSceneInfos, Infos.Scenes,
				[](AdvViz::SDK::SceneInfo const& Info) -> FTwinSceneBasicInfo {
				return FTwinSceneBasicInfo{
					.Id = Info.id.c_str(),
					.ITwinId = Info.iTwinId.c_str(),
					.DisplayName = UTF8_TO_TCHAR(Info.displayName.c_str())
				};
			});
			Callback(Infos);
		}
		else
		{
			Callback(AdvViz::make_unexpected(ret.error()));
		}
	}, bExecuteCallbackInGameThread);
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

void AITwinDecorationHelper::EnableExportOfResourcesInSceneApI(bool bEnable)
{
	AdvViz::SDK::ScenePersistenceAPI::EnableExportOfResources(bEnable);
}

void AITwinDecorationHelper::Lock(SaveLockerImpl& saver)
{
	saver.sceneStatus = Impl->DecorationIO->scene && Impl->DecorationIO->scene->ShouldSave();
	auto linkslock = Impl->DecorationIO->links.GetRAutoLock();
	for (auto link : *linkslock)
	{
		saver.linksStatus[link.first] = link.second->ShouldSave();
	}
	saver.timelineStatus = Impl->DecorationIO->scene->GetTimeline() && Impl->DecorationIO->scene->GetTimeline()->ShouldSave();
}

void AITwinDecorationHelper::Unlock(SaveLockerImpl const& saver)
{
	// Due to asynchronous loading of scene, the flags can now be set to false by a worker thread, so we
	// should no reset them to the old 'should-save' value here.
	// Therefore the only action allowed to the SaveLocker is to *unset* the 'should-save' flag.
	// (Same for all items).
	if (Impl->DecorationIO->scene && saver.sceneStatus == false)
	{
		Impl->DecorationIO->scene->SetShouldSave(false);
	}
	auto linkslock = Impl->DecorationIO->links.GetRAutoLock();
	for (auto link : *linkslock)
	{
		auto const itLinkStatus = saver.linksStatus.find(link.first);
		if (itLinkStatus == saver.linksStatus.end()
			|| itLinkStatus->second == false)
		{
			link.second->SetShouldSave(false);
		}
	}
	if (Impl->DecorationIO->scene->GetTimeline() && saver.timelineStatus == false)
	{
		Impl->DecorationIO->scene->GetTimeline()->SetShouldSave(false);
	}
}

void AITwinDecorationHelper::SetReadOnlyMode(bool value)
{
	Impl->ReadOnly = value;
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
		TWeakObjectPtr<AITwinDecorationHelper> SThis(const_cast<AITwinDecorationHelper*>(this));
		auto linkslock = SThis->Impl->DecorationIO->links.GetRAutoLock();
		auto iter = linkslock->find(key);
		if (iter != linkslock->end())
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
