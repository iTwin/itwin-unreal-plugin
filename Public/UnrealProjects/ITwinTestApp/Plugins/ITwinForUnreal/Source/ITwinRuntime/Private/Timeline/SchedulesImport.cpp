/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesImport.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesImport.h"

#include "SchedulesGeneration.h"
#include "SchedulesJsonMacros.h"
#include "SchedulesParse.h"
#include "SchedulesStructs.h"
#include "TimeInSeconds.h"
#include "Timeline.h"
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex
#include <Network/JsonQueriesCache.h>
#include <Network/JsonQueriesCacheInit.h> // for FChangesetFinderIterator
#include <Network/ReusableJsonQueries.h>

#include <Dom/JsonObject.h>
#include <HAL/PlatformFileManager.h>
#include <HttpModule.h>
#include <Input/Reply.h>
#include <Math/UnrealMathUtility.h>
#include <Math/Vector.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ITwin_TestOverrides
{
	// See comment on declaration in SchedulesConstants.h
	int RequestPagination = -1;
	int BindingsRequestPagination = -1;
}
constexpr double FetchingPropertyDeltasPeriod = 5.; // seconds

namespace APIParams
{
	static const FString PageSize("$top");
}

class FITwinSchedulesImport::FImpl
{
private:
	using FLock = ITwinHttp::FLock;
	friend class FITwinSchedulesImport;

	FITwinSchedulesImport const* Owner;
	FOnReceivedScheduleStats OnReceivedScheduleStats = [](FITwinScheduleStats const&) {};
	ITwinHttp::FMutex& Mutex;
	const int RequestPagination;///< pageSize for paginated requests EXCEPT animation bindings
	/// pageSize for paginated *animation bindings* requests: not const, depends on generation (legacy/nextgen)
	int BindingsRequestPagination;
	int DebugCurrentSubIncrement = -1;
	bool bDebugNextScheduleUpdateIncrement = false;
	bool bHasFinishedPrefetching = false;
	bool bHasFetchingErrors = false;
	bool bHasFailedListingSchedules = false;
	bool bFetchingPropertyDeltas = false;
	bool bFetchingPropertyDeltasGotUpdates = false;
	bool bNeedToSaveScheduleToDisk = false;
	bool bAllowIncrementalUpdatesToFinishPrefetching = false;
	double LastFetchingPropertyDeltasTime = 0.;
	FString FirstFetchingError;
	EHttpResponseCodes::Type FirstFetchingErrorCode = EHttpResponseCodes::Ok;
	double LastCheckTotalBindings = 0.;
	size_t LastTotalBindingsFound = 0;
	int SchedApiSession = -1;
	static int s_NextSchedApiSession;
	FString ITwinId, TargetedIModelId, ChangesetId; ///< Set in FITwinSchedulesImport::ResetConnection
	/// "Unknown" also means "Not needed", because APIM hides this detail from us.
	EITwinSchedulesGeneration SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
	std::optional<FITwinSchedule>& Schedule;
	struct FUnitTesting
	{
		FString BaseUrl;
		FITwinScheduleTimeline& MainTimeline;
		TStrongObjectPtr<UObject> OwnerUObject;
		EITwinEnvironment Environment = EITwinEnvironment::Invalid;
	};
	std::optional<FUnitTesting> UnitTesting;
	std::unordered_map<FString, std::function<void(bool bTaskSuccess)>> WaitingForBackgroundTasks;
	std::unordered_map<FString, std::function<void(bool bTaskSuccess)>> BackgroundTaskCompletionsInGT;
	std::shared_ptr<std::atomic_bool> IsThisValid;

	/// KEEP LAST, so that it is deleted first, because it's dtor waits on async tasks that use some of the
	/// above members (mostly Schedule)
	TPimplPtr<FReusableJsonQueries> Queries;

	static int CheckPagination(int Pagination, FString const& PaginationSetting)
	{
		// 50K is the limit on the server for Legacy schedules, but 10K for NextGen.
		int const MaxPagination = 50'000;
		if (Pagination > MaxPagination)
		{
			BE_LOGW("ITwin4DImp", "Capping " << TCHAR_TO_UTF8(*PaginationSetting) << " to "
				<< MaxPagination << " iof. " << Pagination << " because of 4D API internal limit");
			Pagination = MaxPagination;
		}
		return Pagination;
	}

public:
	FImpl(FITwinSchedulesImport const& InOwner, ITwinHttp::FMutex& InMutex,
			std::optional<FITwinSchedule>& InSchedule)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, RequestPagination(CheckPagination(
			ITwin_TestOverrides::RequestPagination > 0
				? ITwin_TestOverrides::RequestPagination : InOwner.Owner->ScheduleQueriesServerPagination,
			TEXT("ScheduleQueriesServerPagination")))
		, BindingsRequestPagination(CheckPagination(
			ITwin_TestOverrides::BindingsRequestPagination > 0
				? ITwin_TestOverrides::BindingsRequestPagination
				: InOwner.Owner->ScheduleQueriesBindingsPagination,
			TEXT("ScheduleQueriesBindingsPagination")))
		//, SchedApiSession(s_NextSchedApiSession++) <== (re-)init by each call to ResetConnection
		, Schedule(InSchedule)
		, IsThisValid(std::make_shared<std::atomic_bool>(true))
	{
	}
	// For unit tests
	FImpl(FITwinSchedulesImport const& InOwner, FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
		TStrongObjectPtr<UObject> OwnerUObj, ITwinHttp::FMutex& InMutex, std::optional<FITwinSchedule>& Sched)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, RequestPagination(CheckPagination(ITwin_TestOverrides::RequestPagination,
											TEXT("ScheduleQueriesServerPagination")))
		, BindingsRequestPagination(CheckPagination(ITwin_TestOverrides::BindingsRequestPagination,
													TEXT("ScheduleQueriesBindingsPagination")))
		, Schedule(Sched)
		, UnitTesting(FUnitTesting{ BaseUrl, MainTimeline, OwnerUObj,
			BaseUrl.StartsWith(TEXT("https://qa-")) ? EITwinEnvironment::QA
			: (BaseUrl.StartsWith(TEXT("https://dev-")) ? EITwinEnvironment::Dev : EITwinEnvironment::Prod) })
		, IsThisValid(std::make_shared<std::atomic_bool>(true))
	{
		ensure(RequestPagination >= 0 && BindingsRequestPagination >= 0);
	}

	~FImpl() { IsThisValid->store(false); }

	FImpl(FImpl const&) = delete;
	FImpl& operator=(FImpl const&) = delete;

	void ResetConnection(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
						 FString const& InChangesetId, FString const& CustomCacheDir,
						 EITwinSchedulesGeneration CustomScheduleGeneration = EITwinSchedulesGeneration::Unknown);
	void SetSchedulesImportConnectors(FOnReceivedScheduleStats const& InOnReceivedScheduleStats);
	void CreateIncrementalUpdateAndWrapUpBatches();
	std::pair<int, int> HandlePendingQueries();
	bool IsWaitingForBackgroundTasksAndCompletionInGT() const;
	bool SaveScheduleToDiskIfNeeded(FLock& Lock);
	void ComputeNeedToSaveScheduleToDisk(FLock& Lock);

private:
	bool IsSimulatedSchedule() const { return Schedule && Schedule->Id.StartsWith(TEXT("fadafada-")); }
	UITwinSynchro4DSchedules const& SchedulesComponent() const { return *Owner->Owner; }
	UITwinSynchro4DSchedules& SchedulesComponent() { return *Owner->Owner; }
	TObjectPtr<AITwinServerConnection> const& GetServerConnection() const
		{ return Cast<AITwinIModel>(SchedulesComponent().GetOwner())->ServerConnection; }
	FITwinSynchro4DSchedulesInternals const& SchedulesInternals() const
		{ return GetInternals(SchedulesComponent()); }
	FITwinSynchro4DSchedulesInternals& SchedulesInternals() { return GetInternals(SchedulesComponent()); }
	FString ComputeCacheName(std::optional<EITwinSchedulesGeneration> GenOverride = {}) const;
	void SetScheduleTimeRangeIsKnown();
	bool OnFoundScheduleForTargetedIModel(FString const& ScheduleId, FString const& ScheduleName,
		FString const& CustomCacheDir, EITwinSchedulesGeneration const ScheduleGen,
		std::function<void(bool bSuccess)>&& OnScheduleLoadedFromJsonFile);
	int ScheduleDependentPaginationCap() const;
	FUrlArgList ConcatPaginationOrDeltaTokenParams(FUrlArgList&& ArgList, std::optional<FString> const& PageToken,
		std::optional<FString> const& DeltaToken, std::optional<int> CustomRequestPagination = {});
	template<typename TPropertyContainer>
	bool CanRequestAll(FString const& DeltaToken, TPropertyContainer const& PropertyContainer,
					   int const DebugUpdateSubIncrement, FLock&);
	void RequestSchedules(ReusableJsonQueries::FStackingToken const&,
		std::optional<FString> const PageToken = {}, FLock* optLock = nullptr,
		std::shared_ptr<std::vector<FITwinSchedule>> ScheduleCandidates = {});
	void RequestScheduleStatistics(ReusableJsonQueries::FStackingToken const&, FLock&);
	void AutoRequestScheduleItems(ReusableJsonQueries::FStackingToken const& Token, FLock* optLock = nullptr);
	void RequestAllAnimationBindings(ReusableJsonQueries::FStackingToken const&, FLock& Lock,
									 std::optional<FString> const PageToken = {});
	/// \return A pair with the iterator to the created or existing property, and an 'incomplete' flag ie false
	///		when the property is already fully defined (queried + reply handled), or true when the property either
	///		has a still pending query, or needs to be queried (see CreatedProperties parameter for that case)
	template<typename TProperty>
	TProperty* EmplaceProperty(FString const& PropertyId, size_t& PropertyInVec,
		std::vector<TProperty>& SchedProperties, std::unordered_map<FString, size_t>& SchedKnownProps, FLock&);
	void HandleDeltaToken(TSharedPtr<FJsonObject> const& Reply, FString& PropDeltaToken, TCHAR const* PropName,
						  FLock&);
	void RequestAllTasks(ReusableJsonQueries::FStackingToken const&, FLock&,
						 std::optional<FString> const PageToken = {});
	void RequestTask(ReusableJsonQueries::FStackingToken const&, size_t const AnimIdx,
					 FLock&);
	void RequestAllAppearanceProfiles(ReusableJsonQueries::FStackingToken const&, FLock&,
									  std::optional<FString> const PageToken = {});
	void RequestAppearanceProfile(ReusableJsonQueries::FStackingToken const&, size_t const AnimIdx, FLock&);
	void RequestAllStaticTransfoAssignments(ReusableJsonQueries::FStackingToken const&,
											std::optional<FString> PageToken, FLock&);
	void RequestAll3DPathTransfoAssignments(ReusableJsonQueries::FStackingToken const&,
											std::optional<FString> PageToken, FLock&);
	/// \return Cf. ParseAppearanceProfileDetails, etc.
	bool ParseStaticTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
		FLock* optLock = nullptr, std::optional<size_t> const AnimIdx = {});
	/// \return Cf. ParseAppearanceProfileDetails, etc.
	bool Parse3DPathTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
									  ReusableJsonQueries::FStackingToken const& Token, FLock* optLock = nullptr,
									  std::optional<size_t> const AnimIdx = {});
	void Request3DPathKeyframes(ReusableJsonQueries::FStackingToken const&, size_t const TransfoAssignmentIdx,
								std::optional<FString> const PageToken, FLock&);
	void RequestAll3DPaths(ReusableJsonQueries::FStackingToken const&, std::optional<FString> const PageToken, FLock&);
	void RequestAll3DPathKeyframes(ReusableJsonQueries::FStackingToken const&, std::optional<FString> const PageToken,
								   FLock&);

	//void DetermineTaskElements(...); <== blame here to retrieve method to query resource assignments
	//void RequestResourceEntity3Ds(...); <== blame here to retrieve method to query resource entity3Ds

	/// \param bCreateGroupFromResource3DEntities Actually untested, would be needed only if reusing
	///		methods RequestResourceEntity3Ds & co. above (which retrieved the Element IDs assigned to a
	///		task by querying the task resources and then the resource Entity3Ds), for testing purposes
	void CreateRandomAppearanceProfile(FAnimationBinding& AnimationBinding, FLock& Lock,
									   bool const bCreateGroupFromResource3DEntities = false);
	bool ReadAndParseScheduleJsonCacheInWorkerThread(FString CacheName, FString JsonPath);
	void ComputeTimeRange()
	{
		auto& MainTimeline = UnitTesting ? UnitTesting->MainTimeline : SchedulesInternals().Timeline();
		MainTimeline.ResetTimeRange();
		for (auto&& Task : Schedule->Tasks)
			if (!Task.bDeleted)
				MainTimeline.IncludeTimeRange(Task.TimeRange);
	}

	// No longer used - blame here
	//void RequestAnimatedEntityUserFieldId(ReusableJsonQueries::FStackingToken const&, FLock&);
	//bool SetAnimatedEntityUserFieldId(TSharedRef<FJsonObject> JsonObj, FITwinSchedule const& Schedule) const;
	//bool SupportsAnimationBindings(FLock&) const;

	EITwinEnvironment GetScheduleEnvironment() const
	{
		if (UnitTesting)
		{
			ensure(UnitTesting->Environment != EITwinEnvironment::Invalid);
			return UnitTesting->Environment;
		}
		else
			return GetServerConnection()->Environment;
	}

	FString GetSchedulesAPIBaseUrl() const
	{
		if (UnitTesting)
		{
			return UnitTesting->BaseUrl;
		}
		return FString::Printf(TEXT("https://%sapi.bentley.com/schedules"), *GetServerConnection()->UrlPrefix());
	}

	FString GetIdToQuerySchedules() const
	{
		ensure(!UnitTesting);
		return TEXT("iTwinId");
	}

	/// Log (only once, of course) and aggregate currently downloaded "properties" counts vs. total counts
	/// into a single percentage value to notify the UITwinSynchro4DSchedules which will ultimately relay
	/// the information to some UX widget
	void OnScheduleDownloadProgressed(FITwinSchedule& Sched, FLock&)
	{
		if (!Sched.StatisticsTotal) // totals not received, do nothing
			return;
		size_t const TotalCount = Sched.StatisticsTotal->TaskCount
								+ Sched.StatisticsTotal->AppearanceProfileCount
								+ Sched.StatisticsTotal->AnimationBindingCount
								+ Sched.StatisticsTotal->Animation3dTransformCount
								+ Sched.StatisticsTotal->Animation3dPathAssignmentCount
								+ Sched.StatisticsTotal->Animation3dPathCount
								+ Sched.StatisticsTotal->Animation3dPathKeyframeCount;
		if (TotalCount != 0)
		{
			bool bPlayableSchedule = false;
			size_t const Progression = Sched.StatisticsCurrent.TaskCount
									 + Sched.StatisticsCurrent.AppearanceProfileCount
									 + Sched.StatisticsCurrent.AnimationBindingCount
									 + Sched.StatisticsCurrent.Animation3dTransformCount
									 + Sched.StatisticsCurrent.Animation3dPathAssignmentCount
									 + Sched.StatisticsCurrent.Animation3dPathCount
									 + Sched.StatisticsCurrent.Animation3dPathKeyframeCount;
			if (Progression >= TotalCount)
			{
				auto const& MainTimeline =
					UnitTesting ? UnitTesting->MainTimeline : SchedulesInternals().GetTimeline();
				bPlayableSchedule = (FDateRange() != MainTimeline.GetDateRange())
									&& !Sched.AnimationBindings.empty();
			}
			double const ProgressPercent = 100. * std::min(1., Progression / (double)TotalCount);
			if (UnitTesting)
			{
				if (Progression >= TotalCount)
				{
					BE_LOGI("ITwin4DImp", "Schedule loading complete for " << TCHAR_TO_UTF8(*TargetedIModelId)
							<< " (bPlayableSchedule? " << bPlayableSchedule << ")");
				}
				else
				{
					BE_LOGI("ITwin4DImp", "Schedule loading progress for " << TCHAR_TO_UTF8(*TargetedIModelId)
							<< " reached " << ProgressPercent << "%");
				}
			}
			else
			{
				// This will reach UObject properties and BP stuff => game thread only
				AsyncTask(ENamedThreads::GameThread,
					[IsThisValid = this->IsThisValid, this, ProgressPercent, bPlayableSchedule]()
					{
						if (IsThisValid->load())
							SchedulesInternals().OnDownloadProgressed(ProgressPercent, bPlayableSchedule);
					});
			}
		}
	}

	void NextDebugSubIncrement()
	{
		++DebugCurrentSubIncrement;
		if ((DebugCurrentSubIncrement % 7) == 0) // loop over the 7 properties...
			DebugCurrentSubIncrement = 0;
	}

	// for debug logs
	std::string ToLoggerStr(TSharedPtr<FJsonObject> const& JsonObj)
	{
		FString JsonString;
		auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
		return TCHAR_TO_UTF8(*JsonString);
	}

}; // class FITwinSchedulesImport::FImpl

int FITwinSchedulesImport::FImpl::s_NextSchedApiSession = 0;

void FITwinSchedulesImport::FImpl::SetScheduleTimeRangeIsKnown()
{
	if (!UnitTesting)
	{
		// This will reach UObject properties and BP stuff => game thread only
		AsyncTask(ENamedThreads::GameThread, [IsThisValid = this->IsThisValid, this]
			{
				if (IsThisValid->load())
					SchedulesInternals().SetScheduleTimeRangeIsKnown();
			});
	}
}

FString FITwinSchedulesImport::FImpl::ComputeCacheName(std::optional<EITwinSchedulesGeneration> GenOverride/*={}*/)
	const
{
	if (!ensure(!UnitTesting && Schedule))
		return FString(); // should have CustomCacheDir
	auto Generation = GenOverride ? (*GenOverride) : Schedule->Generation;
	return QueriesCache::GetCacheFolder(QueriesCache::ESubtype::Schedules, GetScheduleEnvironment(),
		ITwinId, TargetedIModelId,
		// NextGen schedules are not tied to a specific changeset:
		EITwinSchedulesGeneration::NextGen == Generation ? FString() : ChangesetId,
		// Cache identification is strictly based on the file or folder name
		FString("APIM_") + Schedule->Id);
}

bool FITwinSchedulesImport::FImpl::OnFoundScheduleForTargetedIModel(FString const& ScheduleId,
	FString const& ScheduleName, FString const& CustomCacheDir,
	EITwinSchedulesGeneration const ScheduleGen,
	std::function<void(bool bSuccess)>&& OnScheduleLoadedFromJsonFile)
{
	SchedulesGeneration = ScheduleGen;
	if (!Schedule || Schedule->Id != ScheduleId)
	{
		// Note: safer to explicit the ctor, in case ScheduleId & ScheduleName are const refs on the reset
		// Schedule's members, in which case the copy must be done before the optional is reset
		Schedule.emplace(FITwinSchedule(ScheduleId, ScheduleName, SchedulesGeneration));
	}
	else if (Schedule->Generation != SchedulesGeneration) // happens when an earlier call returned false below
	{
		Schedule->Generation = SchedulesGeneration;
	}
	//if (IsSimulatedSchedule()) BE_GETLOG("ITwin4DImp")->SetLevel(AdvViz::SDK::Tools::Level::debug); //D-O-NOTC

	// Set up the cache folder for the next requests
	if (UnitTesting // cache mandatory in TUs (and Owner->Owner is nullptr)
		|| (!Owner->Owner->bDisableCaching
			// superceded by the special simulation mode
			&& Owner->Owner->DebugSimulateSessionQueries.IsEmpty()))
	{
		auto&& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		// Older UE levels may have been saved without the Generation... Infer it from cache file or folder
		if (EITwinSchedulesGeneration::Unknown == SchedulesGeneration && CustomCacheDir.IsEmpty())
		{
			FString TryCacheName = ComputeCacheName(EITwinSchedulesGeneration::NextGen);
			if (ensure(!TryCacheName.IsEmpty())
				&& (PlatformFile.FileExists(*(TryCacheName + TEXT(".json")))
					|| PlatformFile.DirectoryExists(*TryCacheName)))
			{
				SchedulesGeneration = EITwinSchedulesGeneration::NextGen;
			}
			else
			{
				TryCacheName = ComputeCacheName(EITwinSchedulesGeneration::Legacy);
				if (ensure(!TryCacheName.IsEmpty())
					&& (PlatformFile.FileExists(*(TryCacheName + TEXT(".json")))
						|| PlatformFile.DirectoryExists(*TryCacheName)))
				{
					SchedulesGeneration = EITwinSchedulesGeneration::Legacy;
				}
			}
			if (EITwinSchedulesGeneration::Unknown == SchedulesGeneration)
			{
				BE_LOGW("ITwin4DImp", "An old Unreal Level with no Generation saved along with the schedule was probably loaded, and we could not infer the generation from local cache data: restarting schedule queries from scratch, which would have happened anyway, lacking usable cache data for this schedule - but you should resave your Unreal Level to update it.");
				return false;
			}
			Schedule->Generation = SchedulesGeneration;
		}
		FString const& CacheName = CustomCacheDir.IsEmpty() ? ComputeCacheName() : CustomCacheDir;
		FString const JsonPath = CacheName + TEXT(".json");
		auto OnTriedLoadScheduleFromJsonFile =
			[this, CacheName, OnScheduleLoadedFromJsonFile = std::move(OnScheduleLoadedFromJsonFile)]
			(bool bTaskSuccess)
			{
				FLock Lock(Mutex);
				if (!bTaskSuccess)
				{
					Schedule->Reserve(200);
					Queries->InitializeCache(CacheName, GetScheduleEnvironment(),
						FString(TEXT("schedule Id ")) + Schedule->Id + " named '" + Schedule->Name
						+ "' for iModel " + TargetedIModelId,
						(bool)UnitTesting);
				}
				OnScheduleLoadedFromJsonFile(bTaskSuccess);
			};
		if (ensure(!CacheName.IsEmpty()) && PlatformFile.FileExists(*JsonPath))
		{
			FLock Lock(Mutex); // lock also the launch in case it finishes before the end of this function
			FString const TaskName(TEXT("ReadAndParseScheduleJsonCache"));
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, TaskName, CacheName, JsonPath]()
				{
					bool const bSuccess = ReadAndParseScheduleJsonCacheInWorkerThread(CacheName, JsonPath);
					FLock Lock(Mutex);
					// The callback happens to create UObjects (thru WorldSingleton) ie game thread only!
					// => I have to defer this back to GT and handle Uninitialize gracefully...
					BackgroundTaskCompletionsInGT[TaskName] = std::move(WaitingForBackgroundTasks[TaskName]);
					WaitingForBackgroundTasks.erase(TaskName);
					AsyncTask(ENamedThreads::GameThread,
						[IsThisValid = this->IsThisValid, pMutex = &Mutex, this, TaskName, bSuccess]()
						{
							// Note: do NOT touch 'this' before checking validity: only 'pMutex' is safe,
							// the mutex outlives the FImpl (it is owned by the schedules component).
							FLock Lock(*pMutex);
							if (!IsThisValid->load())
								return;
							// Uninitialize() may have cleared the completions to prevent their execution,
							// and the callback may re-enter and mutate the map => extract it first.
							auto const Found = BackgroundTaskCompletionsInGT.find(TaskName);
							if (BackgroundTaskCompletionsInGT.end() == Found)
								return;
							auto const Completion = std::move(Found->second);
							BackgroundTaskCompletionsInGT.erase(Found);
							if (Completion)
								Completion(bSuccess);
						});
				});
			WaitingForBackgroundTasks[TaskName] = std::move(OnTriedLoadScheduleFromJsonFile);
		}
		else
		{
			// I think in this case we can call it directly, because it does not reach the UObject creation code
			// (UE will warn if it does, and we can fix it then)
			OnTriedLoadScheduleFromJsonFile(false);
		}
	}
	BE_LOGI("ITwin4DImp", "Added schedule Id " << TCHAR_TO_UTF8(*ScheduleId) << " named '"
		<< TCHAR_TO_UTF8(*Schedule->Name) << "' to iModel " << TCHAR_TO_UTF8(*TargetedIModelId));
	return true;
}

bool FITwinSchedulesImport::FImpl::ReadAndParseScheduleJsonCacheInWorkerThread(FString CacheName, FString JsonPath)
{
	double const StartRead = FPlatformTime::Seconds();
	FString JsonStructs;
	if (FFileHelper::LoadFileToString(JsonStructs, *JsonPath))
	{
		FLock Lock(Mutex);
		if (Schedule->FromJsonString(JsonStructs))
		{
			IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
			if (Schedule->AnimationBindings.empty() || !ensure(Schedule->FullyDefined(Lock)))
			{
				BE_LOGW("ITwin4DImp", "Schedule json cache for schedule Id " << TCHAR_TO_UTF8(*Schedule->Id)
					<< " named '" << TCHAR_TO_UTF8(*Schedule->Name)
					<< "' is incomplete - or simply empty (missing animation bindings or not fully defined), it will be ignored and re-created");
				FileManager.DeleteFile(*JsonPath);
			}
			else if (Schedule->JsonCacheVersion < FITwinSchedule::CurrentJsonCacheVersion)
			{
				BE_LOGW("ITwin4DImp", "Schedule json cache for schedule Id " << TCHAR_TO_UTF8(*Schedule->Id)
					<< " named '" << TCHAR_TO_UTF8(*Schedule->Name) << "' is outdated (cache version "
					<< Schedule->JsonCacheVersion << ", current version is "
					<< FITwinSchedule::CurrentJsonCacheVersion << "), it will be ignored and re-created");
				FileManager.DeleteFile(*JsonPath);
			}
			else
			{
				BE_LOGI("ITwin4DImp", "Loaded json cache for schedule Id " << TCHAR_TO_UTF8(*Schedule->Id)
					<< " took " << (FPlatformTime::Seconds() - StartRead)
					<< "s, named '" << TCHAR_TO_UTF8(*Schedule->Name) << "' to iModel "
					<< TCHAR_TO_UTF8(*TargetedIModelId));
				ComputeTimeRange();
				SetScheduleTimeRangeIsKnown();
				if (IsSimulatedSchedule())
				{
					// Still init the cache, so that we can test the incremental update mechanism
					Queries->InitializeCache(CacheName, GetScheduleEnvironment(),
						FString(TEXT("Simulated schedule Id ")) + Schedule->Id, (bool)UnitTesting);
				}
				return true;
			}
		}
		// Clear the partially loaded schedule!
		Schedule.emplace(FITwinSchedule(Schedule->Id, Schedule->Name, SchedulesGeneration));
	}
	return false;
}

int FITwinSchedulesImport::FImpl::ScheduleDependentPaginationCap() const
{
	return (Schedule && Schedule->Generation == EITwinSchedulesGeneration::NextGen) ? 10'000 : 50'000;
}

FUrlArgList FITwinSchedulesImport::FImpl::ConcatPaginationOrDeltaTokenParams(FUrlArgList&& ArgList,
	std::optional<FString> const& PageToken, std::optional<FString> const& DeltaToken,
	std::optional<int> CustomRequestPagination/*={}*/)
{
	ArgList.push_back({ APIParams::PageSize, FString::Printf(TEXT("%d"),
		std::min(CustomRequestPagination ? (*CustomRequestPagination) : RequestPagination,
				 ScheduleDependentPaginationCap())) });
	if (PageToken)
		ArgList.push_back({ APIParams::PageToken, *PageToken });
	else if (bFetchingPropertyDeltas && DeltaToken && !DeltaToken->IsEmpty())
		ArgList.push_back({ APIParams::DeltaToken, *DeltaToken });
	return ArgList;
}

void FITwinSchedulesImport::FImpl::RequestSchedules(ReusableJsonQueries::FStackingToken const& Token,
	std::optional<FString> const PageToken /*= {}*/, FLock* optLock /*= nullptr*/,
	std::shared_ptr<std::vector<FITwinSchedule>> ScheduleCandidates /*= {}*/)
{
	ensure(!UnitTesting);
	// To gather schedule candidates for this iModel and choose the desired one (Legacy or NextGen) only when
	// we're done with all possibly paginated requests
	if (!ScheduleCandidates)
		ScheduleCandidates = std::make_shared<std::vector<FITwinSchedule>>();
	auto ArgList = ConcatPaginationOrDeltaTokenParams(FUrlArgList{ { GetIdToQuerySchedules(), ITwinId } },
													  PageToken, {});
	// Here we are in the game thread so this is safe, whereas in the request callback it's not
	EITwinSchedulesGeneration const GenerationRequested = (SchedulesComponent().bFavorNextGenSchedule
		? EITwinSchedulesGeneration::NextGen
		: EITwinSchedulesGeneration::Legacy);
	// 1. First thing is to get the list of schedules.
	//
	// {} because the base URL (eg https://dev-synchro4dschedulesapi-eus.bentley.com/api/v1/schedules),
	// is actually the endpoint for listing the schedules related to a contextId/projectId (= iTwinId!)
	Queries->StackRequest(Token, optLock, ITwinHttp::EVerb::Get, {}, std::move(ArgList),
		[this, &Token, ScheduleCandidates, GenerationRequested] (TSharedPtr<FJsonObject> const& Reply)
		{
			ensure(!UnitTesting);
			auto NewScheds = Reply->GetArrayField(TEXT("schedules"));
			BE_LOGI("ITwin4DImp", "Received " << NewScheds.Num() << " schedules for iTwin "
								  << TCHAR_TO_UTF8(*ITwinId));
			if (0 == NewScheds.Num())
			{
				return;
			}
			FLock Lock(Mutex);
			for (const auto& SchedVal : NewScheds)
			{
				const auto& SchedObj = SchedVal->AsObject();
				FString IModelId;
				if (!SchedObj->TryGetStringField(TEXT("iModelId"), IModelId) || IModelId.IsEmpty())
				{
					FString ScheduleId;
					FString ScheduleName;
					SchedObj->TryGetStringField(TEXT("id"), ScheduleId);
					SchedObj->TryGetStringField(TEXT("name"), ScheduleName);
					std::string Details;
					if (!ScheduleId.IsEmpty())
					{
						Details += " (id=\"";
						Details += TCHAR_TO_UTF8(*ScheduleId);
						Details += "\")";
					}
					if (!ScheduleName.IsEmpty())
					{
						Details += " (name=\"";
						Details += TCHAR_TO_UTF8(*ScheduleName);
						Details += "\")";
					}
					BE_LOGW("ITwin4DImp", "Skipping schedule entry missing iModelId"
						<< Details
						<< " while querying schedules for iTwinId \"" << TCHAR_TO_UTF8(*ITwinId) << "\"");
					continue;
				}
				if (IModelId == TargetedIModelId)
				{
					FString Id;
					if (!ensure(SchedObj->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty()))
						continue;
					ScheduleCandidates->emplace_back(Id, SchedObj->GetStringField(TEXT("name")));
					bool isNextGen;
					if (SchedObj->TryGetBoolField(TEXT("isNextGen"), isNextGen))
					{
						ScheduleCandidates->back().Generation = isNextGen
							? EITwinSchedulesGeneration::NextGen
							: EITwinSchedulesGeneration::Legacy;
					}
				}
			}
			FString NextPageToken;
			if (Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
			{
				RequestSchedules(Token, NextPageToken, &Lock, ScheduleCandidates);
			}
			else
			{
				// We should get at most one schedule of each Legacy or NextGen type for a given iModel,
				// otherwise we will need a way to select which to use!
				ensure(ScheduleCandidates->size() <= 1
					|| (ScheduleCandidates->size() == 2
						&& ScheduleCandidates->at(0).Generation != EITwinSchedulesGeneration::Unknown
						&& ScheduleCandidates->at(1).Generation != EITwinSchedulesGeneration::Unknown
						&& ScheduleCandidates->at(0).Generation != ScheduleCandidates->at(1).Generation));
				auto Found = std::find_if(ScheduleCandidates->begin(), ScheduleCandidates->end(),
					[this, GenerationRequested](auto const& Sched)
					{
						return Sched.Generation == GenerationRequested;
					});
				if (ScheduleCandidates->end() == Found)
				{
					// Look for the other, non-favored generation types, ie any schedule which generation
					// is /not/ Unknown:
					Found = std::find_if(ScheduleCandidates->begin(), ScheduleCandidates->end(),
						[](auto const& Sched)
						{
							return Sched.Generation != EITwinSchedulesGeneration::Unknown;
						});
				}
				// If still not found, just use the first schedule (of Unknown type), if any:
				if (ScheduleCandidates->end() == Found && !ScheduleCandidates->empty())
				{
					Found = ScheduleCandidates->begin();
				}
				if (ScheduleCandidates->end() != Found)
				{
					ensure(OnFoundScheduleForTargetedIModel(Found->Id, Found->Name, {}, Found->Generation,
						[this, &Token](bool bLoadedFromJson)
						{
							FLock Lock(Mutex);
							if (!bLoadedFromJson && Schedule)
							{
								Queries->StatsResetActiveTime();
								AutoRequestScheduleItems(Token, &Lock);
							}
						}));
				}
			}
		});
}

template<typename TPropertyContainer>
bool FITwinSchedulesImport::FImpl::CanRequestAll(FString const& DeltaToken,
	TPropertyContainer const& PropertyContainer, int const DebugUpdateSubIncrement, FLock&)
{
	if (!bFetchingPropertyDeltas)
	{
		return true;
	}
	bool bHasDeltaToken = false;
	if (DeltaToken.IsEmpty())
	{
		// Only make the request again if the property container is empty, otherwise we already have "some" items and
		// don't want to request them again in a non-incremental manner since we have no DeltaToken to send
		bHasDeltaToken = PropertyContainer.empty();
	}
	else
	{
		// "empty" should not happen, unless for old, obsolete schedules? (got one in EA demo p1...)
		// "special-dev-no-delta-token" is useful for testing with a fabricated requests cache
		bHasDeltaToken = (DeltaToken != TEXT("special-dev-no-delta-token"));
	}
	if (bDebugNextScheduleUpdateIncrement)
	{
		if (DebugCurrentSubIncrement == DebugUpdateSubIncrement)
		{
			if (bHasDeltaToken)
			{
				// We'll request this property's delta update, so we can reset this now
				bDebugNextScheduleUpdateIncrement = false;
			}
			else
			{
				// try the next property (if we loop, it's OK, the next call to HandlePendingQueries will process
				// it since bDebugNextScheduleUpdateIncrement remains true).
				NextDebugSubIncrement();
			}
		}
		else
		{
			bHasDeltaToken = false; // this is not the property marked to be updated, skip it
		}
	}
	return bHasDeltaToken;
}

void FITwinSchedulesImport::FImpl::AutoRequestScheduleItems(ReusableJsonQueries::FStackingToken const& Token,
															FLock* Lock)
{
	std::optional<ITwinHttp::FLock> optLock;
	if (!Lock)
	{
		optLock.emplace(Mutex);
		Lock = &(*optLock);
	}
	bool const bDebugSingleRequest = bDebugNextScheduleUpdateIncrement;
	if (!bFetchingPropertyDeltas)
	{
		RequestScheduleStatistics(Token, *Lock);
	}
	if (CanRequestAll(Schedule->TasksDeltaToken, Schedule->Tasks, 0, *Lock))
	{
		RequestAllTasks(Token, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->AppearanceProfilesDeltaToken, Schedule->AppearanceProfiles, 1, *Lock))
	{
		RequestAllAppearanceProfiles(Token, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->StaticTransfosDeltaToken, Schedule->StaticTransfoAssignments, 2, *Lock))
	{
		RequestAllStaticTransfoAssignments(Token, {}, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->Anim3DPathsAssignmentsDeltaToken, Schedule->PathTransfoAssignments, 3, *Lock))
	{
		RequestAll3DPathTransfoAssignments(Token, {}, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->Anim3DPathsDeltaToken, Schedule->Animation3DPaths, 4, *Lock))
	{
		RequestAll3DPaths(Token, {}, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->Anim3DPathKeyframesDeltaToken, Schedule->Animation3DPaths/*same as above*/, 5, *Lock))
	{
		RequestAll3DPathKeyframes(Token, {}, *Lock);
		if (bDebugSingleRequest)
			return;
	}
	if (CanRequestAll(Schedule->BindingsDeltaToken, Schedule->AnimationBindings, 6, *Lock))
	{
		RequestAllAnimationBindings(Token, *Lock);
		if (bDebugSingleRequest)
			return;
	}
}

void FITwinSchedulesImport::FImpl::RequestScheduleStatistics(ReusableJsonQueries::FStackingToken const& Token,
															 FLock& Lock)
{
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Schedule->Id, TEXT("animation-statistics") }, {},
		[this, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			FITwinScheduleStats Tmp;
			if (!FSchedulesParse::ParseScheduleStatistics(Reply, Tmp, Schedule->Id, Schedule->Name))
			{
				if (!bHasFetchingErrors)
				{
					FirstFetchingErrorCode = EHttpResponseCodes::ServerError;
					FirstFetchingError = TEXT("Request succeeded but expected statistics are missing from reply");
					bHasFetchingErrors = true;
				}
				return;
			}
			FLock Lock(Mutex);
			Schedule->StatisticsTotal.emplace(Tmp);
			if (OnReceivedScheduleStats)
			{
				// As a callback we should assume this could reach UObject properties and BP stuff => game thread only
				// (although at the moment it is a no-op)
				AsyncTask(ENamedThreads::GameThread, [IsThisValid = this->IsThisValid, this]
					{
						if (IsThisValid->load())
							OnReceivedScheduleStats(*Schedule->StatisticsTotal);
					});
			}
			OnScheduleDownloadProgressed(*Schedule, Lock);
		});
}

void FITwinSchedulesImport::FImpl::HandleDeltaToken(TSharedPtr<FJsonObject> const& Reply, FString& PropDeltaToken,
													TCHAR const* PropName, FLock&)
{
	if (EITwinSchedulesGeneration::NextGen == Schedule->Generation)
	{
		if (!Reply->TryGetStringField(TEXT("deltaToken"), PropDeltaToken)
			|| PropDeltaToken.IsEmpty())
		{
			// This is apparently normal so no need to flood the logs...
			//BE_LOGI("ITwin4DImp", "Expected 'Delta-Token' not found or empty in reply: incremental schedule updates"
			//	<< " will be skipped unless the " << TCHAR_TO_UTF8(PropName)
			//	<< " reply simply was empty (no such property yet) for schedule '" // indeed, see CanRequestAll
			//	<< TCHAR_TO_UTF8(*Schedule->Name) << "'");
		}
		else
			bNeedToSaveScheduleToDisk = true;
	}
}

void FITwinSchedulesImport::FImpl::RequestAllTasks(ReusableJsonQueries::FStackingToken const& Token,
	FLock& Lock, std::optional<FString> const PageToken /*= {}*/)
{
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->TasksDeltaToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("tasks") },
							std::move(RequestArgList),
		[this, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("tasks"));
			if (Items.IsEmpty())
			{
				if (!bFetchingPropertyDeltas)
				{
					BE_LOGW("ITwin4DImp", "Did not receive any task for schedule '"
											<< TCHAR_TO_UTF8(*Schedule->Name) << "'!");
				}
			}
			else
			{
				FString NextPageToken;
				bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
				FLock Lock(Mutex);
				BE_LOGD("ITwin4DImp", "Received " << Items.Num() << " tasks (total "
					<< ((int)Schedule->Tasks.size() + Items.Num())
					<< (bMoreToCome ? ", more to come" : ", final reply")
					<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
				if (bMoreToCome)
					RequestAllTasks(Token, Lock, NextPageToken);
				else
					HandleDeltaToken(Reply, Schedule->TasksDeltaToken, TEXT("Tasks"), Lock);
				if (!bFetchingPropertyDeltas)
					Schedule->Tasks.reserve(Schedule->Tasks.size() + Items.Num());
				for (auto&& Item : Items)
				{
					const auto& TaskObj = Item->AsObject();
					if (bFetchingPropertyDeltas)
						BE_LOGD("ITwin4DImp", "Task increment: " << ToLoggerStr(TaskObj));
					FString TaskId = TaskObj->GetStringField(TEXT("id"));
					size_t TaskInVec = ITwin::INVALID_IDX;
					auto pTask = EmplaceProperty(TaskId, TaskInVec, Schedule->Tasks, Schedule->KnownTasks, Lock);
					bFetchingPropertyDeltasGotUpdates |=
						FSchedulesParse::ParseTaskDetails(TaskObj, *pTask, Schedule->Id, Lock);
				}
				if (!bFetchingPropertyDeltas)
				{
					Schedule->StatisticsCurrent.TaskCount += (size_t)Items.Num();
					OnScheduleDownloadProgressed(*Schedule, Lock);
				}
				if (!bMoreToCome)
				{
					ComputeTimeRange();
					SetScheduleTimeRangeIsKnown();
				}
			}
		});
}

void FITwinSchedulesImport::FImpl::RequestAllAppearanceProfiles(
	ReusableJsonQueries::FStackingToken const& Token,
	FLock& Lock, std::optional<FString> const PageToken /*= {}*/)
{
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->AppearanceProfilesDeltaToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("appearance-profiles") },
		std::move(RequestArgList),
		[this, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("appearanceProfiles"));
			if (Items.IsEmpty())
			{
				if (!bFetchingPropertyDeltas)
				{
					BE_LOGW("ITwin4DImp", "Did not receive any appearance profile for schedule '"
						<< TCHAR_TO_UTF8(*Schedule->Name) << "'!");
				}
			}
			else
			{
				FString NextPageToken;
				bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
				FLock Lock(Mutex);
				BE_LOGD("ITwin4DImp", "Received " << Items.Num() << " appearance profiles (total "
					<< ((int)Schedule->AppearanceProfiles.size() + Items.Num())
					<< (bMoreToCome ? ", more to come" : ", final reply")
					<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
				if (bMoreToCome)
					RequestAllAppearanceProfiles(Token, Lock, NextPageToken);
				else
					HandleDeltaToken(Reply, Schedule->AppearanceProfilesDeltaToken, TEXT("AppearanceProfiles"), Lock);
				if (!bFetchingPropertyDeltas)
					Schedule->AppearanceProfiles.reserve(Schedule->AppearanceProfiles.size() + Items.Num());
				for (auto&& Item : Items)
				{
					const auto& AppearanceObj = Item->AsObject();
					if (bFetchingPropertyDeltas)
						BE_LOGD("ITwin4DImp", "AppearanceProfile increment: " << ToLoggerStr(AppearanceObj));
					size_t AppearanceInVec = ITwin::INVALID_IDX;
					auto* pAppearance = EmplaceProperty(AppearanceObj->GetStringField(TEXT("id")), AppearanceInVec,
						Schedule->AppearanceProfiles, Schedule->KnownAppearanceProfiles, Lock);
					bFetchingPropertyDeltasGotUpdates |= FSchedulesParse::ParseAppearanceProfileDetails(
						AppearanceObj, *pAppearance, Lock);
				}
				if (!bFetchingPropertyDeltas)
				{
					Schedule->StatisticsCurrent.AppearanceProfileCount += (size_t)Items.Num();
					OnScheduleDownloadProgressed(*Schedule, Lock);
				}
			}
		});
}

template<typename TProperty>
TProperty* FITwinSchedulesImport::FImpl::EmplaceProperty(FString const& PropertyId, size_t& PropertyInVec,
	std::vector<TProperty>& SchedProperties, std::unordered_map<FString, size_t>& SchedKnownProperties, FLock&)
{
	if (PropertyId.IsEmpty()) // could be optional (transfo assignment - tested elsewhere)
	{
		return nullptr;
	}
	if (ITwin::INVALID_IDX == PropertyInVec)
	{
		auto KnownProperty = SchedKnownProperties.try_emplace(PropertyId, SchedProperties.size());
		PropertyInVec = KnownProperty.first->second;
		if (KnownProperty.second) // was inserted => need to create it
		{
			SchedProperties.emplace_back(TProperty{ PropertyId });
			return &SchedProperties.back();
		}
	}
	return &SchedProperties[PropertyInVec];
}

void FITwinSchedulesImport::FImpl::RequestAllAnimationBindings(ReusableJsonQueries::FStackingToken const& Token,
	FLock& Lock, std::optional<FString> const PageToken/*= {}*/)
{
	// APIM implements time-, task- and Element-filtering as parameters, no longer in a POST content json.
	// Since we no longer use it, I'm not implementing it for the time being: blame here to retrieve.
	//bool bHasTimeRange = false;
	// (...)
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->BindingsDeltaToken,
															 BindingsRequestPagination);
	Queries->StackRequest(
		Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("animation-bindings") }, std::move(RequestArgList),
		[this, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("animationBindings"));
			if (Items.IsEmpty())
				return;
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			FLock Lock(Mutex);
			if (bMoreToCome)
				RequestAllAnimationBindings(Token, Lock, NextPageToken);
			else
				HandleDeltaToken(Reply, Schedule->BindingsDeltaToken, TEXT("AnimationBindings"), Lock);
			if (!bFetchingPropertyDeltas)
				Schedule->AnimationBindings.reserve(Schedule->AnimationBindings.size() + Items.Num());
			size_t const FirstNewGroupIndex = Schedule->GetNextGroupID();
			bool const bCanResetElemIDGroups = (EITwinSchedulesGeneration::NextGen == Schedule->Generation)
				&& !IsSimulatedSchedule();
			bool bNeedResetElemIDGroups = false;
			for (auto&& NewBinding : Items)
			{
				const auto& BindingObj = NewBinding->AsObject();
				if (bFetchingPropertyDeltas)
					BE_LOGD("ITwin4DImp", "Binding increment: " << ToLoggerStr(BindingObj));

				ITwinElementID ElementID = ITwin::NOT_ELEMENT;
				FGuid ElementGuid;
				FAnimationBinding Tmp;
				if (!FSchedulesParse::ParseAnimationBinding(BindingObj, ElementID, ElementGuid, Tmp))
				{
					BE_LOGW("ITwin4DImp", "Skipping animation binding (parse error) in '"
						<< TCHAR_TO_UTF8(*Schedule->Name));
					continue;
				}

				// KnownAnimationBinding handling: cannot call EmplaceProperty here, bindings have no "id"!
				// Indeed they are compared by comparing all their members.
				size_t AnimIdx = Schedule->AnimationBindings.size();
				auto KnownAnim = Schedule->KnownAnimationBindings.try_emplace(Tmp, AnimIdx);
				if (KnownAnim.second) // was inserted => need to create it
				{
					Schedule->AnimationBindings.emplace_back(std::move(Tmp));
					// unless already deleted, it obviously affects the 4D anim
					if (bFetchingPropertyDeltas && !Tmp.bDeleted)
					{
						bFetchingPropertyDeltasGotUpdates = true;
						bNeedResetElemIDGroups |= bCanResetElemIDGroups;
					}
				}
				else // Already inserted and thus structure filled and property queries stacked BUT we have to...:
				{
					AnimIdx = KnownAnim.first->second;
					// ... update the properties if they changed, for incremental updates: bindings are identified by
					// all their properties (incl. appearance profile id) _except_ the bDeleted flag. We could change
					// the hash/equality operators to only consider the animated entity and possible resource/group Ids
					// but not if the 4D api deletes and recreates bindings for each change (eg; appearance) because in
					// that case the result on our end would depend on the order of the increments in the reply.
					if (bFetchingPropertyDeltas)
					{
						Schedule->AnimationBindings[AnimIdx].bDeleted = Tmp.bDeleted;
						// so, unless deleted, it does not affect the 4D anim since it was already present as-is
						// will probably not happen unless response time is larger than the update period and we get
						// the reply twice for the same delta token...
						if (Tmp.bDeleted)
						{
							bFetchingPropertyDeltasGotUpdates = true;
							bNeedResetElemIDGroups |= bCanResetElemIDGroups;
						}
					}
					// ... update the group with the new Element (not always "new" when bFetchingPropertyDeltas)
					// (see below)
				}
				FAnimationBinding& Anim = Schedule->AnimationBindings[AnimIdx];

				// Group creation or update
				if (std::holds_alternative<FString>(Anim.AnimatedEntities))
				{
					if (ITwin::INVALID_IDX == Anim.GroupInVec)
					{
						auto KnownGroup = Schedule->KnownGroups.try_emplace(
							std::get<FString>(Anim.AnimatedEntities), Schedule->GetNextGroupID());
						Anim.GroupInVec = KnownGroup.first->second;
						if (KnownGroup.second) // was inserted => need to create it
							Schedule->CreateNextGroup(ITwin::NOT_ELEMENT != ElementID);
					}
					if (ITwin::NOT_ELEMENT != ElementID)
						Schedule->AddToGroup(Anim.GroupInVec, ElementID);
					else if (ensure(ElementGuid.IsValid()))
						Schedule->AddToGroup(Anim.GroupInVec, ElementGuid);
				}
				if (!KnownAnim.second)
				{
					continue;
				}

				EmplaceProperty(Anim.TaskId, Anim.TaskInVec, Schedule->Tasks, Schedule->KnownTasks, Lock);
				EmplaceProperty(Anim.AppearanceProfileId, Anim.AppearanceProfileInVec,
								Schedule->AppearanceProfiles, Schedule->KnownAppearanceProfiles, Lock);
			#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
				EmplaceProperty(Anim.StaticTransfoAssignmentId, Anim.StaticTransfoAssignmentInVec,
					Schedule->StaticTransfoAssignments, Schedule->KnownStaticTransfoAssignments, Lock);
				EmplaceProperty(Anim.PathTransfoAssignmentId, Anim.PathTransfoAssignmentInVec,
					Schedule->PathTransfoAssignments, Schedule->KnownPathTransfoAssignments, Lock);
			#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()

			} // for NewBindings

			if (bNeedResetElemIDGroups)
				Schedule->ResetElemIDGroups();

			BE_LOGD("ITwin4DImp", "Received " << Items.Num() << " Bindings, total Elements/Groups bound: "
				<< [&Sched = (*Schedule)]()
				{
					std::unordered_set<decltype(FAnimationBinding::AnimatedEntities)> Bound;
					//could explore the variant and recurse into groups...
					for (auto&& Binding : Sched.AnimationBindings)
						Bound.insert(Binding.AnimatedEntities);
					return Bound.size();
				}());

			if (!bFetchingPropertyDeltas)
			{
				Schedule->StatisticsCurrent.AnimationBindingCount += (size_t)Items.Num();
				OnScheduleDownloadProgressed(*Schedule, Lock);
			}
		});
}

void FITwinSchedulesImport::FImpl::RequestTask(ReusableJsonQueries::FStackingToken const& Token,
	size_t const AnimIdx, FLock& Lock)
{
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Schedule->Id, TEXT("tasks"), Schedule->AnimationBindings[AnimIdx].TaskId },
		{},
		[this, AnimIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& JsonObj = Reply->GetObjectField(TEXT("task"));
			if (!ensure(JsonObj))
				return;
			FLock Lock(Mutex);
			auto& Binding = Schedule->AnimationBindings[AnimIdx];
			FSchedulesParse::ParseTaskDetails(
				JsonObj, Schedule->Tasks[Schedule->AnimationBindings[AnimIdx].TaskInVec], Schedule->Id, Lock);
		});
}

// 4dschedule/v1/schedules/{scheduleId}/tasks/{id}/resourceAssignments: should still be possible with Legacy
// schedules, if I understand correctly, but we should no longer needed them now that animation bindings
// are available - blame here to retrieve
//void FITwinSchedulesImport::FImpl::DetermineTaskElements(...)

// 4dschedule/v1/schedules/{scheduleId}/resources/{id}/entity3Ds: should still be possible with Legacy
// schedules, if I understand correctly, but we should no longer needed them now that animation bindings
// are available - blame here to retrieve
//void FITwinSchedulesImport::FImpl::RequestResourceEntity3Ds(...)

namespace ITwin::Timeline
{
	float fProbaOfOpacityAnimation = 0.5f;
}

void FITwinSchedulesImport::FImpl::CreateRandomAppearanceProfile(FAnimationBinding& AnimationBinding,
	FLock& Lock, bool const bCreateGroupFromResource3DEntities/*= false*/)
{
	using ITwin::Timeline::fProbaOfOpacityAnimation;
	size_t Seed = std::hash<decltype(AnimationBinding.AnimatedEntities)>()(AnimationBinding.AnimatedEntities);
	boost::hash_combine(Seed, GetTypeHash(AnimationBinding.TaskId));

	float CrudeFloatRand = 0.f;
	FVector const RandClr = FITwinMathExts::RandomFloatColorFromIndex(
		Seed, fProbaOfOpacityAnimation > 0.f ? &CrudeFloatRand : nullptr);
	constexpr bool bUseOriginalColorBeforeTask = true;
	constexpr bool bUseOriginalColorAfterTask = false;
	constexpr bool bUseGrowthSimulation = true;
	bool const bTestOpacityAnimation =
		(fProbaOfOpacityAnimation > 0.f) ? (fProbaOfOpacityAnimation >= CrudeFloatRand) : false;
	if (bCreateGroupFromResource3DEntities)
	{
		size_t const Dummy = Schedule->AppearanceProfiles.size();
		Schedule->KnownAppearanceProfiles[AnimationBinding.AppearanceProfileId] = Dummy;
		AnimationBinding.AppearanceProfileInVec = Dummy;
		Schedule->AppearanceProfiles.emplace_back();
	}

	auto& AppearanceProfile = Schedule->AppearanceProfiles[AnimationBinding.AppearanceProfileInVec];
	AppearanceProfile = FAppearanceProfile{
		TEXT("<DummyAppearanceProfileId>"),
		EDeletedProp(false),
		EProfileAction::Install,
		FSimpleAppearance(
			RandClr,
			/* translucency at start*/bTestOpacityAnimation ? .1f : 1.f,
			bUseOriginalColorBeforeTask,
			!bTestOpacityAnimation // use original color / alpha?
		),
		FActiveAppearance{
			FSimpleAppearance(
				FMath::Lerp(RandClr, FVector::OneVector, 0.5),
				bTestOpacityAnimation ? 0.25f : 1.f, // translucency at start / end of task
				false, !bTestOpacityAnimation // use original color / alpha?
			),
			FVector(1, 1, 1), // custom growth dir
			bTestOpacityAnimation ? 0.9f : 1.f, // finish alpha
			bUseGrowthSimulation ? (EGrowthSimulationMode)(Seed % 8) : EGrowthSimulationMode::None,
			true, true, // unimpl
			false // invert growth
		},
		FSimpleAppearance{
			0.5 * RandClr,
			1.f, // translucency at start / end ('end' one unused tho...)
			bUseOriginalColorAfterTask, true // use original color / alpha?
		}
	};
	if (bCreateGroupFromResource3DEntities)
	{
		size_t const ElementsGroupInVec = Schedule->NumGroups();
		AnimationBinding.AnimatedEntities = AnimationBinding.TaskId;//reuse as groupId
		AnimationBinding.AppearanceProfileId = TEXT("<DummyAppearanceProfileId>");
		AnimationBinding.GroupInVec = ElementsGroupInVec;
	}
	BE_LOGV("ITwin4DImp", "Random appearance profile used for " << TCHAR_TO_UTF8(*AnimationBinding.ToString()));
}

void FITwinSchedulesImport::FImpl::RequestAppearanceProfile(
	ReusableJsonQueries::FStackingToken const& Token, size_t const AnimIdx,
	FLock& Lock)
{
	if (!UnitTesting && SchedulesComponent().bDebugWithRandomProfiles)
	{
		CreateRandomAppearanceProfile(Schedule->AnimationBindings[AnimIdx], Lock);
		return;
	}
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Schedule->Id, TEXT("appearance-profiles"),
		  Schedule->AnimationBindings[AnimIdx].AppearanceProfileId },
		{},
		[this, AnimIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& JsonObj = Reply->GetObjectField(TEXT("appearanceProfile"));
			if (!ensure(JsonObj))
				return;
			FLock Lock(Mutex);
			auto& Binding = Schedule->AnimationBindings[AnimIdx];
			auto& AppearanceProfile = Schedule->AppearanceProfiles[Binding.AppearanceProfileInVec];
			ensure(Binding.AppearanceProfileId == AppearanceProfile.Id);
			FSchedulesParse::ParseAppearanceProfileDetails(JsonObj, AppearanceProfile, Lock);
		});
}

bool FITwinSchedulesImport::FImpl::ParseStaticTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
	FLock* pLock/*= nullptr*/, std::optional<size_t> const AnimIdx/*= {}*/)
{
	FMatrix Mat;
	if (!FSchedulesParse::ParseStaticTransform(Mat, JsonObj))
		return false;
	std::optional<FLock> optLock;
	if (!pLock)
	{
		optLock.emplace(Mutex);
		pLock = &(*optLock);
	}
	FStaticTransformAssignment* pTransformAssignment;
	if (AnimIdx) // in case of standalone request, it comes from a binding => index is known
	{
		pTransformAssignment =
			&Schedule->StaticTransfoAssignments[Schedule->AnimationBindings[*AnimIdx].StaticTransfoAssignmentInVec];
	}
	else // in case of batched transfo assignment request => index is unknown, entry may or may not exist
	{
		size_t TransfoAssignmentInVec = ITwin::INVALID_IDX;
		pTransformAssignment = EmplaceProperty(JsonObj->GetStringField(TEXT("id")),
			TransfoAssignmentInVec, Schedule->StaticTransfoAssignments, Schedule->KnownStaticTransfoAssignments, *pLock);
	}
	auto const WasDeleted = pTransformAssignment->bDeleted;
	JSON_GETDELETEDORFALSE(JsonObj, (*pTransformAssignment))
	FTransform AsTransform(Mat);
	bool const bSameAssignment = (WasDeleted == pTransformAssignment->bDeleted)
		&& (WasDeleted || pTransformAssignment->Transform.Equals(AsTransform));
	if (!bSameAssignment)
		pTransformAssignment->Transform = AsTransform;
	return !bSameAssignment;
}

void FITwinSchedulesImport::FImpl::RequestAllStaticTransfoAssignments(
	ReusableJsonQueries::FStackingToken const& Token, std::optional<FString> PageToken,
	FLock& Lock)
{
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->StaticTransfosDeltaToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("animation-3d-transforms") },
		std::move(RequestArgList),
		[this, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			auto Items = Reply->GetArrayField(TEXT("animation3dTransforms"));
			FLock Lock(Mutex);
			if (!Items.IsEmpty() || !bFetchingPropertyDeltas)
			{
				BE_LOGD("ITwin4DImp", "Received " << Items.Num() << " static transfo. assignments (total: "
					<< ((int)Schedule->StaticTransfoAssignments.size() + Items.Num())
					<< (bMoreToCome ? ", more to come" : ", final reply")
					<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
			}
			if (bMoreToCome)
				RequestAllStaticTransfoAssignments(Token, NextPageToken, Lock);
			else
				HandleDeltaToken(Reply, Schedule->StaticTransfosDeltaToken, TEXT("StaticTransforms"), Lock);
			if (!bFetchingPropertyDeltas)
				Schedule->StaticTransfoAssignments.reserve(Schedule->StaticTransfoAssignments.size() + Items.Num());
			for (auto&& TransfoObj : Items)
			{
				if (bFetchingPropertyDeltas)
					BE_LOGD("ITwin4DImp", "StaticTransfoAssign increment: " << ToLoggerStr(TransfoObj->AsObject()));
				bFetchingPropertyDeltasGotUpdates |= ParseStaticTransfoAssignment(TransfoObj->AsObject(), &Lock);
			}
			if (!bFetchingPropertyDeltas)
			{
				Schedule->StatisticsCurrent.Animation3dTransformCount += (size_t)Items.Num();
				OnScheduleDownloadProgressed(*Schedule, Lock);
			}
		});
}

bool FITwinSchedulesImport::FImpl::Parse3DPathTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
	ReusableJsonQueries::FStackingToken const& Token, FLock* pLock/*= nullptr*/,
	std::optional<size_t> const AnimIdx/*= {}*/)
{
	FPathTransformAssignment PathAssignment;
	if (!FSchedulesParse::Parse3DPathAssignment(PathAssignment, JsonObj))
		return false;

	std::optional<FLock> optLock;
	if (!pLock) optLock.emplace(Mutex);
	FLock& Lock = pLock ? (*pLock) : (*optLock);
	FPathTransformAssignment* pTransformAssignment;
	size_t TransfoAssignmentIndex = ITwin::INVALID_IDX;
	if (AnimIdx)
	{
		TransfoAssignmentIndex = Schedule->AnimationBindings[*AnimIdx].PathTransfoAssignmentInVec;
		pTransformAssignment = &Schedule->PathTransfoAssignments[TransfoAssignmentIndex];
	}
	else
	{
		// create the path *assignment*, if needed
		pTransformAssignment = EmplaceProperty(PathAssignment.Id,
			TransfoAssignmentIndex, Schedule->PathTransfoAssignments, Schedule->KnownPathTransfoAssignments, Lock);
		pTransformAssignment = &Schedule->PathTransfoAssignments[TransfoAssignmentIndex];
	}
	EDeletedProp const WasDeleted = pTransformAssignment->bDeleted;
	JSON_GETDELETEDORFALSE(JsonObj, (*pTransformAssignment))
	bool const bSameAssignment = (WasDeleted == pTransformAssignment->bDeleted)
		&& (WasDeleted || (*pTransformAssignment) == PathAssignment);
	if (!bSameAssignment)
	{
		*pTransformAssignment = std::move(PathAssignment);
		// create the path it refers to, if needed
		EmplaceProperty(pTransformAssignment->Animation3DPathId, pTransformAssignment->Animation3DPathInVec,
						Schedule->Animation3DPaths, Schedule->KnownAnimation3DPaths, Lock);
		return true;
	}
	return false;
}

void FITwinSchedulesImport::FImpl::RequestAll3DPathTransfoAssignments(
	ReusableJsonQueries::FStackingToken const& Token, std::optional<FString> PageToken,
	FLock& Lock)
{
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams(
		{}, PageToken, Schedule->Anim3DPathsAssignmentsDeltaToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("animation-3d-path-assignments") },
		std::move(RequestArgList),
		[this, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			auto Items = Reply->GetArrayField(TEXT("animation3dPathAssignments"));
			FLock Lock(Mutex);
			if (!Items.IsEmpty() || !bFetchingPropertyDeltas)
			{
				BE_LOGD("ITwin4DImp", "Received " << Items.Num()
					<< " 3D path transfo. assignments (total static+3D paths: "
					<< ((int)Schedule->PathTransfoAssignments.size() + Items.Num())
					<< (bMoreToCome ? ", more to come" : ", final reply")
					<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
			}
			if (bMoreToCome)
				RequestAll3DPathTransfoAssignments(Token, NextPageToken, Lock);
			else
				HandleDeltaToken(Reply, Schedule->Anim3DPathsAssignmentsDeltaToken,
								 TEXT("Animation3DPathAssignments"), Lock);
			if (!bFetchingPropertyDeltas)
				Schedule->PathTransfoAssignments.reserve(Schedule->PathTransfoAssignments.size() + Items.Num());
			for (auto&& TransfoObj : Items)
			{
				if (bFetchingPropertyDeltas)
					BE_LOGD("ITwin4DImp", "3DPathTransfoAssign increment: " << ToLoggerStr(TransfoObj->AsObject()));
				bFetchingPropertyDeltasGotUpdates |= Parse3DPathTransfoAssignment(TransfoObj->AsObject(), Token,
																				  &Lock);
			}
			if (!bFetchingPropertyDeltas)
			{
				Schedule->StatisticsCurrent.Animation3dPathAssignmentCount += (size_t)Items.Num();
				OnScheduleDownloadProgressed(*Schedule, Lock);
			}
		});
}

void FITwinSchedulesImport::FImpl::RequestAll3DPaths(ReusableJsonQueries::FStackingToken const& Token,
	std::optional<FString> const PageToken, FLock& Lock)
{
	auto RequestArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->Anim3DPathsDeltaToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedule->Id, TEXT("animation-3d-paths") },
		std::move(RequestArgList),
		[this, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("animation3dPaths"));
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			FLock Lock(Mutex);
			if (!Items.IsEmpty() || !bFetchingPropertyDeltas)
			{
				BE_LOGD("ITwin4DImp", "Received " << Items.Num() << " 3D paths (total "
					<< ((int)Schedule->Animation3DPaths.size() + Items.Num())
					<< (bMoreToCome ? ", more to come" : ", final reply")
					<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
			}
			if (bMoreToCome)
				RequestAll3DPaths(Token, NextPageToken, Lock);
			else
				HandleDeltaToken(Reply, Schedule->Anim3DPathsDeltaToken, TEXT("Animation3DPaths"), Lock);
			if (!bFetchingPropertyDeltas)
				Schedule->Animation3DPaths.reserve(Schedule->Animation3DPaths.size() + Items.Num());
			for (auto&& Item : Items)
			{
				const auto& PathObj = Item->AsObject();
				if (bFetchingPropertyDeltas)
					BE_LOGD("ITwin4DImp", "3DPath increment: " << ToLoggerStr(PathObj));
				FString PathId;
				JSON_GETSTR_OR(PathObj, "id", PathId, continue)
				size_t PathInVec = ITwin::INVALID_IDX;
				auto* pPath = EmplaceProperty(PathId, PathInVec, Schedule->Animation3DPaths,
											  Schedule->KnownAnimation3DPaths, Lock);
				EDeletedProp const WasDeleted = pPath->bDeleted;
				FString const WasName = pPath->Name;
				FVector const WasColor = pPath->Color;
				JSON_GETDELETEDORFALSE(PathObj, (*pPath))
				JSON_GETSTR_OR(PathObj, "name", pPath->Name, continue)
				FString ColorStr;
				JSON_GETSTR_OR(PathObj, "color", ColorStr, continue)
				if (!FSchedulesParse::ParseColorFromHexString(ColorStr, pPath->Color))
					continue;
				bFetchingPropertyDeltasGotUpdates |= (WasDeleted != pPath->bDeleted);
				// NOT testing those, as they don't affect 4D animation:
				//|| (WasName != pPath->Name) || (WasColor != pPath->Color);
			}
			if (!bFetchingPropertyDeltas)
			{
				Schedule->StatisticsCurrent.Animation3dPathCount += (size_t)Items.Num();
				OnScheduleDownloadProgressed(*Schedule, Lock);
			}
		});
}

// Added for consistency, useful when looking up RequestAll* ;-)
void FITwinSchedulesImport::FImpl::RequestAll3DPathKeyframes(ReusableJsonQueries::FStackingToken const& Token,
	std::optional<FString> const PageToken, FLock& Lock)
{
	Request3DPathKeyframes(Token, ITwin::INVALID_IDX, {}, Lock);
}

/// \param TransfoAssignmentIdx Index of one (of possibly several) FPathTransformAssignment pointing at this path:
///		easier than passing the path Id and path index in the schedule's vector
void FITwinSchedulesImport::FImpl::Request3DPathKeyframes(ReusableJsonQueries::FStackingToken const& Token,
	size_t const TransfoAssignmentIdx, std::optional<FString> const PageToken, FLock& Lock)
{
	auto ArgList = ConcatPaginationOrDeltaTokenParams({}, PageToken, Schedule->Anim3DPathKeyframesDeltaToken);
	bool const bFirstPage = !((bool)PageToken);
	FUrlSubpath SubPath{ Schedule->Id, TEXT("animation-3d-paths") };
	if (ITwin::INVALID_IDX != TransfoAssignmentIdx)
	{
		// old behavior (non-batched): ask for keyframes of a specific 3D path
		SubPath.emplace_back(Schedule->PathTransfoAssignments[TransfoAssignmentIdx].Animation3DPathId);
	}
	SubPath.emplace_back(TEXT("keyframes"));
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, std::move(SubPath), std::move(ArgList),
		[this, TransfoAssignmentIdx, bFirstPage, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			auto&& KeyframesArray = Reply->GetArrayField(TEXT("keyframes"));
			if (KeyframesArray.IsEmpty())
				return;
			FAnimation3DPath Parsed;
			Parsed.Keyframes.reserve(KeyframesArray.Num());
			// new behavior (batched): we'll receive the same kind of array of keyframes, the only difference
			// is that each keyframe might point at a different pathId, although the keyframes of each path
			// are most certainly streamed sequentially
			bool const bMixedKeyframes = (ITwin::INVALID_IDX == TransfoAssignmentIdx);
			std::vector<FString> KeyframePathIds;
			if (bMixedKeyframes)
				KeyframePathIds.reserve(KeyframesArray.Num());
			for (auto&& Entry : KeyframesArray)
			{
				if (bFetchingPropertyDeltas)
					BE_LOGD("ITwin4DImp", "3DPathKeyframe increment: " << ToLoggerStr(Entry->AsObject()));
				FTransformKey Keyframe;
				if (!FSchedulesParse::Parse3DPathKeyframe(KeyframePathIds, Keyframe, Entry->AsObject(),
														  bMixedKeyframes))
					continue;
				Parsed.Keyframes.emplace_back(std::move(Keyframe));
			}
			FLock Lock(Mutex);
			auto Finalize3DPath = [this, &Lock](FAnimation3DPath& Path3D)
				{
					// A bit overkill but probably safer to sort the whole vector even though the only
					// requirement is that Add3DPathTransformToTimeline needs the first frame of the list
					// (usually where t=0)
					std::sort(Path3D.Keyframes.begin(), Path3D.Keyframes.end(),
						[](FTransformKey const& J, FTransformKey const& K)
						{ return J.RelativeTime < K.RelativeTime; });
				};
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			static size_t TotalParsedKeyframes = 0;
			if (bFirstPage)
				TotalParsedKeyframes = KeyframesArray.Num();
			else
				TotalParsedKeyframes += KeyframesArray.Num();
			BE_LOGD("ITwin4DImp", "Received " << KeyframesArray.Num() << " 3D path keyframes (total: "
				<< TotalParsedKeyframes << (bMoreToCome ? ", more to come" : ", final reply")
				<< ") for schedule '" << TCHAR_TO_UTF8(*Schedule->Name) << "'");
			if (bMoreToCome)
				Request3DPathKeyframes(Token, TransfoAssignmentIdx, std::move(NextPageToken), Lock);
			else
				HandleDeltaToken(Reply, Schedule->Anim3DPathKeyframesDeltaToken, TEXT("Animation3DPathKeyframes"),
								 Lock);
			bFetchingPropertyDeltasGotUpdates = true; // KF has no property that does not affect anim...
			if (bMixedKeyframes)
			{
				ensure(Parsed.Keyframes.size() == KeyframePathIds.size());
				auto ParsedKF = Parsed.Keyframes.begin();
				auto ParsedKFPathId = KeyframePathIds.begin();
				for ( ; ParsedKF != Parsed.Keyframes.end() && ParsedKFPathId != KeyframePathIds.end();
						++ParsedKF, ++ParsedKFPathId)
				{
					if (!ensure(!ParsedKFPathId->IsEmpty()))
						continue;
					size_t PathInVec = ITwin::INVALID_IDX;
					auto* pPath = EmplaceProperty(*ParsedKFPathId, PathInVec, Schedule->Animation3DPaths,
												  Schedule->KnownAnimation3DPaths, Lock);
					// This is necessary to handle updating existing keyframes when querying incremental schedule
					// updates, as the only way to identify a keyframe in a path is by its time point!
					if (bFetchingPropertyDeltas)
					{
						auto FoundKF = std::find_if(pPath->Keyframes.begin(), pPath->Keyframes.end(),
							[&ParsedKF](FTransformKey const& KF)
								{ return KF.RelativeTime == ParsedKF->RelativeTime; });
						if (FoundKF != pPath->Keyframes.end())
						{
							if (ParsedKF->bDeleted)
								pPath->Keyframes.erase(FoundKF);
							else
								*FoundKF = *ParsedKF;
						}
						else
						{
							if (!ParsedKF->bDeleted)
								pPath->Keyframes.push_back(*ParsedKF);
							// else: do nothing, we don't want to add a deleted KF, and if it's not found it means it
							// was already deleted - so no need to worry about it either way
							// Note: this also means that if the KF is new but already deleted, it won't be added, but
							// that shouldn't be an issue since it's deleted anyway and it would be weird to have a KF
							// created and deleted at the same time, but who knows, with concurrent queries...
						}
					}
					else // avoid the added cost
						pPath->Keyframes.push_back(*ParsedKF);
				}
				if (!bMoreToCome)
				{
					// Finalize ALL 3D paths: there should be no redundancy since their querying is batched
					// and no individual requests should have been made that could have already completed one
					for (auto&& Path3D : Schedule->Animation3DPaths)
						Finalize3DPath(Path3D);
				}
			}
			else
			{
				auto& Path3D = Schedule->Animation3DPaths[
					Schedule->PathTransfoAssignments[TransfoAssignmentIdx].Animation3DPathInVec];
				if (bFirstPage)
				{
					Path3D.Keyframes = std::move(Parsed.Keyframes);
				}
				else
				{
					size_t const CurrentSize = Path3D.Keyframes.size();
					Path3D.Keyframes.resize(CurrentSize + Parsed.Keyframes.size());
					std::copy(Parsed.Keyframes.begin(), Parsed.Keyframes.end(),
							  Path3D.Keyframes.begin() + CurrentSize);
				}
				if (!bMoreToCome)
					Finalize3DPath(Path3D);
			}
			if (!bFetchingPropertyDeltas)
			{
				Schedule->StatisticsCurrent.Animation3dPathKeyframeCount += (size_t)KeyframesArray.Num();
				OnScheduleDownloadProgressed(*Schedule, Lock);
			}
		});
}

void FITwinSchedulesImport::FImpl::SetSchedulesImportConnectors(
	FOnReceivedScheduleStats const& InOnReceivedScheduleStats)
{
	FLock Lock(Mutex);
	if (ensure(InOnReceivedScheduleStats))
		OnReceivedScheduleStats = InOnReceivedScheduleStats;
}

// No longer used: see comment where it was called, and blame here to retrieve implem
//bool FITwinSchedulesImport::FImpl::ResetDeltaTokenFor(FString URL)

/// CustomCacheDir and CustomScheduleGeneration used only for unit testing
void FITwinSchedulesImport::FImpl::ResetConnection(FString const& ITwinAkaProjectAkaContextId,
	FString const& IModelId, FString const& InChangesetId, FString const& CustomCacheDir,
	EITwinSchedulesGeneration CustomScheduleGeneration /*= EITwinSchedulesGeneration::Unknown*/)
{
	{
		FLock Lock(Mutex);
		if (Schedule)
			SaveScheduleToDiskIfNeeded(Lock);
		bNeedToSaveScheduleToDisk = false; // even if it failed
		// I can imagine the URL or the token could need updating (new mirror, auth renew),
		// but not the iTwin nor the iModel
		ensure((!Queries && ITwinId.IsEmpty() && TargetedIModelId.IsEmpty())
			|| (ITwinAkaProjectAkaContextId == ITwinId && IModelId == TargetedIModelId));

		SchedApiSession = s_NextSchedApiSession++;
		LastCheckTotalBindings = 0.;
		LastTotalBindingsFound = 0;
		SchedulesGeneration = CustomScheduleGeneration;
		if (!Queries)
		{
			ITwinId = ITwinAkaProjectAkaContextId;
			TargetedIModelId = IModelId;
			ChangesetId = InChangesetId;
			// See also comment about empty changeset in QueriesCache::GetCacheFolder():
			ensureMsgf(ChangesetId.ToLower() != TEXT("latest"), TEXT("Need to pass the resolved changeset!"));
			if (Owner->Owner)
				Owner->Owner->OnQueryLoopStatusChange(true);
		}
		std::function<FString()> GetBearerToken = [] { return FString(); };
		if (!UnitTesting)
			GetBearerToken = [SchedComp = Owner->Owner]() -> FString
			{
				AITwinIModel const& IModel = *Cast<AITwinIModel const>(SchedComp->GetOwner());
				if (ensure(IModel.ServerConnection))
					return IModel.ServerConnection->GetAccessToken();
				else
					return TEXT("_TokenError_");
			};
		Queries = MakePimpl<FReusableJsonQueries>(
			UnitTesting ? (*UnitTesting->OwnerUObject.Get()) : (*Owner->Owner),
			GetSchedulesAPIBaseUrl(),
			[]()
			{
				static const FString AcceptJson(
					"application/json;odata.metadata=minimal;odata.streaming=true");
				const auto Request = FHttpModule::Get().CreateRequest();
				Request->SetHeader("Accept", AcceptJson);
				Request->SetHeader("Content-Type", AcceptJson);
				return Request;
			},
			/*SimultaneousRequestsAllowed =*/ 6,
			[this](FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
				ITwinHttp::ConnectionSuccess bConnectedSuccessfully, ITwinHttp::RetryQuery& bWillRetry,
				DeltaTokenExpired& bDeltaTokenExpired)
			{
				FString StrError;
				bDeltaTokenExpired = DeltaTokenExpired(false);
				bool const bRequestOK = ITwinHttp::CheckRequest(
					CompletedRequest, Response, bConnectedSuccessfully, &StrError, bWillRetry);
				if (!bRequestOK)
				{
					if (Response && EHttpResponseCodes::BadRequest == Response->GetResponseCode()
						&& StrError.Contains(TEXT("[InvalidSchedulesRequest]"))
						&& StrError.Contains(TEXT("[InvalidParameter]"))
						&& StrError.Contains(TEXT("target: $deltaToken")))
					{
						bDeltaTokenExpired = DeltaTokenExpired(true);
						// Don't: the deltaToken will be edited out of the retry anyway; and the retry may very well
						// be interrupted by a ResetConnection, which is in fact frequent at the start of a session.
						// In that case the new deltaToken is not received and an empty string remains in the schedule
						// json, which leads to ignoring incremental updates entirely the next time it is loaded!
						// (see FITwinSchedulesImport::FImpl::CanRequestAll)
						//if (ResetDeltaTokenFor(CompletedRequest->GetURL()))
						{
							BE_LOGI("ITwin4DImp", "While loading schedule Id " << TCHAR_TO_UTF8(*Schedule->Id)
								<< ", expired delta token handled (will recover) for URL: "
								<< TCHAR_TO_UTF8(*CompletedRequest->GetURL()));
							Queries->OnDeltaTokenExpired(CompletedRequest);
							// Force retry, in case the token expiration occurred on the *last* retry of a request
							bWillRetry = ITwinHttp::RetryQuery(true);
						}
						//else // sth went wrong, we don't know which token expired, so we can't recover
						//	bWillRetry = ITwinHttp::RetryQuery(false);
					}
					// Note: not marking delta queries errors, which would block 4D replay entirely
					else if (!bHasFetchingErrors && !bWillRetry && !bFetchingPropertyDeltas)
					{
						FirstFetchingErrorCode = Response ? EHttpResponseCodes::Type(Response->GetResponseCode())
							: EHttpResponseCodes::Unknown;
						FirstFetchingError = StrError;
						bHasFetchingErrors = true;
						if (!Schedule)
							bHasFailedListingSchedules = true;
					}
				}
				return bRequestOK;
			},
			Mutex,
			(!Owner->Owner || Owner->Owner->DebugRecordSessionQueries.IsEmpty()
				|| !Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
			? nullptr : (*Owner->Owner->DebugRecordSessionQueries),
			SchedApiSession,
			(!Owner->Owner || Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
			? nullptr : (*Owner->Owner->DebugSimulateSessionQueries),
			GetBearerToken,
			std::bind(&FITwinSchedulesImport::FImpl::IsWaitingForBackgroundTasksAndCompletionInGT, this));

		// Reset after pending queries have been cancelled by FReusableJsonQueries::FImpl::~FImpl
		bFetchingPropertyDeltas = false;
		bFetchingPropertyDeltasGotUpdates = false;
		bAllowIncrementalUpdatesToFinishPrefetching = false;

	} // end Lock

	ensure(!UnitTesting || !CustomCacheDir.IsEmpty());
	// bHasFinishedPrefetching = false; No, ResetConnection doesn't reset the structures...
	// We may be resetting a known Schedule (OR running a unit test), in which case we kept its Id, Name and Generation
	if (Schedule)
	{
		if (!OnFoundScheduleForTargetedIModel(Schedule->Id, Schedule->Name, CustomCacheDir, Schedule->Generation,
			[this](bool bLoadedFromJson)
			{
				FLock Lock(Mutex);
				if (!bLoadedFromJson)
				{
					Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
						{ AutoRequestScheduleItems(Token, nullptr); });
				}
				CreateIncrementalUpdateAndWrapUpBatches();
			}))
		{
			Schedule.reset();
		}
	}
	if (!Schedule && !UnitTesting)
	{
		Queries->NewBatch(
			[this](ReusableJsonQueries::FStackingToken const& Token) { RequestSchedules(Token); });
		CreateIncrementalUpdateAndWrapUpBatches();
	}
}

void FITwinSchedulesImport::FImpl::CreateIncrementalUpdateAndWrapUpBatches()
{
	// Safety against possible intermingling of calls to ResetConnection and periodical incremental update batches
	auto CancelWrapUpBatch = std::make_shared<bool>(false);
	Queries->NewBatch([this, CancelWrapUpBatch](ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			if (Schedule && EITwinSchedulesGeneration::NextGen == Schedule->Generation)
			{
				double const CurTime = FPlatformTime::Seconds();
				// Query incremental updates of the schedule before applying it, in case the cache is missing recent
				// changes: the very first time, we always enter this branch, because LastFetchingPropertyDeltasTime
				// is initialized to 0.0
				if ((CurTime - LastFetchingPropertyDeltasTime) > FetchingPropertyDeltasPeriod)
				{
					LastFetchingPropertyDeltasTime = CurTime;
					bFetchingPropertyDeltas = true;
					// Might reach this during the next periodical schedule update, after early-exiting because of
					// "!Schedule->FullyDefined(Lock)" below => do the reset only in the wrap-up batch
					//bFetchingPropertyDeltasGotUpdates = false;
					if (UnitTesting || !SchedulesComponent().bDebugFreezeIncrementalScheduleUpdates
									|| bDebugNextScheduleUpdateIncrement)
					{
						AutoRequestScheduleItems(Token, &Lock);
					}
				}
				else if (bHasFinishedPrefetching)
				{
					*CancelWrapUpBatch = true;
				}
			}
		});
	// Wrap-up batch: does not query anything, acts as a "join" to exec stuff when all previous queries are finished:
	Queries->NewBatch([this, CancelWrapUpBatch](ReusableJsonQueries::FStackingToken const& Token)
		{
			if (*CancelWrapUpBatch)
				return;
			// bFetchingPropertyDeltas is accessed from concurrent reply handlers, but by definition this is executed
			// (in the game thread, as all functors passed to "NewBatch") after all previous requests' reply handlers
			// have finished executing, so normally no need to put this inside a locked session. Which is in fact
			// probably not needed either for Schedule accesses below for the same reason.
			// But for consistency, I leave everything locked...
			FLock Lock(Mutex);
			if (!Schedule)
			{
				SetScheduleTimeRangeIsKnown();
				if (Owner->Owner)
					Owner->Owner->OnScheduleQueryingStatusChanged.Broadcast(false);
			}
			else
			{
				if (bFetchingPropertyDeltas)
				{
					bFetchingPropertyDeltas = false;
					// If !bHasFinishedPrefetching, this means that this batch is running after the initial queries for
					// all schedule items (to avoid applying a stale cached schedule, we send delta queries
					// immediately): in that case we need to proceed below as usual as it has not been applied yet!
					if (bHasFinishedPrefetching)
					{
						// This was a periodical batch of delta queries:
						// 1- if no actual change was returned affecting the 4D animation: we must skip the
						//    notifications below which would lead to rebuilding the timelines:
						if (!bFetchingPropertyDeltasGotUpdates || !Schedule->FullyDefined(Lock))
							return;
						// Don't save "simulated" schedule's incremental updates to disk, otherwise there will be
						// nothing left to test next time it is loaded...
						bNeedToSaveScheduleToDisk = !IsSimulatedSchedule();
						// 2- if incremental updates are frozen (for debugging purposes), the in-flight requests were
						//    handled, but do not apply the possible changes:
						// NO, otherwise update increments obtained by clicking "DebugProcessScheduleUpdateIncrement"
						// will also be bypassed here... So just ignore possible in-flight requests when this debug
						// flag is toggled on:
						//if (!UnitTesting && SchedulesComponent().bDebugFreezeIncrementalScheduleUpdates)
						//	return;
					}
					else
					{
						if (!Schedule->FullyDefined(Lock)) [[unlikely]]
						{
							bAllowIncrementalUpdatesToFinishPrefetching = true;
							return;
						}
						ComputeNeedToSaveScheduleToDisk(Lock);
					}
				}
				else
				{
					// With NextGen, we should always finish the initial loading of a schedule by a batch of delta
					// queries, in case there are updates: both when reloading from the local cache(s) but also when
					// first querying from the remote (since it can be long, a quick batch of update queries sounds
					// useful to make sure we're up to date).
					ensure(EITwinSchedulesGeneration::Legacy == Schedule->Generation);
					ComputeNeedToSaveScheduleToDisk(Lock);
				}
				if (!bHasFinishedPrefetching) // <=> initial schedule load (remote or caches)
				{
					bHasFinishedPrefetching = true;//needed before ClearCacheOnlyThis() which checks it
					bool const bSaved = SaveScheduleToDiskIfNeeded(Lock);
					if (!IsSimulatedSchedule())
					{
						// Don't clear when a schedule is "simulated" from a local cache folder, otherwise debug
						// increments requests will not look in the cache and try to emit requests:
						Queries->ClearCacheFromMemory();
						// We don't keep the requests cache once we have the full schedule, because it will be reloaded
						// directly from the raw structures saved above. Except for unit tests where the requests cache
						// is part of the repo and of the test resources! (and also Owner->Owner is null)
						if (bSaved && !UnitTesting)
							Owner->Owner->ClearCacheOnlyThis();
					}
				}
				Queries->EraseDroppableEntries();
				bFetchingPropertyDeltasGotUpdates = false;
				// if Elements metatada not yet available, this will just update the DL progress,
				// otherwise note that this also broadcasts OnScheduleQueryingStatusChanged(!IsAvailable())
				// (formerly done by FReusableJsonQueries)
				if (Owner->Owner)
					Owner->Owner->OnQueryLoopStatusChange(false, false);
			}
		});
}

void FITwinSchedulesImport::FImpl::ComputeNeedToSaveScheduleToDisk(FLock& Lock)
{
	bNeedToSaveScheduleToDisk = !IsSimulatedSchedule()
		&& !bHasFetchingErrors
		// ie schedule was NOT loaded entirely from the cached json
		&& (!Queries->CacheFolder().IsEmpty()
			// OR some incremental updates were applied afterwards so we need to resave it
			|| bFetchingPropertyDeltasGotUpdates);
}

bool FITwinSchedulesImport::FImpl::SaveScheduleToDiskIfNeeded(FLock& Lock)
{
	if (!bNeedToSaveScheduleToDisk
		// Don't create the ".json" cached schedule which would change the way it is loaded in
		// later tests! (Loading the schedule from the json is tested separately)
		|| UnitTesting)
	{
		return false;
	}
	// No cache folder means the requests cache was not initialized because most likely the schedule was
	// loaded from the json (or caching was disabled)
	FString CacheName = Queries->CacheFolder();
	if (CacheName.IsEmpty())
		CacheName = ComputeCacheName();
	if (ensure(!CacheName.IsEmpty()))
	{
		// Use the same name, don't write inside the cache folder which is entirely removed by ClearCacheOnlyThis
		double const StartWrite = FPlatformTime::Seconds();
		if (Schedule->SaveToJson(CacheName + TEXT(".json"), false, Lock))
		{
			bNeedToSaveScheduleToDisk = false;
			BE_LOGI("ITwin4DImp", "Saved schedule '" << TCHAR_TO_UTF8(*Schedule->Name)
				<< "' to cache file '" << TCHAR_TO_UTF8(*(CacheName + TEXT(".json")))
				<< "' in " << (FPlatformTime::Seconds() - StartWrite) << "s");
			if (EITwinSchedulesGeneration::Legacy == Schedule->Generation)
			{
				// Immediately delete cache files for other (older) changesets
				FString const CachesFolder = FPaths::GetPath(CacheName);
				QueriesCache::FChangesetFinderIterator DirIter(CacheName + TEXT(".json"));
				IFileManager::Get().IterateDirectory(*CachesFolder, DirIter);
				DirIter.DeleteOtherChangesetJsons();
			}
			return true;
		}
		else
		{
			BE_LOGW("ITwin4DImp", "Failed to save schedule '" << TCHAR_TO_UTF8(*Schedule->Name)
				<< "' to cache file '" << TCHAR_TO_UTF8(*(CacheName + TEXT(".json"))) << "'");
		}
	}
	return false;
}

bool FITwinSchedulesImport::FImpl::IsWaitingForBackgroundTasksAndCompletionInGT() const
{
	FLock Lock(Mutex);
	return !(WaitingForBackgroundTasks.empty() && BackgroundTaskCompletionsInGT.empty());
}

std::pair<int, int> FITwinSchedulesImport::FImpl::HandlePendingQueries()
{
	if (!Queries)
		return std::make_pair(0, 0);

	if (IsWaitingForBackgroundTasksAndCompletionInGT())
		return Queries->QueueSize();

	if ((bHasFinishedPrefetching || bAllowIncrementalUpdatesToFinishPrefetching)
		&& Queries->QueueSize() == std::make_pair(0, 0))
	{
		bAllowIncrementalUpdatesToFinishPrefetching = false;
		FLock Lock(Mutex);
		if (Schedule && EITwinSchedulesGeneration::NextGen == Schedule->Generation
			&& (UnitTesting || !SchedulesComponent().bDebugFreezeIncrementalScheduleUpdates
							|| bDebugNextScheduleUpdateIncrement))
		{
			double const CurTime = FPlatformTime::Seconds();
			if ((CurTime - LastFetchingPropertyDeltasTime) > FetchingPropertyDeltasPeriod)
			{
				// Don't update LastFetchingPropertyDeltasTime, it's retested & updated in the batch itself.
				CreateIncrementalUpdateAndWrapUpBatches();
			}
		}
	}
	Queries->HandlePendingQueries();
	return Queries->QueueSize();
}

/// Needed in the CPP otherwise the default ctor impl complains about FITwinSchedulesConnector being unknown
FITwinSchedulesImport::FITwinSchedulesImport(UITwinSynchro4DSchedules& InOwner,
	std::recursive_mutex& Mutex, std::optional<FITwinSchedule>& Schedule)
	: Owner(&InOwner)
	, Impl(MakePimpl<FImpl>(*this, Mutex, Schedule))
{
}

FITwinSchedulesImport::FITwinSchedulesImport(FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
	TStrongObjectPtr<UObject> OwnerUObj, std::recursive_mutex& Mutex, std::optional<FITwinSchedule>& Schedule)
	: Owner(nullptr)
	, Impl(MakePimpl<FImpl>(*this, BaseUrl, MainTimeline, OwnerUObj, Mutex, Schedule))
{
}

FITwinSchedulesImport& FITwinSchedulesImport::operator=(FITwinSchedulesImport&& Other)
{
	Uninitialize(); // needed because std::move below actually destroys the Impl...
	Impl = std::move(Other.Impl);
	Impl->Owner = this;
	return *this;
}

bool FITwinSchedulesImport::IsReadyToQuery() const
{
	return Impl->Queries.Get() != nullptr;
}

void FITwinSchedulesImport::BeginShutdown()
{
	if (Impl->Queries)
		Impl->Queries->BeginShutdown();
}

bool FITwinSchedulesImport::HasFinishedPrefetching() const
{
	return Impl->bHasFinishedPrefetching;
}

bool FITwinSchedulesImport::HasFetchingErrors() const
{
	return Impl->bHasFetchingErrors;
}

FString FITwinSchedulesImport::FirstFetchingErrorString() const
{
	return Impl->FirstFetchingError;
}

EHttpResponseCodes::Type FITwinSchedulesImport::FirstFetchingErrorCode() const
{
	return Impl->FirstFetchingErrorCode;
}

size_t FITwinSchedulesImport::FetchedFromRemote() const
{
	return Impl->Queries ? Impl->Queries->FetchedFromRemote() : 0;
}

size_t FITwinSchedulesImport::FetchedFromCache() const
{
	return Impl->Queries ? Impl->Queries->FetchedFromCache() : 0;
}

bool FITwinSchedulesImport::HasSchedulesListingFailed() const
{
	return Impl->bHasFailedListingSchedules;
}

void FITwinSchedulesImport::ResetConnection(FString const& ITwinAkaProjectAkaCtextId, FString const& IModelId,
											FString const& InChangesetId)
{
	Impl->ResetConnection(ITwinAkaProjectAkaCtextId, IModelId, InChangesetId, {});
}

/// During testing, we can't make the initial first request for the schedule Id: it has been set
/// specifically from the FITwinSchedulesImport and the FImpl's dedicated constructors
void FITwinSchedulesImport::ResetConnectionForTesting(FString const& ITwinAkaProjectAkaContextId,
	FString const& IModelId, FString const& InChangesetId, FString const& CacheDir,
	EITwinSchedulesGeneration ScheduleGeneration)
{
	Impl->ResetConnection(ITwinAkaProjectAkaContextId, IModelId, InChangesetId, CacheDir, ScheduleGeneration);
}

void FITwinSchedulesImport::SetSchedulesImportConnectors(
	FOnReceivedScheduleStats const& InOnReceivedScheduleStats)
{
	Impl->SetSchedulesImportConnectors(InOnReceivedScheduleStats);
}

std::pair<int, int> FITwinSchedulesImport::HandlePendingQueries()
{
	return Impl->HandlePendingQueries();
}

void FITwinSchedulesImport::DebugProcessScheduleUpdateIncrement()
{
	Impl->bDebugNextScheduleUpdateIncrement = true;
	Impl->NextDebugSubIncrement();
	Impl->HandlePendingQueries();
}

FString FITwinSchedulesImport::ComputeCacheName() const
{
	return Impl->ComputeCacheName();
}

void FITwinSchedule::Reserve(size_t Count)
{
	AnimationBindings.reserve(Count);
	Tasks.reserve(Count);
	if (EITwinSchedulesGeneration::Legacy == Generation)
		ElemIDGroups.reserve(Count);
	else if (EITwinSchedulesGeneration::NextGen == Generation)
		FedGUIDGroups.reserve(Count);
	AppearanceProfiles.reserve(Count);
	StaticTransfoAssignments.reserve(Count);
	PathTransfoAssignments.reserve(Count);
	Animation3DPaths.reserve(Count);
	KnownTasks.reserve(Count);
	KnownGroups.reserve(Count);
	KnownAppearanceProfiles.reserve(Count);
	KnownStaticTransfoAssignments.reserve(Count);
	KnownPathTransfoAssignments.reserve(Count);
	KnownAnimation3DPaths.reserve(Count);
}

bool FITwinSchedule::FullyDefined(ITwinHttp::FLock& Lock) const
{
	for (auto&& Binding : AnimationBindings)
		if (!Binding.FullyDefined(*this, Lock)) // deleted bindings are handled inside Binding.FullyDefined
			return false;
	return true;
}

bool FAnimationBinding::FullyDefined(FITwinSchedule const& Schedule, ITwinHttp::FLock&) const
{
	// Deleted bindings are considered fully defined, as they don't need any of the details to be applied (and in fact
	// should not be applied at all: they are skipped in FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline)
	if (bDeleted)
		return true;
	if (std::holds_alternative<FString>(AnimatedEntities) && ITwin::INVALID_IDX == GroupInVec)
	{
		ensure(false); return false;
	}
	if (ITwin::INVALID_IDX == TaskInVec)
		return false;
	// Note: since bDeleted is default-init to "true", this both checks that the Task details have been fetched
	// (if the task was "just" created) and that the Task is not deleted - same for other props below
	if (Schedule.Tasks[TaskInVec].bDeleted)
		return false;
	if (ITwin::INVALID_IDX == AppearanceProfileInVec)
		return false;
	if (Schedule.AppearanceProfiles[AppearanceProfileInVec].bDeleted)
		return false;
	if (!StaticTransfoAssignmentId.IsEmpty()
		&& (ITwin::INVALID_IDX == StaticTransfoAssignmentInVec
			|| Schedule.StaticTransfoAssignments[StaticTransfoAssignmentInVec].bDeleted))
	{
		return false;
	}
	if (PathTransfoAssignmentId.IsEmpty())
		return true;
	if (ITwin::INVALID_IDX == PathTransfoAssignmentInVec)
		return false;
	if (Schedule.PathTransfoAssignments[PathTransfoAssignmentInVec].bDeleted)
		return false;
	FPathTransformAssignment const& PathAssignment = Schedule.PathTransfoAssignments[PathTransfoAssignmentInVec];
	ensure(!PathAssignment.Animation3DPathId.IsEmpty());
	if (ITwin::INVALID_IDX == PathAssignment.Animation3DPathInVec)
		return false;
	auto&& Path = Schedule.Animation3DPaths[PathAssignment.Animation3DPathInVec];
	if (Path.bDeleted)
		return false;
	auto&& Keyframes = Path.Keyframes;
	for (auto&& Keyframe : Keyframes)
		if (!Keyframe.bDeleted)
			return true;
	return false;
}

FString FITwinSchedulesImport::ToString() const
{
	if (!HasFinishedPrefetching())
		return TEXT("");
	if (Impl->Schedule && !Impl->Schedule->AnimationBindings.empty())
	{
		return FString::Printf(TEXT("Statistics for %s\nQuerying statistics: %s"),
			*Impl->Schedule->ToString(), Impl->Queries ? (*Impl->Queries->Stats()) : TEXT("na."));
	}
	return TEXT("<Empty schedule>");
}

size_t FITwinSchedulesImport::NumTasks() const
{
	if (Impl->Schedule)
	{
		return Impl->Schedule->Tasks.size();
	}
	return 0;
}

void FITwinSchedulesImport::Uninitialize()
{
	// Wait for BG tasks, otherwise the Impl will be destroyed while they are still running
	// but we can't wait for the "BG tasks completions", because they run in the game thread, like us
	auto WaitingForBGTasksWithoutCompletion = [&Impl = Impl]()
		{
			FLock Lock(Impl->Mutex);
			if (Impl->WaitingForBackgroundTasks.empty())
			{
				Impl->BackgroundTaskCompletionsInGT.clear(); // so that they don't execute
				return false;
			}
			return true;
		};
	while (WaitingForBGTasksWithoutCompletion())
	{
		FPlatformProcess::Sleep(0.1);
	}
	if (Impl->Schedule && Impl->bNeedToSaveScheduleToDisk)
	{
		FLock Lock(Impl->Mutex);
		Impl->SaveScheduleToDiskIfNeeded(Lock);
	}
	if (Impl->Queries)
		Impl->Queries->UninitializeCache();
}

#if WITH_TESTS
bool FITwinSchedulesImport::SimulateScheduleForTest(FSimulatedScheduleOptions const& Options)
{
	Impl->bHasFailedListingSchedules = Options.WithListingError();
	Impl->bHasFetchingErrors = Options.WithQueryError() || Options.WithListingError();
	Impl->bHasFinishedPrefetching = Options.IsAvailable() || Impl->bHasFetchingErrors;
	// We need an animation binding otherwise the schedule is (rightfully) considered empty for animation purposes
	// (see test on AnimationBindings in UITwinSynchro4DSchedules::OnQueryLoopStatusChange)
	if (Options.IsAvailable() && ensure(Impl->Schedule))
	{
		Impl->Schedule->Tasks.emplace_back(
			FScheduleTask{ TEXT("TaskId"), EDeletedProp(false), TEXT("TaskName"), Options.TimeRange() });
		Impl->Schedule->KnownTasks[Impl->Schedule->Tasks.back().Id] = 0;
		Impl->Schedule->AnimationBindings.emplace_back(
			FAnimationBinding{ TEXT(""), EDeletedProp(false), Impl->Schedule->Tasks.back().Id, 0 });
		Impl->Schedule->KnownAnimationBindings[Impl->Schedule->AnimationBindings.back()] = 0;
	}
	if (Impl->bHasFetchingErrors)
	{
		Impl->FirstFetchingError = TEXT("SimulatedScheduleError");
		Impl->FirstFetchingErrorCode = EHttpResponseCodes::ServerError;
	}
	return true;
}
#endif // WITH_TESTS

FString FAnimationBinding::ToString(const TCHAR* SpecificElementID/* = nullptr*/) const
{
	FString Res;
	std::visit([&Res](auto&& Ident)
		{
			using T = std::decay_t<decltype(Ident)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
				Res = FString::Printf(TEXT("%#x"), Ident.value());
			else if constexpr (std::is_same_v<T, FGuid>)
				Res = Ident.ToString(EGuidFormats::DigitsWithHyphensLower);
			else if constexpr (std::is_same_v<T, FString>)
				Res = FString("in group ") + Ident;
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		AnimatedEntities);
	return FString::Printf(TEXT("binding for ent. %s%s, appear. %s%s%s"),
		//(Name.IsEmpty() ? (*(FString(" Id ") + TaskId)) : *Name),
		SpecificElementID ? SpecificElementID : TEXT(""), *Res, *AppearanceProfileId,
		StaticTransfoAssignmentId.IsEmpty() ? TEXT("") : *(TEXT(", static transf. ") + StaticTransfoAssignmentId),
		PathTransfoAssignmentId.IsEmpty() ? TEXT("") : *(TEXT(", path transf. ") + PathTransfoAssignmentId));
}

FString FITwinSchedule::ToString() const
{
	return FString::Printf(TEXT("%s Schedule %s (\"%s\"), with:\n" \
		"\t%llu bindings, %llu tasks, %llu groups, %llu appearance profiles,\n" \
		"\t%llu static transfo. assignments,\n" \
		"\t%llu 3D path transfo. assignments,\n" \
		"\t%llu unique resources (single Elements or groups of them) are bound to a task,\n" \
		"\t%llu unique Elements are bound to a task."),
		::ToString(Generation),
		*Id, *Name, AnimationBindings.size(), Tasks.size(), NumGroups(), AppearanceProfiles.size(),
		StaticTransfoAssignments.size(),
		PathTransfoAssignments.size(),
		Animation3DPaths.size()
		, [this]()
		{
			std::unordered_set<decltype(FAnimationBinding::AnimatedEntities)> Bound;
			for (auto&& Binding : AnimationBindings)
				Bound.insert(Binding.AnimatedEntities);//could explore the variant and recurse into groups...
			return Bound.size();
		}()
		, [this]()
		{
			// Only one sort will be actually used, ElemIDs or FedGUIDs
			std::unordered_set<ITwinElementID> BoundIDs;
			std::unordered_set<FGuid> BoundGUIDs;
			for (auto&& Binding : AnimationBindings)
			{
				std::visit([&](auto&& Ident)
					{
						using T = std::decay_t<decltype(Ident)>;
						if constexpr (std::is_same_v<T, ITwinElementID>)
						{
							BoundIDs.insert(Ident);
						}
						else if constexpr (std::is_same_v<T, FGuid>)
						{
							BoundGUIDs.insert(Ident);
						}
						else if constexpr (std::is_same_v<T, FString>)
						{
							if (EITwinSchedulesGeneration::Legacy == Generation
								// Bypass the Generation test to allow pseudo-NextGen test data generated from a Legacy
								// schedule's requests cache to work
								|| FedGUIDGroups.empty())
							{
								for (auto&& GroupElem : ElemIDGroups[Binding.GroupInVec])
									BoundIDs.insert(GroupElem);
							}
							else
							{
								for (auto&& GroupElem : FedGUIDGroups[Binding.GroupInVec])
									BoundGUIDs.insert(GroupElem);
							}
						}
						else static_assert(always_false_v<T>, "non-exhaustive visitor!");
					},
					Binding.AnimatedEntities);
			}
			return BoundIDs.size() + BoundGUIDs.size();
		}()
	);
}
