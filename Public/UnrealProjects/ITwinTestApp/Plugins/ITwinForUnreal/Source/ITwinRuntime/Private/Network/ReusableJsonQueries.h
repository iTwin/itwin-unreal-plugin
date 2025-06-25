/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Async/Async.h>
#include <Dom/JsonObject.h>
#include <HttpFwd.h>
#include <Templates/PimplPtr.h>

#include <ITwinSynchro4DSchedules.h>
#include <Network/HttpUtils.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinRequestTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
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
	bool bShouldCancel{ false };
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
using FCheckRequest = std::function<bool(FHttpRequestPtr const& /*CompletedRequest*/,
	FHttpResponsePtr const& /*Response*/, bool /*connectedSuccessfully*/, bool const/*bWillRetry*/)>;

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
		TCHAR const* const InSimulateFromFolder,
		FScheduleQueryingDelegate const* OnScheduleQueryingStatusChanged,
		std::function<FString()> const& GetBearerToken);

	void ChangeRemoteUrl(FString const& NewRemoteUrl);

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
