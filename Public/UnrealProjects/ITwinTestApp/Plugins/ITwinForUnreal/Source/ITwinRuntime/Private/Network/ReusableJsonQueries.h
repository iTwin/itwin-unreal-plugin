/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Async/Async.h>
#include <Dom/JsonObject.h>
#include <HttpFwd.h>
#include <Templates/PimplPtr.h>

#include <ITwinHttpUtils.h>
#include <ITwinSynchro4DSchedules.h>
#include <Network/HttpUtils.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/StrongTypes/TaggedValue.h>
#	include <Core/ITwinAPI/ITwinRequestTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

enum class EITwinEnvironment : uint8;

class FPoolRequest
{
public:
	FHttpRequestPtr Request;
	bool bIsAvailable{ true };
	bool bSuccess{ true };
	bool bTryFromCache{ true };
	std::shared_ptr<std::atomic_bool> bShouldCancel = std::make_shared<std::atomic_bool>(false);
	TSharedPtr<TPromise<void>> AsyncRoutine;

	void Cancel();
};

// CANNOT use string views: I thought they would all be either static strings or references to stable strings
// stored in the import structures (IDs for iTwin, iModel, Schedule, Task, etc.)
// BUT Schedules, AnimationBindings etc. are all vectors that could be resized when querying using
// pagination :/ So use strings for the time being, maybe use accessors to arrays later, so that only the
// indices need be copied?
using FUrlArgList = std::vector<std::pair<FString, FString>>;
using FUrlSubpath = std::vector<FString>;
using FProcessJsonObject = std::function<void(TSharedPtr<FJsonObject> const&)>;
using FAllocateRequest = std::function<FHttpRequestPtr()>;
DEFINE_STRONG_BOOL(DeltaTokenExpired);
// Returns whether the response is valid and safe to parse. Returning false keeps retry handling active.
using FCheckRequest = std::function<bool(FHttpRequestPtr const& /*CompletedRequest*/,
	FHttpResponsePtr const& /*Response*/, ITwinHttp::ConnectionSuccess, ITwinHttp::RetryQuery&, DeltaTokenExpired&)>;

namespace APIParams
{
	static const FString PageToken("$continuationToken");
	static const FString DeltaToken("$deltaToken");
}

struct FRequestArgs
{
	ITwinHttp::EVerb Verb = ITwinHttp::EVerb::Get;
	FUrlSubpath UrlSubpath;
	FUrlArgList Params;
	FProcessJsonObject ProcessJsonResponseFunc;
	FString PostDataString;
	int RetriesLeft = 0; ///< Actual value set in FImpl::StackRequest
	double DontRetryUntil = -1.; ///< Absolute time in seconds comparable to FPlatformTime::Seconds()
};

// No use making these types depend on FReusableJsonQueries's template parameter
namespace ReusableJsonQueries
{
	class FStackingToken;
	using FStackedRequests = std::deque<FRequestArgs>;
	using FStackingFunc = std::function<void(FStackingToken const&)>;
	struct FNewBatch
	{
		FStackingFunc Exec;
		bool bPseudoBatch = false;
	};
	using FStackedBatches = std::deque<FNewBatch>;

	enum class EReplayMode {
		/// FReusableJsonQueries is called "normally" but do not always emit the http request, using persisted
		/// data instead to match queries to replies. If no entry is found in the cache, the request is sent.
		TryLocalCache,
		/// Special simulation mode that could be useful for unit/integration testing or debugging: almost the
		/// same as TryLocalCache, except that not finding the reply in the "cache" (aka "simulation folder")
		/// is an error and no http request is sent.
		OnDemandSimulation,
		/// (TODO_GCO Unimplemented) Session is replayed sequentially based on persisted timestamps: was
		/// supposed to be used to debug a faulty session of queries to see what went wrong but it was
		/// never actually needed in the end so left unimplemented.
		SequentialSession,
		None,
	};
}

class FReusableJsonQueries
{
public:
	FReusableJsonQueries(UObject const& Owner, FString const& RemoteUrl,
		FAllocateRequest const& AllocateRequest, uint8_t const SimultaneousRequestsAllowed,
		FCheckRequest const& CheckRequest, ITwinHttp::FMutex& Mutex,
		TCHAR const* const InSavedFolderForReplay, int const InRecorderSessionIndex,
		TCHAR const* const InSimulateFromFolder, std::function<FString()> const& GetBearerToken,
		std::function<bool()> const& FreezeNextBatches);

	void ChangeRemoteUrl(FString const& NewRemoteUrl);
	[[nodiscard]] FString JoinToBaseUrl(FUrlSubpath const& UrlSubpath);
	void BeginShutdown();

	/// Called during game tick to sent new requests and handle request batches in the waiting list
	void HandlePendingQueries();

	/// Set the folder into which to cache all requests and their replies from now on (support a single
	/// folder ie a single schedule for the moment, see comment over ensure(Schedules.empty()) in
	/// SchedulesImport.cpp
	/// \param DisplayName Informative name, for debugging
	void InitializeCache(FString const& CacheFolder, EITwinEnvironment const Env, FString const& DisplayName,
						 bool bUnitTesting = false);
	void UninitializeCache();
	/// Reset data structures into which were parsed data from the local cache used to map requests to their
	/// possible cache entries (reply payloads are never kept in memory). Also resets all internal variables
	/// to a state leading to not using the cache at all.
	void ClearCacheFromMemory();
	FString CacheFolder() const;
	
	/// A request may need to prevent other unrelated requests to be stacked and sent at the same time,
	/// and/or wait for the current queue and running requests to finish, to use their result for example.
	/// Use this method to stack requests to be executed after all current and pending requests are done.
	/// \param Func Functor for creating the requests to be stacked once the current/running ones are done
	void NewBatch(ReusableJsonQueries::FStackingFunc&& Func, bool const bPseudoBatch = false);

	/// To be used only from a FStackingFunc functor, itself passed to NewBatch for execution or postponement
	/// \param Token Passed by the FReusableJsonQueries itself to the stacking functor, to allow it to
	///		actually stack requests. Its sole purpose is to prevent direct calls to StackRequest, except
	///		from the stacking functors themselves, where the caller is responsible for request ordering.
	/// \param Lock optional existing lock
	void StackRequest(ReusableJsonQueries::FStackingToken const&, ITwinHttp::FLock* Lock,
		ITwinHttp::EVerb const Verb, FUrlSubpath&& UrlSubpath, FUrlArgList&& Params,
		FProcessJsonObject&& ProcessCompletedFunc, FString&& PostDataString = {});

	/// Delete from the cache (and the filesystem) the entry corresponding to the first reply of a set of paginated
	/// replies which last page yielded the delta token used in the \see DeltaRequest. All other replies will be
	/// marked droppable: in case the first page is the same, we will be able to reuse them, otherwise the next call to
	/// EraseDroppableEntries will erase them.
	///
	/// \param DeltaRequest Incremental update request using a delta token, for which the server replied that the
	///		token has expired, ie we need to request again from scratch.
	/// \return Number of cache entries dropped (for the first page), or marked droppable (all subsequent pages)
	size_t OnDeltaTokenExpired(FHttpRequestPtr const& DeltaRequest);

	/// Erase from cache and filesystem any entry marked 'droppable' by earlier calls to OnDeltaTokenExpired, and not
	/// reused since.
	size_t EraseDroppableEntries();

	/// Returns the current size of the requests queue expressed as a pair of values in the form
	/// '{Batches,CurrentBatchRequests}' where 'Batches' in the number of request batches left to process
	/// (@see NewBatch), including the current batch being processed, and 'CurrentBatchRequests' is the
	/// number of uncompleted requests in the current batch. Note that the latter can grow during the scope
	/// of a batch, depending on the type of, and responses to the requests (need for pagination, follow-up
	/// requests for further details, etc.). The number of requests in queued batches cannot be known in
	/// advance because it can typically depend on the responses to all requests after which they were queued
	/// (if it did not, it would not have been necessary to make separate batches in the first place!).
	std::pair<int, int> QueueSize() const;

	/// Return some statistics
	FString Stats() const;
	size_t FetchedFromRemote() const;
	size_t FetchedFromCache() const;
	/// Resets the time used for statistics as the start time of the first request (useful to avoid accouting
	/// for the delay between the initial listing of the schedules of an iModel and the start of the actual
	/// querying of bindings)
	void StatsResetActiveTime();

	void SwapQueues(ITwinHttp::FLock&, ReusableJsonQueries::FStackedBatches& NextBatches,
		ReusableJsonQueries::FStackedRequests& RequestsInQueue,
		ReusableJsonQueries::FStackingFunc&& PriorityRequest = {});

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
