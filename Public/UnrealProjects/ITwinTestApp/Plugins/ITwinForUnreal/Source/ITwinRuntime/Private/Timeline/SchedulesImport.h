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

#include <Templates/PimplPtr.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/Object.h>

#include <functional>
#include <mutex>
#include <set>
#include <vector>

enum class EITwinEnvironment : uint8;

class FITwinSchedulesImport
{
	friend class FSynchro4DImportTestHelper;

	// For unit testing
	FITwinSchedulesImport(FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
		TStrongObjectPtr<UObject> OwnerUObj, std::recursive_mutex& Mux, std::vector<FITwinSchedule>& Scheds);
	void ResetConnectionForTesting(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
								   FString const& InChangesetId, FString const& CustomCacheDir);

public:
	FITwinSchedulesImport(UITwinSynchro4DSchedules& Owner, std::recursive_mutex& Mutex,
						  std::vector<FITwinSchedule>& Schedules);
	FITwinSchedulesImport(FITwinSchedulesImport&& InOwner) = delete;
	FITwinSchedulesImport& operator=(FITwinSchedulesImport&& Other);
	FString ToString() const;

	/// Tells whether the connection information was set up and the structure is ready to start querying
	bool IsReadyToQuery() const;
	/// When pre-fetching everything, including animation bindings, tells whether everything has been queried
	/// and all replies have been received from the server (including retries, in case of unsuccessful
	/// requests). This doesn't mean all replies were successful: @see HasFetchingErrors.
	/// When NOT pre-fetching, always returns false because we cannot know if/when we have everything.
	bool HasFinishedPrefetching() const;
	/// When HasFinishedPrefetching() returns true, tells whether there has been an error to any request, ie.
	/// a request that remained unsuccessful, even after the allocated amount of retries.
	bool HasFetchingErrors() const;
	/// When HasFetchingErrors() returns true, returns the description message for the first encountered
	/// error.
	FString FirstFetchingErrorString() const;
	void UninitializeCache();
	size_t NumTasks() const;
	void ResetConnection(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
						 FString const& InChangesetId);
	void SetSchedulesImportConnectors(FOnAnimationBindingAdded const& InOnAnimBindingAdded,
									  FOnAnimationGroupModified const& InOnAnimationGroupModified,
									  FFindElementIDFromGUID const& InFncElementIDFromGUID);
	std::pair<int, int> HandlePendingQueries();
	/// \param FromTime Restrict the query to tasks starting (or ending) at or after this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	/// \param UntilTime Restrict the query to tasks starting (or ending) at or before this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	void QueryEntireSchedules(FDateTime const FromTime = {}, FDateTime const UntilTime = {},
							  std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});
	void QueryAroundElementTasks(ITwinElementID const ElementID, FTimespan const MarginFromStart,
		FTimespan const MarginFromEnd, std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});
	/// \param ElementIDs Collection of Elements to query. Empty when returning from this method.
	/// \param FromTime Restrict the query to tasks starting (or ending) at or after this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	/// \param UntilTime Restrict the query to tasks starting (or ending) at or before this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	void QueryElementsTasks(std::set<ITwinElementID>& ElementIDs, FDateTime const FromTime = {},
		FDateTime const UntilTime = {}, std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});

private:
	UITwinSynchro4DSchedules* Owner;///< Never nullptr, not a ref because of move-assignment op
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
