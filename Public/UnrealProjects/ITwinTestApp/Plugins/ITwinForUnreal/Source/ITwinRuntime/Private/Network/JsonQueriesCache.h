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
		MaterialMLPrediction,
		ElementsMetadataCombined,
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
		FString const& ContentAsString, bool const bConnectedSuccessfully, bool const bRequestSucceeded,
		ITwinHttp::FMutex& Mutex, int const QueryTimestamp);

public:
	explicit FJsonQueriesCache(UObject const& Owner);
	~FJsonQueriesCache();

	bool IsValid() const;
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

	/// Read a request's reply from the cache, based on the handle returned by one of the LookUp methods
	[[nodiscard]] TSharedPtr<FJsonObject> Read(QueriesCache::FSessionMap::const_iterator const It) const;

	/// Look up the response to an Unreal Http request in the cache. Note: AcceptHeader, ContentType and
	/// custom headers are not taken into account for indexing. When non-empty, pass the resulting
	/// iterator to Read to actually load and parse the response Json.
	/// \return Empty object on cache miss
	[[nodiscard]] std::optional<QueriesCache::FSessionMap::const_iterator> LookUp(
		FHttpRequestPtr const& Request, ITwinHttp::EVerb const Verb, ITwinHttp::FMutex& Mutex) const;

	/// Look up the response to an AdvViz::SDK request in the cache. Note: AcceptHeader, ContentType and
	/// custom headers are not taken into account for indexing. When non-empty, pass the resulting
	/// iterator to Read to actually load and parse the response Json
	/// \return Empty object on cache miss
	[[nodiscard]] std::optional<QueriesCache::FSessionMap::const_iterator> LookUp(
		AdvViz::SDK::ITwinAPIRequestInfo const& RequestInfo, ITwinHttp::FMutex& Mutex) const;

	/// Save the response to an Unreal Http query in the cache
	/// \parameter CompletedRequest Request for which we just obtained a response
	/// \parameter QueryTimestamp Only relevant for bIsRecordingForSimulation. TODO_GCO: Could use RequestID
	///		instead except that it's practical for inspection to have files with "simple" names made from 
	///		integers like "00000004_res_00000002.json" instead of using GUIDs
	void Write(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const Response,
		bool const bConnectedSuccessfully, ITwinHttp::FMutex& Mutex, int const QueryTimestamp = -1);
	void Write(AdvViz::SDK::ITwinAPIRequestInfo const& CompletedRequest,
		FString const& QueryResult, bool const bConnectedSuccessfully, ITwinHttp::FMutex& Mutex,
		int const QueryTimestamp = -1);

	/// Internal use (public unless you know how to befriend a private nested class of a template class...)
	[[nodiscard]] int CurrentTimestamp() const;
	/// Internal use (public unless you know how to befriend a private nested class of a template class...)
	void RecordQuery(FHttpRequestPtr const& Request, ITwinHttp::FMutex& Mutex);
};
