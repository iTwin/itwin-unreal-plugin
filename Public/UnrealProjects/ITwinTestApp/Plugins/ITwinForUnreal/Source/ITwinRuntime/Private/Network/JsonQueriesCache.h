/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCache.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "HttpUtils.h"
#include "JsonQueriesCacheTypes.h"
#include <ITwinHttpUtils.h>

#include <Dom/JsonObject.h>
#include <Interfaces/IHttpRequest.h>
#include <Templates/PimplPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

namespace AdvViz::SDK { struct ITwinAPIRequestInfo; }

enum class EITwinEnvironment : uint8;

namespace QueriesCache
{
	enum class ESubtype : uint8_t
	{
		Schedules,
		DEPRECATED_ElementsHierarchies,
		DEPRECATED_ElementsSourceIDs,
		ElementsMetadataCombined,
		ElementsMetadataNoBBoxes,
		ElementsMetadataBBoxes,
		ConstructionDetailing,
	};

	/// \param ITwinId If empty, the base folder for all caches of the passed Type is returned. IModelId,
	///		ChangesetId and ExtraStr are thus ignored.
	/// \param ChangesetId May be empty in the special case of an iModel without a changeset
	/// \param ExtraStr For schedules, you must pass a non-empty schedule Id to get the cache folder for this
	///		specific schedule
	[[nodiscard]] FString GetCacheFolder(ESubtype const Type, EITwinEnvironment const Environment,
		FString const& ITwinId, FString const& IModelId, FString const& ChangesetId,
		FString const& ExtraStr = {});

} // namespace QueriesCache

/// Cache for requests getting replies as json objects. Default-constructed as uninitialized, use
/// Initialize to set the folder from which to load the available cache entries and into which
/// new entries can be recorded.
///
/// A disk size limit (default 2GB) applies to all caches of a given ServerEnvironment (QA/PROD/DEV).
/// Cache cleaning only happens when initializing or releasing a cache, to avoid synchronizing on all
/// Read/Write operations.
///
/// Thread-safety: Read/Write operations are synchronized using a user-supplied mutex, which thus only
/// protects against concurrent operations on the same cache instance. Synchronization of operations
/// using all caches (like LRU-cleaning) is done using an internal mutex, only in the "Initialize" and
/// destructor methods, so that it does not affect I/O operations of cache instances currently in use.
///
/// TODO_GCO: only GET and POST requests are supported at the moment.
class FJsonQueriesCache
{
	class FImpl;
	TPimplPtr<FImpl> Impl;

	void ToJson(FHttpRequestPtr const& Req, TSharedRef<FJsonObject>& JsonObj) const;
	void ToJson(AdvViz::SDK::ITwinAPIRequestInfo const& Req, TSharedRef<FJsonObject>& JsonObj) const;
	void Write(TSharedRef<FJsonObject>& JsonObj, int const ResponseCode,
		FString const& ContentAsString, ITwinHttp::ConnectionSuccess fConnectedSuccessfully,
		bool const bRequestSucceeded, int const QueryTimestamp);
	bool IsValid() const;

public:
	explicit FJsonQueriesCache(UObject const& Owner, ITwinHttp::FMutex& Mutex);
	~FJsonQueriesCache();

	bool IsUnitTesting() const;
	/// Actually initializes the cache for your "session"
	[[nodiscard]] bool Initialize(FString CacheFolder, EITwinEnvironment const Environment,
		FString const& DisplayName, bool const bIsRecordingForSimulation = false,
		bool const bUnitTesting = false);
	/// Reset to uninitialized state (as if just default-constructed), clearing memory in the process
	/// (but not the disk folder! see ClearFromDisk). Must be called by an owner UObject when it is about to
	/// become garbage-collectable, for example in AActor::EndPlay, because when the owner's destructor will
	/// be called, it may be too late already to use the static(!) data in the cache implementation details.
	void Uninitialize();
	[[nodiscard]] bool LoadSessionSimulation(FString const& SimulateFromFolder);
	/// Deletes the filesystem folder containing the cache data
	void ClearFromDisk();
	FString CacheFolder() const;

	/// Read a request's reply from the cache, based on the filepath returned by one of the LookUp methods
	static [[nodiscard]] TSharedPtr<FJsonObject> Read(FString&& CacheEntryPath);

	/// Look up the response to an Unreal Http request in the cache. Note: AcceptHeader, ContentType and
	/// custom headers are not taken into account for indexing. When non-empty, pass the resulting
	/// file path to Read to actually load and parse the response Json.
	/// \param EraseParameter Redact a parameter from the passed Request's URL before looking up the request
	///		(used for "$deltaToken"). This will NOT erase the parameter from the URL of the cache entries, so
	///		that those will never be returned by this method when using this parameter.
	///		Search is case-sensitive.
	/// \return Empty object on cache miss
	[[nodiscard]] std::optional<FString> LookUp(FHttpRequestPtr const& Request, ITwinHttp::EVerb const Verb,
		bool bUnsetDroppableFlagOnHit, std::optional<FString> EraseParameter = {}) const;
	/// Same as above, but with the caller already holding a lock on the cache's mutex, which is required for the
	/// returned iterator to remain valid until the caller releases the lock.
	[[nodiscard]] std::optional<QueriesCache::FCacheMap::const_iterator> LookUp(ITwinHttp::FLock& Lock,
		FHttpRequestPtr const& Request, ITwinHttp::EVerb const Verb, bool bUnsetDroppableFlagOnHit,
		std::optional<FString> EraseParameter = {}) const;

	/// Look up the response to an AdvViz::SDK request in the cache. Note: AcceptHeader, ContentType and
	/// custom headers are not taken into account for indexing. When non-empty, pass the resulting
	/// file path to Read to actually load and parse the response Json.
	/// \return Empty object on cache miss
	[[nodiscard]] std::optional<FString> LookUp(AdvViz::SDK::ITwinAPIRequestInfo const& RequestInfo) const;

	/// Save the response to an Unreal Http query in the cache
	/// \parameter CompletedRequest Request for which we just obtained a response
	/// \parameter QueryTimestamp Only relevant for bIsRecordingForSimulation. TODO_GCO: Could use RequestID
	///		instead except that it's practical for inspection to have files with "simple" names made from 
	///		integers like "00000004_res_00000002.json" instead of using GUIDs
	void Write(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const Response,
		ITwinHttp::ConnectionSuccess bConnectedSuccessfully, int const QueryTimestamp = -1);
	void Write(AdvViz::SDK::ITwinAPIRequestInfo const& CompletedRequest, FString const& QueryResult,
		ITwinHttp::ConnectionSuccess bConnectedSuccessfully, int const QueryTimestamp = -1);

	/// \see FReusableJsonQueries::OnDeltaTokenExpired
	/// \param bMarkSimilarAsDroppable When true, will traverse the whole cache and mark 'droppable' ALL entries
	///		sharing the same URL and parameters *after* removing any continuationToken parameter. It would have been
	///		better to rely on 'nextPageToken' to rebuild the actual chaining, but it would have meant parsing a
	///		possibly large number of multi-MB replies (or at least reading the files to memory, even if actually
	///		parsing the JSON structure were skipped by simply grepping for the token.
	///		Only intended and to be used with GET requests.
	/// \param SimilarIgnoreParam URL parameter allowed to differ when looking for "similar" cache entries
	/// \return Number of entries marked droppable, if bMarkSimilarAsDroppable is true. Zero otherwise.
	size_t EraseEntry(QueriesCache::FCacheMap::const_iterator Entry, bool bMarkSimilarAsDroppable,
		ITwinHttp::FLock& Lock, FString const& SimilarIgnoreParam = {}, int32 SimilarParamMaxLengthHint = -1);

	/// \see FReusableJsonQueries::OnDeltaTokenExpired
	/// \return Number of entries erased.
	size_t EraseDroppableEntries();

	/// Internal use (public unless you know how to befriend a private nested class of a template class...)
	[[nodiscard]] int CurrentTimestamp() const;
	/// Internal use (public unless you know how to befriend a private nested class of a template class...)
	void RecordQuery(FHttpRequestPtr const& Request);
};
