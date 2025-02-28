/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedules.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <ITwinFwd.h>

#include <functional>
#include <memory>
#include <vector>

#include <ITwinSynchro4DSchedules.generated.h>

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FScheduleQueryingDelegate, bool, bIsRunning);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FScheduleTimeRangeDelegate, FDateTime, StartTime, FDateTime, EndTime);

/// Component of an AITwinIModel handling the Synchro4D schedules for a given iModel: it will query the
/// REST api to compute the animation scripts for all tasks, and store the result for the iTwin's
/// FITwinSynchro4DAnimator component to use for replay.
UCLASS()
class ITWINRUNTIME_API UITwinSynchro4DSchedules : public UActorComponent
{
	GENERATED_BODY()

public:	
	UITwinSynchro4DSchedules();
	UITwinSynchro4DSchedules(bool bDoNotBuildTimelines);
	~UITwinSynchro4DSchedules();

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialMasked = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialTranslucent = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialGlass = nullptr;

	UPROPERTY(Category = "Schedules Querying",
		VisibleAnywhere)
	FString ScheduleId;

	UPROPERTY(Category = "Schedules Querying",
		VisibleAnywhere)
	FString ScheduleName;

	/// Update the remote connection details with the current URL, authorization token, etc. from the outer
	/// iTwin's ServerConnection data
	UFUNCTION(Category = "Schedules Querying",
		CallInEditor,
		BlueprintCallable)
	void UpdateConnection();

	/// Launches asynchronous querying of schedules data for all Elements of the iModel, optionally
	/// restricting to the time range given by QueryAllFromTime and QueryAllUntilTime (unless equal: add at
	/// least one second to QueryAllUntilTime beyond QueryAllFromTime to get a "time point" query)
	UFUNCTION(Category = "Schedules Querying",
		CallInEditor,
		BlueprintCallable)
	void QueryAll();

	/// Clear all previously queried schedules data and reset the remote connection details
	/// \return Whether the component's structures could be reset successfully
	UFUNCTION(Category = "Schedules Querying",
		CallInEditor,
		BlueprintCallable)
	void ResetSchedules();

	/// Launches asynchronous querying of schedule data for an Element and around its assigned tasks,
	/// searching before and after the Element's tasks by a specified time extent (both can be zero).
	/// \param ElementID Hexadecimal or decimal number representing the ElementID, which should be an
	///		uint64 (but Blueprints do not support uint64)
	/// \param MarginFromStart Signed timespan to extend the search period from the start of the first task
	///		involving the specified Element. Be careful, the value is signed, thus a negative timespan means
	///		"before the start ...", a positive one "after the start ..."
	/// \param MarginFromEnd Signed timespan to extend the search period from the end of the last task
	///		involving the specified Element. Be careful, the value is signed, thus a negative timespan means
	///		"before the end ...", a positive one "after the end ..."
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	void QueryAroundElementTasks(FString const ElementID, FTimespan const MarginFromStart,
								 FTimespan const MarginFromEnd);

	/// Launches asynchronous querying of schedule data for a set of Elements
	/// \param Collection of Elements as hexadecimal or decimal number strings representing the ElementIDs,
	///		which should be uint64 (but Blueprints do not support uint64)
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	void QueryElementsTasks(TArray<FString> const& Elements);

	/// Tells whether the whole 4D Schedule is available locally. Until then, the 4D animation cannot be
	/// replayed. Note that the total time range of the project can be known before the whole Schedule is
	/// ready (see OnScheduleTimeRangeKnown)
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool IsAvailable() const;

	/// Called when the time range of the whole Schedule is known, with the StartTime and EndTime passed as
	/// arguments. FDateTime::MinValue() is passed twice when there is on schedule, or no tasks were found
	/// in the schedule.
	/// Never called when bPrefetchAllTasksAndAppearanceProfiles is false.
	/// Known limitation: in case of connection or server failures preventing schedule information from being
	/// retrieved (despite retries), the delegate is never called.
	UPROPERTY(BlueprintAssignable)
	FScheduleTimeRangeDelegate OnScheduleTimeRangeKnown;

	/// Returns the time range of the Schedule, if any and already currently known. Will return FDateRange()
	/// when either there is no schedule, the schedule has zero task, or the task data has not yet been
	/// received.
	/// Use OnScheduleTimeRangeKnown if you'd rather wait and be notified when the final value of the time
	/// range is known.
	//UFUNCTION() <== FDateRange not UFUNCTION-able...
	[[nodiscard]] FDateRange GetDateRange() const;

	/// Called when the status of the Schedule data request process changes: the parameter passed is 'true'
	/// when some more data needs to be requested, or 'false' when all requests have been processed.
	UPROPERTY(BlueprintAssignable)
	FScheduleQueryingDelegate OnScheduleQueryingStatusChanged;

	/// Force redownloading schedules instead of using cached results, and do not write new schedules to
	/// the cache. Does *not* clear the existing cache.
	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	bool bDisableCaching = false;
	/// Clear the persistence cache (for this schedule only)
	UFUNCTION(Category = "Schedules Querying", BlueprintCallable, CallInEditor)
	void ClearCacheOnlyThis();
	/// Clear the persistence cache (for all currently cached schedules of the current Environment)
	UFUNCTION(Category = "Schedules Querying", BlueprintCallable, CallInEditor)
	void ClearCacheAllSchedules();

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	int ScheduleQueriesServerPagination = 10000;

	/// Query all 4D Schedules tasks and appearance profiles at once as soon as the Schedule Id is known for
	/// an iModel. This will vastly speed up querying animation bindings (half the time is typically spared)
	UPROPERTY(Category = "Schedules Querying|Advanced",
		EditAnywhere)
	bool bPrefetchAllTasksAndAppearanceProfiles = true;

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	uint64 ScheduleQueriesMaxElementIDsFilterSize = 500;

	/// Query all 4D Schedules animated Element bindings profiles at once as soon as the Schedule task list
	/// is known for an iModel. This will speed up querying animation bindings, and also avoid unpleasant
	/// transient display states when receiving new Cesium tiles (typically those of finer LOD).
	/// Only relevant when bPrefetchAllTasksAndAppearanceProfiles is true, and also ignored when
	/// bDebugWithDummyTimelines is true.
	UPROPERTY(Category = "Schedules Querying|Advanced",
		EditAnywhere)
	bool bPrefetchAllElementAnimationBindings = true;

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	uint64 ScheduleQueriesMaxTaskIDsFilterSize = 100;

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	uint64 ScheduleQueriesBindingsPagination = 30000;

	/// Use the correct schedules' task but use random appearance profiles (color, opacity and growth
	/// simulations) for visual debugging.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugWithRandomProfiles = false;

	/// Use the correct schedules' animated Elements, but they will all use a same test timeline with
	/// various test appearance profiles occurring in succession in a dummy time range.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugWithDummyTimelines = false;

	/// When not empty, dump the full timelines as a json named like this to the project's Saved folder
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	FString DebugDumpAsJsonAfterQueryAll;

	/// When not empty, persist all queries and their replies (for later replay/simulation) to the indicated
	/// folder inside the project's Saved folder. Note that this mode is now almost useless, since downloads
	/// are usually cached (see bDisableCaching), but two situations can still warrant its use: firstly, when
	/// you want to save the initial requests for an iModel's schedule Id, which is not cache since we need
	/// it before starting to cache (a schedule is identified by its own Id, not its iModel's). And secondly,
	/// for testing purposes, in case you want to record a requests session without "polluting" your cache.
	/// Superceded by DebugSimulateSessionQueries.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	FString DebugRecordSessionQueries;

	/// When not empty, simulate all queries and their replies (for later replay/simulation) using the
	/// persisted query/reply pairs read from the specified subfolder inside the project's Saved folder.
	/// Takes precedence over both normal caching (ie. disables it), and also DebugRecordSessionQueries.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	FString DebugSimulateSessionQueries;

#if WITH_EDITORONLY_DATA
	/// In-editor helper to only request the task for this Element using QueryElementsTasks (enter a decimal
	/// or hexadecimal Element ID here).
	UPROPERTY(Category = "Schedules Test Query",
		EditAnywhere)
	FString QueryOnlyThisElementSchedule;

	/// In-editor helper to request the tasks "around" the time where "QueryOnlyThisElementSchedule" is
	/// participating to its own tasks. Format is DDDDDDDD.HH:MM:SS.SSSSSSSS. Positive values will extend
	/// the time range /before/ the start and /after the end of the tasks involving 
	/// "QueryOnlyThisElementSchedule" (see QueryAroundElementTasks for comparison). Negative values are
	/// still possible, for example if you want the overlapping tasks with a minimum overlap margin.
	UPROPERTY(Category = "Schedules Test Query",
		EditAnywhere)
	FTimespan QueryScheduleBeforeAndAfterElement;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/// In-editor helper to launch the asynchronous querying of partial schedules data for
	/// "QueryOnlyThisElementSchedule", extending the search to elements with tasks happening around the
	/// same time if "QueryScheduleBeforeAndAfterElement" is set.
	UFUNCTION(Category = "Schedules Test Query",
		CallInEditor,
		BlueprintCallable)
	void SendPartialQuery();
#endif // WITH_EDITOR

	/// Restrict the QueryAll action to tasks starting (or ending) at or after this date. Ignored if
	/// QueryAllUntilTime and QueryAllFromTime are strictly equal.
	UPROPERTY(Category = "Schedules Test Query",
		EditAnywhere)
	FDateTime QueryAllFromTime = FDateTime::UtcNow();//see ScheduleTime about UtcNow()

	/// Restrict the QueryAll action to tasks starting (or ending) at or before this date. Ignored if
	/// QueryAllUntilTime and QueryAllFromTime are strictly equal.
	UPROPERTY(Category = "Schedules Test Query",
		EditAnywhere)
	FDateTime QueryAllUntilTime = FDateTime::UtcNow();//see ScheduleTime about UtcNow()

	// Note: local time (Now() insead of UtcNow()) is just not possible because in that case the TZ offset
	// (+0200 for GMT+2) is added in the Outliner field! The variable needs to be UTC it seems, and the
	// Outliner field correctly converts it to local timezone. Which was not obvious from the doc...
	/// Animation replay's current time in UTC time. Default is in the future so that the initial state is the
	/// fully completed project.
	UPROPERTY(Category = "Schedules Replay",
		EditAnywhere,
		BlueprintReadWrite,
		BlueprintGetter = GetScheduleTime,
		BlueprintSetter = SetScheduleTime)
	FDateTime ScheduleTime = FDateTime(2099, 12, 31, 12, 0, 0);
	UFUNCTION(BlueprintGetter)
	FDateTime GetScheduleTime() const;
	UFUNCTION(BlueprintSetter)
	void SetScheduleTime(FDateTime NewScheduleTime);

	/// Animation replay speed, expressed as a period of schedule time per second of replay time (default: one
	/// day per second). Outliner field format is "DAYS.HOURS:MIN:SEC.decimals"
	UPROPERTY(Category = "Schedules Replay",
		EditAnywhere,
		BlueprintReadWrite,
		BlueprintGetter = GetReplaySpeed,
		BlueprintSetter = SetReplaySpeed)
	FTimespan ReplaySpeed = FTimespan::FromDays(1.);
	UFUNCTION(BlueprintGetter)
	FTimespan GetReplaySpeed() const;
	UFUNCTION(BlueprintSetter)
	void SetReplaySpeed(FTimespan NewReplaySpeed);
	
	/// Set the script time to the beginning of the construction schedule
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void JumpToBeginning();

	/// Set the script time to the end of the construction schedule
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void JumpToEnd();

	/// Helper method: determines the schedule's time range (at least the part that has been streamed to us
	/// so far), then determines and sets the script speed so that the whole construction schedule's replay
	/// takes a fixed duration (currently around 30 seconds, plus or minus a small amount of rounding)
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void AutoReplaySpeed();

	/// Start or restart replay of the schedule animation at the current script time and speed
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Play();

	/// Pause replay of the schedule animation, freezing the display at the current script time, whereas
	/// "Stop" would reset the display to disable all scheduling effects.
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Pause();

	/// Stop replay of the schedule animation, staying at the current script time, but resetting the
	/// display to disable all scheduling effects (see "Pause" for the alternative).
	/// Note: whether transformed Elements stay in place or are reset to their initial position is as yet
	/// undefined.
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Stop();

	/// Split applying animation on Elements among subsequent ticks to avoid spending more than this amount
	/// of time each time. Visual update only occurs once the whole iModel (?) has been updated, though.
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	double MaxTimelineUpdateMilliseconds = 50;

	/// Disable application of color highlights on animated Elements
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableColoring = false;

	/// Disable application of partial visibility on animated Elements
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableVisibilities = false;

	/// Disable the cutting planes used to simulate the Elements' "growth" (construction/removal/...)
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableCuttingPlanes = false;

	/// Disable the Elements' transformations in the 4D Schedule (static or following a 3D paths)
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableTransforms = false;

	/// Fade out all non-animated elements, ultimately using partial transparency, but for the moment a
	/// neutral light grey color is used instead. Note that tiles where no animated element is present will
	/// not be affected.
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bFadeOutNonAnimatedElements = false;

	/// Mask out entirely all non-animated elements inside tiles where there is at least one animated Element
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bMaskOutNonAnimatedElements = false;

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
	void TickSchedules(float DeltaTime);
	void OnVisibilityChanged(FITwinSceneTile& SceneTile, bool bVisible);

	// Must be marked UFUNCTION to be bound to a delegate...
	UFUNCTION()
	void LogStatisticsUponQueryLoopStatusChange(bool bQueryLoopIsRunning);
	UFUNCTION()
	void LogStatisticsUponFullScheduleReceived(FDateTime StartTime, FDateTime EndTime);

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
	//! Allows the entire plugin to access the FITwinSynchro4DSchedulesInternals.
	//! Actually, code outside the plugin (ie. "client" code) can also call this function,
	//! but since FITwinSynchro4DSchedulesInternals is defined in the Private folder,
	//! client code cannot do anything with it (because it cannot even include its declaration header).
	friend FITwinSynchro4DSchedulesInternals& GetInternals(UITwinSynchro4DSchedules& Schedules);
	friend FITwinSynchro4DSchedulesInternals const& GetInternals(UITwinSynchro4DSchedules const& Schedules);
};
