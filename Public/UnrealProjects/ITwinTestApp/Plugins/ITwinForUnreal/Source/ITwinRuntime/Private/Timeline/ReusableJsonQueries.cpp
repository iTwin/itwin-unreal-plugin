/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ReusableJsonQueries.h"
#include "ReusableJsonQueriesImpl.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <GenericPlatform/GenericPlatformTime.h>
#include <HAL/PlatformFileManager.h>
#include <HttpModule.h>
#include <Logging/LogMacros.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/Paths.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <algorithm>
#include <optional>

DECLARE_LOG_CATEGORY_EXTERN(ITwinS4DQueries, Log, All);
DEFINE_LOG_CATEGORY(ITwinS4DQueries);

using namespace ReusableJsonQueries;

class ReusableJsonQueries::FStackingToken
{
//public:
	//FLock& Lock; <== No, because Token is typically captured by the FProcessJsonObject lambdas!
public:
	FStackingToken() {}
	~FStackingToken() {}
	//template<uint16_t SimultaneousRequestsT> friend
	//	void FReusableJsonQueries<SimultaneousRequestsT>::HandlePendingQueries();
	//template<uint16_t SimultaneousRequestsT> friend
	//	void FReusableJsonQueries<SimultaneousRequestsT>::NewBatch(ReusableJsonQueries::FStackingFunc&&);

	FStackingToken(FStackingToken const&) = delete;
	FStackingToken(FStackingToken&&) = delete;
	FStackingToken& operator=(FStackingToken const&) = delete;
	FStackingToken& operator=(FStackingToken&&) = delete;
};

template<uint16_t SimultaneousRequestsT>
FReusableJsonQueries<SimultaneousRequestsT>::FImpl::FImpl(
	FString const& InBaseUrlNoSlash, FAllocateRequest const& AllocateRequest,
	FCheckRequest const& InCheckRequest, ReusableJsonQueries::FMutex& InMutex,
	TCHAR const* const InRecordToFolder, int const InRecorderSessionIndex,
	TCHAR const* const InSimulateFromFolder,
	std::optional<ReusableJsonQueries::EReplayMode> const InReplayMode,
	FScheduleQueryingDelegate const* InOnScheduleQueryingStatusChanged,
	std::function<FString()> const& InGetBearerToken)
: BaseUrlNoSlash(InBaseUrlNoSlash)
, CheckRequest(InCheckRequest)
, GetBearerToken(InGetBearerToken)
, Mutex(InMutex)
, RecordToFolder(InRecordToFolder)
, RecorderSessionIndex(InRecorderSessionIndex)
, SimulateFromFolder(InSimulateFromFolder)
, OnScheduleQueryingStatusChanged(InOnScheduleQueryingStatusChanged)
, IsThisValid(new bool(true))
{
	if (SimulateFromFolder && !FString(SimulateFromFolder).IsEmpty()
		&& InReplayMode && (*InReplayMode) != ReusableJsonQueries::EReplayMode::None)
	{
		IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
		FString PathBase = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		if (ensure(FileManager.DirectoryExists(*PathBase)
			&& FileManager.DirectoryExists(*(PathBase += SimulateFromFolder))))
		{
			FRecordDirIterator DirIter(SimulationMap, ReplayMap);
			if (FileManager.IterateDirectory(*PathBase, DirIter))
				ReplayMode = *InReplayMode;
		}
	}
	// Allocate those even when simulating! (see DoEmitRequest)
	for (auto& FromPool : RequestsPool)
	{
		FromPool.Request = AllocateRequest();
	}
	if (RecordToFolder && !FString(RecordToFolder).IsEmpty())
	{
		IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
		FString PathBase = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		FString SubDirs[] = { FString(RecordToFolder),
			FString::Printf(TEXT("%s_session%02d"), *FDateTime::Now().ToString(), InRecorderSessionIndex) };
		if (!ensure(FileManager.DirectoryExists(*PathBase)))
			return;
		for (auto&& Comp : SubDirs)
		{
			PathBase += Comp;
			if (!ensure(FileManager.CreateDirectory(*PathBase)))
				return;
			PathBase += TEXT('/');
		}
		RecorderPathBase = PathBase;
	}
} // ctor

template<uint16_t SimultaneousRequestsT>
FReusableJsonQueries<SimultaneousRequestsT>::FImpl::~FImpl()
{
	FLock Lock(Mutex);
	RequestsInBatch = 0;
	NextBatches.clear();
	RequestsInQueue.clear();
	for (auto&& FromPool : RequestsPool)
	{
		if (!FromPool.bIsAvailable
			&& !EHttpRequestStatus::IsFinished(FromPool.Request->GetStatus()))
		{
			FromPool.Request->CancelRequest();
		}
	}
	// CancelRequest is not blocking, and FromPool.Request's are SharedPtr hence still held by the
	// FHttpManager after FromPool's deletion, so 'Completed' delegates can still be called and we need to
	// signal to them that they should no longer access any reference to destroyed data:
	*IsThisValid = false;
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::FImpl::DoEmitRequest(FPoolRequest& FromPool,
																	   FRequestArgs const RequestArgs)
{
	check(!FromPool.bIsAvailable);//flag already toggled
	FromPool.Request->SetVerb(EVerb::GET == RequestArgs.Verb ? TEXT("GET") : TEXT("POST"));
	FString FullUrl;
	if (RequestArgs.Params.empty())
	{
		FullUrl = JoinToBaseUrl(RequestArgs.UrlSubpath, 0);
		FromPool.Request->SetURL(RecorderPathBase.IsEmpty() ? std::move(FullUrl) : FullUrl);
	}
	else
	{
		int32 ExtraSlack = 0;
		for (auto const& Parameter : RequestArgs.Params)
		{
			ExtraSlack += 2 + Parameter.first.Len() + Parameter.second.Len();
		}
		FullUrl = JoinToBaseUrl(RequestArgs.UrlSubpath, ExtraSlack);
		bool first = true;
		for (auto const& Parameter : RequestArgs.Params)
		{
			FullUrl += (first ? '?' : '&');
			FullUrl += Parameter.first;
			FullUrl += '=';
			FullUrl += Parameter.second;
			first = false;
		}
		FromPool.Request->SetURL(RecorderPathBase.IsEmpty() ? std::move(FullUrl) : FullUrl);
	}
	// Content-Length should be present http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	// See FCurlHttpRequest::SetupRequest: if we don't set it up here correctly, reusing requests
	// with Payload of different size will keep an incorrect length! The length required is that of the
	// converted utf8 buffer for the payload, so it's better to set an empty string here and let
	// FCurlHttpRequest::SetupRequest set the proper size.
	FromPool.Request->SetHeader("Content-Length", "");
	FromPool.Request->SetHeader("Authorization", "Bearer " + GetBearerToken());
	if (!RecorderPathBase.IsEmpty())
	{
		FLock Lock(Mutex);
		RecordQuery(ToJson(FromPool.Request, FullUrl, RequestArgs.PostDataString), Lock);
	}
	FromPool.Request->SetContentAsString(std::move(RequestArgs.PostDataString));
	std::function<TSharedPtr<FJsonObject>(FHttpResponsePtr)> GetJsonObj =
		[](FHttpResponsePtr Response) -> TSharedPtr<FJsonObject>
		{
			TSharedPtr<FJsonObject> ResponseJson;
			FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(Response->GetContentAsString()),
				ResponseJson);
			return ResponseJson;
		};
	if (ReusableJsonQueries::EReplayMode::OnDemandSimulation == ReplayMode)
	{
		// Overwrite because ternary operator for init wouldn't compile...
		GetJsonObj = GetJsonObjGetterForSimulation(FromPool, EVerb::GET == RequestArgs.Verb);
	}
	auto&& RequestCompletionCallback =
		[ IsThisValid=this->IsThisValid, // need to copy shared_ptr
		  JsonQueries=this, // just to make it explicit
		  pbRequestAvailableFlag=&(FromPool.bIsAvailable), // usable as long as (*IsThisValid)
		  ProcessJsonResponseFunc=std::move(RequestArgs.ProcessJsonResponseFunc),
		  &RequestsInBatch=this->RequestsInBatch,
		  QueryTimestamp = RecorderPathBase.IsEmpty() ? -1 : (RecorderTimestamp - 1),
		  GetJsonObj = std::move(GetJsonObj)]
		(FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			// IsThisValid access not thread-safe otherwise... If needed, the destructor and this callback
			// would have to also use another sync mechanism like a semaphore + a compare_and_swap loop
			check(IsInGameThread());
			if ((*IsThisValid) == false)
			{
				// FRequestPtr was cancelled and both FPoolRequest and owning FReusableJsonQueries deleted
				// so other captures are invalid
				return;
			}
			JsonQueries->LastCompletionTime = FPlatformTime::Seconds();
			check(*pbRequestAvailableFlag == false);
			Be::CleanUpGuard CleanUp([JsonQueries, pbRequestAvailableFlag, &RequestsInBatch,
									  Response, bConnectedSuccessfully, QueryTimestamp]
				{
					FLock Lock(JsonQueries->Mutex);
					*pbRequestAvailableFlag = true;
					++JsonQueries->AvailableRequestSlots; // next Tick will call HandlePendingRequests
					--RequestsInBatch;
					if (-1 != QueryTimestamp)
						JsonQueries->RecordReply(Response, bConnectedSuccessfully, QueryTimestamp, Lock);
				});
			if ((ReusableJsonQueries::EReplayMode::OnDemandSimulation == JsonQueries->ReplayMode)
				|| (JsonQueries->CheckRequest(CompletedRequest, Response, bConnectedSuccessfully, nullptr)
					//synonym to 20X response?
					&& ensure(EHttpRequestStatus::Succeeded == CompletedRequest->GetStatus())))
			{
				TSharedPtr<FJsonObject> ResponseJson = GetJsonObj(Response);
				if (ResponseJson)
					ProcessJsonResponseFunc(ResponseJson);
			}
		};
	switch (ReplayMode)
	{
	case ReusableJsonQueries::EReplayMode::OnDemandSimulation:
		// "Simulation" mode, we should have a matching entry in the simulation map (see GetJsonObj)
		RequestCompletionCallback({}, {}, true);
		break;
	case ReusableJsonQueries::EReplayMode::SequentialSession:
		// TODO_GCO: A little harder: can't persist the callbacks passed from SchedulesImport.cpp!
		break;
	case ReusableJsonQueries::EReplayMode::None:
		// "Single" delegate, no need to Unbind to reuse:
		FromPool.Request->OnProcessRequestComplete().BindLambda(std::move(RequestCompletionCallback));
		++TotalRequestsCount;
		if (FirstActiveTime == 0.) // assumed invalid
			FirstActiveTime = FPlatformTime::Seconds();
		FromPool.Request->ProcessRequest();
		break;
	}
}

template<uint16_t SimultaneousRequestsT>
FString FReusableJsonQueries<SimultaneousRequestsT>::FImpl::JoinToBaseUrl(
	FUrlSubpath const& UrlSubpath, int32 const ExtraSlack) const
{
	int32 TotalExtraSlack = ExtraSlack;
	for (auto const& Component : UrlSubpath)
	{
		TotalExtraSlack += Component.Len() + 1; // for the slash
	}
	FString FullUrl(BaseUrlNoSlash, TotalExtraSlack);
	for (auto const& Component : UrlSubpath)
	{
		FullUrl += '/';
		FullUrl += Component;
	}
	return FullUrl;
}

// Note: this method only sends one request, but the loop is in the other HandlePendingQueries below!
template<uint16_t SimultaneousRequestsT>
bool FReusableJsonQueries<SimultaneousRequestsT>::FImpl::HandlePendingQueries()
{
	FPoolRequest* Slot = nullptr;
	FRequestArgs RequestArgs;
	{	FLock Lock(Mutex);
		if (AvailableRequestSlots > 0 && !RequestsInQueue.empty())
		{
			if (!ensure(AvailableRequestSlots <= SimultaneousRequestsT))
			{
				AvailableRequestSlots = 0;
				for (auto&& FromPool : RequestsPool) if (FromPool.bIsAvailable) ++AvailableRequestSlots;
				if (!AvailableRequestSlots)
					return false;
			}
			for (auto&& FromPool : RequestsPool)
			{
				if (FromPool.bIsAvailable)
				{
					Slot = &FromPool;
					break;
				}
			}
			if (ensure(Slot))
			{
				Slot->bIsAvailable = false;
				--AvailableRequestSlots;
				RequestArgs = std::move(RequestsInQueue.front());
				RequestsInQueue.pop_front();
			}
		}
	}
	if (Slot)
	{
		DoEmitRequest(*Slot, std::move(RequestArgs));
		return true;
	}
	else
		return false;
}

template<uint16_t SimultaneousRequestsT>
FReusableJsonQueries<SimultaneousRequestsT>::FReusableJsonQueries(FString const& InBaseUrlNoSlash,
		FAllocateRequest const& AllocateRequest, FCheckRequest const& InCheckRequest,
		ReusableJsonQueries::FMutex& InMutex, TCHAR const* const InRecordToFolder,
		int const InRecorderSessionIndex, TCHAR const* const InSimulateFromFolder,
		std::optional<ReusableJsonQueries::EReplayMode> const ReplayMode,
		FScheduleQueryingDelegate const* OnScheduleQueryingStatusChanged,
		std::function<FString()> const& InGetBearerToken)
: Impl(MakePimpl<FReusableJsonQueries<SimultaneousRequestsT>::FImpl>(
	InBaseUrlNoSlash, AllocateRequest, InCheckRequest, InMutex, InRecordToFolder, InRecorderSessionIndex, 
	InSimulateFromFolder, ReplayMode, OnScheduleQueryingStatusChanged, InGetBearerToken))
{
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::ChangeRemoteUrl(FString const& NewRemoteUrl)
{
	FLock Lock(Impl->Mutex);
	Impl->BaseUrlNoSlash = NewRemoteUrl;
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::HandlePendingQueries()
{
	FStackingFunc NextBatch;
	{	FLock Lock(Impl->Mutex);
		if (Impl->RequestsInBatch)
		{
			if (!Impl->bIsRunning)
			{
				Impl->bIsRunning = true;
				if (Impl->OnScheduleQueryingStatusChanged)
					Impl->OnScheduleQueryingStatusChanged->Broadcast(Impl->bIsRunning);
			}
		}
		else
		{
			if (Impl->NextBatches.empty())
			{
				if (Impl->bIsRunning)
				{
					Impl->bIsRunning = false;
					if (Impl->OnScheduleQueryingStatusChanged)
						Impl->OnScheduleQueryingStatusChanged->Broadcast(Impl->bIsRunning);
				}
			}
			else
			{
				NextBatch = std::move(Impl->NextBatches.front().Exec);
				Impl->NextBatches.pop_front();
			}
		}
	}
	if (NextBatch)
	{
		FStackingToken Token;
		NextBatch(Token);
		// re-enter in case the batch resulted in zero request: can easily happen when Elements filtering
		// and the AnimBindingsFullyKnownForElem system, so don't wait for next tick
		HandlePendingQueries();
	}
	else
	{
		while (Impl->HandlePendingQueries()) {}
	}
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::StackRequest(ReusableJsonQueries::FStackingToken const&,
	ReusableJsonQueries::FLock* Lock, EVerb const Verb, FUrlSubpath&& UrlSubpath, FUrlArgList&& Params,
	FProcessJsonObject&& ProcessCompletedFunc, FString&& PostDataString /*= {}*/)
{
	std::optional<FLock> optLock;
	if (!Lock)
		optLock.emplace(Impl->Mutex);
	++Impl->RequestsInBatch;
	Impl->RequestsInQueue.emplace_back(FRequestArgs{Verb, std::move(UrlSubpath),
		std::move(Params), std::move(ProcessCompletedFunc), std::move(PostDataString)});
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::NewBatch(FStackingFunc&& StackingFunc,
														   bool const bPseudoBatch/*= false*/)
{
	FLock Lock(Impl->Mutex);
	if (!Impl->RequestsInBatch && Impl->NextBatches.empty())
	{
		// Stack immediately, to avoid delays (in case of empty batches, in particular)
		FStackingToken Token;
		StackingFunc(Token);
	}
	else Impl->NextBatches.emplace_back(FNewBatch{ std::move(StackingFunc), bPseudoBatch });
}

template<uint16_t SimultaneousRequestsT>
std::pair<int, int> FReusableJsonQueries<SimultaneousRequestsT>::QueueSize() const
{
	FLock Lock(Impl->Mutex);
	return {
		(Impl->RequestsInBatch ? 1 : 0) + (int)std::count_if(
			Impl->NextBatches.begin(), Impl->NextBatches.end(),
			[](auto&& PendingBatch) { return !PendingBatch.bPseudoBatch; }),
		Impl->RequestsInBatch
	};
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::SwapQueues(ReusableJsonQueries::FLock&,
	ReusableJsonQueries::FStackedBatches& NextBatches, ReusableJsonQueries::FStackedRequests& RequestsInQ,
	ReusableJsonQueries::FStackingFunc&& PriorityRequest)
{
	NextBatches.swap(Impl->NextBatches);
	RequestsInQ.swap(Impl->RequestsInQueue);
	if (PriorityRequest)
	{
		if (!Impl->RequestsInQueue.empty())
		{
			FStackedRequests ReqsInQ;
			ReqsInQ.swap(Impl->RequestsInQueue);
			Impl->NextBatches.push_front(
				FNewBatch{
					[this, Reqs = std::move(ReqsInQ)](FStackingToken const&) mutable
					{
						ensure(Impl->RequestsInQueue.empty());
						Impl->RequestsInQueue.swap(Reqs);
					},
					false });
		}
		Impl->NextBatches.push_front(FNewBatch{ PriorityRequest, false });
	}
}

template<uint16_t SimultaneousRequestsT>
FString FReusableJsonQueries<SimultaneousRequestsT>::Stats() const
{
	return FString::Printf(TEXT("Processed %llu requests in %.1fs."), Impl->TotalRequestsCount,
		Impl->LastCompletionTime - Impl->FirstActiveTime);
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::StatsResetActiveTime()
{
	Impl->FirstActiveTime = 0.;
}

// Need to be included in this CPP because of the explicit instantiation below
#include "ReusableJsonQueriesRecording.inl"

/// Must match SimultaneousRequestsAllowed but since it's declared in SchedulesImport.h, I didn't want to
/// introduce a dependency for that (you'll get link errors in case of inconsistency anyway)
template class FReusableJsonQueries<8/*SimultaneousRequestsAllowed*/>;
#define REUSABLEJSONQUERIES_TEMPLATE_INSTANTIATED // see SchedulesImport.cpp ...
