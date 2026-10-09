/*--------------------------------------------------------------------------------------+
|
|     $Source: PaginatedIModelRowsQuerying.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "PaginatedIModelRowsQuerying.h"

#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinSceneMapping.h>
#include <Network/JsonQueriesCache.h>

#include <HAL/FileManager.h>
#include <HAL/PlatformFileManager.h>
#include <HAL/PlatformProcess.h>
#include <Internationalization/Regex.h>
#include <Tasks/Task.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Tools/Tools.h>
#	include <SDK/Core/ITwinAPI/ITwinTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace {

FString GetMetadataQueryString(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		return FString(
			TEXT("SELECT e.ECInstanceId, b.BBoxLow, b.BBoxHigh, e.Parent.Id, e.FederationGuid, a.Identifier"))
			+ TEXT(" FROM bis.Element e")
			+ TEXT(" LEFT JOIN bis.ExternalSourceAspect a ON a.Element.Id = e.ECInstanceId")
			+ TEXT(" LEFT JOIN bis.GeometricElement3d b ON b.ECInstanceId = e.ECInstanceId");
	case EElementsMetadata::CombinedNoBBoxes:
		return FString(
			TEXT("SELECT e.ECInstanceId, e.Parent.Id, e.FederationGuid, a.Identifier"))
			+ TEXT(" FROM bis.Element e")
			+ TEXT(" LEFT JOIN bis.ExternalSourceAspect a ON a.Element.Id = e.ECInstanceId");
	case EElementsMetadata::StandaloneBBoxes:
		return FString(TEXT("SELECT ECInstanceId, BBoxLow, BBoxHigh FROM bis.GeometricElement3d"));
	case EElementsMetadata::ConstructionDetailing:
		return FString(TEXT("SELECT DISTINCT TargetECInstanceId"))
			+ TEXT(" FROM Construction.ConstructionDetailingElementSplitsGeometricElement3d");
	default:
		ensure(false);
		return {};
	}
}

FString GetMetadataQueryCountString(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
	case EElementsMetadata::CombinedNoBBoxes:
		return TEXT("SELECT COUNT(*) FROM bis.Element");
	case EElementsMetadata::StandaloneBBoxes:
		return TEXT("SELECT COUNT(*) FROM bis.GeometricElement3d");
	case EElementsMetadata::ConstructionDetailing:
		//QueryCountString = TEXT("SELECT DISTINCT COUNT(*) FROM ..."); <= not possible (and/or not efficient)
		return {};
	default:
		ensure(false);
		return {};
	}
}

std::string GetMetadataQueryDescription(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		return "Elements metadata";
	case EElementsMetadata::CombinedNoBBoxes:
		return "Elements metadata (no BBoxes)";
	case EElementsMetadata::StandaloneBBoxes:
		return "Elements bounding boxes";
	case EElementsMetadata::ConstructionDetailing:
		return "Construction detailing";
	default:
		ensure(false);
		return "<INVAL>";
	}
}

FString GetCacheFolder(EElementsMetadata const KindOfMetadata, AITwinIModel const& IModel)
{
	QueriesCache::ESubtype Type;
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		Type = QueriesCache::ESubtype::ElementsMetadataCombined;
		break;
	case EElementsMetadata::CombinedNoBBoxes:
		Type = QueriesCache::ESubtype::ElementsMetadataNoBBoxes;
		break;
	case EElementsMetadata::StandaloneBBoxes:
		Type = QueriesCache::ESubtype::ElementsMetadataBBoxes;
		break;
	case EElementsMetadata::ConstructionDetailing:
		Type = QueriesCache::ESubtype::ConstructionDetailing;
		break;
	default:
		ensure(false);
		return {};
	}
	return QueriesCache::GetCacheFolder(Type, IModel.ServerConnection->Environment, IModel.ITwinId, IModel.IModelId,
										IModel.ResolvedChangesetId);
}

}

FPaginatedIModelRowsQueries::FPaginatedIModelRowsQueries(AITwinIModel& InIModel,
	EElementsMetadata const InKindOfMetadata, std::shared_ptr<ITwinHttp::FMutex> InMutex,
	FOnLoadProgressUpdated InOnLoadProgressUpdated, int InQueryRowCount, int InMaxNumPageInProgress)
	: IModel(InIModel)
	, KindOfMetadata(InKindOfMetadata)
	, ECSQLQueryString(GetMetadataQueryString(InKindOfMetadata))
	, ECSQLQueryCount(GetMetadataQueryCountString(InKindOfMetadata))
	, Description(GetMetadataQueryDescription(InKindOfMetadata) + " queries for " + TCHAR_TO_UTF8(*InIModel.IModelId)
		+ " (\""  + TCHAR_TO_UTF8(*InIModel.GetActorNameOrLabel()) + "\")")
	, Cache(InIModel, *InMutex)
	, MutexPtr(InMutex)
	, Mutex(*InMutex)
	, OnLoadProgressUpdated(std::move(InOnLoadProgressUpdated))
	, QueryRowCount(InQueryRowCount)
	, MaxNumPageInProgress(InMaxNumPageInProgress)
{
	bQueryTableCount = !ECSQLQueryCount.IsEmpty();
}

void FPaginatedIModelRowsQueries::Cancel()
{
	ITwinHttp::FLock Lock(Mutex);
	// If we switch 'NotStarted' to 'Cancelling', we get problems when exiting the IModelHeadless test (where
	// auto-load of S4D is disabled for good reasons): when the IModel is destroyed by the GC, we try to
	// access the FTSTicker too late in OnIModelUninit. It did not make sense anyway to not skip directly to
	// 'Cancelled' as there is nothing to wait for in that case.
	if (EState::Running == State /*|| EState::NotStarted == State*/ || EState::NeedRestart == State)
		State = EState::Cancelling;
	else if (EState::NotStarted == State)
		State = EState::Cancelled;
}

double FPaginatedIModelRowsQueries::PercentComplete() const
{
	ITwinHttp::FLock Lock(Mutex);
	switch (State)
	{
	case EState::NotStarted:
	case EState::NeedRestart:
		return 0.;
	case EState::Finished:
		return 100.;
	case EState::StoppedOnError:
	case EState::Running:
	case EState::Cancelling:
	case EState::Cancelled:
		break;
	}
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
	case EElementsMetadata::CombinedNoBBoxes:
	case EElementsMetadata::StandaloneBBoxes:
		if (TotalRowsExpected > 0)
			// Do not return 100% before all queries are actually finished!
			return std::min(95., (100. * QueryRowStart) / TotalRowsExpected);
		else
			return 0.;
	case EElementsMetadata::ConstructionDetailing:
	default:
		// ConstructionDetailing querying progress is not handled because of the SELECT DISTINCT query
		return 0.;
	}
}

size_t FPaginatedIModelRowsQueries::GetRequestsFromRemote() const
{
	ITwinHttp::FLock Lock(Mutex);
	return RequestsFromRemote;
}

size_t FPaginatedIModelRowsQueries::GetRequestsFromCache() const
{
	ITwinHttp::FLock Lock(Mutex);
	return RequestsFromCache;
}

EHttpResponseCodes::Type FPaginatedIModelRowsQueries::GetFirstErrorCode() const
{
	ITwinHttp::FLock Lock(Mutex);
	return FirstErrorCode;
}

FString FPaginatedIModelRowsQueries::GetFirstErrorString() const
{
	ITwinHttp::FLock Lock(Mutex);
	return FirstErrorString;
}

FPaginatedIModelRowsQueries::EState FPaginatedIModelRowsQueries::GetState() const
{
	ITwinHttp::FLock Lock(Mutex);
	return State;
}

void FPaginatedIModelRowsQueries::DoRestart()
{
	ITwinHttp::FLock Lock(Mutex);
	QueryRowStart = TotalRowsParsed = 0;
	RequestsFromCache = RequestsFromRemote = 0;
	TotalRowsExpected = -1;
	OnLoadProgressUpdated();
	FString const CacheFolder = GetCacheFolder(KindOfMetadata, IModel);
	if (LastCacheFolderUsed != CacheFolder && ensure(!CacheFolder.IsEmpty()))
	{
		if (!Cache.Initialize(CacheFolder, IModel.ServerConnection->Environment, UTF8_TO_TCHAR(Description.c_str())))
		{
			BE_LOGW("ITwinQuery", "Something went wrong while setting up the local http cache for Elements metadata queries - cache will NOT be used!");
		}
		LastCacheFolderUsed = CacheFolder;
	}
	State = EState::Running;
	bQueryTableCount = !ECSQLQueryCount.IsEmpty();
	NumPageInProgress = 0;
	lastPageReached = false;
	for (int i = 0; i < MaxNumPageInProgress; ++i)
	{
		std::weak_ptr<FPaginatedIModelRowsQueries> wptr = shared_from_this();
		UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[wptr]()
			{
				auto pThis = wptr.lock();
				if (!pThis)
					return;
				pThis->QueryNextPage();
			},
			UE::Tasks::ETaskPriority::BackgroundLow);
	}
}

void FPaginatedIModelRowsQueries::Restart()
{
	if (EState::Running != State && EState::NeedRestart != State)
	{
		UninitializeCache(); // reinit, we may have a new changesetId for example
		BE_LOGI("ITwinAPI", Description << ": queries (re)starting...");
		DoRestart();
	}
	else
	{
		State = EState::NeedRestart;
	}
}

bool FPaginatedIModelRowsQueries::QueryNextPage()
{
	std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo;
	int currentQueryRowStart = -1; // will stay "-1" for 'bQueryTableCount'
	{
		ITwinHttp::FLock Lock(Mutex);
		if (EState::Cancelling == State || EState::Cancelled == State || EState::StoppedOnError == State)
		{
			BE_LOGI("ITwinAPI", Description << ": queries cancelled.");
			return false;
		}
		NumPageInProgress++;
		RequestInfo = std::make_shared<AdvViz::SDK::ITwinAPIRequestInfo>(
			IModel.GetMutableWebServices()->InfosToQueryIModel(
				IModel.ITwinId, IModel.IModelId, IModel.ResolvedChangesetId,
				bQueryTableCount ? ECSQLQueryCount : ECSQLQueryString, QueryRowStart, QueryRowCount));
		if (!bQueryTableCount)
		{
			currentQueryRowStart = QueryRowStart;
			QueryRowStart += QueryRowCount;
		}
		bQueryTableCount = false;
	}

	auto Hit = Cache.LookUp(*RequestInfo);
	if (Hit)
	{
		BE_LOGD("ITwinAPI", Description << ": start query page in cache begin rowstart:" << currentQueryRowStart << " count:" << QueryRowCount << " RequestInfoId:" << RequestInfo.get());
		OnQueryCompleted(true, Cache.Read(std::move(*Hit)), RequestInfo, currentQueryRowStart == -1, false);
	}
	else
	{
		BE_LOGD("ITwinAPI", Description << ": start query with http begin: rowstart:" << currentQueryRowStart << " count:" << QueryRowCount << " RequestInfoId:" << RequestInfo.get());
		std::weak_ptr< FPaginatedIModelRowsQueries> wptr(shared_from_this());
		AdvViz::SDK::FilterErrorFunc funcFilterError;
		AdvViz::SDK::FilterErrorFunc funcStoreFirstErrorCode =
			[this](long statusCode, std::string const& requestError, bool& bAllowRetry, bool& /*bLogError*/)
			{
				if (EHttpResponseCodes::IsOk(statusCode))
				{
					// Avoid retrying requests that explicitly fail with an error about data base schema
					// mismatch (e.g. missing ECClass) - this is not a transient error and will not be fixed
					// by retrying.
					FRegexPattern Pattern(TEXT("ECClass '([^']*)' does not exist"));
					FRegexMatcher PatternMatcher(Pattern, FString(requestError.c_str()));
					if (PatternMatcher.FindNext())
					{
						bAllowRetry = false;
					}
				}
				else
				{
					ITwinHttp::FLock Lock(Mutex);
					if (EHttpResponseCodes::IsOk(FirstErrorCode))
					{
						FirstErrorCode = EHttpResponseCodes::Type(statusCode);
						FirstErrorString = UTF8_TO_TCHAR(requestError.c_str());
					}
				}
			};
		std::shared_ptr<bool> bIgnoreMissingConstructionDetailingECClass;
		if (EElementsMetadata::ConstructionDetailing == KindOfMetadata)
		{
			bIgnoreMissingConstructionDetailingECClass = std::make_shared<bool>(false);
			funcFilterError = [this, funcStoreFirstErrorCode, bIgnoreMissingConstructionDetailingECClass]
				(long statusCode, std::string const& requestError, bool& bAllowRetry, bool& bLogError)
				{
					if (requestError.find("ECClass 'Construction.ConstructionDetailingElementSplitsGeometricElement3d' does not exist")
						!= std::string::npos)
					{
						bAllowRetry = false;
						bLogError = false;
						// Flag the error to be ignored in OnQueryCompleted so that finalization can happen
						// and 4D actually become available!
						{
							ITwinHttp::FLock Lock(Mutex);
							*bIgnoreMissingConstructionDetailingECClass = true;
						}
					}
					else
					{
						funcStoreFirstErrorCode(statusCode, requestError, bAllowRetry, bLogError);
					}
				};
		}
		else
		{
			funcFilterError = std::move(funcStoreFirstErrorCode);
		}
		IModel.GetMutableWebServices()->QueryIModelRows({}, {}, {}, {}, 0, 0, // everything's in RequestInfo
			{},
			[wptr, RequestInfo, currentQueryRowStart, bIgnoreMissingConstructionDetailingECClass]
			(const AdvViz::expected<AdvViz::SDK::Http::Response, std::string>& exp)
			{
				std::shared_ptr<FPaginatedIModelRowsQueries> pThis = wptr.lock();
				if (!pThis)
					return;

				BE_LOGD("ITwinAPI", pThis->Description << ": http query end rowstart:" << currentQueryRowStart << " count:" << pThis->QueryRowCount << " RequestInfoId:" << RequestInfo.get());

				bool bSuccess = true;
				FString QueryResult;
				if (!exp)
				{
					BE_LOGE("ITwinAPI", "iModel request Failed: " << exp.error());
					bSuccess = false;
				}
				else
				{
					QueryResult = UTF8_TO_TCHAR(exp->second.c_str());
				}
				if (!pThis->OnQueryCompleted(bSuccess, QueryResult, RequestInfo, currentQueryRowStart == -1,
					bIgnoreMissingConstructionDetailingECClass && (*bIgnoreMissingConstructionDetailingECClass)))
				{
					BE_LOGE("ITwinAPI", "iModel request not recognized");
				}
			},
			AdvViz::SDK::Http::EAsyncCallbackExecutionMode::WorkerThread,
			&(*RequestInfo), std::move(funcFilterError));
	}
	return true;
}

bool FPaginatedIModelRowsQueries::OnQueryCompleted(bool const bSuccess,
	std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult,
	std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo,
	bool const bTableCountReply, bool const bIgnoreError)
{
	// cleanup() must be called before fctFinish(), otherwise it will be done automatically
	// when going out of scope
	Be::CleanUpGuard PageDecrementer([this]()
		{
			ITwinHttp::FLock Lock(Mutex);
			NumPageInProgress--;
		});

	bool const bFromCache = (QueryResult.index() == 1);
	TSceneMappingPtr sceneMapping = GetInternals(IModel).SceneMapping;
	std::weak_ptr<FPaginatedIModelRowsQueries> wptr = shared_from_this();

	auto fctFinish = [wptr, sceneMapping, bFromCache]() {
		auto pThis = wptr.lock();
		if (!pThis)
			return;

		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			if (pThis->State == EState::Finished || pThis->State == EState::Cancelled)
				return;

			BE_LOGD("ITwinAPI", pThis->Description << ": check if finished: NumPageInProgress="
								<< pThis->NumPageInProgress << ", lastPageReached=" << pThis->lastPageReached);

			if (pThis->NumPageInProgress != 0 || (!pThis->lastPageReached && pThis->State != EState::Cancelling))
				return;

			BE_LOGI("ITwinAPI", pThis->Description << ": page query finished, total retrieved from "
								// likely all retrieved from same source...
								<< (bFromCache ? "cache: " : "remote: ") << pThis->TotalRowsParsed);
		}

		// This call will release hold of the cache folder, which will "often" allow reuse by cloned
		// actor when entering PIE (unless it was not yet finished downloading, of course)
		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			pThis->UninitializeCache();
			BE_LOGD("ITwinAPI", pThis->Description << ": final preparation started");
		}

		{
			auto SceneMappingLock = sceneMapping->GetAutoLock();
			SceneMappingLock->FinishedParsingIModelMetadata(
				EElementsMetadata::Combined == pThis->KindOfMetadata
					|| EElementsMetadata::CombinedNoBBoxes == pThis->KindOfMetadata,
				EElementsMetadata::Combined == pThis->KindOfMetadata
					|| EElementsMetadata::StandaloneBBoxes == pThis->KindOfMetadata);
		}
		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			ensure(EState::NotStarted != pThis->State);
			if (EState::Running == pThis->State)
				pThis->State = EState::Finished;
			else if (EState::Cancelling == pThis->State)
			{
				// will break OnIModelUninit()'s waiting loop (once the lock is released), after which almost
				// everything is unsafe, from IModel to pThis itself! (sceneMapping is OK as a shared_ptr)
				pThis->State = EState::Cancelled;
			}
			BE_LOGD("ITwinAPI", pThis->Description << ": final preparation finished");
		}
	};

	{
		ITwinHttp::FLock Lock(Mutex);
		if (EState::Cancelling == State || EState::StoppedOnError == State || EState::Cancelled == State)
		{
			BE_LOGI("ITwinAPI", Description << ": queries cancelled"
								<< ((EState::StoppedOnError == State) ? " (on error)." : "."));
			PageDecrementer.cleanup();
			fctFinish();
			return true;
		}
		if (EState::NeedRestart == State)
		{
			BE_LOGI("ITwinAPI", Description << ": queries interrupted, will restart...");
			DoRestart();
			return true;
		}
		if (!bSuccess && !bIgnoreError)
		{
			State = EState::StoppedOnError;
			PageDecrementer.cleanup();
			fctFinish();
			if (OnLoadProgressUpdated)
				OnLoadProgressUpdated();
			// ResetSchedules will cancel the existing 4D querying process, which will not restart because
			// of the error state just flagged. It will broadcast OnScheduleQueryingStatusChanged(false)
			// instead and thus notify Carrot's MainPanel and the iTS part if present...
			if (IsValid(IModel.Synchro4DSchedules))
				IModel.Synchro4DSchedules->ResetSchedules();
			return true;
		}
	}
	int RowsParsed = 0;
	bool bHasReceivedTableCount = false;
	TSharedPtr<FJsonObject> JsonObj;
	if (bFromCache)
	{
		JsonObj = std::get<1>(QueryResult);
	}
	else
	{
		BE_LOGD("ITwinAPI", Description << ": Deserialize json RequestInfoId:" << RequestInfo.get() << " started");
		Cache.Write(*RequestInfo, std::get<0>(QueryResult), ITwinHttp::ConnectionSuccess(true));
		auto Reader = TJsonReaderFactory<TCHAR>::Create(std::get<0>(QueryResult));
		if (!FJsonSerializer::Deserialize(Reader, JsonObj))
			JsonObj.Reset();
		BE_LOGD("ITwinAPI", Description << ": Deserialize json RequestInfoId:" << RequestInfo.get() << " finished");
	}

	TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
	if (JsonObj.IsValid() && JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
	{
		if (bTableCountReply)
		{
			if (ensure(JsonRows->Num() == 1))
			{
				auto const& Entries = (*JsonRows)[0]->AsArray();
				if (ensure(!Entries.IsEmpty() && Entries[0]->TryGetNumber(TotalRowsExpected)))
				{
					bHasReceivedTableCount = true;
					if (TotalRowsExpected > 0)
					{
						auto SceneMappingLock = sceneMapping->GetAutoLock();
						SceneMappingLock->ReserveIModelMetadata(TotalRowsExpected);
					}
				}
			}
		}
		else
		{
			std::stringstream log;
			log << Description << ": parsing for RequestInfoId:" << RequestInfo.get();

			PageDecrementer.release(); // why can't I move it :-(
			UE::Tasks::Launch(UE_SOURCE_LOCATION,
				[JsonRows, wptr, sceneMapping, JsonObj, strlog = log.str(), fctFinish, bFromCache
				// Moving does not work, I got compilation error :/
				/*MovedPageDecrementer = std::move(PageDecrementer)*/]() mutable
				{
					auto pThis = wptr.lock();
					if (!pThis)
						return;

					BE_LOGD("ITwinAPI", strlog << " started");
					int RowsParsed = 0;
					switch (pThis->KindOfMetadata)
					{
					case EElementsMetadata::Combined:
					case EElementsMetadata::CombinedNoBBoxes:
						RowsParsed = FITwinSceneMapping::ParseIModelMetadata(
							sceneMapping, *JsonRows, EElementsMetadata::Combined == pThis->KindOfMetadata);
						break;
					case EElementsMetadata::StandaloneBBoxes:
						RowsParsed = FITwinSceneMapping::ParseStandaloneBoundingBoxes(sceneMapping, *JsonRows);
						break;
					case EElementsMetadata::ConstructionDetailing:
						RowsParsed = FITwinSceneMapping::ParseConstructionDetailingParentIDs(sceneMapping, *JsonRows);
						break;
					default: ensure(false); break;
					}
					BE_LOGD("ITwinAPI", strlog << " finished");

					bool bLocalLastPageReached;
					{
						ITwinHttp::FLock Lock(pThis->Mutex);
						pThis->NumPageInProgress--;
						pThis->TotalRowsParsed += RowsParsed;
						if (RowsParsed > 0)
							if (bFromCache)
								pThis->RequestsFromCache++;
							else
								pThis->RequestsFromRemote++;
						bLocalLastPageReached = pThis->lastPageReached;
						// release lock before QueryNextPage: when in cache, even though processing the metadata
						// is moved to a task, reading/parsing the json will chain and block substantially
					}
					if (!bLocalLastPageReached)
						pThis->QueryNextPage();

					fctFinish();
				},
				UE::Tasks::ETaskPriority::Normal);
		}
	}

	if ((JsonRows && JsonRows->Num() > 0) || bHasReceivedTableCount)
	{
		if (bHasReceivedTableCount)
		{
			BE_LOGI("ITwinAPI", Description << ": table count retrieved from " << (bFromCache ? "cache: " : "remote: ")
								<< TotalRowsExpected);
			QueryNextPage();
		}
		else
		{
			BE_LOGD("ITwinAPI", Description << ": " << TotalRowsParsed << " rows retrieved from "
				<< (bFromCache ? "cache" : "remote") << ", asking for more...");
			if (TotalRowsExpected != -1)
			{
				if (OnLoadProgressUpdated)
					OnLoadProgressUpdated();
			}
			// Not calling QueryNextPage() here because it will be done when the curent reply is finished parsing,
			// in the worker thread (see above). This is to avoid having too many queries in flight OR parsing at the
			// same time because, when replies are in cache, a lot more than MaxNumPageInProgress would end up
			// being processed at the same time, leading to a severe memory spike for large iModels.
		}
	}
	else
	{
		// Current page queried returned no result => signal completion.
		lastPageReached = true;
	}
	// Must decrement before fctFinish, in case we are the last one.
	if (!PageDecrementer.isClean())
		PageDecrementer.cleanup();
	fctFinish();
	return true;
}

void FPaginatedIModelRowsQueries::UninitializeCache()
{
	Cache.Uninitialize();
	LastCacheFolderUsed = {};// otw Cache is never re-init!! see azdev#1621189, Investigation Notes
}

bool FPaginatedIModelRowsQueries::ClearCacheOnDisk()
{
	if (EState::Finished != GetState())
		return false;
	FString const CacheFolder = GetCacheFolder(KindOfMetadata, IModel);
	if (ensure(!CacheFolder.IsEmpty()))
	{
		return IFileManager::Get().DeleteDirectory(*CacheFolder, /*requireExists*/false, /*recurse*/true);
	}
	return false;
}

void FPaginatedIModelRowsQueries::OnIModelUninit()
{
	Cancel();// State becomes EState::Cancelling, unless it was already stopped somehow (Finished or StoppedOnError)
	int WaitSomeMore = 60;
	while (WaitSomeMore--)
	{
		{	ITwinHttp::FLock Lock(Mutex);
			if (EState::Cancelling != State) // ie Cancelled, StoppedOnError, NotStarted or Finished
				break;
		}
		// Don't! Can crash like in #2111180 - no longer needed as I had to remove the delayed call to fix it.
		//FTSTicker::GetCoreTicker().Tick(1.0);
		FPlatformProcess::Sleep(.5f);
	}
	UninitializeCache();
}
