/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueriesImpl.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "ReusableJsonQueries.h"

#include "CoreMinimal.h"
#include <GenericPlatform/GenericPlatformFile.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <map>
#include <unordered_map>
#include <variant>

namespace ReusableJsonQueries {

/// Maps replies to queries contents
using FQueryKey = std::pair<FString/*url*/, FString/*payload*/>;
using FSimulationMap
	= std::unordered_map<std::variant<FString/*url*/, FQueryKey>, TSharedPtr<FJsonObject>/*reply*/>;
/// Map of the queries/replies sent/received in the order in which it happened during a session
using FReplayMap = std::map<int32/*Timestamp*/,
	std::variant</*get or post query:*/FString/*url*/, FQueryKey, /*or reply:*/TSharedPtr<FJsonObject>>>;

} // namespace ReusableJsonQueries

template <>
struct std::hash<std::variant<FString, ReusableJsonQueries::FQueryKey>>
{
public:
	size_t operator()(std::variant<FString, ReusableJsonQueries::FQueryKey> const& Key) const
	{
		size_t Res = 9876;
		std::visit([&Res](auto&& StrOrPair)
			{
				using T = std::decay_t<decltype(StrOrPair)>;
				if constexpr (std::is_same_v<T, FString>)
					boost::hash_combine(Res, GetTypeHash(StrOrPair));
				else if constexpr (std::is_same_v<T, ReusableJsonQueries::FQueryKey>)
				{
					boost::hash_combine(Res, GetTypeHash(StrOrPair.first));
					boost::hash_combine(Res, GetTypeHash(StrOrPair.second));
				}
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Key);
		return Res;
	}
};

#include <array>
#include <memory>

template<uint16_t SimultaneousRequestsT>
class FReusableJsonQueries<SimultaneousRequestsT>::FImpl
{
	friend class FReusableJsonQueries<SimultaneousRequestsT>;

	FString BaseUrlNoSlash;
	FCheckRequest const CheckRequest;
	std::function<FString()> const GetBearerToken;
	ReusableJsonQueries::FMutex& Mutex;
	TCHAR const* const RecordToFolder; ///< Only for devs (save requests and replies for later replay)
	FString RecorderPathBase; ///< \see RecordToFolder
	int const RecorderSessionIndex; ///< Subfolder index used with RecordToFolder
	int RecorderTimestamp = 0; ///< Timestamp used with RecorderPathBase. \see RecordToFolder
	/// Only for devs: when !nullptr, bypass actual queries and use saved query/reply pairs from this folder.
	/// Only allows for "dumb" simulations, ie repeating persisted queries exactly (you can't query tasks on
	/// separate elements if they were queried together during the recording session)
	TCHAR const* const SimulateFromFolder;
	ReusableJsonQueries::FSimulationMap SimulationMap;
	ReusableJsonQueries::FReplayMap ReplayMap;
	ReusableJsonQueries::EReplayMode ReplayMode = ReusableJsonQueries::EReplayMode::None;

	/// Flag tracking the status of "RequestsInBatch != 0 || !NextBatches.empty()" in order to trigger
	/// OnScheduleQueryingStatusChanged when it changes
	bool bIsRunning = false;
	FScheduleQueryingDelegate const& OnScheduleQueryingStatusChanged;

	/// Number of requests in the current "batch", which is a grouping of requests which ordering is not
	/// relevant. Incremented when stacking requests, decremented when finishing a request.
	/// => Until it's back down to zero, incoming request stacking functors are put on a waiting list.
	int RequestsInBatch = 0;
	ReusableJsonQueries::FStackedBatches NextBatches;

	/// ProcessRequest doc says a Request can be re-used (but not while still being processed, obviously)
	std::array<FPoolRequest, SimultaneousRequestsT> RequestsPool;
	ReusableJsonQueries::FStackedRequests RequestsInQueue;
	std::atomic<uint16_t> AvailableRequestSlots{ SimultaneousRequestsT };

	/// Stats: total number of requests emitted in the lifetime of this instance
	size_t TotalRequestsCount = 0;
	/// Stats: start time of the first query (ever, or since the last call to ResetActiveTime)
	double FirstActiveTime = 0.;
	/// Stats: last completion time
	double LastCompletionTime = 0.;

	std::shared_ptr<bool> IsThisValid;

	/// \return Whether a pending request was emitted
	bool HandlePendingQueries();
	void DoEmitRequest(FPoolRequest& FromPool, FRequestArgs const RequestArgs);
	[[nodiscard]] std::function<TSharedPtr<FJsonObject>(FHttpResponsePtr)> GetJsonObjGetterForSimulation(
		FPoolRequest& FromPool, bool const bVerbIsGET);
	[[nodiscard]] FString JoinToBaseUrl(FUrlSubpath const& UrlSubpath, int32 const ExtraSlack) const;
	[[nodiscard]] TSharedRef<FJsonObject> ToJson(FRequestPtr const& Req, FString const& FullUrl,
												 FString const& PostContentString);
	void RecordQuery(TSharedRef<FJsonObject> const JsonObj, ReusableJsonQueries::FLock&);
	void RecordReply(FHttpResponsePtr const Response, bool const bConnectedSuccessfully,
					 int const QueryTimestamp, ReusableJsonQueries::FLock&);

public:
	~FImpl();

	FImpl(FString const& InBaseUrlNoSlash, FAllocateRequest const& AllocateRequest,
		FCheckRequest const& InCheckRequest, ReusableJsonQueries::FMutex& InMutex,
		TCHAR const* const InRecordToFolder, int const InRecorderSessionIndex,
		TCHAR const* const InSimulateFromFolder,
		std::optional<ReusableJsonQueries::EReplayMode> const InReplayMode,
		FScheduleQueryingDelegate const& OnScheduleQueryingStatusChanged,
		std::function<FString()> const& GetBearerToken);

}; // class FReusableJsonQueries<SimultaneousRequestsT>::FImpl

class FRecordDirIterator : public IPlatformFile::FDirectoryVisitor
{
	ReusableJsonQueries::FSimulationMap& SimulationMap;
	ReusableJsonQueries::FReplayMap& ReplayMap;
public:
	FRecordDirIterator(ReusableJsonQueries::FSimulationMap& InSimulationMap,
					   ReusableJsonQueries::FReplayMap& InReplayMap)
		: SimulationMap(InSimulationMap), ReplayMap(InReplayMap)
	{
	}
	virtual bool Visit(const TCHAR* Filename, bool bIsDirectory) override;
};
