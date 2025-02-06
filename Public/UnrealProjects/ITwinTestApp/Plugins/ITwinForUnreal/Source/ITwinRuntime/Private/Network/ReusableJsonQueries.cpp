/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ReusableJsonQueries.h"
#include "ReusableJsonQueriesImpl.h"
#include "ITwinServerEnvironment.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <GenericPlatform/GenericPlatformTime.h>
#include <HAL/FileManager.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/Guid.h>
#include <Misc/Paths.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <algorithm>
#include <optional>

DEFINE_LOG_CATEGORY(ITwinQuery);

using namespace ReusableJsonQueries;

class ReusableJsonQueries::FStackingToken
{
//public:
	//ITwinHttp::FLock& Lock; <== No, because Token is typically captured by the FProcessJsonObject lambdas!
	FStackingToken() {}
public:
	~FStackingToken() {}
	friend void FReusableJsonQueries::HandlePendingQueries();
	friend void FReusableJsonQueries::NewBatch(ReusableJsonQueries::FStackingFunc&&, bool const);

	FStackingToken(FStackingToken const&) = delete;
	FStackingToken(FStackingToken&&) = delete;
	FStackingToken& operator=(FStackingToken const&) = delete;
	FStackingToken& operator=(FStackingToken&&) = delete;
};

FReusableJsonQueries::FImpl::FImpl(UObject const& Owner,
	FString const& InBaseUrlNoSlash, FAllocateRequest const& AllocateRequest,
	uint8_t const SimultaneousRequestsAllowed, FCheckRequest const& InCheckRequest, ITwinHttp::FMutex& InMutex,
	TCHAR const* const InRecordToFolder, int const InRecorderSessionIndex,
	TCHAR const* const InSimulateFromFolder,
	FScheduleQueryingDelegate const* InOnScheduleQueryingStatusChanged,
	std::function<FString()> const& InGetBearerToken)
: BaseUrlNoSlash(InBaseUrlNoSlash)
, CheckRequest(InCheckRequest)
, GetBearerToken(InGetBearerToken)
, Mutex(InMutex)
, Cache(Owner)
, OnScheduleQueryingStatusChanged(InOnScheduleQueryingStatusChanged)
, IsThisValid(new bool(true))
{
	if (InSimulateFromFolder && !FString(InSimulateFromFolder).IsEmpty()
		&& Cache.LoadSessionSimulation(InSimulateFromFolder))
	{
		ReplayMode = ReusableJsonQueries::EReplayMode::OnDemandSimulation;
	}
	// Allocate those even when simulating! (see DoEmitRequest)
	AvailableRequestSlots = SimultaneousRequestsAllowed;
	RequestsPool.resize(SimultaneousRequestsAllowed);
	for (auto& FromPool : RequestsPool)
	{
		FromPool.Request = AllocateRequest();
	}
	if (InRecordToFolder && !FString(InRecordToFolder).IsEmpty())
	{
		FString PathBase = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		if (ensure(IFileManager::Get().DirectoryExists(*PathBase)))
		{
			PathBase = FPaths::Combine(PathBase,
				ITwinServerEnvironment::ToName(EITwinEnvironment::Dev).ToString(),
				InRecordToFolder,
				FString::Printf(TEXT("%s_session%02d"), *FDateTime::Now().ToString(), InRecorderSessionIndex));
			if (IFileManager::Get().DirectoryExists(*PathBase)
				|| ensure(IFileManager::Get().MakeDirectory(*PathBase, /*recurse*/true)))
			{
				if (Cache.Initialize(PathBase, EITwinEnvironment::Dev, TEXT("Dev-RecordToFolder"), true))
					bIsRecordingForSimulation = true;
			}
		}
	}
} // ctor

FReusableJsonQueries::FImpl::~FImpl()
{
	ITwinHttp::FLock Lock(Mutex);
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

UE5Coro::TCoroutine<void> FReusableJsonQueries::FImpl::FRequestHandler::Run(
	std::shared_ptr<FReusableJsonQueries::FImpl::FRequestHandler> This,
	FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	// IsThisValid access not thread-safe otherwise... If needed, the destructor and this callback
	// would have to also use another sync mechanism like a semaphore + a compare_and_swap loop
	check(IsInGameThread());
	if ((*IsJsonQueriesValid) == false)
	{
		// FHttpRequestPtr was cancelled and both FPoolRequest and owning FReusableJsonQueries deleted
		// so other captures are invalid
		co_return;
	}
	check(FromPool.bIsAvailable == false);
	bool bRetry = !FromPool.bTryFromCache && (RequestArgs.RetriesLeft > 0);
	// Could probably be in the destructor now (but we'd have to store Response & bConnectedSuccessfully)
	Be::CleanUpGuard CleanUp([this, Response, bConnectedSuccessfully, &bRetry
							  /*, ReplayModeBeforeHandling = JsonQueries.ReplayMode*/] () mutable
		{
			ITwinHttp::FLock Lock(JsonQueries.Mutex);
			JsonQueries.LastCompletionTime = FPlatformTime::Seconds();
			if (bRetry)
			{
				ensure(!FromPool.bTryFromCache);
				JsonQueries.StackRequest(&Lock, RequestArgs.Verb, std::move(RequestArgs.UrlSubpath),
					std::move(RequestArgs.Params), std::move(RequestArgs.ProcessJsonResponseFunc),
					std::move(RequestArgs.PostDataString), RequestArgs.RetriesLeft - 1,
					// wait a short time before first retry, a longer time before 2nd (in seconds)
					FPlatformTime::Seconds() + (RequestArgs.RetriesLeft == 2 ? 2. : 8.));
			}
			if (!FromPool.bTryFromCache || FromPool.bSuccess)
			{
				FromPool.bIsAvailable = true;
				++JsonQueries.AvailableRequestSlots; // next Tick will call HandlePendingRequests
				--JsonQueries.RequestsInBatch;
			}
			if (-1 != QueryTimestamp && !FromPool.bTryFromCache && FromPool.bSuccess)
				// Do not write to cache the result of the initial "RequestSchedules" query which
				// ProcessJsonResponseFunc actually initializes the cache: this would write duplicates of the
				// same over and over (since this initial query can never by definition be read from the cache)
				// which raises a sanity check error in FRecordDirIterator::Visit.
				// QueryTimestamp's test against -1 skips the Write call: semantically it would seem cleaner
				// to have this additional test, but it would not work when resetting a corrupted cache because
				// in that case Initialize has returned false and TryLocalCache is not set...
				//&& ReusableJsonQueries::EReplayMode::TryLocalCache == ReplayModeBeforeHandling)
			{
				JsonQueries.Cache.Write(FromPool.Request, Response, bConnectedSuccessfully,
										JsonQueries.Mutex, QueryTimestamp);
			}
		});
	TSharedPtr<FJsonObject> ResponseJson;
	if (FromPool.bTryFromCache)
	{
		// Look up the request in the cache but move the heavier part (Read = load reply from filesystem
		// + parsing the Json, then processing the caller-supplied callback) in the concurrent task:
		auto const Hit = JsonQueries.Cache.LookUp(FromPool.Request, RequestArgs.Verb, JsonQueries.Mutex);
		if (Hit)
		{
			FromPool.bSuccess = true; // needed before yield, used by caller
			co_await UE5Coro::Async::MoveToTask();
			ResponseJson = JsonQueries.Cache.Read(*Hit);
		}
	}
	else if (
		JsonQueries.CheckRequest(CompletedRequest, Response, bConnectedSuccessfully, nullptr, bRetry)
		//synonym to 20X response?
		&& ensure(EHttpRequestStatus::Succeeded == CompletedRequest->GetStatus()))
	{
		bRetry = false;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),
									 ResponseJson);
	}
	if (ResponseJson)
	{
		RequestArgs.ProcessJsonResponseFunc(ResponseJson);
		FromPool.bSuccess = true;
	}
	else
	{
		FromPool.bSuccess = false;
		// final error, clean cache being written into!
		if (!FromPool.bTryFromCache && !bRetry && JsonQueries.bIsRecordingForSimulation)
		{
			JsonQueries.Cache.ClearFromDisk();
		}
	}
}

void FReusableJsonQueries::FImpl::DoEmitRequest(FPoolRequest& FromPool, FRequestArgs RequestArgs)
{
	check(!FromPool.bIsAvailable);//flag already toggled
	FromPool.Request->SetVerb(ITwinHttp::GetVerbString(RequestArgs.Verb));
	FString FullUrl;
	if (RequestArgs.Params.empty())
	{
		FullUrl = JoinToBaseUrl(RequestArgs.UrlSubpath, 0);
		FromPool.Request->SetURL(FullUrl);
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
		FromPool.Request->SetURL(FullUrl);
	}
	// Content-Length should be present http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	// See FCurlHttpRequest::SetupRequest: if we don't set it up here correctly, reusing requests
	// with Payload of different size will keep an incorrect length! The length required is that of the
	// converted utf8 buffer for the payload, so it's better to set an empty string here and let
	// FCurlHttpRequest::SetupRequest set the proper size.
	FromPool.Request->SetHeader(TEXT("Content-Length"), TEXT(""));
	FromPool.Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + GetBearerToken());
	FromPool.Request->SetHeader(TEXT("X-Correlation-ID"), *FGuid::NewGuid().ToString());
	FromPool.Request->SetContentAsString(RequestArgs.PostDataString);
	FromPool.bTryFromCache = (ReusableJsonQueries::EReplayMode::None != ReplayMode);
	// Note: a unique_ptr was almost right except that it cannot be passed to BindLambda, which copies its
	// arguments, whereas Callback would have had to be moved again.
	auto Handler = std::make_shared<FRequestHandler>(*this, FromPool, std::move(RequestArgs), 
													 Cache.CurrentTimestamp());
	if (FirstActiveTime == 0.) // assumed invalid
		FirstActiveTime = FPlatformTime::Seconds();
	++TotalRequestsCount;
	switch (ReplayMode)
	{
	case ReusableJsonQueries::EReplayMode::OnDemandSimulation:
		Handler->Run(Handler, {}, {}, true);
		if (FromPool.bSuccess)
			++CacheHits;
		else
		{
			UE_LOG(ITwinQuery, Warning, TEXT("SimulationMode: no reply found for '%s %s'!"),
				   *FromPool.Request->GetVerb(), *FromPool.Request->GetURL());
		}
		break;
	case ReusableJsonQueries::EReplayMode::TryLocalCache:
		Handler->Run(Handler, {}, {}, true);
		if (FromPool.bSuccess) // was set before co_await
		{
			++CacheHits;
			break;
		}
		[[fallthrough]];// cache miss -> send request
	case ReusableJsonQueries::EReplayMode::None:
		FromPool.bTryFromCache = false;
		if (bIsRecordingForSimulation)
		{
			Cache.RecordQuery(FromPool.Request, Mutex);
		}
		// "Single" delegate, no need to Unbind to reuse:
		FromPool.Request->OnProcessRequestComplete().BindLambda(
			[Callback=std::move(Handler)](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response,
										  bool bConnectedSuccessfully) mutable
			{
				Callback->Run(Callback, CompletedRequest, Response, bConnectedSuccessfully);
			});
		FromPool.Request->ProcessRequest();
		break;
	// TODO_GCO: a little harder: can't persist the callbacks passed from SchedulesImport.cpp!
	case ReusableJsonQueries::EReplayMode::SequentialSession:
		ensure(false);//unimplem...
		break;
	}
}

FString FReusableJsonQueries::FImpl::JoinToBaseUrl(FUrlSubpath const& UrlSubpath, int32 const ExtraSlack) const
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
bool FReusableJsonQueries::FImpl::HandlePendingQueries()
{
	FPoolRequest* Slot = nullptr;
	FRequestArgs RequestArgs;
	{	ITwinHttp::FLock Lock(Mutex);
		if (AvailableRequestSlots > 0 && !RequestsInQueue.empty())
		{
			if (!ensure(AvailableRequestSlots <= RequestsPool.size()))
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
				std::optional<ReusableJsonQueries::FStackedRequests> RetriesDelayed;
				bool bHasFoundRequestToProcess = false;
				// Loop until we find a request that's either not a retry or that has been delayed long enough.
				do
				{
					RequestArgs = std::move(RequestsInQueue.front());
					RequestsInQueue.pop_front();
					if (RequestArgs.DontRetryUntil == -1.
						|| RequestArgs.DontRetryUntil <= (FPlatformTime::Seconds() + 0.1/*don't be picky*/))
					{
						bHasFoundRequestToProcess = true;
						break;
					}
					else
					{
						if (!RetriesDelayed)
							RetriesDelayed.emplace();
						RetriesDelayed->emplace_back(std::move(RequestArgs));
					}
				}
				while (!RequestsInQueue.empty());

				// Move the still delayed requests to the end of the queue
				if (RetriesDelayed)
				{
					if (RequestsInQueue.empty())
						RequestsInQueue.swap(*RetriesDelayed);
					else
					{
						for (auto&& Req : (*RetriesDelayed))
							RequestsInQueue.emplace_back(std::move(Req));
					}
				}
				if (bHasFoundRequestToProcess)
				{
					Slot->bIsAvailable = false;
					--AvailableRequestSlots;
				}
				else
					Slot = nullptr; // emit nothing + need to return false to yield back to game thread
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

FReusableJsonQueries::FReusableJsonQueries(UObject const& Owner, FString const& InBaseUrlNoSlash,
		FAllocateRequest const& AllocateRequest, uint8_t const SimultaneousRequestsAllowed,
		FCheckRequest const& InCheckRequest, ITwinHttp::FMutex& InMutex, TCHAR const* const InRecordToFolder,
		int const InRecorderSessionIndex, TCHAR const* const InSimulateFromFolder,
		FScheduleQueryingDelegate const* OnScheduleQueryingStatusChanged,
		std::function<FString()> const& InGetBearerToken)
: Impl(MakePimpl<FReusableJsonQueries::FImpl>(Owner, InBaseUrlNoSlash, AllocateRequest,
	SimultaneousRequestsAllowed, InCheckRequest, InMutex, InRecordToFolder, InRecorderSessionIndex,
	InSimulateFromFolder, OnScheduleQueryingStatusChanged, InGetBearerToken))
{
}

void FReusableJsonQueries::ChangeRemoteUrl(FString const& NewRemoteUrl)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	Impl->BaseUrlNoSlash = NewRemoteUrl;
}

void FReusableJsonQueries::HandlePendingQueries()
{
	FStackingFunc NextBatch;
	{	ITwinHttp::FLock Lock(Impl->Mutex);
		ensure(Impl->RequestsInBatch >= 0);
		if (Impl->RequestsInBatch)
		{
			if (!Impl->bIsRunning)
			{
				Impl->bIsRunning = true;
				if (Impl->OnScheduleQueryingStatusChanged) // <== NB: only set for prefetched case
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
					if (Impl->OnScheduleQueryingStatusChanged) // <== NB: only set for prefetched case
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

void FReusableJsonQueries::FImpl::StackRequest(
	ITwinHttp::FLock* Lock, ITwinHttp::EVerb const Verb, FUrlSubpath&& UrlSubpath, FUrlArgList&& Params,
	FProcessJsonObject&& ProcessCompletedFunc, FString&& PostDataString /*= {}*/, int const RetriesLeft/*= 2*/,
	double const DontRetryUntil/*= -1.*/)
{
	std::optional<ITwinHttp::FLock> optLock;
	if (!Lock)
		optLock.emplace(Mutex);
	++RequestsInBatch;
	RequestsInQueue.emplace_back(FRequestArgs{Verb, std::move(UrlSubpath),
		std::move(Params), std::move(ProcessCompletedFunc), std::move(PostDataString), RetriesLeft,
		DontRetryUntil});
}

void FReusableJsonQueries::StackRequest(ReusableJsonQueries::FStackingToken const&,
	ITwinHttp::FLock* Lock, ITwinHttp::EVerb const Verb, FUrlSubpath&& UrlSubpath, FUrlArgList&& Params,
	FProcessJsonObject&& ProcessCompletedFunc, FString&& PostDataString /*= {}*/)
{
	Impl->StackRequest(Lock, Verb, std::move(UrlSubpath), std::move(Params), std::move(ProcessCompletedFunc),
					   std::move(PostDataString));
}

void FReusableJsonQueries::NewBatch(FStackingFunc&& StackingFunc, bool const bPseudoBatch/*= false*/)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!Impl->RequestsInBatch && Impl->NextBatches.empty())
	{
		// Stack immediately, to avoid delays (in case of empty batches, in particular)
		FStackingToken Token;
		StackingFunc(Token);
	}
	else Impl->NextBatches.emplace_back(FNewBatch{ std::move(StackingFunc), bPseudoBatch });
}

void FReusableJsonQueries::InitializeCache(FString const& CacheFolder, EITwinEnvironment const Env,
										   FString const& DisplayName)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	ensure(ReusableJsonQueries::EReplayMode::None == Impl->ReplayMode);
	if (Impl->Cache.Initialize(CacheFolder, Env, DisplayName))
		Impl->ReplayMode = ReusableJsonQueries::EReplayMode::TryLocalCache;
}

void FReusableJsonQueries::UninitializeCache()
{
	Impl->Cache.Uninitialize();
}

void FReusableJsonQueries::ClearCacheFromMemory()
{
	Impl->Cache.Uninitialize();
	Impl->ReplayMode = ReusableJsonQueries::EReplayMode::None;
}

std::pair<int, int> FReusableJsonQueries::QueueSize() const
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	return {
		(Impl->RequestsInBatch ? 1 : 0) + (int)std::count_if(
			Impl->NextBatches.begin(), Impl->NextBatches.end(),
			[](auto&& PendingBatch) { return !PendingBatch.bPseudoBatch; }),
		Impl->RequestsInBatch
	};
}

void FReusableJsonQueries::SwapQueues(ITwinHttp::FLock&, ReusableJsonQueries::FStackedBatches& NextBatches,
	ReusableJsonQueries::FStackedRequests& RequestsInQ, ReusableJsonQueries::FStackingFunc&& PriorityRequest)
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

FString FReusableJsonQueries::Stats() const
{
	return FString::Printf(TEXT("Processed %llu requests (%llu from local cache) in %.1fs."),
		Impl->TotalRequestsCount, Impl->CacheHits, Impl->LastCompletionTime - Impl->FirstActiveTime);
}

void FReusableJsonQueries::StatsResetActiveTime()
{
	Impl->FirstActiveTime = 0.;
}
