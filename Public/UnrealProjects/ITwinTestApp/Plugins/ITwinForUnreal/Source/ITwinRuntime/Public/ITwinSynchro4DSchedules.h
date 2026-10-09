/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedules.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <Interfaces/IHttpResponse.h>
#include <ITwinFwd.h>
#include <ITwinIModelSettings.h> // for EITwin4DGlTFTranslucencyRule enum (temp?)

#include <functional>
#include <memory>
#include <vector>

#if WITH_TESTS
	#include <ITwinRuntime/Private/Timeline/TimeInSeconds.h>
	#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
		#include <BeHeaders/Util/OptionsClass.h>
	#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>
#endif // WITH_TESTS

#include <ITwinSynchro4DSchedules.generated.h>

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FScheduleQueryingDelegate, bool, bIsRunning);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FScheduleTimeRangeDelegate, FDateTime, StartTime, FDateTime, EndTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnScheduleInformationReceived, AITwinIModel*, IModel, FString, ScheduleId, FString, ScheduleName);

/// Should be irrelevant ultimately but still makes a difference for animation bindings pagination size and for
/// naming the schedule cache folders and files. Also, incremental schedule updates are only supported for NextGen
/// schedules.
UENUM()
enum class EITwinSchedulesGeneration : uint8
{
	Legacy,
	NextGen,
	Unknown
};

#if WITH_TESTS
	OPTIONS_CLASS_START(FSimulatedScheduleOptions, ITWINRUNTIME_API)
		OPTIONS_CLASS_ADD_MEMBER(bool, WithListingError, false)
		OPTIONS_CLASS_ADD_MEMBER(bool, WithQueryError, false)
		OPTIONS_CLASS_ADD_MEMBER(FTimeRangeInSeconds, TimeRange, ITwin::Time::Undefined())
		OPTIONS_CLASS_ADD_MEMBER(bool, IsAvailable, false)
		OPTIONS_CLASS_ADD_MEMBER(bool, NotifyScheduleId, false)
		OPTIONS_CLASS_ADD_MEMBER(bool, NotifyTimerange, false)
		OPTIONS_CLASS_ADD_MEMBER(bool, NotifyQueryingStopped, false)
		OPTIONS_CLASS_ADD_MEMBER(TCHAR, DigitSuffix, TCHAR('0'))
	OPTIONS_CLASS_END
#endif // WITH_TESTS

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
	UMaterialInterface* BaseMaterialTranslucent_TwoSided = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialGlass = nullptr;

	UPROPERTY(Category = "Schedules Querying",
		EditAnywhere)
	FString ScheduleId;

	UPROPERTY(Category = "Schedules Querying",
		VisibleAnywhere)
	FString ScheduleName;

	/// When both a Legacy and a NextGen schedules are available for this component's iModel actor,
	/// we cannot use both so we have to choose.
	UPROPERTY(Category = "Schedules Querying",
		EditAnywhere)
	bool bFavorNextGenSchedule = false;

	/// Generation of this schedule: only valid when the schedule Id is. "Legacy" schedule means created with
	/// SYNCHRO Pro, ie. a schedule version of 6.X, whereas "NextGen" means created in SYNCHRO+ (version > 10).
	UPROPERTY(Category = "Schedules Querying",
		VisibleAnywhere)
	EITwinSchedulesGeneration ScheduleGeneration = EITwinSchedulesGeneration::Unknown;

	/// Update the remote connection details with the current URL, authorization token, etc. from the outer
	/// iTwin's ServerConnection data
	UFUNCTION(Category = "Schedules Querying",
		CallInEditor,
		BlueprintCallable)
	void UpdateConnection();

	/// Clear all previously queried schedules data and reset the remote connection details
	/// \return Whether the component's structures could be reset successfully
	UFUNCTION(Category = "Schedules Querying",
		CallInEditor,
		BlueprintCallable)
	void ResetSchedules();

	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool HasValidId() const;

	/// Tells whether the whole 4D Schedule is available locally. Until then, the 4D animation cannot be replayed.
	/// Note that the total time range of the project can be known before the whole Schedule is
	/// ready (see OnScheduleTimeRangeKnown)
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool IsAvailable() const;

	/// Returns true if a 4D Schedule is available as a "next-gen" schedule (version >= 10).
	/// @see See also bFavorNextGenSchedule
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool IsAvailableAsNextGenSchedule() const;

	/// Tells whether merely listing the schedules has failed, ie. the lack of schedule Id does not necessarily mean
	/// that the owner iModel has no schedule.
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool SchedulesListingFailed() const;

	/// When IsAvailable() returns false, tells whether there has been an error to any request, ie.
	/// a request that remained unsuccessful, even after the allocated amount of retries.
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool HasFetchingErrors() const;

	/// When HasFetchingErrors() returns true, tells whether there has been an error specifically to a
	/// 4D API request (and not to an iModel metadata request).
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	bool Has4DAPIFetchingErrors() const;

	/// When SchedulesListingFailed() or HasFetchingErrors() returns true, returns the description message for
	/// the first encountered error
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	FString FirstRequestErrorString() const;

	/// @deprecated Use FirstRequestErrorString()
	UFUNCTION(Category = "Schedules Querying",
		BlueprintCallable)
	FString FirstFetchingErrorString() const;

	EHttpResponseCodes::Type FirstRequestErrorCode() const;

	/// Percentage of the data needed to replay a 4D schedule (if any) that was loaded directly from the local caches.
	/// Only available once Synchro4DSchedules->IsAvailable() returns true, otherwise returns 0.
	double PercentageLoadedFromCache() const;

	/// Called as soon as we have determined whether a Schedule is available for this component's owner iModel,
	/// which can be substantially earlier than OnScheduleTimeRangeKnown (which requires the list of tasks to be
	/// known). The schedule's name and Id are passed even if the schedule actually has no animation (eg. no tasks
	/// or no animation bindings). Empty strings are passed when no schedule was found for the iModel.
	UPROPERTY(BlueprintAssignable)
	FOnScheduleInformationReceived OnScheduleInformationReceived;

	/// Called when the time range of the whole Schedule is known, with the StartTime and EndTime passed as
	/// arguments. FDateTime::MinValue() is passed twice when there is on schedule, or no tasks were found
	/// in the schedule.
	UPROPERTY(BlueprintAssignable)
	FScheduleTimeRangeDelegate OnScheduleTimeRangeKnown;

	/// Returns the time range of the Schedule, if any and already currently known. Will return FDateRange()
	/// when either there is no schedule, the schedule has zero task, or the task data has not yet been
	/// received.
	/// Use OnScheduleTimeRangeKnown if you'd rather wait and be notified when the final value of the time
	/// range is known.
	//UFUNCTION() <== FDateRange not UFUNCTION-able...
	[[nodiscard]] FDateRange GetDateRange() const;
	/// Returns the planned start time of the Schedule, if any and already currently known. Will return
	/// FDateTime() when either there is no schedule, the schedule has zero task, or the task data has not
	/// yet been received.
	/// Use OnScheduleTimeRangeKnown if you'd rather wait and be notified when the final value of the time
	/// range is known.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	FDateTime GetPlannedStartDate() const;
	/// Returns the planned end time of the Schedule, if any and already currently known. Will return
	/// FDateTime() when either there is no schedule, the schedule has zero task, or the task data has not
	/// yet been received.
	/// Use OnScheduleTimeRangeKnown if you'd rather wait and be notified when the final value of the time
	/// range is known.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	FDateTime GetPlannedEndDate() const;

	/// Called when the status of the Schedule data request process changes: the parameter passed is 'true'
	/// when some more data needs to be requested, or 'false' when all requests have been processed.
	UPROPERTY(BlueprintAssignable)
	FScheduleQueryingDelegate OnScheduleQueryingStatusChanged;

	/// Force redownloading schedules instead of using cached results, and do not write new schedules to
	/// the cache. Does *not* clear the existing cache.
	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	bool bDisableCaching = false;
	/// Clear the persistence cache (for this schedule only). See also the equivalent function on AITwinIModel, for the
	/// Elements metadata cache which the schedule relies on for interpretation.
	/// @return True when the schedule was able to delete its cache
	UFUNCTION(Category = "Schedules Querying", BlueprintCallable, CallInEditor)
	bool ClearCacheWithConfirmation();
	/// Same as ClearCacheWithConfirmation, but no return value in order to have a button in the Editor
	UFUNCTION(Category = "Schedules Querying", BlueprintCallable, CallInEditor)
	void ClearCacheOnlyThis();

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	int ScheduleQueriesServerPagination = 10000;

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	uint64 ScheduleQueriesBindingsPagination = 50000;

	UPROPERTY(Category = "Schedules Querying|Advanced", EditAnywhere)
	uint64 IModelDataQueriesPagination = 32000;

	/// For Next-gen schedules, when true, do not emit queries for the incremental updates. When toggling this off,
	/// delta requests that may be in flight will be processed and the raw schedule data will be updated, but the
	/// changes will not be saved to the cached json and the 4D animation timelines will not be rebuilt.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugFreezeIncrementalScheduleUpdates = false;

	/// If bDebugFreezeIncrementalScheduleUpdates=true, this will process a single incremental update (if any) and
	/// then freeze again.
	UFUNCTION(Category = "Schedules Querying|Debug", CallInEditor)
	void DebugProcessScheduleUpdateIncrement();

	/// Use the correct schedules' task but use random appearance profiles (color, opacity and growth
	/// simulations) for visual debugging.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugWithRandomProfiles = false;

	/// Log information about the currently selected Element's 4D animation properties as applied by the
	/// '4D animator' class.
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugSelectedElemAnim = false;

	/// When not empty, dump the full timelines as a json named like this to the project's Saved folder
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	FString DebugDumpAsJsonAfterQueryAll;

	/// When using DebugDumpAsJsonAfterQueryAll, the timeline dump will contain human-readable times (with a precision
	/// limited to 1s) when this is true, or time ticks from FDateTime::GetTicks() when false
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	bool bDebugDumpUseHumanReadableTimes = true;

	/// When using DebugDumpAsJsonAfterQueryAll, the timeline dump will have the numerical values limited to this
	/// number of decimals (unless -1 = unlimited)
	UPROPERTY(Category = "Schedules Querying|Debug",
		EditAnywhere)
	int DebugDumpLimitDecimals = -1;

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

	UPROPERTY(Category = "Schedules Replay", meta = (DisplayName = "Translucent Mesh Grouping"),
		EditAnywhere)
	EITwin4DGlTFTranslucencyRule GlTFTranslucencyRule = EITwin4DGlTFTranslucencyRule::Unlimited;

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
	/// takes a fixed duration (Default is 30 seconds)
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void AutoReplaySpeed(int ReplayPlayTime = 30);

	/// Start or restart replay of the schedule animation at the current script time and speed
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Play();

	UFUNCTION(Category = "Schedules Replay",
		BlueprintCallable)
	bool IsPlaying() const;

	/// Pause replay of the schedule animation, freezing the display at the current script time, whereas
	/// "Stop" would reset the display to disable all scheduling effects.
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Pause();

	UFUNCTION(Category = "Schedules Replay",
		BlueprintCallable)
	bool IsPaused() const;

	/// Stop replay of the schedule animation, staying at the current script time, but resetting the
	/// display to disable all 4D animation effects (see "Pause" for the alternative).
	/// Note: whether transformed Elements stay in place or are reset to their initial position is as yet
	/// undefined.
	UFUNCTION(Category = "Schedules Replay",
		CallInEditor,
		BlueprintCallable)
	void Stop();

	UFUNCTION(Category = "Schedules Replay",
		BlueprintCallable)
	bool IsStopped() const;

	/// Split applying animation on Elements among subsequent ticks to avoid spending more than this amount
	/// of time each time. Visual update only occurs once the whole iModel (?) has been updated, though.
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	double MaxTimelineUpdateMilliseconds = 50;

	/// Disable application of color highlights on animated Elements
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableColoring = false;

	/// Disable application of visibilities on animated Elements: this includes partial visibilities,
	/// but also:
	///  - all automatic hiding of Elements depending on their task action and current time, like Neutral
	///		task Elements (hidden during the whole schedule), Install task Elements before task begins,
	///		Remove task Elements after task has ended, Temporary tasks Elements before and after the task's
	///		time range.
	///  - all de-facto hiding of Elements subject to Growth Simulation when the current position of the
	///		clipping plane would hide them entirely.
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisableVisibilities = false;

	/// Disable application of partial visibility (only) on animated Elements
	UPROPERTY(Category = "Schedules Replay|Settings", EditAnywhere)
	bool bDisablePartialVisibilities = false;

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
	void TickSchedules(float DeltaSeconds);
	void OnVisibilityChanged(const TITwinSceneTilePtr& SceneTilePtr, bool bVisible);
	void OnQueryLoopStatusChange(bool bQueryLoopIsRunning, bool logFullScheduleStats = true);

	UFUNCTION()
	void LogStatisticsUponFullScheduleReceived(FDateTime StartTime, FDateTime EndTime);

#if WITH_TESTS
	bool SimulateScheduleForTest(FSimulatedScheduleOptions const& Options);
#endif // WITH_TESTS

	// For debugging, passing opaque FITwinSceneTile pointer.
	void ResetAnimationInTile(void* SceneTile);

	/// <summary>
	///  manage dynamic shadows for animated meshes
	/// </summary>
	/// <param name="bDynamic"></param>
	void SetMeshesDynamicShadows(bool bDynamic);

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
