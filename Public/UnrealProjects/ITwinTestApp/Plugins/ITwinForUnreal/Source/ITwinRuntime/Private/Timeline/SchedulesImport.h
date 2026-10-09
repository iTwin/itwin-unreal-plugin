/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesImport.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "SchedulesStructs.h"
#include "TimelineFwd.h"
#include <ITwinFwd.h>

#include <Interfaces/IHttpResponse.h>
#include <Templates/PimplPtr.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/Object.h>

#include <functional>
#include <mutex>
#include <optional>
#include <set>

enum class EITwinEnvironment : uint8;

class FITwinSchedulesImport
{
	friend class FSynchro4DImportTestHelper;

	// For unit testing
	FITwinSchedulesImport(FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
		TStrongObjectPtr<UObject> OwnerUObj, std::recursive_mutex& Mux, std::optional<FITwinSchedule>& Scheds);
	void ResetConnectionForTesting(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
		FString const& InChangesetId, FString const& CacheDir, EITwinSchedulesGeneration ScheduleGeneration);

public:
	FITwinSchedulesImport(UITwinSynchro4DSchedules& Owner, std::recursive_mutex& Mutex,
						  std::optional<FITwinSchedule>& Schedule);
	FITwinSchedulesImport(FITwinSchedulesImport&& InOwner) = delete;
	FITwinSchedulesImport& operator=(FITwinSchedulesImport&& Other);
	FString ToString() const;

	/// Tells whether the connection information was set up and the structure is ready to start querying
	bool IsReadyToQuery() const;
	FString ComputeCacheName() const;
	void BeginShutdown();
	/// When pre-fetching everything, including animation bindings, tells whether everything has been queried
	/// and all replies have been received from the server (including retries, in case of unsuccessful
	/// requests). This doesn't mean all replies were successful: @see HasFetchingErrors.
	/// When NOT pre-fetching, always returns false because we cannot know if/when we have everything.
	bool HasFinishedPrefetching() const;
	bool HasSchedulesListingFailed() const;
	/// When HasFinishedPrefetching() returns true, tells whether there has been an error to any request, ie.
	/// a request that remained unsuccessful, even after the allocated amount of retries.
	bool HasFetchingErrors() const;
	/// When HasFetchingErrors() returns true, returns the description message for the first encountered
	/// error.
	FString FirstFetchingErrorString() const;
	EHttpResponseCodes::Type FirstFetchingErrorCode() const;
	size_t FetchedFromRemote() const;
	size_t FetchedFromCache() const;
	void Uninitialize();
	size_t NumTasks() const;
	void ResetConnection(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
						 FString const& InChangesetId);
	void SetSchedulesImportConnectors(FOnReceivedScheduleStats const& InOnReceivedScheduleStats);
	std::pair<int, int> HandlePendingQueries();
	void DebugProcessScheduleUpdateIncrement();
	// Note: QueryEntireSchedules (includes time range filtering), QueryAroundElementTasks and
	// QueryElementsTasks have been removed as support on APIM required substantial changes - blame here.

#if WITH_TESTS
	bool SimulateScheduleForTest(FSimulatedScheduleOptions const& Options);
#endif // WITH_TESTS

private:
	UITwinSynchro4DSchedules* Owner;///< Never nullptr, not a ref because of move-assignment op
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
