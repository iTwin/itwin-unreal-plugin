/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueries.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Dom/JsonObject.h>
#include <HttpFwd.h>
#include <Templates/PimplPtr.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

enum class EVerb : uint8_t { GET, POST };

using FRequestPtr = TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>;
struct FPoolRequest
{
	FRequestPtr Request;
	bool bIsAvailable{ true };
};

// CANNOT use string views: I though they would all be either static strings or references to stable strings
// stored in the import structures (IDs for iTwin, iModel, Schedule, Task, etc.)
// BUT Schedules, AnimationBindings etc. are all vectors that could be resized when querying using
// pagination :/ So use strings for the time being, maybe use accessors to arrays later, so that only the
// indices need be copied?
using FUrlArgList = std::vector<std::pair<FString, FString>>;
using FUrlSubpath = std::vector<FString>;
using FProcessJsonObject = std::function<void(TSharedPtr<FJsonObject> const&)>;
using FAllocateRequest = std::function<FRequestPtr()>;
using FCheckRequest = std::function<bool(
	FHttpRequestPtr const& /*CompletedRequest*/, FHttpResponsePtr const& /*Response*/,
	bool /*connectedSuccessfully*/, FString* /*pStrError*/)>;

struct FRequestArgs
{
	EVerb Verb = EVerb::GET;
	FUrlSubpath UrlSubpath;
	FUrlArgList Params;
	FProcessJsonObject ProcessJsonResponseFunc;
	FString PostDataString;
};

// No use making these types depend on FReusableJsonQueries's template parameter
namespace ReusableJsonQueries
{
	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<std::recursive_mutex>;
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
		/// Session is replayed sequentially based on persisted timestamps
		SequentialSession,
		/// FReusableJsonQueries is called "normally" but do not emit any actual http requests, using persisted
		/// data instead to match queries to replies
		OnDemandSimulation,
		None,
	};
}

template<uint16_t SimultaneousRequestsT>
class FReusableJsonQueries
{
public:
	FReusableJsonQueries(FString const& RemoteUrl, FAllocateRequest const& AllocateRequest,
		FCheckRequest const& CheckRequest, ReusableJsonQueries::FMutex& Mutex,
		TCHAR const* const InSavedFolderForReplay, int const SchedApiSession,
		TCHAR const* const InSimulateFromFolder,
		std::optional<ReusableJsonQueries::EReplayMode> const ReplayMode);

	void ChangeRemoteUrl(FString const& NewRemoteUrl);

	/// Called during game tick to sent new requests and handle request batches in the waiting list
	void HandlePendingQueries();
	
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
	void StackRequest(ReusableJsonQueries::FStackingToken const&, ReusableJsonQueries::FLock* Lock,
		EVerb const Verb, FUrlSubpath&& UrlSubpath, FUrlArgList&& Params,
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

	void SwapQueues(ReusableJsonQueries::FLock&, ReusableJsonQueries::FStackedBatches& NextBatches,
		ReusableJsonQueries::FStackedRequests& RequestsInQueue,
		ReusableJsonQueries::FStackingFunc&& PriorityRequest = {});

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
