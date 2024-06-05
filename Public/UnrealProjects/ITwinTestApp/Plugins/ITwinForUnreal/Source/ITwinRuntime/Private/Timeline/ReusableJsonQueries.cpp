/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ReusableJsonQueries.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <optional>

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
class FReusableJsonQueries<SimultaneousRequestsT>::FImpl
{
	friend class FReusableJsonQueries<SimultaneousRequestsT>;

	FString const BaseUrlNoSlash;
	FCheckRequest const CheckRequest;
	ReusableJsonQueries::FMutex& Mutex;

	/// Number of requests in the current "batch", which is a grouping of requests which ordering is not
	/// relevant. Incremented when stacking requests, decremented when finishing a request.
	/// => Until it's back down to zero, incoming request stacking functors are put on a waiting list.
	int RequestsInBatch = 0;
	ReusableJsonQueries::FStackedBatches NextBatches;

	/// ProcessRequest doc says a Request can be re-used (but not while still being processed, obviously)
	std::array<FPoolRequest, SimultaneousRequestsT> RequestsPool;
	ReusableJsonQueries::FStackedRequests RequestsInQueue;
	std::atomic<uint16_t> AvailableRequestSlots{ SimultaneousRequestsT };

	std::shared_ptr<bool> IsThisValid;

	void HandlePendingQueries();
	void DoEmitRequest(FPoolRequest& FromPool, FRequestArgs const RequestArgs);
	[[nodiscard]] FString JoinToBaseUrl(FUrlSubpath const& UrlSubpath, int32 const ExtraSlack) const;

public:
	FImpl(FString const& InBaseUrlNoSlash, FAllocateRequest const& AllocateRequest,
		FCheckRequest const& InCheckRequest, ReusableJsonQueries::FMutex& InMutex);
	~FImpl();
};

template<uint16_t SimultaneousRequestsT>
FReusableJsonQueries<SimultaneousRequestsT>::FImpl::FImpl(FString const& InBaseUrlNoSlash,
		FAllocateRequest const& AllocateRequest, FCheckRequest const& InCheckRequest,
		ReusableJsonQueries::FMutex& InMutex)
	: BaseUrlNoSlash(InBaseUrlNoSlash)
	, CheckRequest(InCheckRequest)
	, Mutex(InMutex)
	, IsThisValid(new bool(true))
{
	for (auto& FromPool : RequestsPool)
	{
		FromPool.Request = AllocateRequest();
	}
}

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
	if (RequestArgs.Params.empty())
	{
		FromPool.Request->SetURL(JoinToBaseUrl(RequestArgs.UrlSubpath, 0));
	}
	else
	{
		int32 ExtraSlack = 0;
		for (auto const& Parameter : RequestArgs.Params)
		{
			ExtraSlack += 2 + Parameter.first.Len() + Parameter.second.Len();
		}
		FString FullUrl = JoinToBaseUrl(RequestArgs.UrlSubpath, ExtraSlack);
		bool first = true;
		for (auto const& Parameter : RequestArgs.Params)
		{
			FullUrl += (first ? '?' : '&');
			FullUrl += Parameter.first;
			FullUrl += '=';
			FullUrl += Parameter.second;
			first = false;
		}
		FromPool.Request->SetURL(std::move(FullUrl));
	}
	// Content-Length should be present http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
	// See FCurlHttpRequest::SetupRequest: if we don't set it up here correctly, reusing requests
	// with Payload of different size will keep an incorrect length! The length required is that of the
	// converted utf8 buffer for the payload, so it's better to set an empty string here and let
	// FCurlHttpRequest::SetupRequest set the proper size.
	FromPool.Request->SetHeader("Content-Length", "");
	FromPool.Request->SetContentAsString(std::move(RequestArgs.PostDataString));
	// "Single" delegate, no need to Unbind to reuse:
	FromPool.Request->OnProcessRequestComplete().BindLambda(
		[ IsThisValid=this->IsThisValid, // need to copy shared_ptr
		  JsonQueries=this, // just to make it explicit
		  pbRequestAvailableFlag=&(FromPool.bIsAvailable), // usable as long as (*IsThisValid)
		  ProcessJsonResponseFunc=std::move(RequestArgs.ProcessJsonResponseFunc),
		  &RequestsInBatch=this->RequestsInBatch]
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
			check(*pbRequestAvailableFlag == false);
			Be::CleanUpGuard CleanUp([&JsonQueries, pbRequestAvailableFlag, &RequestsInBatch]
				{
					FLock Lock(JsonQueries->Mutex);
					*pbRequestAvailableFlag = true;
					++JsonQueries->AvailableRequestSlots; // next Tick will call HandlePendingRequests
					--RequestsInBatch;
				});
			if (JsonQueries->CheckRequest(CompletedRequest, Response, bConnectedSuccessfully, nullptr))
			{
				//synonym to 20X response?
				check(EHttpRequestStatus::Succeeded == CompletedRequest->GetStatus());
				TSharedPtr<FJsonObject> responseJson;
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),
											 responseJson);
				if (responseJson)
				{
					ProcessJsonResponseFunc(responseJson);
				}
			}
		});
	FromPool.Request->ProcessRequest();
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

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::FImpl::HandlePendingQueries()
{
	FPoolRequest* Slot = nullptr;
	FRequestArgs RequestArgs;
	{	FLock Lock(Mutex);
		if (AvailableRequestSlots > 0 && !RequestsInQueue.empty())
		{
			if (AvailableRequestSlots > SimultaneousRequestsT)
			{
				check(false);
				AvailableRequestSlots = 0;
				for (auto&& FromPool : RequestsPool) if (FromPool.bIsAvailable) ++AvailableRequestSlots;
				if (!AvailableRequestSlots)
				{
					return;
				}
			}
			for (auto&& FromPool : RequestsPool)
			{
				if (FromPool.bIsAvailable)
				{
					Slot = &FromPool;
					break;
				}
			}
			if (!Slot) { check(false); return; }
			Slot->bIsAvailable = false;
			--AvailableRequestSlots;
			RequestArgs = std::move(RequestsInQueue.front());
			RequestsInQueue.pop_front();
		}
	}
	if (Slot)
	{
		DoEmitRequest(*Slot, std::move(RequestArgs));
	}
}

template<uint16_t SimultaneousRequestsT>
FReusableJsonQueries<SimultaneousRequestsT>::FReusableJsonQueries(FString const& InBaseUrlNoSlash,
		FAllocateRequest const& AllocateRequest, FCheckRequest const& InCheckRequest,
		ReusableJsonQueries::FMutex& InMutex)
	: Impl(MakePimpl<FReusableJsonQueries<SimultaneousRequestsT>::FImpl>(
		InBaseUrlNoSlash, AllocateRequest, InCheckRequest, InMutex))
{
}

// IMPORTANT: this is never called in the Editor! (because there is no ticking!)
// Even if we had our own in-Editor ticking system, we may have to call FHttpManager::Tick as well?
template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::HandlePendingQueries()
{
	FStackingFunc NextBatch;
	{	FLock Lock(Impl->Mutex);
		if (!Impl->RequestsInBatch && !Impl->NextBatches.empty())
		{
			NextBatch = std::move(Impl->NextBatches.front());
			Impl->NextBatches.pop_front();
		}
	}
	if (NextBatch)
	{
		FStackingToken Token;
		NextBatch(Token);
	}
	Impl->HandlePendingQueries();
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
void FReusableJsonQueries<SimultaneousRequestsT>::NewBatch(FStackingFunc&& StackingFunc)
{
	FLock Lock(Impl->Mutex);
	if (!Impl->RequestsInBatch && Impl->NextBatches.empty())
	{
		// Stack immediately, to avoid delays (in case of empty batches, in particular)
		FStackingToken Token;
		StackingFunc(Token);
	}
	else Impl->NextBatches.emplace_back(std::move(StackingFunc));
}

template<uint16_t SimultaneousRequestsT>
std::pair<int, int> FReusableJsonQueries<SimultaneousRequestsT>::QueueSize() const
{
	FLock Lock(Impl->Mutex);
	return {
		(Impl->RequestsInBatch ? 1 : 0) + (int)Impl->NextBatches.size(),
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
				[this, Reqs = std::move(ReqsInQ)](FStackingToken const&) mutable
				{
					check(Impl->RequestsInQueue.empty());
					Impl->RequestsInQueue.swap(Reqs);
				});
		}
		Impl->NextBatches.push_front(PriorityRequest);
	}
}

/// Must match SimultaneousRequestsAllowed but since it's declared in SchedulesImport.h, I didn't want to
/// introduce a dependency for that (you'll get link errors in case of inconsistency anyway)
template class FReusableJsonQueries<8/*SimultaneousRequestsAllowed*/>;
#define REUSABLEJSONQUERIES_TEMPLATE_INSTANTIATED // see SchedulesImport.cpp ...
