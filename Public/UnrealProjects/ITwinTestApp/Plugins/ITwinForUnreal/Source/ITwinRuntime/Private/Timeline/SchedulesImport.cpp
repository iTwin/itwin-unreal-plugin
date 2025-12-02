/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesImport.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesImport.h"
#include "SchedulesStructs.h"
#include "TimeInSeconds.h"
#include "Timeline.h"
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex
#include <Network/JsonQueriesCache.h>
#include <Network/ReusableJsonQueries.h>

#include <Dom/JsonObject.h>
#include <HttpModule.h>
#include <Input/Reply.h>
#include <Logging/LogMacros.h>
#include <Math/UnrealMathUtility.h>
#include <Math/Vector.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <ranges>
#include <vector>
#include <unordered_map>
#include <unordered_set>

DECLARE_LOG_CATEGORY_EXTERN(ITwin4DImp, Log, All);
DEFINE_LOG_CATEGORY(ITwin4DImp);
#define S4D_VERBOSE(FORMAT, ...) UE_LOG(ITwin4DImp, Verbose, FORMAT, ##__VA_ARGS__)
#define S4D_LOG(FORMAT, ...) UE_LOG(ITwin4DImp, Display, FORMAT, ##__VA_ARGS__)
// Note: use BE_LOGE/BE_LOGW for errors/warnings!

namespace ITwin_TestOverrides
{
	// See comment on declaration in SchedulesConstants.h
	int RequestPagination = -1;
	int BindingsRequestPagination = -1;
	int64_t MaxElementIDsFilterSize = -1;
}

constexpr bool s_bDebugNoPartialTransparencies = false;
constexpr bool s_bDebugForcePartialTransparencies = false; // will extract EVERYTHING! SLOW!!

namespace APIParams
{
	static const FString PageSizeAPIM("$top");
	static const FString PageSizeES("pageSize");
	static const FString PageTokenAPIM("$continuationToken");
	static const FString PageTokenES("pageToken");
}

class FITwinSchedulesImport::FImpl
{
private:
	using FLock = ITwinHttp::FLock;
	friend class FITwinSchedulesImport;

	FITwinSchedulesImport const* Owner;
	FOnAnimationBindingAdded OnAnimationBindingAdded =
		[](FITwinSchedule const&, size_t const/*AnimIdx*/, FLock&) {};
	FOnAnimationGroupModified OnAnimationGroupModified =
		[](size_t const/*GroupIdx*/, FElementsGroup const&/*GroupElements*/, FLock&) {};
	FFindElementIDFromGUID FncElementIDFromGUID = [](FGuid const&, ITwinElementID& OutElem) {
		OutElem = ITwin::NOT_ELEMENT; return false; };
	ITwinHttp::FMutex& Mutex;///< TODO_GCO: use a per-Schedule mutex?
	bool bUseAPIM = false; ///< MUST be before XXXPagination members below! (see ctors)
	const int RequestPagination;///< pageSize for paginated requests EXCEPT animation bindings
	/*const <= NextGen hack*/ int BindingsRequestPagination;///< pageSize for paginated *animation bindings* requests
	/// When passing a collection of ElementIDs to filter a request, we need to cap the size for performance
	/// reasons. Julius suggested to cap to 1000 on the server.
	const size_t MaxElementIDsFilterSize;
	bool bHasFinishedPrefetching = false;
	bool bHasFetchingErrors = false;
	FString FirstFetchingError;
	std::pair<int, int> LastDisplayedQueueSizeIncrements = { -1, -1 };
	std::pair<int, int> LastRoundedQueueSize = { -1, -1 };
	double LastCheckTotalBindings = 0.;
	size_t LastTotalBindingsFound = 0;
	int SchedApiSession = -1;
	static int s_NextSchedApiSession;
	FString ITwinId, TargetedIModelId, ChangesetId; ///< Set in FITwinSchedulesImport::ResetConnection
	/// "Unknown" also means "Not needed", when used with APIM, which hides this detail from us.
	EITwinSchedulesGeneration SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
	std::vector<FITwinSchedule>& Schedules;
	struct FUnitTesting
	{
		FString BaseUrl;
		FITwinScheduleTimeline& MainTimeline;
		TStrongObjectPtr<UObject> OwnerUObject;
		EITwinEnvironment Environment = EITwinEnvironment::Invalid;
	};
	std::optional<FUnitTesting> UnitTesting;

	/// KEEP LAST, so that it is deleted first, because it's dtor waits on async tasks that use some of the
	/// above members (mostly Schedules)
	TPimplPtr<FReusableJsonQueries> Queries;

	static int CheckPagination(int Pagination, FString const& PaginationSetting)
	{
		// 50K is the true limit on server: until now I had used 30K with ES-API because of "gateway timeout"
		// errors, but APIM seems to work well with 50K (well, only for Legacy for the moment...)
		int const MaxPagination = 50'000;
		if (Pagination > MaxPagination)
		{
			BE_LOGW("ITwin4DImp", "Capping " << TCHAR_TO_UTF8(*PaginationSetting) << " to "
				<< MaxPagination << " iof. " << Pagination << " because of 4D API internal limit");
			Pagination = MaxPagination;
		}
		return Pagination;
	}

public:
	FImpl(FITwinSchedulesImport const& InOwner, ITwinHttp::FMutex& InMutex,
			std::vector<FITwinSchedule>& InSchedules)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, bUseAPIM(Owner->Owner->bStream4DFromAPIM && !Owner->Owner->bDebugWithDummyTimelines)
		, RequestPagination(CheckPagination(
			ITwin_TestOverrides::RequestPagination > 0
				? ITwin_TestOverrides::RequestPagination : InOwner.Owner->ScheduleQueriesServerPagination,
			TEXT("ScheduleQueriesServerPagination")))
		, BindingsRequestPagination(CheckPagination(
			ITwin_TestOverrides::BindingsRequestPagination > 0
				? ITwin_TestOverrides::BindingsRequestPagination
				: InOwner.Owner->ScheduleQueriesBindingsPagination,
			TEXT("ScheduleQueriesBindingsPagination")))
		, MaxElementIDsFilterSize(ITwin_TestOverrides::MaxElementIDsFilterSize > 0
			? (size_t)ITwin_TestOverrides::MaxElementIDsFilterSize
			: InOwner.Owner->ScheduleQueriesMaxElementIDsFilterSize)
		//, SchedApiSession(s_NextSchedApiSession++) <== (re-)init by each call to ResetConnection
		, Schedules(InSchedules)
	{
	}
	// For unit tests
	FImpl(FITwinSchedulesImport const& InOwner, FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
		TStrongObjectPtr<UObject> OwnerUObj, ITwinHttp::FMutex& InMutex, std::vector<FITwinSchedule>& Scheds)
		: Owner(&InOwner)
		, Mutex(InMutex)
		// non-APIM URLs have "bentley.com/4dschedule" or "bentley.com/api" ...
		, bUseAPIM(BaseUrl.Contains(TEXT("api.bentley.com/schedules")))
		, RequestPagination(CheckPagination(ITwin_TestOverrides::RequestPagination,
											TEXT("ScheduleQueriesServerPagination")))
		, BindingsRequestPagination(CheckPagination(ITwin_TestOverrides::BindingsRequestPagination,
													TEXT("ScheduleQueriesBindingsPagination")))
		, MaxElementIDsFilterSize(ITwin_TestOverrides::MaxElementIDsFilterSize > 0
			? (size_t)ITwin_TestOverrides::MaxElementIDsFilterSize
			: 0)
		, Schedules(Scheds)
		, UnitTesting(FUnitTesting{ BaseUrl, MainTimeline, OwnerUObj,
			BaseUrl.StartsWith(TEXT("https://qa-")) ? EITwinEnvironment::QA
			: (BaseUrl.StartsWith(TEXT("https://dev-")) ? EITwinEnvironment::Dev : EITwinEnvironment::Prod) })
	{
		ensure(RequestPagination >= 0 && BindingsRequestPagination >= 0 && MaxElementIDsFilterSize > 0);
	}
	FImpl(FImpl const&) = delete;
	FImpl& operator=(FImpl const&) = delete;

	void ResetConnection(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
						 FString const& InChangesetId, FString const& CustomCacheDir);
	void SetSchedulesImportConnectors(FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
									  FOnAnimationGroupModified const& InOnAnimationGroupModified,
									  FFindElementIDFromGUID const& FncElementIDFromGUID);
	std::pair<int, int> HandlePendingQueries();
	void QueryEntireSchedules(FDateTime const FromTime, FDateTime const UntilTime,
							  std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryAroundElementTasks(ITwinElementID const ElementID, FTimespan const MarginFromStart,
		FTimespan const MarginFromEnd, std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryElementsTasks(std::set<ITwinElementID>&& ElementIDs, FDateTime const FromTime,
		FDateTime const UntilTime, std::function<void(bool/*success*/)>&& OnQueriesCompleted);

private:
	UITwinSynchro4DSchedules const& SchedulesComponent() const { return *Owner->Owner; }
	UITwinSynchro4DSchedules& SchedulesComponent() { return *Owner->Owner; }
	TObjectPtr<AITwinServerConnection> const& GetServerConnection() const
		{ return Cast<AITwinIModel>(SchedulesComponent().GetOwner())->ServerConnection; }
	FITwinSynchro4DSchedulesInternals const& SchedulesInternals() const
		{ return GetInternals(SchedulesComponent()); }
	FITwinSynchro4DSchedulesInternals& SchedulesInternals() { return GetInternals(SchedulesComponent()); }
	void SetScheduleTimeRangeIsKnown();
	bool PrefetchWholeSchedule();
	FScheduleQueryingDelegate* GetOnScheduleQueryingStatusChanged();
	void OnFoundScheduleForTargetedIModel(FString const& ScheduleId, FString const& ScheduleName,
		FString const& CustomCacheDir = {}, std::optional<EITwinSchedulesGeneration> ScheduleGen = {});
	FUrlArgList ConcatPaginationParams(FUrlArgList&& ArgList, std::optional<FString> const& PageToken);
	void RequestSchedules(ReusableJsonQueries::FStackingToken const&,
		std::optional<FString> const PageToken = {}, FLock* optLock = nullptr);
	void RequestScheduleStatistics(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx, FLock&);
	void AutoRequestScheduleItems(ReusableJsonQueries::FStackingToken const& Token,
		size_t const SchedStartIdx, size_t const SchedEndIdx, FLock* optLock = nullptr);
	/// \param ElementsIt When not equal to ElementsEnd from the start, get ElementIDs with which to filter
	///		the query by incrementing this iterator, until either reaching ElementsEnd, or reaching any
	///		internal limit or other reason for splitting the query (see MaxElementIDsFilterSize)
	/// \param ElementsEnd Iterator at which to stop asking for more!
	/// \param InOutElemCount Useful hint to reserve capacity for the structures used internally by this
	///		method. Need not be exact but why not! Decremented as Elements are drawn from the input iterator.
	/// \return The iterator position at which the method stopped for the query just stacked. When not equal
	///		to ElementsEnd, it just means you need to call again the method, passing it as ElementsIt.
	std::set<ITwinElementID>::const_iterator RequestAnimationBindings(
		ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
		FLock&, std::optional<FTimeRangeInSeconds> const TimeRange = {},
		std::set<ITwinElementID>::const_iterator const ElementsBegin = {},
		std::set<ITwinElementID>::const_iterator const ElementsEnd = {}, int64_t* InOutElemCount = nullptr,
		std::optional<size_t> const BeginTaskIdx = {}, std::optional<size_t> const EndTaskIdx = {},
		std::optional<FString> const PageToken = {}, std::optional<FString> JsonPostString = {});
	/// \param CreatedProperties When additional information for the property needs to be queried, the
	///		AnimIdx where to find the PropertyId (possibly in a nested property) is inserted in this set.
	///		The Insertable type need only support 'void insert(size_t)'
	/// \return A pair with the iterator to the created or existing property, and a flag which is false when
	///		the property is already fully defined (queried + reply handled), or true when the property either
	///		has a still pending query, or needs to be queried (see CreatedProperties parameter for that case)
	template<typename TPropertyId, typename TProperty, class TInsertable>
	std::pair<TProperty*, bool> EmplaceProperty(size_t const AnimIdx, TPropertyId const& PropertyId,
		size_t& PropertyInVec, std::vector<TProperty>& SchedProperties,
		std::unordered_map<TPropertyId, size_t>& SchedKnownPropertys,
		TInsertable& CreatedProperties, FLock&);
	void CompletedProperty(FITwinSchedule& Schedule, std::vector<size_t>& Bindings, FLock& Lock,
						   FString const& From);
	void RequestAllTasks(ReusableJsonQueries::FStackingToken const&, size_t const SchedStartIdx,
		size_t const SchedEndIdx, FLock&, std::optional<FString> const PageToken = {});
	void RequestTask(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx, size_t const AnimIdx,
					 FLock&);
	void ParseTaskDetails(TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx,
						  size_t const TaskInVec, FLock& Lock);
	bool ParseTaskDates(TSharedPtr<FJsonObject> const& JsonObj, FDateTime& Start, FDateTime& Finish,
						FString& StartStr, FString& FinishStr,
		bool const bActualOrPlanned);
	void ParseAppearanceProfileDetails(TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx,
		FString const AppearanceProfileId, size_t const AppearanceProfileInVec, FLock& Lock);
	void RequestAllAppearanceProfiles(ReusableJsonQueries::FStackingToken const&, size_t const SchedStartIdx,
		size_t const SchedEndIdx, FLock&, std::optional<FString> const PageToken = {});
	void RequestAppearanceProfile(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								  size_t const AnimIdx, FLock&);
	bool Parse3DPathAlignment(FString const& Alignment,
							  std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor);
	void RequestAllStaticTransfoAssignments(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
											 std::optional<FString> PageToken, FLock&);
	void RequestAll3DPathTransfoAssignments(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
											 std::optional<FString> PageToken, FLock&);
	void ParseStaticTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx,
		FLock* optLock = nullptr, std::optional<size_t> const AnimIdx = {});
	void Parse3DPathTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx,
		ReusableJsonQueries::FStackingToken const& Token, FLock* optLock = nullptr,
		std::optional<size_t> const AnimIdx = {});
	void RequestAnimationBindingTransfoAssignment(ReusableJsonQueries::FStackingToken const&,
		size_t const SchedIdx, size_t const AnimIdx, FLock&);
	void Request3DPathKeyframes(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
		size_t const TransfoAssignmentIdx, std::optional<FString> const PageToken, FLock&);

	//void DetermineTaskElements(...); <== blame here to retrieve method to query resource assignments
	//void RequestResourceEntity3Ds(...); <== blame here to retrieve method to query resource entity3Ds

	/// \param bCreateGroupFromResource3DEntities Actually untested, would be needed only if reusing
	///		methods RequestResourceEntity3Ds & co. above (which retrieved the Element IDs assigned to a
	///		task by querying the task resources and then the resource Entity3Ds), for testing purposes 
	void CreateRandomAppearanceProfile(size_t const SchedIdx, size_t const AnimIdx, FLock& Lock,
									   bool const bCreateGroupFromResource3DEntities = false);
	EProfileAction ParseProfileAction(FString const& FromStr);
	bool ParseSimpleAppearance(FSimpleAppearance& Appearance, bool const bBaseOfActiveAppearance,
							   TSharedPtr<FJsonObject> const& JsonObj);
	bool ParseActiveAppearance(FActiveAppearance& Appearance, TSharedPtr<FJsonObject> const& JsonObj);
	bool ColorFromHexString(FString const& FromStr, FVector& Color);
	bool ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out);
	bool ParseGrowthSimulationMode(FString const& FromStr, EGrowthSimulationMode& Mode);

	// No longer used - blame here
	//void RequestAnimatedEntityUserFieldId(ReusableJsonQueries::FStackingToken const&,
	//	size_t const SchedStartIdx, size_t const SchedEndIdx, FLock&);
	//bool SetAnimatedEntityUserFieldId(TSharedRef<FJsonObject> JsonObj, FITwinSchedule const& Schedule) const;
	//bool SupportsAnimationBindings(size_t const SchedIdx, FLock&) const;

	EITwinEnvironment GetScheduleEnvironment() const
	{
		if (UnitTesting)
		{
			ensure(UnitTesting->Environment != EITwinEnvironment::Invalid);
			return UnitTesting->Environment;
		}
		else
			return GetServerConnection()->Environment;
	}

	/// From Julius Senkus: "es-api.bentley.com/4dschedule is a proxy redirecting to
	/// esapi-4dschedules.bentley.com, which checks if the scheduleId is for Legacy or NextGen and then
	/// retrieves the data accordingly (either from next gen services or legacy)".
	/// So I guess synchro4dschedulesapi-eus.bentley.com/api is for NextGen internally, but Julius recommended
	/// we use only the proxy.
	FString GetSchedulesAPIBaseUrl() const
	{
		if (UnitTesting)
		{
			return UnitTesting->BaseUrl;
		}
		if (bUseAPIM)
		{
			// If changing this URL, change also in FImpl's second ctor! (for unit tests)
			return FString::Printf(TEXT("https://%sapi.bentley.com/schedules"),
								   *GetServerConnection()->UrlPrefix());
		}
		// es-api: if changing any of these URLs, check also that it doesn't screw bUseAPIM detection in
		// FImpl's second ctor! (for unit tests)
		switch (SchedulesGeneration)
		{
		case EITwinSchedulesGeneration::NextGen:
			ensure(false); // so far, NextGen on ES-API doesn't support itwin-platform scope!
			return FString::Printf(TEXT("https://%ssynchro4dschedulesapi-eus.bentley.com/api/v1/schedules"),
								   *GetServerConnection()->UrlPrefix());
		// not yet known: try Legacy first: same assumption should also be enforced in GetIdToQuerySchedules 
		// below, as well as in FImpl::ResetConnection and FImpl::RequestSchedules!
		case EITwinSchedulesGeneration::Unknown:
			[[fallthrough]];
		case EITwinSchedulesGeneration::Legacy:
			return FString::Printf(TEXT("https://%ses-api.bentley.com/4dschedule/v1/schedules"),
								   *GetServerConnection()->UrlPrefix());
		}
		check(false);
		return "<invalidUrl>";
	}

	FString GetIdToQuerySchedules() const
	{
		if (UnitTesting)
		{
			return "projectId";
		}
		if (bUseAPIM)
		{
			return TEXT("iTwinId");
		}
		// es-api:
		switch (SchedulesGeneration)
		{
		case EITwinSchedulesGeneration::Unknown:
			// not yet known: we'll try Legacy first (see important comment on GetSchedulesAPIBaseUrl!)
			[[fallthrough]];
		case EITwinSchedulesGeneration::Legacy:
			return "projectId";
		case EITwinSchedulesGeneration::NextGen:
			ensure(false); // so far, NextGen on ES-API doesn't support itwin-platform scope!
			return "contextId";
		}
		check(false);
		return "<invalId>";
	}

	void OnScheduleDownloadProgressed(FITwinSchedule& Sched, FLock&)
	{
		// TODO: log only once, of course + aggregate currently downloaded "properties" counts vs. total counts
		// into a single percentage value to notify the UITwinSynchro4DSchedules which will ultimately relay
		// the information to some UX widget
		if (!Sched.bHasLoggedStats)
		{
			Sched.bHasLoggedStats = true;
			S4D_LOG(TEXT("Statistics for the schedule Id %s named '%s':\n\tTasks: %llu,\n\tAppearance profiles: %llu,\n\tAnimation Bindings: %llu,\n\tStatic transforms: %llu,\n\t3D Path assignments: %llu,\n\t3D Paths: %llu,\n\t3D Paths keyframes: %llu"),
				*Sched.Id, *Sched.Name, Sched.StatisticsTotal.TaskCount, Sched.StatisticsTotal.AppearanceProfileCount,
				// Note: the animation binding count does not match the final "bindings" count displayed, by far.
				// I'm sure there's a reason, probably due to grouping and/or leaf vs. non-leaf Element nodes, etc. ;-)
				Sched.StatisticsTotal.AnimationBindingCount, Sched.StatisticsTotal.Animation3dTransformCount,
				Sched.StatisticsTotal.Animation3dPathAssignmentCount, Sched.StatisticsTotal.Animation3dPathCount,
				Sched.StatisticsTotal.Animation3dPathKeyframeCount);
		}
		size_t const TotalCount = Sched.StatisticsTotal.TaskCount
			+ Sched.StatisticsTotal.AppearanceProfileCount
			+ Sched.StatisticsTotal.AnimationBindingCount
			+ Sched.StatisticsTotal.Animation3dTransformCount
			+ Sched.StatisticsTotal.Animation3dPathAssignmentCount
			+ Sched.StatisticsTotal.Animation3dPathCount
			+ Sched.StatisticsTotal.Animation3dPathKeyframeCount;
		if (TotalCount != 0) // could happen depending on ordering of replies
		{
			SchedulesInternals().OnDownloadProgressed(100. * std::min(1.,
				( Sched.StatisticsCurrent.TaskCount
				+ Sched.StatisticsCurrent.AppearanceProfileCount
				+ Sched.StatisticsCurrent.AnimationBindingCount
				+ Sched.StatisticsCurrent.Animation3dTransformCount
				+ Sched.StatisticsCurrent.Animation3dPathAssignmentCount
				+ Sched.StatisticsCurrent.Animation3dPathCount
				+ Sched.StatisticsCurrent.Animation3dPathKeyframeCount)
					/ (double)TotalCount));
		}
	}

	FString const& PageSizeParam() const
		{ return bUseAPIM ? APIParams::PageSizeAPIM : APIParams::PageSizeES; }
	FString const& PageTokenParam() const
		{ return bUseAPIM ? APIParams::PageTokenAPIM : APIParams::PageTokenES; }

	FString ToString(TSharedPtr<FJsonObject> const& JsonObj)
	{
		FString JsonString;
		auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
		return JsonString;
	}

}; // class FITwinSchedulesImport::FImpl

int FITwinSchedulesImport::FImpl::s_NextSchedApiSession = 0;

// conflicts with ITwinWebServices.cpp
#undef JSON_GETOBJ_OR
#undef JSON_GETOBJ_OR_CUSTOM

/// Get a non-empty string from the Json object passed, or log an error and do something (typically continue
/// or return)
#define JSON_GETSTR_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetStringField(TEXT(Field), Dest) || Dest.IsEmpty()) { \
		BE_LOGE("ITwin4DImp", "Parsing error or empty string field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get a number from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETNUMBER_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetNumberField(TEXT(Field), Dest)) { \
		BE_LOGE("ITwin4DImp", "Parsing error for number field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get a boolean from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETBOOL_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetBoolField(TEXT(Field), Dest)) { \
		BE_LOGE("ITwin4DImp", "Parsing error for boolean field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get an Object from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETOBJ_OR(JsonObj, Field, Dest, WhatToDo) \
	{ Dest = nullptr; \
		if (!(JsonObj)->TryGetObjectField(TEXT(Field), Dest) || !Dest) { \
		BE_LOGE("ITwin4DImp", "Parsing error for object field in Json response: " << Field); \
		WhatToDo; \
	}}


void FITwinSchedulesImport::FImpl::SetScheduleTimeRangeIsKnown()
{
	if (!UnitTesting)
		SchedulesInternals().SetScheduleTimeRangeIsKnown();
}

bool FITwinSchedulesImport::FImpl::PrefetchWholeSchedule()
{
	if (UnitTesting)
		return true;
	else
		return SchedulesInternals().PrefetchWholeSchedule();
}

FScheduleQueryingDelegate* FITwinSchedulesImport::FImpl::GetOnScheduleQueryingStatusChanged()
{
	if (UnitTesting)
		return nullptr;
	else
		return PrefetchWholeSchedule() ? (&Owner->Owner->OnScheduleQueryingStatusChanged) : nullptr;
}

void FITwinSchedulesImport::FImpl::OnFoundScheduleForTargetedIModel(FString const& ScheduleId,
	FString const& ScheduleName, FString const& CustomCacheDir/*= {}*/,
	std::optional<EITwinSchedulesGeneration> ScheduleGen/*= {}*/)
{
	// We can't actually support multiple Schedules per FITwinSchedulesImport at the moment, because
	// caching uses a single folder: I probably need to either replace Schedules by a single
	// optional<Schedule>, OR have an array of cache folders and pass the schedule index (aka
	// cache folder index) to the Queries for each request - TODO_GCO.
	ensure(Schedules.empty() || (Schedules.size() == 1 && Schedules[0].Id == ScheduleId));
	if (ScheduleGen)
		SchedulesGeneration = *ScheduleGen;
	if (!bUseAPIM && EITwinSchedulesGeneration::Unknown == SchedulesGeneration)
	{
		// was tried first (see important comment on GetSchedulesAPIBaseUrl!)
		SchedulesGeneration = EITwinSchedulesGeneration::Legacy;
	}
	auto It = std::find_if(Schedules.begin(), Schedules.end(),
		[&ScheduleId](FITwinSchedule& Sched) { return Sched.Id == ScheduleId; });
	FITwinSchedule& Sched = (Schedules.end() == It)
		? Schedules.emplace_back(FITwinSchedule{ ScheduleId, ScheduleName, SchedulesGeneration })
		: (*It);
	Sched.Reserve(200);
	// Set up the cache folder for the next requests
	if (UnitTesting // cache mandatory in TUs (and Owner->Owner is nullptr)
		|| (!Owner->Owner->bDisableCaching
			// superceded by the special simulation mode
			&& Owner->Owner->DebugSimulateSessionQueries.IsEmpty()))
	{
		FString const& CacheFolder = CustomCacheDir.IsEmpty()
			? QueriesCache::GetCacheFolder(QueriesCache::ESubtype::Schedules,
				GetScheduleEnvironment(), ITwinId, TargetedIModelId, ChangesetId,
				// Cache identification is strictly based on the folder, so it's OK to just prepend APIM_ here
				bUseAPIM ? (FString("APIM_") + ScheduleId) : ScheduleId)
			: CustomCacheDir;
		if (ensure(!CacheFolder.IsEmpty()))
		{
			Queries->InitializeCache(CacheFolder, GetScheduleEnvironment(), Sched.Name, (bool)UnitTesting);
		}
	}
	S4D_LOG(TEXT("Added schedule Id %s named '%s' to iModel %s"), *ScheduleId,
			*Sched.Name, *TargetedIModelId);
}

FUrlArgList FITwinSchedulesImport::FImpl::ConcatPaginationParams(FUrlArgList&& ArgList,
	std::optional<FString> const& PageToken)
{
	ArgList.push_back({ PageSizeParam(), FString::Printf(TEXT("%d"), RequestPagination) });
	if (PageToken)
		ArgList.push_back({ PageTokenParam(), *PageToken });
	return ArgList;
}

void FITwinSchedulesImport::FImpl::RequestSchedules(ReusableJsonQueries::FStackingToken const& Token,
	std::optional<FString> const PageToken /*= {}*/, FLock* optLock /*= nullptr*/)
{
	ensure(!UnitTesting);
	//
	// Note that my latest testing on qa-synchro4dschedulesapi-eus showed that pagination was not supported
	// on schedules, although it worked as expected on Tasks
	auto ArgList = ConcatPaginationParams(FUrlArgList{ { GetIdToQuerySchedules(), ITwinId } }, PageToken);
	// 1. First thing is to get the list of schedules.
	//
	// {} because the base URL (eg https://dev-synchro4dschedulesapi-eus.bentley.com/api/v1/schedules),
	// is actually the endpoint for listing the schedules related to a contextId/projectId (= iTwinId!)
	Queries->StackRequest(Token, optLock, ITwinHttp::EVerb::Get, {}, std::move(ArgList),
		[this, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			ensure(!UnitTesting);
			auto NewScheds = Reply->GetArrayField(bUseAPIM ? TEXT("schedules") : TEXT("items"));
			S4D_LOG(TEXT("Received %d schedules for iTwin %s"), (int)NewScheds.Num(), *ITwinId);
			if (0 == NewScheds.Num())
			{
				if (bUseAPIM || EITwinSchedulesGeneration::Unknown != SchedulesGeneration)
				{
					SetScheduleTimeRangeIsKnown();
				}
				return;
			}
			FLock Lock(Mutex);
			size_t const SchedStartIdx = Schedules.size();
			for (const auto& SchedVal : NewScheds)
			{
				const auto& SchedObj = SchedVal->AsObject();
				FString IModelId;
				JSON_GETSTR_OR(SchedObj, "iModelId", IModelId, continue)
				if (IModelId == TargetedIModelId)
				{
					FString Id;
					if (!ensure(SchedObj->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty()))
						continue;
					if (Schedules.end() == std::find_if(Schedules.begin(), Schedules.end(),
						[&Id](FITwinSchedule& Sched) { return Sched.Id == Id; }))
					{
						OnFoundScheduleForTargetedIModel(Id, SchedObj->GetStringField(TEXT("name")));
					}
				}
			}
			FString NextPageToken;
			if (Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
			{
				RequestSchedules(Token, NextPageToken, &Lock);
			}
			else
			{
				Queries->StatsResetActiveTime();
				if (Schedules.empty()
					&& (bUseAPIM || EITwinSchedulesGeneration::Unknown != SchedulesGeneration))
				{
					SetScheduleTimeRangeIsKnown();
				}
			}
			if (SchedStartIdx != Schedules.size())
			{
				AutoRequestScheduleItems(Token, SchedStartIdx, Schedules.size(), &Lock);
			}
		});
}

void FITwinSchedulesImport::FImpl::AutoRequestScheduleItems(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedStartIdx, size_t const SchedEndIdx, FLock* Lock)
{
	std::optional<ITwinHttp::FLock> optLock;
	if (!Lock)
	{
		optLock.emplace(Mutex);
		Lock = &(*optLock);
	}
	if (bUseAPIM)
		for (size_t SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
		{
			RequestScheduleStatistics(Token, SchedIdx, *Lock);
		}
	RequestAllTasks(Token, SchedStartIdx, SchedEndIdx, *Lock);
	if (PrefetchWholeSchedule())
	{
		RequestAllAppearanceProfiles(Token, SchedStartIdx, SchedEndIdx, *Lock);
		for (size_t SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
		{
			RequestAllStaticTransfoAssignments(Token, SchedIdx, {}, *Lock);
			RequestAll3DPathTransfoAssignments(Token, SchedIdx, {}, *Lock);
			Request3DPathKeyframes(Token, SchedIdx, ITwin::INVALID_IDX, {}, *Lock);
		}
	}
}

void FITwinSchedulesImport::FImpl::RequestScheduleStatistics(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, FLock& Lock)
{
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Schedules[SchedIdx].Id, TEXT("animation-statistics") }, {},
		[this, SchedIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& JsonObj = Reply->GetObjectField(TEXT("animationStatistics"));
			FITwinScheduleStats Tmp;
			JSON_GETNUMBER_OR(JsonObj, "animation3dPathAssignmentCount", Tmp.Animation3dPathAssignmentCount, return);
			JSON_GETNUMBER_OR(JsonObj, "animation3dPathCount", Tmp.Animation3dPathCount, return);
			JSON_GETNUMBER_OR(JsonObj, "animation3dPathKeyframeCount", Tmp.Animation3dPathKeyframeCount, return);
			JSON_GETNUMBER_OR(JsonObj, "animation3dTransformCount", Tmp.Animation3dTransformCount, return);
			JSON_GETNUMBER_OR(JsonObj, "animationBindingCount", Tmp.AnimationBindingCount, return);
			JSON_GETNUMBER_OR(JsonObj, "appearanceProfileCount", Tmp.AppearanceProfileCount, return);
			JSON_GETNUMBER_OR(JsonObj, "taskCount", Tmp.TaskCount, return);
			FLock Lock(Mutex);
			Schedules[SchedIdx].StatisticsTotal = Tmp;
			OnScheduleDownloadProgressed(Schedules[SchedIdx], Lock);
		});
}

void FITwinSchedulesImport::FImpl::RequestAllTasks(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedStartIdx, size_t const SchedEndIdx, FLock& Lock,
	std::optional<FString> const PageToken /*= {}*/)
{
	// Cannot have a page token common to several Schedules tasks queries...
	check(!PageToken || SchedEndIdx == (SchedStartIdx + 1));
	auto RequestArgList = ConcatPaginationParams({}, PageToken);
	for (auto SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
	{
		Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedules[SchedIdx].Id, TEXT("tasks") },
							  std::move(RequestArgList),
			[this, SchedIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
			{
				auto const& Items = Reply->GetArrayField(bUseAPIM ? TEXT("tasks") : TEXT("items"));
				if (Items.IsEmpty())
				{
					BE_LOGW("ITwin4DImp", "Did not receive any task for schedule '"
						<< TCHAR_TO_UTF8(*Schedules[SchedIdx].Name) << "'!");
				}
				else
				{
					FString NextPageToken;
					bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
					FLock Lock(Mutex);
					auto&& Sched = Schedules[SchedIdx];
					S4D_VERBOSE(TEXT("Received %d tasks (total %d%s) for schedule '%s'"), Items.Num(),
						(int)Sched.Tasks.size() + Items.Num(),
						bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
						*Schedules[SchedIdx].Name);
					if (bMoreToCome)
					{
						RequestAllTasks(Token, SchedIdx, SchedIdx + 1, Lock, NextPageToken);
					}
					Sched.Tasks.reserve(Sched.Tasks.size() + Items.Num());
					auto& MainTimeline = UnitTesting ? UnitTesting->MainTimeline
													 : SchedulesInternals().Timeline();
					for (auto&& Item : Items)
					{
						const auto& TaskObj = Item->AsObject();
						FString TaskId = TaskObj->GetStringField(TEXT("id"));
						// Create task directly instead of using EmplaceProperty: tasks don't depend on any
						// subproperty, which is witnessed by the fact that ParseTaskDetails below directly
						// calls CompletedProperty.
						auto const KnownTask = Sched.KnownTasks.try_emplace(TaskId, Sched.Tasks.size());
						if (KnownTask.second) // was inserted
							Sched.Tasks.emplace_back();
						Sched.Tasks[KnownTask.first->second].Id = std::move(TaskId);
						ParseTaskDetails(TaskObj, SchedIdx, KnownTask.first->second, Lock);
						MainTimeline.IncludeTimeRange(Sched.Tasks[KnownTask.first->second].TimeRange);
					}
					Sched.StatisticsCurrent.TaskCount += (size_t)Items.Num();
					OnScheduleDownloadProgressed(Sched, Lock);
					if (!bMoreToCome)
					{
						SetScheduleTimeRangeIsKnown();
						if (PrefetchWholeSchedule())
							RequestAnimationBindings(Token, SchedIdx, Lock);
					}
				}
			});
	}
}

void FITwinSchedulesImport::FImpl::RequestAllAppearanceProfiles(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedStartIdx, size_t const SchedEndIdx,
	FLock& Lock, std::optional<FString> const PageToken /*= {}*/)
{
	// Cannot have a page token common to several Schedules appearanceProfiles queries...
	check(!PageToken || SchedEndIdx == (SchedStartIdx + 1));
	auto RequestArgList = ConcatPaginationParams({}, PageToken);
	for (auto SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
	{
		Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
			{ Schedules[SchedIdx].Id, bUseAPIM ? TEXT("appearance-profiles") : TEXT("appearanceProfiles") },
			std::move(RequestArgList),
			[this, SchedIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
			{
				auto const& Items =
					Reply->GetArrayField(bUseAPIM ? TEXT("appearanceProfiles") : TEXT("items"));
				if (Items.IsEmpty())
				{
					BE_LOGW("ITwin4DImp", "Did not receive any appearance profile for schedule '"
						<< TCHAR_TO_UTF8(*Schedules[SchedIdx].Name) << "'!");
				}
				else
				{
					FString NextPageToken;
					bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
					FLock Lock(Mutex);
					auto&& Sched = Schedules[SchedIdx];
					S4D_VERBOSE(TEXT("Received %d appearance profiles (total %d%s) for schedule '%s'"),
						Items.Num(), (int)Sched.AppearanceProfiles.size() + Items.Num(),
						bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
						*Schedules[SchedIdx].Name);
					if (bMoreToCome)
					{
						RequestAllAppearanceProfiles(Token, SchedIdx, SchedIdx + 1, Lock, NextPageToken);
					}
					Sched.AppearanceProfiles.reserve(Sched.AppearanceProfiles.size() + Items.Num());
					for (auto&& Item : Items)
					{
						const auto& AppearanceObj = Item->AsObject();
						FString const AppearanceId = AppearanceObj->GetStringField(TEXT("id"));
						// Create appearance profile directly instead of using EmplaceProperty: appearance
						// profiles do not depend on any subproperty, which is witnessed by the fact that
						// ParseAppearanceProfileDetails below directly calls CompletedProperty.
						auto const KnownAppearance = Sched.KnownAppearanceProfiles.try_emplace(
							AppearanceId, Sched.AppearanceProfiles.size());
						if (KnownAppearance.second) // was inserted
							Sched.AppearanceProfiles.emplace_back();
						ParseAppearanceProfileDetails(AppearanceObj, SchedIdx, AppearanceId,
													  KnownAppearance.first->second, Lock);
					}
					Sched.StatisticsCurrent.AppearanceProfileCount += (size_t)Items.Num();
					OnScheduleDownloadProgressed(Sched, Lock);
				}
			});
	}
}

namespace Detail {

/// Type suitable for the Insertable template parameter to FITwinSchedulesImport::FImpl::EmplaceProperty,
/// but only recording the fact that a single value was inserted or not.
class FInsertionFlag
{
	bool bInserted = false;
public:
	void insert(size_t const&) { ensure(!Inserted()); bInserted = true; }
	/// Just tells if "something" was inserted: we already know which anyway...
	bool Inserted() const { return bInserted; }
};

template<typename TPropertyId> bool IsEmpty(TPropertyId const& PropertyId);
template<> bool IsEmpty(FString const& PropertyId) { return PropertyId.IsEmpty(); }
template<> bool IsEmpty(std::pair<FString, bool> const& PropertyId) { return PropertyId.first.IsEmpty(); }

} // ns Detail

template<typename TPropertyId, typename TProperty, class TInsertable>
std::pair<TProperty*, bool> FITwinSchedulesImport::FImpl::EmplaceProperty(
	size_t const AnimIdx, TPropertyId const& PropertyId, size_t& PropertyInVec,
	std::vector<TProperty>& SchedProperties, std::unordered_map<TPropertyId, size_t>& SchedKnownPropertys,
	TInsertable& CreatedProperties, FLock&)
{
	if (Detail::IsEmpty<TPropertyId>(PropertyId)) // could be optional (tested elsewhere)
	{
		return std::make_pair((TProperty*)nullptr, false);
	}
	if (ITwin::INVALID_IDX == PropertyInVec)
	{
		auto KnownProperty = SchedKnownPropertys.try_emplace(PropertyId, SchedProperties.size());
		PropertyInVec = KnownProperty.first->second;
		if (KnownProperty.second) // was inserted => need to create it
		{
			SchedProperties.emplace_back();
			// Insert even "invalid" indices (ie unknown dependent bindings), in both collections:
			//	* 'Bindings' emptiness is also used (below, by the last 'return' clause) to signal
			//		uncompletely defined properties
			//	* 'CreatedProperties' non-emptiness, too, is meaningful to signal that the property
			//		needs to be queried (see Parse3DPathTransfoAssignment)
			//if (ITwin::INVALID_IDX != AnimIdx) {
			SchedProperties.back().Bindings.emplace_back(AnimIdx);
			CreatedProperties.insert(AnimIdx);
			// }
			return std::make_pair(&SchedProperties.back(), true);
		}
		else
		{
			auto& Property = SchedProperties[PropertyInVec];
			// already present + Bindings empty = its query was already completed.
			if (Property.Bindings.empty())
				return std::make_pair(&Property, false);
			// otherwise, add this binding to the list that needs to be notified
			else
			{
				//if (ITwin::INVALID_IDX != AnimIdx) <== see comment above
				Property.Bindings.emplace_back(AnimIdx);
				return std::make_pair(&Property, true);
			}
		}
	}
	else // already added to Property.Bindings earlier when PropertyInVec was set, don't do it again
	{
		auto& Property = SchedProperties[PropertyInVec];
		return std::make_pair(&Property, !Property.Bindings.empty());
	}
}

std::set<ITwinElementID>::const_iterator FITwinSchedulesImport::FImpl::RequestAnimationBindings(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, FLock& Lock,
	std::optional<FTimeRangeInSeconds> const TimeRange/* = {}*/,
	std::set<ITwinElementID>::const_iterator const ElementsBegin/* = {}*/,
	std::set<ITwinElementID>::const_iterator const ElementsEnd/* = {}*/, int64_t* InOutElemCount/*=nullptr*/,
	std::optional<size_t> const BeginTaskIdx/*= {}*/, std::optional<size_t> const EndTaskIdx/*= {}*/,
	std::optional<FString> const PageToken/*= {}*/, std::optional<FString> JsonPostString/*= {}*/)
{
	// APIM implements time-, task- and Element-filtering as parameters, no longer in a POST content json.
	// Since we no longer use it, I'm not implementing it for the time being.
	if (!ensure((ElementsBegin == ElementsEnd && TimeRange == std::nullopt && BeginTaskIdx == std::nullopt
					&& EndTaskIdx == std::nullopt && JsonPostString == std::nullopt)
				|| !bUseAPIM))
	{
		return ElementsBegin;
	}
	bool bHasTimeRange = false;
	auto ElementsIt = ElementsBegin;
	// See comment below about AnimBindingsFullyKnownForElem "optim": it could be fixed in the non-prefetched
	// case by switching from 'None' to 'InitialVersion' only after the last page of a given query is fully
	// processed, which probably means keeping the ElementsBegin/End range alive for the whole duration, or
	// using an intermediate 'BeingQueried' state between 'None' and 'InitialVersion'.
	ensure(PrefetchWholeSchedule());
	if (JsonPostString)
	{
		// Parameters were not forwarded (they shouldn't be: they were deallocated by now)
		// so rely on post string content
		bHasTimeRange = JsonPostString->Contains("startTime") || JsonPostString->Contains("endTime");
	}
	else if (!bUseAPIM)
	{
		JsonPostString.emplace();
		auto JsonObj = MakeShared<FJsonObject>();
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&(*JsonPostString),
																				/*Indent=*/0);
		auto& Sched = Schedules[SchedIdx];
		if (ElementsBegin != ElementsEnd)
		{
			ensure(false);//removed AnimBindingsFullyKnownForElem...
			TArray<TSharedPtr<FJsonValue>> AnimatedEntityIDs;
			//if (InOutElemCount && (*InOutElemCount) > 0)
			//	AnimatedEntityIDs.Reserve(std::min(*InOutElemCount, (int64_t)MaxElementIDsFilterSize));
			//if (TimeRange)
			//{
			//	for ( ; ElementsIt != ElementsEnd && AnimatedEntityIDs.Num() <= MaxElementIDsFilterSize;
			//		 ++ElementsIt)
			//	{
			//		// Do not insert anything: the query is only for a specific time range...
			//		if (Sched.AnimBindingsFullyKnownForElem.end()
			//			== Sched.AnimBindingsFullyKnownForElem.find(*ElementsIt))
			//		{
			//			AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(ITwin::ToString(*ElementsIt)));
			//		}
			//		if (InOutElemCount) --(*InOutElemCount);
			//	}
			//}
			//else
			//{
			//	for ( ; ElementsIt != ElementsEnd && AnimatedEntityIDs.Num() <= MaxElementIDsFilterSize;
			//		 ++ElementsIt)
			//	{
			//		if (Sched.AnimBindingsFullyKnownForElem.try_emplace(*ElementsIt, VersionToken::None)
			//			.second)
			//		{
			//			// not known => was inserted
			//			AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(ITwin::ToString(*ElementsIt)));
			//		}
			//		if (InOutElemCount) --(*InOutElemCount);
			//	}
			//}
			if (AnimatedEntityIDs.IsEmpty()) // nothing left to query
			{
				return ElementsEnd;
			}
			JsonObj->SetArrayField(TEXT("animatedEntityIds"), std::move(AnimatedEntityIDs));
		}
		if (TimeRange && TimeRange->first < TimeRange->second)
		{
			FDateRange const DateRange = ITwin::Time::ToDateRange(*TimeRange);
			if (!DateRange.IsEmpty() && DateRange.HasLowerBound() && DateRange.HasUpperBound())
			{
				JsonObj->SetStringField(TEXT("startTime"), DateRange.GetLowerBoundValue().ToIso8601());
				JsonObj->SetStringField(TEXT("endTime"), DateRange.GetUpperBoundValue().ToIso8601());
				bHasTimeRange = true;
			}
		}
		if (BeginTaskIdx
			&& ensure(EndTaskIdx && BeginTaskIdx < Sched.Tasks.size() && EndTaskIdx <= Sched.Tasks.size()
				&& (*BeginTaskIdx) != (*EndTaskIdx)
				&& (UnitTesting // in which case Owner->Owner is nullptr
					|| ((*EndTaskIdx) - (*BeginTaskIdx)) <= Owner->Owner->ScheduleQueriesMaxTaskIDsFilterSize)))
		{
			TArray<TSharedPtr<FJsonValue>> TaskIDs;
			TaskIDs.Reserve((*EndTaskIdx) - (*BeginTaskIdx));
			for (size_t TaskIdx = (*BeginTaskIdx); TaskIdx < (*EndTaskIdx); ++TaskIdx)
			{
				TaskIDs.Add(MakeShared<FJsonValueString>(Sched.Tasks[TaskIdx].Id));
			}
			JsonObj->SetArrayField(TEXT("taskIds"), std::move(TaskIDs));
		}
		FJsonSerializer::Serialize(JsonObj, JsonWriter);
	}
	FUrlArgList ArgList = { { PageSizeParam(), FString::Printf(TEXT("%d"), BindingsRequestPagination) } };
	if (PageToken)
		ArgList.push_back({ PageTokenParam(), *PageToken });
	Queries->StackRequest(Token, &Lock, bUseAPIM ? ITwinHttp::EVerb::Get : ITwinHttp::EVerb::Post,
		{ Schedules[SchedIdx].Id,
		  bUseAPIM ? TEXT("animation-bindings") : TEXT("animationBindings/query") },
		std::move(ArgList),
		[this, SchedIdx, TimeRange, JsonPostString, bHasTimeRange, &Token]
		(TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(bUseAPIM ? TEXT("animationBindings") : TEXT("items"));
			if (Items.IsEmpty())
				return;
			FString NextPageToken;
			bool const bHasNextPage = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			FLock Lock(Mutex);
			if (bHasNextPage)
			{
				// No need to repeat the TimeRange and ElementIDs parameters, they are already included in
				// the JsonPostString content
				RequestAnimationBindings(Token, SchedIdx, Lock, {}, {}, {}, nullptr, {}, {}, NextPageToken,
										 JsonPostString);
			}
			auto&& Sched = Schedules[SchedIdx];
			// size_t below are the AnimIdx where to find the PropertyId just created and which need to be
			// queried, BUT only the first AnimIdx using the given PropertyId is inserted, so we do have
			// the unicity we need for the sub-queries
			std::unordered_set<size_t> CreatedTasks;
			std::unordered_set<size_t> CreatedAppearanceProfiles;
			std::unordered_set<size_t> CreatedTransfoAssignments;
			std::unordered_set<size_t> FullyDefinedBindingsToNotify;
			std::unordered_set<size_t> UpdatedGroups;
			size_t const FirstNewGroupIndex = Sched.Groups.size();
			for (auto&& NewBinding : Items)
			{
				const auto& BindingObj = NewBinding->AsObject();

				FString AnimElemIdStr;
				JSON_GETSTR_OR(BindingObj, "animatedEntityId", AnimElemIdStr, continue)
				ITwinElementID ElementID = ITwin::NOT_ELEMENT;
				FGuid ElementGuid;
				if (AnimElemIdStr.Len() >= 36)
				{
					if (!FGuid::ParseExact(AnimElemIdStr, EGuidFormats::DigitsWithHyphensLower, ElementGuid))
					{
						ensure(false);
						continue;
					}
				}
				else
				{
					ElementID = ITwin::ParseElementID(AnimElemIdStr);
					if (ElementID == ITwin::NOT_ELEMENT)
					{
						ensure(false);
						continue;
					}
				}
				// Optim based on AnimBindingsFullyKnownForElem was buggy: it skipped all but the first binding
				// for any given Element, because setting to VersionToken::InitialVersion too early
				// => the optim was useful only before pre-fetching all bindings
				// REMOVED - blame here
				//Sched.AnimBindingsFullyKnownForElem.try_emplace(ElementID, VersionToken::None);

				FAnimationBinding Tmp;
				JSON_GETSTR_OR(BindingObj, "taskId", Tmp.TaskId, continue)
				JSON_GETSTR_OR(BindingObj, "appearanceProfileId", Tmp.AppearanceProfileId, continue)
				FString AnimatedEntitiesAsGroup;
				// 4D team confirmed to check resourceGroupId first, then resourceId - both can be present
				if (BindingObj->TryGetStringField(TEXT("resourceGroupId"), AnimatedEntitiesAsGroup))
					Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
				else if (BindingObj->TryGetStringField(TEXT("resourceId"), AnimatedEntitiesAsGroup))
					Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
				else if (ElementID == ITwin::NOT_ELEMENT)
					Tmp.AnimatedEntities = ElementGuid;
				else
					Tmp.AnimatedEntities = ElementID;
			#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
				if (BindingObj->TryGetStringField(TEXT("pathAssignmentId"), Tmp.TransfoAssignmentId))
				{
					Tmp.bStaticTransform = false;
				}
				else if (BindingObj->TryGetStringField(TEXT("transformId"), Tmp.TransfoAssignmentId))
				{
					Tmp.bStaticTransform = true;
				}
			#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()

				// KnownAnimationBinding handling
				size_t AnimIdx = Sched.AnimationBindings.size();
				auto KnownAnim = Sched.KnownAnimationBindings.try_emplace(Tmp, AnimIdx);
				if (KnownAnim.second) // was inserted => need to create it
					Sched.AnimationBindings.emplace_back(std::move(Tmp));
				else
					// Already inserted and thus structure filled and property queries stacked, but we still
					// need to update the group with the new Element
					AnimIdx = KnownAnim.first->second;
				FAnimationBinding& Anim = Sched.AnimationBindings[AnimIdx];

				// Group creation or update
				if (std::holds_alternative<FString>(Anim.AnimatedEntities))
				{
					if (ITwin::INVALID_IDX == Anim.GroupInVec)
					{
						auto KnownGroup = Sched.KnownGroups.try_emplace(
							std::get<FString>(Anim.AnimatedEntities), Sched.Groups.size());
						Anim.GroupInVec = KnownGroup.first->second;
						if (KnownGroup.second) // was inserted => need to create it
							Sched.Groups.emplace_back();
					}
					if (ITwin::NOT_ELEMENT == ElementID && ensure(ElementGuid.IsValid()))
					{
						if (!FncElementIDFromGUID(ElementGuid, ElementID))
						{
							ensure(false);
							continue;
						}
					}
					if (Sched.Groups[Anim.GroupInVec].insert(ElementID).second // was inserted...
						&& Anim.GroupInVec < FirstNewGroupIndex)			   //...into a pre-existing group
					{
						// rare case of a binding using an existing group that we happen to discover has more
						// Elements than we initially thought (since the current workflow of spatially-
						// -filtered queries can mean we might miss some Elements, as it was convened for ease
						// of implementation that spatial filtering would NOT (on server side) also return the
						// bindings for all items of the same group as the Elements passed in the query.
						UpdatedGroups.insert(Anim.GroupInVec);
					}
				}
				if (!KnownAnim.second)
				{
					continue;
				}
				ensure(Anim.NotifiedVersion == VersionToken::None);

				// Handle Task property
				bool bIncomplete = EmplaceProperty(AnimIdx, Anim.TaskId, Anim.TaskInVec, Sched.Tasks, 
												   Sched.KnownTasks, CreatedTasks, Lock)
					.second;
				// Handle AppearanceProfile property
				bIncomplete |=
					EmplaceProperty(AnimIdx, Anim.AppearanceProfileId, Anim.AppearanceProfileInVec,
									Sched.AppearanceProfiles, Sched.KnownAppearanceProfiles,
									CreatedAppearanceProfiles, Lock)
						.second;

			#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
				// Handle TransfoAssignment property
				auto&& TransfoAssignmentIncomplete =
					EmplaceProperty(AnimIdx, std::make_pair(Anim.TransfoAssignmentId, Anim.bStaticTransform),
						Anim.TransfoAssignmentInVec, Sched.TransfoAssignments, Sched.KnownTransfoAssignments,
						CreatedTransfoAssignments, Lock);
				if (TransfoAssignmentIncomplete.second)
				{
					bIncomplete = true;
				}
				// Check nested Animation3DPath property to see if binding is actually fully known
				else if (TransfoAssignmentIncomplete.first)
				{
					// TransfoAssignment's properties are known, but its Animation3DPath details may not
					// (no such case with a static transform), but in that case it was created and requested
					// when the transfo-assignment query completed
					auto& TransfoAssignment = Sched.TransfoAssignments[Anim.TransfoAssignmentInVec];
					if (!Anim.bStaticTransform
						&& ensure(std::holds_alternative<FPathAssignment>(TransfoAssignment.Transformation)))
					{
						size_t const Animation3DPathInVec =
							std::get<1>(TransfoAssignment.Transformation).Animation3DPathInVec;
						if (ensure(ITwin::INVALID_IDX != Animation3DPathInVec))
						{
							auto& Animation3DPath = Sched.Animation3DPaths[Animation3DPathInVec];
							if (!Animation3DPath.Bindings.empty())
							{
								// here it is still pending, so this whole binding it not fully known
								bIncomplete = true;
								Animation3DPath.Bindings.emplace_back(AnimIdx);
							}
						}
					}
				}
				//else: there is no TransfoAssignment at all (Id was empty)
			#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()

				// mostly redundant with tests in EmplaceProperty, hence the flag computed above
				//if (!Anim.FullyDefined(Sched, false, Lock))
				if (!bIncomplete)
					FullyDefinedBindingsToNotify.insert(AnimIdx);

			} // for NewBindings

			S4D_VERBOSE(TEXT("Received %d Bindings, total Elements/Groups bound: %llu"), Items.Num(),
				[&Sched]()
				{
					std::unordered_set<decltype(FAnimationBinding::AnimatedEntities)> Bound;
					//could explore the variant and recurse into groups...
					for (auto&& Binding : Sched.AnimationBindings)
						Bound.insert(Binding.AnimatedEntities);
					return Bound.size();
				}());

			// Avoid individual requests: if we are prefetching but had to create missing properties, it can
			// only mean that they are still in transit. Besides being redundant and thus suboptimal, having
			// random individual requests spawned depending on order of received replies is a problem for
			// unit tests which cache can't be populated with all possible non-batched query/reply pairs.
			if (!PrefetchWholeSchedule())
			{
				// Note: see comment above the loop and the unordered_set Created*** definition to understand
				// why the calls below are indeed unique by PropertyId!
				// TODO_GCO: this ordering means until all sub-queries are processed, none of the bindings
				// will probably be fully known. On the other hand, it might mean better data locality on the
				// server...
				for (auto&& AnimIdx : CreatedTasks)
					RequestTask(Token, SchedIdx, AnimIdx, Lock);
				for (auto&& AnimIdx : CreatedAppearanceProfiles)
					RequestAppearanceProfile(Token, SchedIdx, AnimIdx, Lock);
				for (auto&& AnimIdx : CreatedTransfoAssignments)
					RequestAnimationBindingTransfoAssignment(Token, SchedIdx, AnimIdx, Lock);
			}
			if (OnAnimationGroupModified)
				for (auto&& GroupInVec : UpdatedGroups)
					OnAnimationGroupModified(GroupInVec, Sched.Groups[GroupInVec], Lock);
			for (auto&& Binding : FullyDefinedBindingsToNotify)
			{
				if (OnAnimationBindingAdded)
					OnAnimationBindingAdded(Sched, Binding, Lock);
				Sched.AnimationBindings[Binding].NotifiedVersion = VersionToken::InitialVersion;
				S4D_VERBOSE(TEXT("Complete binding notified: %s"),
							*Sched.AnimationBindings[Binding].ToString());
			}
			Sched.StatisticsCurrent.AnimationBindingCount += (size_t)Items.Num();
			OnScheduleDownloadProgressed(Sched, Lock);
		},
		JsonPostString ? FString(*JsonPostString) : FString());

	return ElementsIt;
}

void FITwinSchedulesImport::FImpl::CompletedProperty(FITwinSchedule& Schedule, std::vector<size_t>& Bindings,
													 FLock& Lock, FString const& From)
{
	std::vector<size_t> Swapped;
	Swapped.swap(Bindings);
	for (size_t AnimIdx : Swapped)
	{
		if (ITwin::INVALID_IDX == AnimIdx)
			continue; // OK, "invalid" value is meaningful, see comments in EmplaceProperty
		auto&& AnimationBinding = Schedule.AnimationBindings[AnimIdx];
		if (AnimationBinding.FullyDefined(Schedule, false, Lock))
		{
			if (AnimationBinding.NotifiedVersion != VersionToken::InitialVersion)
			{
				if (OnAnimationBindingAdded)
					OnAnimationBindingAdded(Schedule, AnimIdx, Lock);
				AnimationBinding.NotifiedVersion = VersionToken::InitialVersion;
				S4D_VERBOSE(TEXT("Binding notified from %s: %s"), *From, *AnimationBinding.ToString());
			}
			else S4D_VERBOSE(TEXT("Redundant notif. from %s skipped for %s"), *From,
							 *AnimationBinding.ToString());
		}
		else S4D_VERBOSE(TEXT("Incomplete notif. from %s skipped for %s"), *From,
						 *AnimationBinding.ToString());
	}
}

bool FITwinSchedulesImport::FImpl::ParseTaskDates(TSharedPtr<FJsonObject> const& JsonObj,
	FDateTime& Start, FDateTime& Finish, FString& StartStr, FString& FinishStr,
	bool const bActualOrPlanned)
{
	if (!JsonObj->TryGetStringField(bActualOrPlanned ? TEXT("actualStart") : TEXT("plannedStart"), StartStr)
		|| StartStr.IsEmpty())
	{
		return false;
	}
	if (!JsonObj->TryGetStringField(bActualOrPlanned ? TEXT("actualFinish") : TEXT("plannedFinish"), FinishStr)
		|| FinishStr.IsEmpty())
	{
		return false;
	}
	return FDateTime::ParseIso8601(*StartStr, Start) && FDateTime::ParseIso8601(*FinishStr, Finish);
}

void FITwinSchedulesImport::FImpl::ParseTaskDetails(TSharedPtr<FJsonObject> const& JsonObj,
	size_t const SchedIdx, size_t const TaskInVec, FLock& Lock)
{
	auto& Sched = Schedules[SchedIdx];
	auto& Task = Sched.Tasks[TaskInVec];
	Task.Name = JsonObj->GetStringField(TEXT("name"));
	// Using  "Best dates" (Actual if any, Planned otw) like Synchro/Pineapple/DesignReview
	FDateTime Start, Finish, ActualStart, ActualFinish; FString StartStr, FinishStr;
	bool bUsesActualDates = true;
	bool bCouldParseDates =
		ParseTaskDates(JsonObj, ActualStart, ActualFinish, StartStr, FinishStr, bUsesActualDates);
	bool const bHasActualDates = bCouldParseDates; // might be invalid (Start > Finish)
	if (!bCouldParseDates || ActualStart > ActualFinish)
	{
		bUsesActualDates = false;
		bCouldParseDates = ParseTaskDates(JsonObj, Start, Finish, StartStr, FinishStr, bUsesActualDates);
		// either the Planned ones, or the Actual ones if !bCouldParseDates
		if ((bHasActualDates && ActualStart > ActualFinish)
			|| (bCouldParseDates && Start > Finish))
		{
			bCouldParseDates |= bHasActualDates;
			if (bCouldParseDates)
				Start = Finish;
			else
			{
				bUsesActualDates = true;
				Start = Finish = ActualFinish;
			}
			BE_LOGW("ITwin4DImp", "Task " << TCHAR_TO_UTF8(*Task.Id) << " named '"
				<< TCHAR_TO_UTF8(*Task.Name) << "' for schedule Id " << TCHAR_TO_UTF8(*Sched.Id)
				<< " had Start > Finish => assigned Finish to Start so now both are: "
				<< TCHAR_TO_UTF8(*StartStr)
				<< " (" << (bUsesActualDates ? "Actual" : "Planned") << " dates)");
		}
	}
	else
	{
		Start = ActualStart;
		Finish = ActualFinish;
	}
	if (ensure(bCouldParseDates))
	{
		Task.TimeRange.first = ITwin::Time::FromDateTime(Start);
		Task.TimeRange.second = ITwin::Time::FromDateTime(Finish);
		S4D_VERBOSE(TEXT("Task %s named '%s' for schedule Id %s spans %s to %s ('%s' dates)"),
					*Task.Id, *Task.Name, *Sched.Id, *StartStr, *FinishStr,
					bUsesActualDates ? TEXT("Actual") : TEXT("Planned"));
		CompletedProperty(Sched, Task.Bindings, Lock, TEXT("TaskDetails"));
	}
	else
	{
		Task.TimeRange = ITwin::Time::Undefined();
		BE_LOGE("ITwin4DImp", "Task " << TCHAR_TO_UTF8(*Task.Id) << " named '" << TCHAR_TO_UTF8(*Task.Name)
			<< "' for schedule Id " << TCHAR_TO_UTF8(*Sched.Id) << " has invalid date(s)!");
	}
}

void FITwinSchedulesImport::FImpl::RequestTask(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto& Sched = Schedules[SchedIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, TEXT("tasks"), Schedules[SchedIdx].AnimationBindings[AnimIdx].TaskId },
		{},
		[this, SchedIdx, AnimIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& JsonObj = bUseAPIM ? Reply->GetObjectField(TEXT("task")) : Reply;
			if (!ensure(JsonObj))
				return;
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
			auto& Binding = Sched.AnimationBindings[AnimIdx];
			ParseTaskDetails(JsonObj, SchedIdx, Sched.AnimationBindings[AnimIdx].TaskInVec, Lock);
		});
}

// 4dschedule/v1/schedules/{scheduleId}/tasks/{id}/resourceAssignments: should still be possible with Legacy
// schedules, if I understand correctly, but we should no longer needed them now that animation bindings
// are available - blame here to retrieve
//void FITwinSchedulesImport::FImpl::DetermineTaskElements(...)

// 4dschedule/v1/schedules/{scheduleId}/resources/{id}/entity3Ds: should still be possible with Legacy
// schedules, if I understand correctly, but we should no longer needed them now that animation bindings
// are available - blame here to retrieve
//void FITwinSchedulesImport::FImpl::RequestResourceEntity3Ds(...)

namespace ITwin::Timeline
{
	float fProbaOfOpacityAnimation = 0.5f;
}

void FITwinSchedulesImport::FImpl::CreateRandomAppearanceProfile(size_t const SchedIdx, size_t const AnimIdx,
	FLock& Lock, bool const bCreateGroupFromResource3DEntities/*= false*/)
{
	using ITwin::Timeline::fProbaOfOpacityAnimation;
	auto&& Sched = Schedules[SchedIdx];
	auto& AnimationBinding = Sched.AnimationBindings[AnimIdx];
	size_t Seed = std::hash<decltype(AnimationBinding.AnimatedEntities)>()(AnimationBinding.AnimatedEntities);
	boost::hash_combine(Seed, GetTypeHash(AnimationBinding.TaskId));

	float CrudeFloatRand = 0.f;
	FVector const RandClr = FITwinMathExts::RandomFloatColorFromIndex(
		Seed, fProbaOfOpacityAnimation > 0.f ? &CrudeFloatRand : nullptr);
	constexpr bool bUseOriginalColorBeforeTask = true;
	constexpr bool bUseOriginalColorAfterTask = false;
	constexpr bool bUseGrowthSimulation = true;
	bool const bTestOpacityAnimation =
		(fProbaOfOpacityAnimation > 0.f) ? (fProbaOfOpacityAnimation >= CrudeFloatRand) : false;
	if (bCreateGroupFromResource3DEntities)
	{
		size_t const Dummy = Schedules[SchedIdx].AppearanceProfiles.size();
		Sched.KnownAppearanceProfiles[AnimationBinding.AppearanceProfileId] = Dummy;
		AnimationBinding.AppearanceProfileInVec = Dummy;
		Sched.AppearanceProfiles.emplace_back();
	}

	auto& AppearanceProfile = Sched.AppearanceProfiles[AnimationBinding.AppearanceProfileInVec];
	AppearanceProfile = FAppearanceProfile{
		{ VersionToken::None, std::vector<size_t>{} },
		EProfileAction::Install,
		FSimpleAppearance(
			RandClr,
			/* translucency at start*/bTestOpacityAnimation ? .1f : 1.f,
			bUseOriginalColorBeforeTask, 
			!bTestOpacityAnimation // use original color / alpha?
		),
		FActiveAppearance{
			FSimpleAppearance(
				FMath::Lerp(RandClr, FVector::OneVector, 0.5),
				bTestOpacityAnimation ? 0.25f : 1.f, // translucency at start / end of task
				false, !bTestOpacityAnimation // use original color / alpha?
			),
			FVector(1, 1, 1), // custom growth dir
			bTestOpacityAnimation ? 0.9f : 1.f, // finish alpha
			bUseGrowthSimulation ? (EGrowthSimulationMode)(Seed % 8) : EGrowthSimulationMode::None,
			true, true, // unimpl
			false // invert growth
		},
		FSimpleAppearance{
			0.5 * RandClr,
			1.f, // translucency at start / end ('end' one unused tho...)
			bUseOriginalColorAfterTask, true // use original color / alpha?
		}
	};
	if (bCreateGroupFromResource3DEntities)
	{
		size_t const ElementsGroupInVec = Schedules[SchedIdx].Groups.size();
		AnimationBinding.AnimatedEntities = AnimationBinding.TaskId;//reuse as groupId
		AnimationBinding.AppearanceProfileId = TEXT("<DummyAppearanceProfileId>");
		AnimationBinding.GroupInVec = ElementsGroupInVec;
	}
	S4D_VERBOSE(TEXT("Random appearance profile used for %s"), *AnimationBinding.ToString());
	CompletedProperty(Sched, AppearanceProfile.Bindings, Lock, TEXT("RandomAppearance"));
}

EProfileAction FITwinSchedulesImport::FImpl::ParseProfileAction(FString const& FromStr)
{
	if (ensure(!FromStr.IsEmpty()))
	{
		switch (FromStr.ToLower()[0])
		{
		case 'i':
			return EProfileAction::Install;
		case 'r':
			return EProfileAction::Remove;
		case 't':
			return EProfileAction::Temporary;
		case 'm':
			return EProfileAction::Maintenance;
		case 'n':
			return EProfileAction::Neutral;
		default:
			ensure(false);
			break;
		}
	}
	return EProfileAction::Neutral;
}

/// Success already 'ensure'd, so no obligation to test the result
bool FITwinSchedulesImport::FImpl::ColorFromHexString(FString const& FromStr, FVector& Color)
{
	if (FromStr.Len() < 6)
	{
		ensure(false);
		return false;
	}
	uint64 const Clr = FCString::Strtoui64(*FromStr.Right(6), nullptr, /*base*/16);
	Color.X = ((Clr & 0xFF0000) >> 16) / 255.;
	Color.Y = ((Clr & 0x00FF00) >> 8) / 255.;
	Color.Z =  (Clr & 0x0000FF) / 255.;
	return true;
}

/// Success already 'ensure'd, so no obligation to test the result
bool FITwinSchedulesImport::FImpl::ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out)
{
	JSON_GETNUMBER_OR(JsonObj, "x", Out.X, return false)
	JSON_GETNUMBER_OR(JsonObj, "y", Out.Y, return false)
	JSON_GETNUMBER_OR(JsonObj, "z", Out.Z, return false)
	return true;
}

/// Note: direction of growth kept in iTwin axes convention (and growth never uses relative coordinates).
/// Success already 'ensure'd, so no obligation to test the result
bool FITwinSchedulesImport::FImpl::ParseGrowthSimulationMode(FString const& FromStr,
															 EGrowthSimulationMode& Mode)
{
	if (ensure(FromStr.Len() >= 2))
	{
		auto const Lower = FromStr.ToLower();
		switch (Lower[0])
		{
		case 'b':
			if (Lower[1] == 'o')
				Mode = EGrowthSimulationMode::Bottom2Top;
			else if (Lower[1] == 'a')
				Mode = EGrowthSimulationMode::Back2Front;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 't': Mode = EGrowthSimulationMode::Top2Bottom; break;
		case 'l': Mode = EGrowthSimulationMode::Left2Right; break;
		case 'r': Mode = EGrowthSimulationMode::Right2Left; break;
		case 'f': Mode = EGrowthSimulationMode::Front2Back; break;
		case 'c': Mode = EGrowthSimulationMode::Custom; break;
		case 'n': Mode = EGrowthSimulationMode::None; break;
		case 'u': Mode = EGrowthSimulationMode::Unknown; break;
		default: return false;
		}
		return true;
	}
	return false;
}

bool FITwinSchedulesImport::FImpl::ParseSimpleAppearance(FSimpleAppearance& Appearance,
	bool const bBaseOfActiveAppearance, TSharedPtr<FJsonObject> const& JsonObj)
{
	FString ColorStr;
	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool OrgCol = true, OrgTransp = true;
	JSON_GETBOOL_OR(JsonObj, "useOriginalColor", OrgCol, return false)
	JSON_GETBOOL_OR(JsonObj, "useOriginalTransparency", OrgTransp, return false)
	OrgTransp &= !s_bDebugForcePartialTransparencies;
	OrgTransp |= s_bDebugNoPartialTransparencies;
	Appearance.bUseOriginalColor = OrgCol;
	Appearance.bUseOriginalAlpha = OrgTransp;
	if (!OrgCol)
	{
		JSON_GETSTR_OR(JsonObj, "color", ColorStr, return false)
		if (!ColorFromHexString(ColorStr, Appearance.Color))
			return false;
	}
	if (!OrgTransp)
	{
		if (bBaseOfActiveAppearance)
			JSON_GETNUMBER_OR(JsonObj, "startTransparency", Appearance.Alpha, return false)
		else
			JSON_GETNUMBER_OR(JsonObj, "transparency", Appearance.Alpha, return false)
		if constexpr (s_bDebugForcePartialTransparencies)
			Appearance.Alpha = 0.3f;
		else
			Appearance.Alpha = std::clamp(1.f - Appearance.Alpha / 100.f, 0.f, 1.f);
	}
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseActiveAppearance(FActiveAppearance& Appearance,
														 TSharedPtr<FJsonObject> const& JsonObj)
{
	if (!ParseSimpleAppearance(Appearance.Base, true, JsonObj))
		return false;
	JSON_GETNUMBER_OR(JsonObj, "finishTransparency", Appearance.FinishAlpha, return false)
	if constexpr (s_bDebugNoPartialTransparencies)
		Appearance.FinishAlpha = 1.f; // already the default
	else if constexpr (s_bDebugForcePartialTransparencies)
		Appearance.FinishAlpha = 0.3f;
	else
		Appearance.FinishAlpha = std::clamp(1.f - Appearance.FinishAlpha / 100.f, 0.f, 1.f);
	TSharedPtr<FJsonObject> const* GrowthObj;
	JSON_GETOBJ_OR(JsonObj, "growthSimulation", GrowthObj, return false)
	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool GroPercent = true, GroPause = true, InvertGro = true;
	JSON_GETBOOL_OR(*GrowthObj, "adjustForTaskPercentComplete", GroPercent, return false)
	JSON_GETBOOL_OR(*GrowthObj, "pauseDuringNonWorkingTime", GroPause, return false)
	JSON_GETBOOL_OR(*GrowthObj, "simulateAsRemove", InvertGro, return false)
	Appearance.bGrowthSimulationBasedOnPercentComplete = GroPercent;
	Appearance.bGrowthSimulationPauseDuringNonWorkingTime = GroPause;
	Appearance.bInvertGrowth = InvertGro;
	FString GrowthModeStr;
	JSON_GETSTR_OR(*GrowthObj, "mode", GrowthModeStr, return false)
	if (!ParseGrowthSimulationMode(GrowthModeStr, Appearance.GrowthSimulationMode))
		return false;
	TSharedPtr<FJsonObject> const* GrowthVecObj;
	JSON_GETOBJ_OR(*GrowthObj, "direction", GrowthVecObj, return false)
	if (!ParseVector(*GrowthVecObj, Appearance.GrowthDirectionCustom)) return false;
	return true;
}

void FITwinSchedulesImport::FImpl::ParseAppearanceProfileDetails(TSharedPtr<FJsonObject> const& JsonObj,
	size_t const SchedIdx, FString const AppearanceProfileId, size_t const AppearanceProfileInVec, FLock& Lock)
{
	FAppearanceProfile Parsed;
	FString ProfileTypeStr;
	JSON_GETSTR_OR(JsonObj, "action", ProfileTypeStr, return)
	Parsed.ProfileType = ParseProfileAction(ProfileTypeStr);
	bool const bNeedStartAppearance = EProfileAction::Install != Parsed.ProfileType
								   && EProfileAction::Temporary != Parsed.ProfileType;
	bool const bNeedFinishAppearance = EProfileAction::Remove != Parsed.ProfileType
									&& EProfileAction::Temporary != Parsed.ProfileType;
	TSharedPtr<FJsonObject> const *StartObj = nullptr, *ActiveObj, *EndObj = nullptr;
	if (bNeedStartAppearance)
		JSON_GETOBJ_OR(JsonObj, "startAppearance", StartObj, return)
	JSON_GETOBJ_OR(JsonObj, "activeAppearance", ActiveObj, return)
	if (bNeedFinishAppearance)
		JSON_GETOBJ_OR(JsonObj, "endAppearance", EndObj, return)

	auto& Sched = Schedules[SchedIdx];
	auto& AppearanceProfile = Sched.AppearanceProfiles[AppearanceProfileInVec];
	if ((bNeedStartAppearance && !ParseSimpleAppearance(Parsed.StartAppearance, false, *StartObj))
		|| !ParseActiveAppearance(Parsed.ActiveAppearance, *ActiveObj)
		|| (bNeedFinishAppearance && !ParseSimpleAppearance(Parsed.FinishAppearance, false, *EndObj)))
	{
		BE_LOGE("ITwin4DImp", "Error reading appearance profile " << TCHAR_TO_UTF8(*AppearanceProfileId));
		return;
	}
	// swap with the empty Parsed.Bindings so that we can move the whole thing
	Parsed.Bindings.swap(AppearanceProfile.Bindings); // don't lose this
	AppearanceProfile = std::move(Parsed);
	CompletedProperty(Sched, AppearanceProfile.Bindings, Lock, TEXT("Appearance"));
}

void FITwinSchedulesImport::FImpl::RequestAppearanceProfile(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, size_t const AnimIdx,
	FLock& Lock)
{
	if (!UnitTesting && SchedulesComponent().bDebugWithRandomProfiles)
	{
		CreateRandomAppearanceProfile(SchedIdx, AnimIdx, Lock);
		return;
	}
	auto&& Sched = Schedules[SchedIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, bUseAPIM ? TEXT("appearance-profiles") : TEXT("appearanceProfiles"),
		  Sched.AnimationBindings[AnimIdx].AppearanceProfileId },
		{},
		[this, SchedIdx, AnimIdx, &Token](TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& JsonObj = bUseAPIM ? Reply->GetObjectField(TEXT("appearanceProfile")) : Reply;
			if (!ensure(JsonObj))
				return;
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
			auto& Binding = Sched.AnimationBindings[AnimIdx];
			ParseAppearanceProfileDetails(JsonObj, SchedIdx, Binding.AppearanceProfileId,
										  Binding.AppearanceProfileInVec, Lock);
		});
}

/// Note: anchor point semantic kept in iTwin axes convention.
/// Success already 'ensure'd, so no obligation to test the result
bool FITwinSchedulesImport::FImpl::Parse3DPathAlignment(FString const& FromStr,
	std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor)
{
	if (ensure(FromStr.Len() >= 4))
	{
		auto const Lower = FromStr.ToLower();
		switch (Lower[0])
		{
		case 'c':
			if (Lower == TEXT("custom"))
				Anchor = FVector::Zero(); // make an FVector variant - all other cases use the enum
			else if (Lower == TEXT("center"))
				Anchor = ITwin::Timeline::EAnchorPoint::Center;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 'm':
			if (Lower[1] == 'i' && Lower[2] == 'n')
			{
				if (Lower[3] == 'x')
					Anchor = ITwin::Timeline::EAnchorPoint::MinX;
				else if (Lower[3] == 'y')
					Anchor = ITwin::Timeline::EAnchorPoint::MinY;
				else if (Lower[3] == 'z')
					Anchor = ITwin::Timeline::EAnchorPoint::MinZ;
				else
				{
					ensure(false);
					return false;
				}
			}
			else if (Lower[1] == 'a' && Lower[2] == 'x')
			{
				if (Lower[3] == 'x')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxX;
				else if (Lower[3] == 'y')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxY;
				else if (Lower[3] == 'z')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxZ;
				else
				{
					ensure(false);
					return false;
				}
			}
			else
				return false;
			break;
		case 'o':
			Anchor = ITwin::Timeline::EAnchorPoint::Original;
			break;
		default:
			ensure(false);
			return false;
		}
		return true;
	}
	return false;
}

void FITwinSchedulesImport::FImpl::ParseStaticTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
	size_t const SchedIdx, FLock* pLock/*= nullptr*/, std::optional<size_t> const AnimIdx/*= {}*/)
{
	auto&& TransfoArray = JsonObj->GetArrayField(TEXT("transform"));
	if (!ensure(TransfoArray.Num() == 16))
		return;
	FMatrix Mat;
	// From https://rodolphe-vaillant.fr/entry/145/unreal-engine-c-tmap-doc-sheet-1:
	// Matrix elements are accessed with: FMatrix::M[rowIndex][columnIndex]
	// Unreal's convention is to use a row-matrix representation:
	//	X.x  X.y  X.z  0.0 // Basis vector X
	//	Y.x  Y.y  Y.z  0.0 // Basis vector Y
	//	Z.x  Z.y  Z.z  0.0 // Basis vector Z
	//	T.x  T.y  T.z  1.0 // Translation vector
	// 4D api gives the matrix row by row, interpreted with the normal math convention,
	// ie. the first axis of the new base is { M[0][0], M[1][0], M[2][0] }
	for (int Col = 0; Col < 4; ++Col)
	{
		for (int Row = 0; Row < 4; ++Row)
		{
			// instead of Mat.M[Row][Col], because of the above
			double& DestVal = Mat.M[Col][Row];
			if (!TransfoArray[4 * Row + Col]->TryGetNumber(DestVal))
			{
				ensure(false); return;
			}
			// Doing this introduces a Scale.X:=-1, which is then lost when converting to a
			// keyframe. And it didn't even seem it would've worked with this anyway :/
			// See FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe and
			// comment in AddStaticTransformToTimeline... -_-
			//if (1 == Col)
			//	DestVal *= -1.;
		}
	}
	std::optional<FLock> optLock;
	if (!pLock) optLock.emplace(Mutex);
	auto& Sched = Schedules[SchedIdx];
	FTransformAssignment* pTransformAssignment;
	if (AnimIdx) // in case of standalone request, it comes from a binding => index is known
	{
		pTransformAssignment =
			&Sched.TransfoAssignments[Sched.AnimationBindings[*AnimIdx].TransfoAssignmentInVec];
	}
	else // in case of batched transfo assignment request => index is unknown, entry may or may not exist
	{
		// Create static transformation assignment directly instead of using EmplaceProperty: they do not
		// depend on any subproperty, which is witnessed by the fact that CompletedProperty is called
		// directly below.
		FString TransfoAssignmentId = JsonObj->GetStringField(TEXT("id"));
		auto const Known = Sched.KnownTransfoAssignments.try_emplace(std::make_pair(TransfoAssignmentId, true),
																	 Sched.TransfoAssignments.size());
		if (Known.second) // was inserted
			Sched.TransfoAssignments.emplace_back();
		pTransformAssignment = &Sched.TransfoAssignments[Known.first->second];
	}
	pTransformAssignment->Transformation = FTransform(Mat);
	CompletedProperty(Sched, pTransformAssignment->Bindings, pLock ? (*pLock) : (*optLock),
					  TEXT("StaticTransfoAssign"));
}

void FITwinSchedulesImport::FImpl::RequestAllStaticTransfoAssignments(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, std::optional<FString> PageToken,
	FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto RequestArgList = ConcatPaginationParams({}, PageToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, (bUseAPIM ? TEXT("animation-3d-transforms") : TEXT("animation3dTransforms")) },
		std::move(RequestArgList),
		[this, SchedIdx, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			auto Items = Reply->GetArrayField(bUseAPIM ? TEXT("animation3dTransforms") : TEXT("items"));
			FLock Lock(Mutex);
			auto&& Sched = Schedules[SchedIdx];
			S4D_VERBOSE(TEXT(
				"Received %d static transfo. assignments (total static+3D paths: %d%s) for schedule '%s'"),
				Items.Num(), (int)Sched.TransfoAssignments.size() + Items.Num(),
				bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
				*Schedules[SchedIdx].Name);
			if (bMoreToCome)
			{
				RequestAllStaticTransfoAssignments(Token, SchedIdx, NextPageToken, Lock);
			}
			Sched.TransfoAssignments.reserve(Sched.TransfoAssignments.size() + Items.Num());
			for (auto&& TransfoObj : Items)
				ParseStaticTransfoAssignment(TransfoObj->AsObject(), SchedIdx, &Lock);
			Sched.StatisticsCurrent.Animation3dTransformCount += (size_t)Items.Num();
			OnScheduleDownloadProgressed(Sched, Lock);
		});
}

void FITwinSchedulesImport::FImpl::Parse3DPathTransfoAssignment(TSharedPtr<FJsonObject> const& JsonObj,
	size_t const SchedIdx, ReusableJsonQueries::FStackingToken const& Token, FLock* pLock/*= nullptr*/,
	std::optional<size_t> const AnimIdx/*= {}*/)
{
	FPathAssignment PathAssignment;
	JSON_GETSTR_OR(JsonObj, "pathId", PathAssignment.Animation3DPathId, return);
	FString Alignment;
	JSON_GETSTR_OR(JsonObj, "alignment", Alignment, return);
	if (!Parse3DPathAlignment(Alignment, PathAssignment.TransformAnchor))
	{
		BE_LOGE("ITwin4DImp", "Parsing error for 3D path 'alignment', with value: "
			<< TCHAR_TO_UTF8(*Alignment));
		return;
	}
	if (std::holds_alternative<FVector>(PathAssignment.TransformAnchor))
	{
		TSharedPtr<FJsonObject> const* CenterObj;
		JSON_GETOBJ_OR(JsonObj, "center", CenterObj, return);
		if (!ParseVector(*CenterObj, std::get<1>(PathAssignment.TransformAnchor)))
		{
			BE_LOGE("ITwin4DImp", "Parsing error for 3D path custom alignment, from: "
				<< TCHAR_TO_UTF8(*ToString(*CenterObj)));
			return;
		}
	}
	JSON_GETBOOL_OR(JsonObj, "reverseDirection", PathAssignment.b3DPathReverseDirection, return);

	std::optional<FLock> optLock;
	if (!pLock) optLock.emplace(Mutex);
	FLock& Lock = pLock ? (*pLock) : (*optLock);
	auto& Sched = Schedules[SchedIdx];
	FTransformAssignment* pTransformAssignment;
	size_t TransfoAssignmentIndex = ITwin::INVALID_IDX;
	if (AnimIdx)
	{
		TransfoAssignmentIndex = Sched.AnimationBindings[*AnimIdx].TransfoAssignmentInVec;
		pTransformAssignment = &Sched.TransfoAssignments[TransfoAssignmentIndex];
	}
	else
	{
		::Detail::FInsertionFlag TransfoPropCreated;
		auto TransfoAssignmentIncomplete = EmplaceProperty(ITwin::INVALID_IDX,
			std::make_pair(JsonObj->GetStringField(TEXT("id")), /*bStaticTransform*/false),
			TransfoAssignmentIndex, Sched.TransfoAssignments, Sched.KnownTransfoAssignments,
			TransfoPropCreated, Lock);
		pTransformAssignment = &Sched.TransfoAssignments[TransfoAssignmentIndex];
	}
	pTransformAssignment->Transformation = std::move(PathAssignment);
	auto& TransfoAsPath = std::get<1>(pTransformAssignment->Transformation);
	::Detail::FInsertionFlag KeyframesPropCreated;
	auto&& KeyframeListIncomplete = EmplaceProperty(AnimIdx ? (*AnimIdx) : ITwin::INVALID_IDX,
		TransfoAsPath.Animation3DPathId, TransfoAsPath.Animation3DPathInVec, Sched.Animation3DPaths,
		Sched.KnownAnimation3DPaths, KeyframesPropCreated, Lock);
	if (KeyframesPropCreated.Inserted()) // need to query the newly created/discovered 3D Path's keyframes
	{
		// Transfer the responsibility of checking and notifying the completed bindings (since
		// there is no other sub-property the TransfoAssignment depends on)
		Sched.Animation3DPaths[TransfoAsPath.Animation3DPathInVec].Bindings =
			std::move(pTransformAssignment->Bindings);
		if (!PrefetchWholeSchedule())
			Request3DPathKeyframes(Token, SchedIdx, TransfoAssignmentIndex, {}, Lock);
	}
	else if (KeyframeListIncomplete.second == false)
		CompletedProperty(Sched, pTransformAssignment->Bindings, Lock, TEXT("Path3dAssign"));
	else
	{
		// incomplete but already queried: wait for completion, but also transfer responsibility
		// of checking and notifying the completed bindings (same reason as above)
		std::copy(pTransformAssignment->Bindings.cbegin(), pTransformAssignment->Bindings.cend(),
			std::back_inserter(
				Sched.Animation3DPaths[TransfoAsPath.Animation3DPathInVec].Bindings));
		// Note: this assumes a 1:1 relationship between a path and its list of keyframes
		pTransformAssignment->Bindings.clear();
	}
}

void FITwinSchedulesImport::FImpl::RequestAll3DPathTransfoAssignments(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, std::optional<FString> PageToken,
	FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto RequestArgList = ConcatPaginationParams({}, PageToken);
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, (bUseAPIM ? TEXT("animation-3d-path-assignments") : TEXT("animation3dPathAssignments")) },
		std::move(RequestArgList),
		[this, SchedIdx, &Token] (TSharedPtr<FJsonObject> const& Reply)
		{
			FString NextPageToken;
			bool const bMoreToCome = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			auto Items = Reply->GetArrayField(bUseAPIM ? TEXT("animation3dPathAssignments") : TEXT("items"));
			FLock Lock(Mutex);
			auto&& Sched = Schedules[SchedIdx];
			S4D_VERBOSE(TEXT(
				"Received %d 3D path transfo. assignments (total static+3D paths: %d%s) for schedule '%s'"),
				Items.Num(), (int)Sched.TransfoAssignments.size() + Items.Num(),
				bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
				*Schedules[SchedIdx].Name);
			if (bMoreToCome)
			{
				RequestAll3DPathTransfoAssignments(Token, SchedIdx, NextPageToken, Lock);
			}
			Sched.TransfoAssignments.reserve(Sched.TransfoAssignments.size() + Items.Num());
			for (auto&& TransfoObj : Items)
				Parse3DPathTransfoAssignment(TransfoObj->AsObject(), SchedIdx, Token, &Lock);
			Sched.StatisticsCurrent.Animation3dPathAssignmentCount += (size_t)Items.Num();
			OnScheduleDownloadProgressed(Sched, Lock);
		});
}

void FITwinSchedulesImport::FImpl::RequestAnimationBindingTransfoAssignment(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto&& AnimationBinding = Sched.AnimationBindings[AnimIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id,
		  AnimationBinding.bStaticTransform
			? (bUseAPIM ? TEXT("animation-3d-transforms") : TEXT("animation3dTransforms"))
			: (bUseAPIM ? TEXT("animation-3d-path-assignments") : TEXT("animation3dPathAssignments")),
		  AnimationBinding.TransfoAssignmentId
		},
		{},
		[this, SchedIdx, AnimIdx, &Token, bStaticTransform = AnimationBinding.bStaticTransform]
		(TSharedPtr<FJsonObject> const& Reply)
		{
			auto JsonObj = bUseAPIM
				? Reply->GetObjectField(bStaticTransform ? TEXT("animation3dTransform")
														 : TEXT("animation3dPathAssignment"))
				: Reply;
			if (!JsonObj)
				return;
			if (bStaticTransform)
				ParseStaticTransfoAssignment(JsonObj, SchedIdx, nullptr, AnimIdx);
			else
				Parse3DPathTransfoAssignment(JsonObj, SchedIdx, Token, nullptr, AnimIdx);
		});
}

/// \param TransfoAssignmentIdx Index of one (of possibly several) FTransformAssignment pointing at this path:
///		easier than passing the path Id and path index in the schedule's vector
void FITwinSchedulesImport::FImpl::Request3DPathKeyframes(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const TransfoAssignmentIdx, std::optional<FString> const PageToken,
	FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto ArgList = ConcatPaginationParams({}, PageToken);
	bool const bFirstPage = !((bool)PageToken);
	FUrlSubpath SubPath{ Sched.Id, bUseAPIM ? TEXT("animation-3d-paths") : TEXT("animation3dPaths") };
	if (ITwin::INVALID_IDX != TransfoAssignmentIdx)
	{
		// old behavior (non-batched): ask for keyframes of a specific 3D path
		SubPath.emplace_back(
			std::get<1>(Sched.TransfoAssignments[TransfoAssignmentIdx].Transformation).Animation3DPathId);
	}
	SubPath.emplace_back(TEXT("keyframes"));
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, std::move(SubPath), std::move(ArgList),
		[this, SchedIdx, TransfoAssignmentIdx, bFirstPage, &Token] (TSharedPtr<FJsonObject> const& JsonObj)
		{
			auto&& KeyframesArray = JsonObj->GetArrayField(bUseAPIM ? TEXT("keyframes") : TEXT("items"));
			if (KeyframesArray.IsEmpty())
				return;
			FAnimation3DPath Parsed;
			Parsed.Keyframes.reserve(KeyframesArray.Num());
			// new behavior (batched): we'll receive the same kind of array of keyframes, the only difference
			// is that each keyframe might point at a different pathId, although the keyframes of each path
			// are most certainly streamed sequentially
			bool const bMixedKeyframes = (ITwin::INVALID_IDX == TransfoAssignmentIdx);
			std::vector<FString> KeyframePathIds;
			if (bMixedKeyframes)
				KeyframePathIds.reserve(KeyframesArray.Num());
			for (auto&& Entry : KeyframesArray)
			{
				auto&& KeyframeObj = Entry->AsObject();
				FTransformKey Keyframe;
				JSON_GETNUMBER_OR(KeyframeObj, "time", Keyframe.RelativeTime, continue)
				TSharedPtr<FJsonObject> const *PosObj, *RotObj;
				JSON_GETOBJ_OR(KeyframeObj, "position", PosObj, continue)
				FVector Pos, RotAxis;
				if (!ParseVector(*PosObj, Pos)) continue;
				Keyframe.Transform = FTransform(Pos);
				bool bSkipRotation = false; // support optional rotation
				JSON_GETOBJ_OR(KeyframeObj, "rotation", RotObj, bSkipRotation = true)
				double AngleDegrees = 0.;
				if (!bSkipRotation)
				{
					JSON_GETNUMBER_OR(*RotObj, "angle", AngleDegrees, bSkipRotation = true)
				}
				if (!bSkipRotation && AngleDegrees != 0. && ParseVector(*RotObj, RotAxis))
				{
					Keyframe.Transform.SetRotation(FQuat(RotAxis, FMath::DegreesToRadians(AngleDegrees)));
				}
				// At the end so an invalid keyframe will not be in the Parsed.Keyframes nor KeyframePathIds
				if (bMixedKeyframes)
				{
					KeyframePathIds.emplace_back();
					JSON_GETSTR_OR(KeyframeObj, "pathId", KeyframePathIds.back(), continue);
				}
				Parsed.Keyframes.emplace_back(Keyframe);
			}
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
			auto Finalize3DPath = [this, &Sched, &Lock](FAnimation3DPath& Path3D)
				{
					// A bit overkill but probably safer to sort the whole vector even though the only
					// requirement is that Add3DPathTransformToTimeline needs the first frame of the list
					// (usually where t=0)
					std::sort(Path3D.Keyframes.begin(), Path3D.Keyframes.end(),
						[](FTransformKey const& J, FTransformKey const& K)
						{ return J.RelativeTime < K.RelativeTime; });
					CompletedProperty(Sched, Path3D.Bindings, Lock, TEXT("Path3d"));
				};
			FString NextPageToken;
			bool const bMoreToCome = JsonObj->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
			static size_t TotalParsedKeyframes = 0;
			if (bFirstPage)
				TotalParsedKeyframes = KeyframesArray.Num();
			else
				TotalParsedKeyframes += KeyframesArray.Num();
			S4D_VERBOSE(TEXT("Received %d 3D path keyframes (total: %llu%s) for schedule '%s'"),
				KeyframesArray.Num(), TotalParsedKeyframes,
				bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"), *Schedules[SchedIdx].Name);
			if (bMoreToCome)
				Request3DPathKeyframes(Token, SchedIdx, TransfoAssignmentIdx, std::move(NextPageToken), Lock);
			if (bMixedKeyframes)
			{
				ensure(Parsed.Keyframes.size() == KeyframePathIds.size());
				for (size_t K = 0; K < std::min(Parsed.Keyframes.size(), KeyframePathIds.size()); ++K)
				{
					FString const& PathId = KeyframePathIds[K];
					if (!ensure(!PathId.IsEmpty()))
						continue;
					// Create path directly instead of using EmplaceProperty: paths don't depend on any
					// subproperty, BUT they will be complete only after possibly more requests, because
					// of pagination, so we need to put something in Bindings to mark it as incomplete
					// until we reach "!bMoreToCome"
					auto Known = Sched.KnownAnimation3DPaths.try_emplace(PathId, Sched.Animation3DPaths.size());
					if (Known.second) // was inserted
						Sched.Animation3DPaths.emplace_back();
					auto& Path3D = Sched.Animation3DPaths[Known.first->second];
					if (Known.second)
						Path3D.Bindings.push_back(ITwin::INVALID_IDX); // see above comment
					Path3D.Keyframes.push_back(Parsed.Keyframes[K]);
				}
				if (!bMoreToCome)
				{
					// Finalize ALL 3D paths: there should be no redundancy since their querying is batched
					// and no individual requests should have been made that could have already completed one
					for (auto&& Path3D : Sched.Animation3DPaths)
						Finalize3DPath(Path3D);
				}
			}
			else
			{
				auto& Path3D = Sched.Animation3DPaths[
					std::get<1>(Sched.TransfoAssignments[TransfoAssignmentIdx].Transformation)
						.Animation3DPathInVec];
				if (bFirstPage)
				{
					Parsed.Bindings.swap(Path3D.Bindings); // don't lose this
					Path3D = std::move(Parsed);
				}
				else
				{
					size_t const CurrentSize = Path3D.Keyframes.size();
					Path3D.Keyframes.resize(CurrentSize + Parsed.Keyframes.size());
					std::copy(Parsed.Keyframes.begin(), Parsed.Keyframes.end(),
							  Path3D.Keyframes.begin() + CurrentSize);
				}
				if (!bMoreToCome)
					Finalize3DPath(Path3D);
			}
			Sched.StatisticsCurrent.Animation3dPathKeyframeCount += (size_t)KeyframesArray.Num();
			OnScheduleDownloadProgressed(Sched, Lock);
		});
}

void FITwinSchedulesImport::FImpl::SetSchedulesImportConnectors(
	FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
	FOnAnimationGroupModified const& InOnAnimationGroupModified,
	FFindElementIDFromGUID const& InFncElementIDFromGUID)
{
	FLock Lock(Mutex);
	if (ensure(InOnAnimationBindingAdded))
		OnAnimationBindingAdded = InOnAnimationBindingAdded;
	if (ensure(InOnAnimationGroupModified))
		OnAnimationGroupModified = InOnAnimationGroupModified;
	if (ensure(InFncElementIDFromGUID))
		FncElementIDFromGUID = InFncElementIDFromGUID;
}

void FITwinSchedulesImport::FImpl::ResetConnection(FString const& ITwinAkaProjectAkaContextId,
	FString const& IModelId, FString const& InChangesetId, FString const& CustomCacheDir)
{
	{
		FLock Lock(Mutex);
		// I can imagine the URL or the token could need updating (new mirror, auth renew),
		// but not the iTwin nor the iModel
		ensure((!Queries && ITwinId.IsEmpty() && TargetedIModelId.IsEmpty())
			|| (ITwinAkaProjectAkaContextId == ITwinId && IModelId == TargetedIModelId));

		SchedApiSession = s_NextSchedApiSession++;
		LastDisplayedQueueSizeIncrements = { -1, -1 };
		LastRoundedQueueSize = { -1, -1 };
		LastCheckTotalBindings = 0.;
		LastTotalBindingsFound = 0;
		ReusableJsonQueries::FStackedBatches Batches;
		ReusableJsonQueries::FStackedRequests Requests;
		SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
		if (!Queries)
		{
			ITwinId = ITwinAkaProjectAkaContextId;
			TargetedIModelId = IModelId;
			ChangesetId = InChangesetId;
			// See also comment about empty changeset in QueriesCache::GetCacheFolder():
			ensureMsgf(ChangesetId.ToLower() != TEXT("latest"), TEXT("Need to pass the resolved changeset!"));
			if (GetOnScheduleQueryingStatusChanged())
				GetOnScheduleQueryingStatusChanged()->Broadcast(true);
		}
		std::function<FString()> GetBearerToken = [] { return FString(); };
		if (!UnitTesting)
			GetBearerToken = [SchedComp = Owner->Owner]() -> FString
			{
				AITwinIModel const& IModel = *Cast<AITwinIModel const>(SchedComp->GetOwner());
				if (ensure(IModel.ServerConnection))
					return IModel.ServerConnection->GetAccessToken();
				else
					return TEXT("_TokenError_");
			};
		Queries = MakePimpl<FReusableJsonQueries>(
			UnitTesting ? (*UnitTesting->OwnerUObject.Get()) : (*Owner->Owner),
			GetSchedulesAPIBaseUrl(),
			[]()
			{
				static const FString AcceptJson(
					"application/json;odata.metadata=minimal;odata.streaming=true");
				const auto Request = FHttpModule::Get().CreateRequest();
				Request->SetHeader("Accept", AcceptJson);
				Request->SetHeader("Content-Type", AcceptJson);
				return Request;
			},
			/*SimultaneousRequestsAllowed =*/ 6,
			[this](FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
				bool bConnectedSuccessfully, bool const bWillRetry /*= false*/)
			{
				FString StrError;
				if (!AITwinServerConnection::CheckRequest(CompletedRequest, Response, bConnectedSuccessfully,
					&StrError, bWillRetry))
				{
					if (bUseAPIM && bConnectedSuccessfully && bWillRetry
						&& StrError.Contains(TEXT("InvalidSchedulesRequest"))
						&& StrError.Contains(TEXT("InvalidValue"))
						&& StrError.Contains(TEXT("Top value")))
					{
						SchedulesGeneration = EITwinSchedulesGeneration::NextGen;
					}
					else
					{
						if (!bHasFetchingErrors)
							FirstFetchingError = StrError;
						bHasFetchingErrors = true;
					}
					return false;
				}
				return true;
			},
			Mutex,
			(!Owner->Owner || Owner->Owner->DebugRecordSessionQueries.IsEmpty()
				|| !Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
			? nullptr : (*Owner->Owner->DebugRecordSessionQueries),
			SchedApiSession,
			(!Owner->Owner || Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
			? nullptr : (*Owner->Owner->DebugSimulateSessionQueries),
			GetOnScheduleQueryingStatusChanged(),
			GetBearerToken);
	} // end Lock

	ensure(!UnitTesting || !CustomCacheDir.IsEmpty());
	// We may be resetting known Schedules (OR running a unit test), in which case we kept their Id, Name
	// and Generation
	for (auto const& Sched : Schedules)
		OnFoundScheduleForTargetedIModel(Sched.Id, Sched.Name, CustomCacheDir, Sched.Generation);
	Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
		{ AutoRequestScheduleItems(Token, 0, Schedules.size(), nullptr); });
	// bHasFinishedPrefetching = false; No, ResetConnection doesn't reset the structures...
	// Call RequestSchedules even if we had kept the schedules Ids etc.: we may not have received them all
	// when Reset was called (unlikely as it is: we always have a single schedule in practice...)
	if (!UnitTesting)
	{
		Queries->NewBatch(
			[this](ReusableJsonQueries::FStackingToken const& Token) { RequestSchedules(Token); });
	}
	Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
		{
			// SchedulesGeneration is Unknown unless we got the $top error (see InvalidValue in above handler)
			// in that case we need to query again the bindings with the right $top value
			if (bUseAPIM && !Schedules.empty() && EITwinSchedulesGeneration::NextGen == SchedulesGeneration)
			{
				BindingsRequestPagination = 10'000;
				// just a tmp hack:
				ensure(1 == Schedules.size());
				FLock Lock(Mutex);
				RequestAnimationBindings(Token, 0, Lock);
			}
		});
	Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			if (Schedules.empty())
			{
				SetScheduleTimeRangeIsKnown();
			}
			else
			{
				if (PrefetchWholeSchedule())
				{
					bHasFinishedPrefetching = true;
					Queries->ClearCacheFromMemory();
					// Not the same as what's commented below!
					if (Owner->Owner)
						Owner->Owner->OnQueryLoopStatusChange(false);
					// ...because the method called directly above is NOT registered to the delegate
					// used below (see FITwinSynchro4DSchedulesInternals::Reset, 'prefetch' case):
					//if (GetOnScheduleQueryingStatusChanged())
					//	GetOnScheduleQueryingStatusChanged()->Broadcast(false);
				}
			}
		});
}

std::pair<int, int> FITwinSchedulesImport::FImpl::HandlePendingQueries()
{
	if (!Queries) return std::make_pair(0, 0);
	Queries->HandlePendingQueries();
	auto const& QueueSize = Queries->QueueSize();
#if WITH_EDITOR
	// Avoid flooding the logs... Log only every ~10% more requests processed since last time
	std::pair<int, int> const DisplayedQueueSizeIncrements = {
		10, // batches could be of vastly different sizes, but logging all of them is too much...
		//std::pow(10, std::floor(std::log10(std::max(1,  QueueSize.first)))),
		std::pow(10, std::floor(std::log10(std::max(10, QueueSize.second)))) };
	std::pair<int, int> const RoundedQueueSize = {
		QueueSize.first - (QueueSize.first % DisplayedQueueSizeIncrements.first),
		QueueSize.second - (QueueSize.second % DisplayedQueueSizeIncrements.second)
	};
	if (LastRoundedQueueSize != RoundedQueueSize
		|| LastDisplayedQueueSizeIncrements != DisplayedQueueSizeIncrements)
	{
		S4D_VERBOSE(TEXT("Still %d pending batches, and %d requests in current batch..."), QueueSize.first, QueueSize.second);
		LastRoundedQueueSize = RoundedQueueSize;
		LastDisplayedQueueSizeIncrements = DisplayedQueueSizeIncrements;
	}
#endif // WITH_EDITOR
	return QueueSize;
}

void FITwinSchedulesImport::FImpl::QueryEntireSchedules(FDateTime const FromTime, FDateTime const UntilTime,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted)
{
	if (!Queries /*|| Schedules.empty() <== No, query may still be in transit or pending!*/)
	{
		if (OnQueriesCompleted) OnQueriesCompleted(false);
		return;
	}
	Queries->NewBatch([this, FromTime, UntilTime](ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			std::optional<FTimeRangeInSeconds> TimeRange;
			if (FromTime < UntilTime)
			{
				TimeRange.emplace(FTimeRangeInSeconds{
					ITwin::Time::FromDateTime(FromTime), ITwin::Time::FromDateTime(UntilTime) });
			}
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
				RequestAnimationBindings(Token, SchedIdx, Lock, TimeRange);
		});
	// Not actually a new batch, just a way to have a function called upon completion
	// TODO_GCO: handle 'success' correctly by counting errors per batch in FReusableJsonQueries
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); },
			/*(informative)bPseudoBatch:*/true);
}

void FITwinSchedulesImport::FImpl::QueryAroundElementTasks(ITwinElementID const ElementID,
	FTimespan const MarginFromStart, FTimespan const MarginFromEnd,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted)
{
	if (!Queries /*|| Schedules.empty() <== No, query may still be in transit or pending!*/)
	{
		if (OnQueriesCompleted) OnQueriesCompleted(false);
		return;
	}
	if (!ensure(ITwin::NOT_ELEMENT != ElementID))
	{
		if (OnQueriesCompleted) OnQueriesCompleted(false);
		return;
	}
	Queries->NewBatch(
		[this, ElementID]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			std::set<ITwinElementID> ElementIDs{ ElementID };
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
				RequestAnimationBindings(Token, SchedIdx, Lock, {}, ElementIDs.begin(), ElementIDs.end());
		});

	Queries->NewBatch(
		[this, ElementID, MarginFromStart, MarginFromEnd]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			// Note: all Schedules currently merged in a single Timeline, hence the common extent
			// TODO_GCO => Schedules should be queried independently => one SchedulesApi per Schedule?
			auto const& MainTimeline =
				UnitTesting ? UnitTesting->MainTimeline : SchedulesInternals().GetTimeline();
			FTimeRangeInSeconds ElemTimeRange = ITwin::Time::Undefined();
			SchedulesInternals().ForEachElementTimeline(ElementID,
				[&ElemTimeRange](FITwinElementTimeline const& Timeline)
				{
					auto const& TimeRange = Timeline.GetTimeRange();
					if (ElemTimeRange == ITwin::Time::Undefined())
						ElemTimeRange = TimeRange;
					else if (ITwin::Time::Undefined() != TimeRange)
						ElemTimeRange = ITwin::Time::Union(ElemTimeRange, TimeRange);
				});
			if (ElemTimeRange == ITwin::Time::Undefined())
				return;
			// Note: both margins are signed
			ElemTimeRange.first += ITwin::Time::FromTimespan(MarginFromStart);
			ElemTimeRange.second += ITwin::Time::FromTimespan(MarginFromEnd);
			if (ElemTimeRange.first >= ElemTimeRange.second)
				return;
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
				RequestAnimationBindings(Token, SchedIdx, Lock, ElemTimeRange);
		});

	// see comment in QueryEntireSchedules
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); },
			/*(informative)bPseudoBatch:*/true);
}

void FITwinSchedulesImport::FImpl::QueryElementsTasks(std::set<ITwinElementID>&& ElementIDs,
	FDateTime const FromTime, FDateTime const UntilTime,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted)
{
	if (!Queries /*|| Schedules.empty() <== No, query may still be in transit or pending!*/)
	{
		if (OnQueriesCompleted) OnQueriesCompleted(false);
		return;
	}
	Queries->NewBatch(
		[this, ElementIDs=std::move(ElementIDs), FromTime, UntilTime]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
			{
				std::optional<FTimeRangeInSeconds> TimeRange;
				if (FromTime < UntilTime)
				{
					TimeRange.emplace(FTimeRangeInSeconds{
						ITwin::Time::FromDateTime(FromTime), ITwin::Time::FromDateTime(UntilTime) });
				}
				int64_t InOutElemCount = (int64)ElementIDs.size();
				for (auto It = ElementIDs.begin(), ItEnd = ElementIDs.end(); It != ItEnd;/*no-op*/)
				{
					auto ItOut = RequestAnimationBindings(Token, SchedIdx, Lock, TimeRange, It, ItEnd,
														  &InOutElemCount);
					// just a safety: ensure at least one Element was queried...
					if (!ensure(ItOut != It))
						break;
					It = ItOut;
				}
			}
		});
	// see comment in QueryEntireSchedules
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); },
			/*(informative)bPseudoBatch:*/true);
}

/// Needed in the CPP otherwise the default ctor impl complains about FITwinSchedulesConnector being unknown
FITwinSchedulesImport::FITwinSchedulesImport(UITwinSynchro4DSchedules& InOwner,
	std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules)
	: Owner(&InOwner)
	, Impl(MakePimpl<FImpl>(*this, Mutex, Schedules))
{
}

FITwinSchedulesImport::FITwinSchedulesImport(FString const& BaseUrl, FITwinScheduleTimeline& MainTimeline,
	TStrongObjectPtr<UObject> OwnerUObj, std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules)
	: Owner(nullptr)
	, Impl(MakePimpl<FImpl>(*this, BaseUrl, MainTimeline, OwnerUObj, Mutex, Schedules))
{
}

FITwinSchedulesImport& FITwinSchedulesImport::operator=(FITwinSchedulesImport&& Other)
{
	UninitializeCache(); // needed because std::move below actually destroys the Impl...
	Impl = std::move(Other.Impl);
	Impl->Owner = this;
	return *this;
}

bool FITwinSchedulesImport::IsReadyToQuery() const
{
	return Impl->Queries.Get() != nullptr;
}

bool FITwinSchedulesImport::HasFinishedPrefetching() const
{
	return Impl->bHasFinishedPrefetching;
}

bool FITwinSchedulesImport::HasFetchingErrors() const
{
	return Impl->bHasFetchingErrors;
}

FString FITwinSchedulesImport::FirstFetchingErrorString() const
{
	return Impl->FirstFetchingError;
}

void FITwinSchedulesImport::ResetConnection(FString const& ITwinAkaProjectAkaCtextId, FString const& IModelId,
											FString const& InChangesetId)
{
	Impl->ResetConnection(ITwinAkaProjectAkaCtextId, IModelId, InChangesetId, {});
}

/// During testing, we can't make the initial first request for the schedule Id: it has been set
/// specifically from the FITwinSchedulesImport and the FImpl's dedicated constructors
void FITwinSchedulesImport::ResetConnectionForTesting(FString const& ITwinAkaProjectAkaContextId,
	FString const& IModelId, FString const& InChangesetId, FString const& CustomCacheDir)
{
	Impl->ResetConnection(ITwinAkaProjectAkaContextId, IModelId, InChangesetId, CustomCacheDir);
}

void FITwinSchedulesImport::SetSchedulesImportConnectors(
	FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
	FOnAnimationGroupModified const& InOnAnimationGroupModified,
	FFindElementIDFromGUID const& InFncElementIDFromGUID)
{
	Impl->SetSchedulesImportConnectors(InOnAnimationBindingAdded, InOnAnimationGroupModified,
									   InFncElementIDFromGUID);
}

std::pair<int, int> FITwinSchedulesImport::HandlePendingQueries()
{
	return Impl->HandlePendingQueries();
}

void FITwinSchedulesImport::QueryEntireSchedules(
	FDateTime const FromTime/* = {}*/, FDateTime const UntilTime/* = {}*/,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted/* = {}*/)
{
	Impl->QueryEntireSchedules(FromTime, UntilTime, std::move(OnQueriesCompleted));
}

void FITwinSchedulesImport::QueryAroundElementTasks(ITwinElementID const ElementID,
	FTimespan const MarginFromStart, FTimespan const MarginFromEnd,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted/* = {}*/)
{
	Impl->QueryAroundElementTasks(ElementID, MarginFromStart, MarginFromEnd, std::move(OnQueriesCompleted));
}

void FITwinSchedulesImport::QueryElementsTasks(std::set<ITwinElementID>& ElementIDs,
	FDateTime const FromTime/* = {}*/, FDateTime const UntilTime/* = {}*/,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted/* = {}*/)
{
	std::set<ITwinElementID> LocalElementIDs;
	LocalElementIDs.swap(ElementIDs); // we need to empty the input set (see dox)
	Impl->QueryElementsTasks(std::move(LocalElementIDs), FromTime, UntilTime, std::move(OnQueriesCompleted));
}

void FITwinSchedule::Reserve(size_t Count)
{
	AnimationBindings.reserve(Count);
	Tasks.reserve(Count);
	Groups.reserve(Count);
	AppearanceProfiles.reserve(Count);
	TransfoAssignments.reserve(Count);
	Animation3DPaths.reserve(Count);
	KnownTasks.reserve(Count);
	KnownGroups.reserve(Count);
	KnownAppearanceProfiles.reserve(Count);
	KnownTransfoAssignments.reserve(Count);
	KnownAnimation3DPaths.reserve(Count);
}

bool FAnimationBinding::FullyDefined(FITwinSchedule const& Schedule, bool const bAllowPendingQueries,
									 ITwinHttp::FLock&) const
{
	if (std::holds_alternative<FString>(AnimatedEntities) && ITwin::INVALID_IDX == GroupInVec)
	{
		ensure(false); return false;
	}
	if (ITwin::INVALID_IDX == TaskInVec) return false;
	if (!bAllowPendingQueries && !Schedule.Tasks[TaskInVec].Bindings.empty())
		return false;
	if (ITwin::INVALID_IDX == AppearanceProfileInVec) return false;
	if (!bAllowPendingQueries && !Schedule.AppearanceProfiles[AppearanceProfileInVec].Bindings.empty())
		return false;
	if (TransfoAssignmentId.IsEmpty()) return true;
	if (ITwin::INVALID_IDX == TransfoAssignmentInVec) return false;
	if (!bAllowPendingQueries && !Schedule.TransfoAssignments[TransfoAssignmentInVec].Bindings.empty())
		return false;
	if (bStaticTransform)
		return true;
	FPathAssignment const& PathAssignment = std::get<1>(
		Schedule.TransfoAssignments[TransfoAssignmentInVec].Transformation);
	if (ITwin::INVALID_IDX == PathAssignment.Animation3DPathInVec)
		return false;
	return bAllowPendingQueries
		|| Schedule.Animation3DPaths[PathAssignment.Animation3DPathInVec].Bindings.empty();
}

FString FITwinSchedulesImport::ToString() const
{
	if (Impl->PrefetchWholeSchedule() && !HasFinishedPrefetching())
		return TEXT("");
	for (auto&& Sched : Impl->Schedules)
	{
		if (!Sched.AnimationBindings.empty())
		{
			return FString::Printf(TEXT("Statistics for %s\nQuerying statistics: %s"),
				*Sched.ToString(), Impl->Queries ? (*Impl->Queries->Stats()) : TEXT("na."));
		}
	}
	return TEXT("<Empty schedule>");
}

size_t FITwinSchedulesImport::NumTasks() const
{
	for (auto&& Sched : Impl->Schedules)
	{
		if (!Sched.Tasks.empty())
		{
			return Sched.Tasks.size();
		}
	}
	return 0;
}

void FITwinSchedulesImport::UninitializeCache()
{
	if (Impl->Queries)
		Impl->Queries->UninitializeCache();
}

FString FAnimationBinding::ToString(const TCHAR* SpecificElementID/* = nullptr*/) const
{
	FString Res;
	std::visit([&Res](auto&& Ident)
		{
			using T = std::decay_t<decltype(Ident)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
				Res = FString::Printf(TEXT("%#x"), Ident.value());
			else if constexpr (std::is_same_v<T, FGuid>)
				Res = Ident.ToString(EGuidFormats::DigitsWithHyphensLower);
			else if constexpr (std::is_same_v<T, FString>)
				Res = FString("in group ") + Ident;
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		AnimatedEntities);
	return FString::Printf(TEXT("binding for ent. %s%s, appear. %s%s%s"),
		//(Name.IsEmpty() ? (*(FString(" Id ") + TaskId)) : *Name),
		SpecificElementID ? SpecificElementID : TEXT(""), *Res, *AppearanceProfileId,
		TransfoAssignmentId.IsEmpty() ? TEXT("") : *(TEXT(", transf. ") + TransfoAssignmentId),
		TransfoAssignmentId.IsEmpty() ? TEXT("")
									  : (bStaticTransform ? TEXT(" (static)") : TEXT(" (3D path)")));
}

FString FITwinSchedule::ToString() const
{
	return FString::Printf(TEXT("%s Schedule %s (\"%s\"), with:\n" \
		"\t%llu bindings, %llu tasks, %llu groups, %llu appearance profiles,\n" \
		"\t%llu transfo. assignments (%llu static, %llu along %llu 3D paths).\n" \
		"\t%llu unique Elements or Groups are bound to a task."),
		(EITwinSchedulesGeneration::Unknown == Generation) ? TEXT("<Gen?>")
			: ((EITwinSchedulesGeneration::Legacy == Generation) ? TEXT("Legacy") : TEXT("NextGen")),
		*Id, *Name, AnimationBindings.size(), Tasks.size(), Groups.size(), AppearanceProfiles.size(),
		TransfoAssignments.size(),
		std::count_if(TransfoAssignments.begin(), TransfoAssignments.end(),
			[](auto&& Known) { return Known.Transformation.index() == 0; }),
		std::count_if(TransfoAssignments.begin(), TransfoAssignments.end(),
			[](auto&& Known) { return Known.Transformation.index() == 1; }),
		Animation3DPaths.size()
		//, AnimBindingsFullyKnownForElem.size() - removed, instead:
		, [this]()
		{
			std::unordered_set<decltype(FAnimationBinding::AnimatedEntities)> Bound;
			for (auto&& Binding : AnimationBindings)
				Bound.insert(Binding.AnimatedEntities);//could explore the variant and recurse into groups...
			return Bound.size();
		}()
		// Map value no longer set to InitialVersion, see comments about AnimBindingsFullyKnownForElem
		//std::count_if(AnimBindingsFullyKnownForElem.begin(), AnimBindingsFullyKnownForElem.end(),
		//	[](auto&& Known) { return VersionToken::InitialVersion == Known.second; })
	);
}