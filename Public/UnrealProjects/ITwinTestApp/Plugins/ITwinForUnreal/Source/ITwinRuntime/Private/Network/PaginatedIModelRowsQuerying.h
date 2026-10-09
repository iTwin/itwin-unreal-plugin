/*--------------------------------------------------------------------------------------+
|
|     $Source: PaginatedIModelRowsQuerying.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinWebServices/ITwinWebServices.h>
#include <Network/JsonQueriesCache.h>

#include <Interfaces/IHttpResponse.h>
#include <Templates/SharedPointer.h>

#include <functional>
#include <memory>
#include <variant>

class AITwinIModel;
class FJsonObject;

enum class EElementsMetadata : uint8 {
	/// A single query combining parent-child relationships, bounding boxes, Source ID's, and FederatedGuid's
	Combined,
	/// Same as Combined, without bounding boxes because of https://github.com/iTwin/itwinjs-backlog/issues/2146,
	/// as it was witnessed that splitting the query this way "fixes" the query plan for the problematic models.
	CombinedNoBBoxes,
	/// Standalone query for bounding boxes, because of https://github.com/iTwin/itwinjs-backlog/issues/2146
	StandaloneBBoxes,
	/// Construction detailing Elements' parents need an different kind of request that must be executed separately
	ConstructionDetailing
};

class FPaginatedIModelRowsQueries : public std::enable_shared_from_this<FPaginatedIModelRowsQueries>
{
public:
	enum class EState {
		NotStarted, Running, NeedRestart, Finished, StoppedOnError, Cancelling, Cancelled
	};

	using FOnLoadProgressUpdated = std::function<void()>;

	FPaginatedIModelRowsQueries(AITwinIModel& InIModel, EElementsMetadata InKindOfMetadata,
		std::shared_ptr<ITwinHttp::FMutex> InMutex, FOnLoadProgressUpdated InOnLoadProgressUpdated,
		int InQueryRowCount, int InMaxNumPageInProgress);

	void Cancel();
	double PercentComplete() const;
	size_t GetRequestsFromRemote() const;
	size_t GetRequestsFromCache() const;
	EHttpResponseCodes::Type GetFirstErrorCode() const;
	FString GetFirstErrorString() const;
	EState GetState() const;
	void Restart();
	void UninitializeCache();
	bool ClearCacheOnDisk();
	void OnIModelUninit();

private:
	bool QueryNextPage();
	/// \return Whether the reply was to a request emitted by this instance of metadata requester, and was
	///			thus parsed here.
	bool OnQueryCompleted(bool bSuccess, std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult,
		std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo, bool const bTableCountReply,
		bool const bIgnoreError);
	void DoRestart();

	AITwinIModel& IModel;
	EElementsMetadata const KindOfMetadata;
	FString const ECSQLQueryString;
	FString const ECSQLQueryCount;
	std::string const Description;
	FString LastCacheFolderUsed;
	FJsonQueriesCache Cache;
	std::shared_ptr<ITwinHttp::FMutex> MutexPtr;
	ITwinHttp::FMutex& Mutex;
	FOnLoadProgressUpdated OnLoadProgressUpdated;
	/// Down from 50K to 32K to accommodate BBoxes in "Combined" metadata, because server reply is capped to 8MB!
	int QueryRowCount = 32000;

	EState State = EState::NotStarted;
	EHttpResponseCodes::Type FirstErrorCode = EHttpResponseCodes::Ok;
	FString FirstErrorString;
	int QueryRowStart = 0, TotalRowsParsed = 0, TotalRowsExpected = -1;
	HttpRequestID CurrentRequestID;

	int NumPageInProgress = 0;
	size_t RequestsFromCache = 0, RequestsFromRemote = 0;
	/// The maximum number of pages that can be in progress (either as an in-flight remote query, or as a worker
	/// thread currently parsing a cached or remote reply), both to avoid overwhelm the server, but also to avoid
	/// spawning as many parsing threads as there are cached pages, which can be a lot for large models.
	int MaxNumPageInProgress = 4;
	bool lastPageReached = false;
	bool bQueryTableCount = true;
};