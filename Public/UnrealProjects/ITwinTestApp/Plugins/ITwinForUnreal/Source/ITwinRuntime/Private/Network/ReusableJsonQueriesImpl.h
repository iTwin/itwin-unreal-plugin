/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueriesImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "JsonQueriesCache.h"
#include "ReusableJsonQueries.h"

#include "CoreMinimal.h"

#include <memory>
#include <vector>

class FRequestHandler;

class FReusableJsonQueries::FImpl
{
	friend class FReusableJsonQueries;

	class FRequestHandler
	{
	public:
		static constexpr int DefaultNbRetries = 2;

	private:
		/// JsonQueries and FromPool usable as long as (*IsJsonQueriesValid)
		std::shared_ptr<std::atomic_bool> IsJsonQueriesValid;
		FReusableJsonQueries::FImpl& JsonQueries;
		FPoolRequest& FromPool;
		FRequestArgs RequestArgs;
		int QueryTimestamp;

	public:
		FRequestHandler(FReusableJsonQueries::FImpl& InJsonQueries,
			FPoolRequest& InFromPool, FRequestArgs&& InRequestArgs, int const InQueryTimestamp)
		:
			IsJsonQueriesValid(InJsonQueries.IsThisValid) // need to copy shared_ptr
			, JsonQueries(InJsonQueries), FromPool(InFromPool), RequestArgs(std::move(InRequestArgs))
			, QueryTimestamp(InQueryTimestamp)
		{
		}

		bool IsValid() const { return IsJsonQueriesValid->load(); }
		[[nodiscard]] TSharedPtr<TPromise<void>> Run(std::shared_ptr<FRequestHandler> This,
			FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response,
			ITwinHttp::ConnectionSuccess bConnectedSuccessfully);
		void ProcessResponse(TSharedPtr<FJsonObject> ResponseJson, FHttpResponsePtr Response,
			ITwinHttp::ConnectionSuccess bConnectedSuccessfully, ITwinHttp::RetryQuery bRetry,
			ITwinHttp::DeltaQuery bIsDeltaQuery);
		void CleanUp(FHttpResponsePtr Response, ITwinHttp::ConnectionSuccess bConnectedSuccessfully,
					 ITwinHttp::RetryQuery bRetry, ITwinHttp::DeltaQuery bIsDeltaQuery);
	};

	FString BaseUrlNoSlash;
	FCheckRequest const CheckRequest;
	std::function<FString()> const GetBearerToken;
	std::function<bool()> FreezeNextBatches;
	ITwinHttp::FMutex& Mutex;
	bool bIsRecordingForSimulation = false;
	FJsonQueriesCache Cache;
	ReusableJsonQueries::EReplayMode ReplayMode = ReusableJsonQueries::EReplayMode::None;

	/// Flag tracking the status of "RequestsInBatch != 0 || !NextBatches.empty()", used to be needed to trigger
	/// OnScheduleQueryingStatusChanged but not anymore. Might be useful tho so leaving it.
	bool bIsRunning = false;

	/// Number of requests in the current "batch", which is a grouping of requests which ordering is not
	/// relevant. Incremented when stacking requests, decremented when finishing a request.
	/// => Until it's back down to zero, incoming request stacking functors are put on a waiting list.
	int RequestsInBatch = 0;
	ReusableJsonQueries::FStackedBatches NextBatches;

	/// ProcessRequest doc says a Request can be re-used (but not while still being processed, obviously)
	std::vector<FPoolRequest> RequestsPool;
	ReusableJsonQueries::FStackedRequests RequestsInQueue;
	std::atomic<uint16_t> AvailableRequestSlots{ 0 };

	/// Stats: total number of requests emitted in the lifetime of this instance
	size_t TotalRequestsCount = 0;
	/// Stats: total number of requests successfully obtained from the remote in the lifetime of this instance
	size_t SuccessfulRequestsFromRemote = 0;
	/// Stats: total number of requests obtained from the local cache in the lifetime of this instance
	size_t CacheHits = 0;
	/// Stats: start time of the first query (ever, or since the last call to ResetActiveTime)
	double FirstActiveTime = 0.;
	/// Stats: last completion time
	double LastCompletionTime = 0.;

	/// Only readable after IsThisValid has been confirmed true
	std::atomic_bool bIsShuttingDown{ false };
	std::shared_ptr<std::atomic_bool> IsThisValid;

	/// \return Whether a pending request was emitted
	bool HandlePendingQueries();
	void StackRequest(ITwinHttp::FLock* Lock, ITwinHttp::EVerb const Verb, FUrlSubpath&& UrlSubpath,
		FUrlArgList&& Params, FProcessJsonObject&& ProcessCompletedFunc, FString&& PostDataString,
		int const RetriesLeft = FRequestHandler::DefaultNbRetries, double const DontRetryUntil = -1.);
	void DoEmitRequest(FPoolRequest& FromPool, FRequestArgs RequestArgs);
	[[nodiscard]] FString JoinToBaseUrl(FUrlSubpath const& UrlSubpath, int32 const ExtraSlack) const;
	void ShutdownLocked(ITwinHttp::FLock&/*Lock*/);

public:
	~FImpl();

	/// \param InSimulateFromFolder Only for devs: when !nullptr, bypass actual queries and use saved
	///		query/reply pairs from this folder. Only allows for "dumb" simulations, ie repeating persisted
	///		queries exactly (you can't query tasks on separate elements if they were queried together during
	///		the recording session).
	FImpl(UObject const& Owner, FString const& InBaseUrlNoSlash, FAllocateRequest const& AllocateRequest,
		uint8_t const SimultaneousRequestsAllowed, FCheckRequest const& InCheckRequest,
		ITwinHttp::FMutex& InMutex, TCHAR const* const InRecordToFolder, int const InRecorderSessionIndex,
		TCHAR const* const InSimulateFromFolder, std::function<FString()> const& GetBearerToken,
		std::function<bool()> const& FreezeNextBatches);

}; // class FReusableJsonQueries::FImpl
