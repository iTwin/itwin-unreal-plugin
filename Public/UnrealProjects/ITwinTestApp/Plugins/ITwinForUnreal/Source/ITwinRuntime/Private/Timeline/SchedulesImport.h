/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesImport.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "SchedulesStructs.h"
#include <ITwinFwd.h>
#include <Templates/PimplPtr.h>

#include <functional>
#include <mutex>
#include <vector>

constexpr uint16_t SimultaneousRequestsAllowed = 8;

class FITwinSchedulesImport
{
public:
	FITwinSchedulesImport(UITwinSynchro4DSchedules const& Owner, std::recursive_mutex& Mutex,
						  std::vector<FITwinSchedule>& Schedules);
	FITwinSchedulesImport(FITwinSchedulesImport&& InOwner) = delete;
	FITwinSchedulesImport& operator=(FITwinSchedulesImport&& Other);

	bool IsReady() const;
	void ResetConnection(TObjectPtr<AITwinServerConnection> const ServerConnection,
						 FString const& ITwinAkaProjectAkaContextId, FString const& IModelId);
	void SetOnAnimationBindingAdded(FOnAnimationBindingAdded const& InOnAnimBindingAdded);
	std::pair<int, int> HandlePendingQueries();
	/// \param FromTime Restrict the query to tasks starting (or ending) at or after this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	/// \param UntilTime Restrict the query to tasks starting (or ending) at or before this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	void QueryEntireSchedules(FDateTime const FromTime = {}, FDateTime const UntilTime = {},
							  std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});
	void QueryAroundElementTasks(ITwinElementID const ElementID, FTimespan const MarginFromStart,
		FTimespan const MarginFromEnd, std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});
	/// \param FromTime Restrict the query to tasks starting (or ending) at or after this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	/// \param UntilTime Restrict the query to tasks starting (or ending) at or before this date. Ignored if
	///		UntilTime and FromTime are strictly equal (eg. both default constructed).
	void QueryElementsTasks(std::vector<ITwinElementID>&& ElementIDs, FDateTime const FromTime = {},
		FDateTime const UntilTime = {}, std::function<void(bool/*success*/)>&& OnQueriesCompleted = {});

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
	UITwinSynchro4DSchedules const* Owner;///< Never nullptr, not a ref because of move-assignment op
};
