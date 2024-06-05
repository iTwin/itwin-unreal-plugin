/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesImport.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesImport.h"
#include "SchedulesStructs.h"
#include "ReusableJsonQueries.h"
#include "Schedule/TimeInSeconds.h"
#include "Timeline.h"
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex

#include <Dom/JsonObject.h>
#include <HttpModule.h>
#include <Input/Reply.h>
#include <ITwinServerConnection.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Logging/LogMacros.h>
#include <Math/Vector.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <deque>
#include <mutex>
#include <ranges>
#include <vector>
#include <unordered_map>
#include <unordered_set>

DECLARE_LOG_CATEGORY_EXTERN(LrtuS4dImport, Log, All);
DEFINE_LOG_CATEGORY(LrtuS4dImport);
#define S4D_VERBOSE(FORMAT, ...) UE_LOG(LrtuS4dImport, Verbose, FORMAT, ##__VA_ARGS__)
#define S4D_LOG(FORMAT, ...) UE_LOG(LrtuS4dImport, Display, FORMAT, ##__VA_ARGS__)
#define S4D_WARN(FORMAT, ...) UE_LOG(LrtuS4dImport, Warning, FORMAT, ##__VA_ARGS__)
#define S4D_ERROR(FORMAT, ...) UE_LOG(LrtuS4dImport, Error, FORMAT, ##__VA_ARGS__)

/// Required this early otherwise compiling "TPimplPtr<...> Queries;" below will complain
/// ONLY when building this single compile unit, when fastcompiling one gets error C2961:
/// 'FReusableJsonQueries<8>': inconsistent explicit instantiations, a previous explicit instantiation
/// did not specify 'extern template' (depending on the order of inclusion of the CPPs, of course...).
#ifndef REUSABLEJSONQUERIES_TEMPLATE_INSTANTIATED
extern template class FReusableJsonQueries<SimultaneousRequestsAllowed>;// ReusableJsonQueries.cpp
#endif

namespace ITwin_TestOverrides
{
	// See comment on declaration in SchedulesConstants.h
	int RequestPagination = -1;
	int64_t MaxElementIDsFilterSize = -1;
}

/**
 * The Synchro4D api should allow us to: (https://dev.azure.com/bentleycs/beconnect/_workitems/edit/826180)
 *  1. replay the full construction schedule
 *  2. replay the construction schedule for a given time range
 *  3. display the iModel at a specific time
 *
 * Note: the work item above mentions one of the inputs is a "map of schedule entities (schedule resources)
 *		 to 3D entities (iModel elements)" <== Bernardas said this grouping does not appear in how the
 *		 animations are streamed from the web api... We'll just get a list of animated elements that happen
 *		 share the same task (the resource id is not even mentioned!)
 *
 * FITwinSchedulesImport is only our internal (private) API, but the Bentley-UE plugin will need to expose
 * public entry points for these tasks, in a way that allows to stream only the necessary amount of data:
 *  a. handle construction time for the currently visible iModel (physical) extent
 *    a1. show the whole schedule's time range
 *    a2. set the UE world at a specific time
 *    a3. zoom the UE time into a specific time range (to prioritize loading for the tasks occurring during
 *		  this interval)
 *  b. link each animated element to its own sub-schedule (list of tasks w/ appearance settings)
 *  c. play/pause/stop/reverse/speed up/slow down the construction schedule (whole, and maybe also per
 *	   element?)
 *  d. pre-fetch animation data for a not-yet-visible/focused physical extent and time range?
 */
class FITwinSchedulesImport::FImpl
{
private:
	using FLock = ReusableJsonQueries::FLock;
	friend class FITwinSchedulesImport;
	FITwinSchedulesImport const* Owner;
	FOnAnimationBindingAdded OnAnimationBindingAdded =
		[](FAnimationBinding const&, FAppearanceProfile const&) {};
	TObjectPtr<AITwinServerConnection> ServerConnection;
	ReusableJsonQueries::FMutex& Mutex;///< TODO_GCO: use a per-Schedule mutex?
	const int RequestPagination;///< pageSize for paginated requests
	/// When passing an array of ElementIDs to filter a request, we need to cap the size for performance
	/// reasons. Julius suggested to cap to 1000 on the server.
	const size_t MaxElementIDsFilterSize;
	std::pair<int, int> LastDisplayedQueueSizeIncrements = { -1, -1 };
	std::pair<int, int> LastRoundedQueueSize = { -1, -1 };

	FString ITwinId, TargetedIModelId; ///< Set in FITwinSchedulesImport::ResetConnection
	EITwinSchedulesGeneration SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
	std::vector<FITwinSchedule>& Schedules;
	TPimplPtr<FReusableJsonQueries<SimultaneousRequestsAllowed>> Queries;

public:
	FImpl(FITwinSchedulesImport const& InOwner, ReusableJsonQueries::FMutex& InMutex,
			std::vector<FITwinSchedule>& InSchedules, const int InRequestPagination = 100,
			const size_t InMaxElementIDsFilterSize = 500)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, RequestPagination(ITwin_TestOverrides::RequestPagination > 0
			? ITwin_TestOverrides::RequestPagination : InRequestPagination)
		, MaxElementIDsFilterSize(ITwin_TestOverrides::MaxElementIDsFilterSize > 0
			? (size_t)ITwin_TestOverrides::MaxElementIDsFilterSize : InMaxElementIDsFilterSize)
		, Schedules(InSchedules)
	{}
	FImpl(FImpl const&) = delete;
	FImpl& operator=(FImpl const&) = delete;

	void ResetConnection(TObjectPtr<AITwinServerConnection> ServerConnection,
		FString const& ITwinAkaProjectAkaContextId, FString const& IModelId, bool const bInternalRetry);
	void SetOnAnimationBindingAdded(FOnAnimationBindingAdded const& InCallback);
	std::pair<int, int> HandlePendingQueries();
	void QueryEntireSchedules(FDateTime const FromTime, FDateTime const UntilTime,
							  std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryAroundElementTasks(ITwinElementID const ElementID, FTimespan const MarginFromStart,
		FTimespan const MarginFromEnd, std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryElementsTasks(std::vector<ITwinElementID>&& ElementIDs, FDateTime const FromTime,
		FDateTime const UntilTime, std::function<void(bool/*success*/)>&& OnQueriesCompleted);

private:
	UITwinSynchro4DSchedules const& SchedulesComponent() const
		{ return *Owner->Owner; }
	FITwinSynchro4DSchedulesInternals const& SchedulesInternals() const
		{ return GetInternals(SchedulesComponent()); }
	void RequestSchedules(ReusableJsonQueries::FStackingToken const&,
		std::optional<FString> const PageToken = {}, FLock* optLock = nullptr);
	void RequestSchedulesAnimatedEntityUserFieldId(ReusableJsonQueries::FStackingToken const&,
		size_t const SchedStartIdx, size_t const SchedEndIdx, FLock&);
	void RequestScheduleAnimationBindings(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
		FLock&, std::optional<FTimeRangeInSeconds> const TimeRange = {},
		std::vector<ITwinElementID>::const_iterator const ElementsBegin = {},
		std::vector<ITwinElementID>::const_iterator const ElementsEnd = {},
		std::optional<FString> const PageToken = {}, std::optional<FString> JsonPostString = {});
	void RequestTaskDetails(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
							std::vector<size_t>&& AnimIdxes, FLock&);
	void ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
		TSharedPtr<FJsonObject> const& responseJson, size_t const SchedIdx, std::vector<size_t>&& AnimIdxes,
		FLock* Lock = nullptr);
	void WhenTaskDetailsKnown(ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx,
						  size_t const AnimIdx, FLock& Lock);
	void RequestAppearanceProfiles(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								   size_t const TaskId, FLock&);
	//void RequestSchedulesTasks(...);
	//void DetermineTaskElements(...); <== blame here to retrieve method to query resource assignments
	//void RequestResourceEntity3Ds(...); <== blame here to retrieve method to query resource entity3Ds
	void DuplicateTaskAnimationBindingWithDummyAndNotify(size_t const SchedIdx, size_t const AnimIdx,
		std::unordered_set<ITwinElementID> const& Elements, FLock& Lock);
	EProfileAction ParseProfileAction(FString const& FromStr);
	bool ParseSimpleAppearance(FSimpleAppearance& Appearance, bool const bBaseOfActiveAppearance,
							   TSharedPtr<FJsonObject> const& JsonObj);
	bool ParseActiveAppearance(FActiveAppearance& Appearance, TSharedPtr<FJsonObject> const& JsonObj);
	bool ColorFromHexString(FString const& FromStr, FVector& Color);
	bool ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out);
	bool ParseGrowthSimulationMode(FString const& FromStr, EGrowthSimulationMode& Mode);

	int GetSchedulesAPIVersion() const
	{
		return 1;
		//switch (ServerConnection->SchedulesGeneration)
		//{
		//case EITwinSchedulesGeneration::Legacy:
		//	return 1;
		//case EITwinSchedulesGeneration::Unknown:
		//case EITwinSchedulesGeneration::NextGen:
		//	return 1;
		//}
		//check(false);
		//return 0;
	}

	/// From Julius Senkus: "es-api.bentley.com/4dschedule is a proxy redirecting to
	/// esapi-4dschedules.bentley.com, which checks if the scheduleId is for Legacy or NextGen and then
	/// retrieves the data accordingly (either from next gen services or legacy)".
	/// So I guess synchro4dschedulesapi-eus.bentley.com/api is for NextGen internally, but Julius recommended
	/// we use only the proxy.
	FString GetSchedulesAPIBaseUrl() const
	{
		switch (SchedulesGeneration)
		{
		case EITwinSchedulesGeneration::Unknown: // not yet known: try NextGen first
			[[fallthrough]];
		case EITwinSchedulesGeneration::NextGen:
			return FString::Printf(TEXT("https://%ssynchro4dschedulesapi-eus.bentley.com/api/v%d/schedules"),
								   *ServerConnection->UrlPrefix(), GetSchedulesAPIVersion());
		case EITwinSchedulesGeneration::Legacy:
			return FString::Printf(TEXT("https://%ses-api.bentley.com/4dschedule/v%d/schedules"),
								   *ServerConnection->UrlPrefix(), GetSchedulesAPIVersion());
		}
		check(false);
		return "<invalidUrl>";
	}

	FString GetIdToQuerySchedules() const
	{
		switch (SchedulesGeneration)
		{
		case EITwinSchedulesGeneration::Legacy:
			return "projectId";
		case EITwinSchedulesGeneration::Unknown:
			// not yet known: we'll try NextGen first (see GetSchedulesAPIBaseUrl)
			[[fallthrough]];
		case EITwinSchedulesGeneration::NextGen:
			return "contextId";
		}
		check(false);
		return "<invalId>";
	}
};

void FITwinSchedulesImport::FImpl::RequestSchedules(ReusableJsonQueries::FStackingToken const& Token,
	std::optional<FString> const PageToken /*= {}*/, FLock* optLock /*= nullptr*/)
{
	// 1. First thing is to get the list of schedules.
	//
	// {} because the base URL (eg https://dev-synchro4dschedulesapi-eus.bentley.com/api/v1/schedules),
	// is actually the endpoint for listing the schedules related to a contextId/projectId (= iTwinId!)
	FUrlArgList RequestArgList = {
		{ GetIdToQuerySchedules(), ITwinId },
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	// Note that my latest testing on qa-synchro4dschedulesapi-eus showed that pagination was not supported
	// on schedules, although it worked as expected on Tasks
	if (PageToken)
	{
		// Note: BlockingRequests already incremented (avoids a new Lock)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	}
	Queries->StackRequest(Token, optLock, EVerb::GET, {}, std::move(RequestArgList),
		[this, &Token](TSharedPtr<FJsonObject> const& responseJson)
		{
			auto NewScheds = responseJson->GetArrayField("items");
			S4D_LOG(TEXT("Received %d schedules for iTwin %s"), (int)NewScheds.Num(), *ITwinId);
			if (0 == NewScheds.Num())
				return;
			FLock Lock(Mutex);
			size_t const SchedStartIdx = Schedules.size();
			Schedules.reserve(SchedStartIdx + NewScheds.Num());
			for (const auto& SchedVal : NewScheds)
			{
				const auto& Sched = SchedVal->AsObject();
				if (Sched->GetStringField("iModelId") == TargetedIModelId)
				{
					Schedules.emplace_back(
						FITwinSchedule{ Sched->GetStringField("id"), Sched->GetStringField("name") });
					S4D_LOG(TEXT("Added schedule Id %s named '%s' to iModel %s"), *Schedules.back().Id,
							*Schedules.back().Name, *TargetedIModelId);
				}
			}
			if (responseJson->HasTypedField<EJson::String>("nextPageToken"))
				RequestSchedules(Token, responseJson->GetStringField("nextPageToken"), &Lock);
			RequestSchedulesAnimatedEntityUserFieldId(Token, SchedStartIdx, Schedules.size(), Lock);
		});
}

void FITwinSchedulesImport::FImpl::RequestSchedulesAnimatedEntityUserFieldId(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedStartIdx,
	size_t const SchedEndIdx, FLock& Lock)
{
	static const TCHAR* AnimatedEntityUserField = TEXT("iModel Element Id");
	// 2. Get the animatedElementUserFieldId for each schedule: wil only return something for Next-gen
	//	  schedules, "OK" but empty reply thus means Legacy/Old-gen schedule
	for (auto SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
	{
		Queries->StackRequest(Token, &Lock, EVerb::GET, { Schedules[SchedIdx].Id, TEXT("userFields") },
			{ { TEXT("name"), FString(AnimatedEntityUserField).Replace(TEXT(" "), TEXT("%20")) } },
			[this, SchedIdx](TSharedPtr<FJsonObject> const& responseJson)
			{
				auto const& Items = responseJson->GetArrayField("items");
				if (Items.Num() == 0)
					return;
				FLock Lock(Mutex);
				auto&& Sched = Schedules[SchedIdx];
				for (auto&& Item : Items)
				{
					const auto& JsonObj = Item->AsObject();
					// The 'name' filter "[matches] user fields with specified name or part of it", so we
					// need to check equality:
					if (JsonObj->GetStringField("name") == AnimatedEntityUserField)
					{
						Sched.AnimatedEntityUserFieldId = JsonObj->GetStringField("id");
						if (!Sched.AnimatedEntityUserFieldId.IsEmpty())
						{
							S4D_VERBOSE(TEXT("Found AnimatedEntityUserFieldId %s for schedule Id %s"),
										*Sched.AnimatedEntityUserFieldId, *Sched.Id);
							break;
						}
					}
				}
				if (Sched.AnimatedEntityUserFieldId.IsEmpty())
				{
					S4D_ERROR(TEXT("AnimatedEntityUserFieldId NOT FOUND for schedule Id %s"),
							  *Schedules[SchedIdx].Name);
				}
			});
	}
}

// 4dschedule/v1/schedules/{scheduleId}/tasks: no longer needed - blame here to retrieve
//void FITwinSchedulesImport::FImpl::RequestSchedulesTasks(...)

namespace ITwin {

ITwinElementID ParseElementID(FString FromStr)
{
	int32 IdxOpen;
	FromStr.ToLowerInline();
	if (FromStr.FindChar(TCHAR('['), IdxOpen))
	{
		int32 IdxClose;
		if (FromStr.FindLastChar(TCHAR(']'), IdxClose) && IdxClose > (IdxOpen + 1))
		{
			FromStr.MidInline(IdxOpen + 1, IdxClose - IdxOpen - 1);
		}
		else
		{
			check(false);
			FromStr.RightChopInline(IdxOpen);
		}
	}
	uint64 Parsed = ITwin::NOT_ELEMENT.value();
	errno = 0;
	if (FromStr.StartsWith(TEXT("0x")))//Note: StartsWith ignores case by default!
		Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/16);
	else
		Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/10);
	return (errno == 0) ? ITwinElementID(Parsed) : ITwin::NOT_ELEMENT;
}

} // ns ITwin

void FITwinSchedulesImport::FImpl::RequestScheduleAnimationBindings(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, FLock& Lock,
	std::optional<FTimeRangeInSeconds> const TimeRange/* = {}*/,
	std::vector<ITwinElementID>::const_iterator const ElementsBegin/* = {}*/,
	std::vector<ITwinElementID>::const_iterator const ElementsEnd/* = {}*/,
	std::optional<FString> const PageToken/* = {}*/, std::optional<FString> JsonPostString/* = {}*/)
{
	bool bHasTimeRange = false;
	if (JsonPostString)
	{
		// Parameters were not forwarded (they shouldn't be: they were probably deallocated by now)
		// so rely on post string content
		bHasTimeRange |= JsonPostString->Contains("startTime") || JsonPostString->Contains("endTime");
	}
	else
	{
		JsonPostString.emplace();
		auto JsonObj = MakeShared<FJsonObject>();
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&(*JsonPostString),
																				/*Indent=*/0);
		auto& Sched = Schedules[SchedIdx];
		JsonObj->SetStringField("animatedEntityUserFieldId", Sched.AnimatedEntityUserFieldId);
		if (ElementsBegin != ElementsEnd)
		{
			TArray<TSharedPtr<FJsonValue>> AnimatedEntityIDs;
			AnimatedEntityIDs.Reserve(ElementsEnd - ElementsBegin);
			if (TimeRange)
			{
				for (auto ElemIt = ElementsBegin; ElemIt != ElementsEnd; ++ElemIt)
				{
					// Do not insert anything: the query is only for a specific time range...
					if (Sched.AnimBindingsFullyKnownForElem.end()
						== Sched.AnimBindingsFullyKnownForElem.find(*ElemIt))
					{
						AnimatedEntityIDs.Add(
							MakeShared<FJsonValueString>(FString::Printf(TEXT("0x%I64x"), ElemIt->value())));
					}
				}
			}
			else
			{
				for (auto ElemIt = ElementsBegin; ElemIt != ElementsEnd; ++ElemIt)
				{
					if (Sched.AnimBindingsFullyKnownForElem.try_emplace(*ElemIt, false).second)
					{
						AnimatedEntityIDs.Add(
							MakeShared<FJsonValueString>(FString::Printf(TEXT("0x%I64x"), ElemIt->value())));
					}
				}
			}
			if (AnimatedEntityIDs.IsEmpty()) // nothing left to query
			{
				return;
			}
			JsonObj->SetArrayField("animatedEntityIds", std::move(AnimatedEntityIDs));
		}
		if (TimeRange && TimeRange->first < TimeRange->second)
		{
			FDateRange const DateRange = ITwin::Time::ToDateRange(*TimeRange);
			if (!DateRange.IsEmpty() && DateRange.HasLowerBound() && DateRange.HasUpperBound())
			{
				JsonObj->SetStringField("startTime", DateRange.GetLowerBoundValue().ToIso8601());
				JsonObj->SetStringField("endTime", DateRange.GetUpperBoundValue().ToIso8601());
				bHasTimeRange = true;
			}
		}
		FJsonSerializer::Serialize(JsonObj, JsonWriter);
	}
	FUrlArgList RequestArgList = {
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	if (PageToken)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	Queries->StackRequest(Token, &Lock, EVerb::POST,
		{ Schedules[SchedIdx].Id, TEXT("animationBindings/query") }, std::move(RequestArgList),
		[this, SchedIdx, TimeRange, JsonPostString, bHasTimeRange, &Token]
		(TSharedPtr<FJsonObject> const& responseJson)
		{
			auto const& Items = responseJson->GetArrayField("items");
			if (Items.IsEmpty())
				return;
			FLock Lock(Mutex);
			if (responseJson->HasTypedField<EJson::String>("nextPageToken"))
			{
				// No need to repeat the TimeRange and ElementIDs parameters, they are already included in
				// the JsonPostString content
				RequestScheduleAnimationBindings(Token, SchedIdx, Lock, {}, {}, {},
					responseJson->GetStringField("nextPageToken"), JsonPostString);
			}
			auto&& Sched = Schedules[SchedIdx];
			Sched.AnimationBindings.reserve(Sched.AnimationBindings.size() + Items.Num());
			// Gather as-yet unknown tasks and their bindings, to optimize querying task details
			std::unordered_map<FString/*TaskId*/, std::vector<size_t>/*AnimIdx of same Task*/> UnknownTasks;
			int32 ItemsSeen = 0; // just to optimize a "reserve" call...
			for (auto&& NewTask : Items)
			{
				++ItemsSeen;
				const auto& TaskObj = NewTask->AsObject();

				FString const AnimElemIdHexa = TaskObj->GetStringField("animatedEntityId");
				ITwinElementID const ElementID = ITwin::ParseElementID(AnimElemIdHexa);
				if (ElementID == ITwin::NOT_ELEMENT)
				{
					continue;
				}
				// If there is a time range, only read from AnimBindingsFullyKnownForElem.
				// If not, we'll want to insert it: if ElementIDs filtering was empty, nothing was inserted
				// yet, so we need to do it now
				auto Known = bHasTimeRange ? Sched.AnimBindingsFullyKnownForElem.find(ElementID)
					: Sched.AnimBindingsFullyKnownForElem.try_emplace(ElementID, false).first;
				if (Sched.AnimBindingsFullyKnownForElem.end() != Known)
				{
					if (Known->second)
					{
						continue; // already fully known, can skip
					}
					Known->second = true;
				}
				//else: no-op, query was only on a specific time range
				FString TaskId = TaskObj->GetStringField("taskId");
				size_t const AnimIdx = Sched.AnimationBindings.size();
				if (!Sched.KnownAnimationBindings.try_emplace({ TaskId, ElementID }, AnimIdx).second)
				{
					continue; // already known, can skip
				}
				FAnimationBinding& Anim = Sched.AnimationBindings.emplace_back();
				Anim.TaskId = std::move(TaskId);
				// TODO_GCO: add some error handling... (here and all other RequestXYZ!)
				// Known only upon RequestTaskDetails' completion:
				//Anim.TaskName = TaskObj->GetStringField("name");
				Anim.AppearanceProfileId = TaskObj->GetStringField("appearanceProfileId");
				if (Anim.TaskId.IsEmpty() || Anim.AppearanceProfileId.IsEmpty()
					|| AnimElemIdHexa.IsEmpty())
				{
					S4D_ERROR(TEXT("Error for task '%s' in schedule Id %s with AnimatedEntityId '%s' and "
						"AppearanceProfileId '%s'"), *Anim.TaskId/*Anim.TaskName*/, *Sched.Id,
						*AnimElemIdHexa, *Anim.AppearanceProfileId);
					continue;
				}
				Anim.AnimatedEntityId = ElementID;
				S4D_VERBOSE(TEXT("Added task %s for schedule Id %s with AnimatedEntityId %s and "
								 "AppearanceProfileId %s"),
						*Anim.TaskId/*Anim.TaskName*/, *Sched.Id, *AnimElemIdHexa,
						*Anim.AppearanceProfileId);
				auto KnownTask = Sched.KnownTaskDetails.find(Anim.TaskId);
				if (KnownTask != Sched.KnownTaskDetails.end())
				{
					auto const& BindingOfSameTask = Sched.AnimationBindings[KnownTask->second];
					Anim.TaskName = BindingOfSameTask.TaskName;
					Anim.TimeRange = BindingOfSameTask.TimeRange;
					Anim.ResourceId = BindingOfSameTask.ResourceId; // PROD TMP
					WhenTaskDetailsKnown(Token, SchedIdx, AnimIdx, Lock);
				}
				else
				{
					auto Listed = UnknownTasks.try_emplace(Anim.TaskId, std::vector<size_t>{});
					if (Listed.second)
					{
						Listed.first->second.reserve((size_t)std::max(1, Items.Num() - ItemsSeen + 1));
					}
					Listed.first->second.push_back(AnimIdx);
				}
			}
			for (auto&& Todo : UnknownTasks)
			{
				RequestTaskDetails(Token, SchedIdx, std::move(Todo.second), Lock);
			}
		},
		FString(*JsonPostString));
}

void FITwinSchedulesImport::FImpl::WhenTaskDetailsKnown(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto const& Anim = Schedules[SchedIdx].AnimationBindings[AnimIdx];
	check(Anim.AnimatedEntityId != ITwin::NOT_ELEMENT);
	if (SchedulesComponent().bDebugWithRandomProfiles)
		DuplicateTaskAnimationBindingWithDummyAndNotify(SchedIdx, AnimIdx,
			{ Anim.AnimatedEntityId }, Lock);
	else
		RequestAppearanceProfiles(Token, SchedIdx, AnimIdx, Lock);
}

void FITwinSchedulesImport::FImpl::ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
	TSharedPtr<FJsonObject> const& responseJson, size_t const SchedIdx, std::vector<size_t>&& AnimIdxes,
	FLock* MaybeLock /*= nullptr*/)
{
	if (AnimIdxes.empty()) { check(false); return; }
	FString /*const: std::move'd later!*/ Name = responseJson->GetStringField("name");
	// Should use "Planned" ATM (confirmed by Laurynas)
	FString const PlannedStartStr = responseJson->GetStringField("plannedStart");
	FString const PlannedFinishStr = responseJson->GetStringField("plannedFinish");
	FDateTime PlannedStart, PlannedFinish;
	bool const bCouldParseDates = FDateTime::ParseIso8601(*PlannedStartStr, PlannedStart)
							   && FDateTime::ParseIso8601(*PlannedFinishStr, PlannedFinish);
	std::optional<FLock> OptLockDontUse;
	if (!MaybeLock) OptLockDontUse.emplace(Mutex);
	FLock& Lock = MaybeLock ? (*MaybeLock) : (*OptLockDontUse);
	auto AnimIt = AnimIdxes.begin();
	auto& Anim = Schedules[SchedIdx].AnimationBindings[*AnimIt];
	Anim.TaskName = std::move(Name);
	Schedules[SchedIdx].KnownTaskDetails[Anim.TaskId] = *AnimIt;
	if (bCouldParseDates)
	{
		Anim.TimeRange.first = ITwin::Time::FromDateTime(PlannedStart);
		Anim.TimeRange.second = ITwin::Time::FromDateTime(PlannedFinish);
		WhenTaskDetailsKnown(Token, SchedIdx, *AnimIt, Lock);
		S4D_VERBOSE(TEXT("Task %s for schedule Id %s is named '%s' and spans %s to %s"),
					*Anim.TaskId, *Schedules[SchedIdx].Id, *Anim.TaskName, *PlannedStartStr,
					*PlannedFinishStr);
	}
	else
	{
		Anim.TimeRange = ITwin::Time::Undefined();
		check(false);
		S4D_ERROR(TEXT("Task %s for schedule Id %s is named '%s' BUT date(s) could not be parsed!"),
				  *Anim.TaskId, *Schedules[SchedIdx].Id, *Anim.TaskName);
	}
	++AnimIt;
	for (; AnimIt != AnimIdxes.end(); ++AnimIt)
	{
		auto& ForSameTask = Schedules[SchedIdx].AnimationBindings[*AnimIt];
		ForSameTask.TaskName = Anim.TaskName;
		ForSameTask.TimeRange = Anim.TimeRange;
		ForSameTask.ResourceId = Anim.ResourceId;
		if (bCouldParseDates)
			WhenTaskDetailsKnown(Token, SchedIdx, *AnimIt, Lock);
	}
}

void FITwinSchedulesImport::FImpl::RequestTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, std::vector<size_t>&& AnimIdxes, FLock& Lock)
{
	// copy it because of the 'move' in the lambda, and the fact that param evaluation order is undetermined
	size_t const AnyAnimIdx = AnimIdxes[0];
	Queries->StackRequest(Token, &Lock, EVerb::GET,
		{ Schedules[SchedIdx].Id, TEXT("tasks"), Schedules[SchedIdx].AnimationBindings[AnyAnimIdx].TaskId },
		{},
		[this, &Token, SchedIdx, AnimIdxes=std::move(AnimIdxes)]
			(TSharedPtr<FJsonObject> const& responseJson) mutable
		{
			ParseTaskDetails(Token, responseJson, SchedIdx, std::move(AnimIdxes));
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

namespace ITwin::Schedule
{
	float fProbaOfOpacityAnimation = 0.5f;
}

void FITwinSchedulesImport::FImpl::DuplicateTaskAnimationBindingWithDummyAndNotify(size_t const SchedIdx,
	size_t const AnimIdx, std::unordered_set<ITwinElementID> const& Elements, FLock& Lock)
{
	size_t const Dummy = Schedules[SchedIdx].AppearanceProfiles.size();
	using ITwin::Schedule::fProbaOfOpacityAnimation;
	float CrudeFloatRand = 0.f;
	FVector const RandClr = FITwinMathExts::RandomFloatColorFromIndex(
		AnimIdx, fProbaOfOpacityAnimation > 0.f ? &CrudeFloatRand : nullptr);
	constexpr bool bUseOriginalColorBeforeTask = true;
	constexpr bool bUseOriginalColorAfterTask = false;
	constexpr bool bUseGrowthSimulation = true;
	bool const bTestOpacityAnimation =
		(fProbaOfOpacityAnimation > 0.f) ? (fProbaOfOpacityAnimation >= CrudeFloatRand) : false;
	Schedules[SchedIdx].AppearanceProfiles.push_back(
		FAppearanceProfile{
			EProfileAction::Install,
			FSimpleAppearance{
				RandClr,
				/* translucency at start*/bTestOpacityAnimation ? .1f : 1.f,
				bUseOriginalColorBeforeTask, !bTestOpacityAnimation // use original color / alpha?
			},
			FActiveAppearance{
				FSimpleAppearance{
					FMath::Lerp(RandClr, FVector::OneVector, 0.5),
					bTestOpacityAnimation ? 0.25f : 1.f, // translucency at start / end of task
					false, !bTestOpacityAnimation // use original color / alpha?
				},
				FVector(1, 1, 1), // custom growth dir
				bTestOpacityAnimation ? 0.9f : 1.f, // finish alpha
				bUseGrowthSimulation ? (EGrowthSimulationMode)(Dummy % 8) : EGrowthSimulationMode::None,
				true, true, // unimpl
				false // invert growth
			},
			FSimpleAppearance{
				0.5 * RandClr,
				1.f, // translucency at start / end ('end' one unused tho...)
				bUseOriginalColorAfterTask, true // use original color / alpha?
			}
		}
	);
	auto& SchedAnimationBindings = Schedules[SchedIdx].AnimationBindings;
	std::unordered_set<ITwinElementID>::const_iterator ItElem = Elements.begin();
	SchedAnimationBindings[AnimIdx].AnimatedEntityId = *ItElem;
	// Note that Sched.KnownAppearanceProfiles is not filled, since the Id is the same for all dummmies
	SchedAnimationBindings[AnimIdx].AppearanceProfileId = TEXT("<DummyAppearanceProfileId>");
	SchedAnimationBindings[AnimIdx].AppearanceProfileInVec = Dummy;
	auto const& DummyAppearanceProfile4Resource = Schedules[SchedIdx].AppearanceProfiles[Dummy];
	OnAnimationBindingAdded(SchedAnimationBindings[AnimIdx], DummyAppearanceProfile4Resource);
	S4D_VERBOSE(TEXT("Element 0x%I64x scheduled with dummy appearance profile"), ItElem->value());
	++ItElem;
	// Duplicate for all other Elements in set
	size_t DuplAnimIdx = SchedAnimationBindings.size();
	SchedAnimationBindings.resize(SchedAnimationBindings.size() + Elements.size() - 1,
								  SchedAnimationBindings[AnimIdx]);
	for (; ItElem != Elements.end(); ++ItElem, ++DuplAnimIdx)
	{
		SchedAnimationBindings[DuplAnimIdx].AnimatedEntityId = *ItElem;
		OnAnimationBindingAdded(Schedules[SchedIdx].AnimationBindings[DuplAnimIdx],
								DummyAppearanceProfile4Resource);
		S4D_VERBOSE(TEXT("Element 0x%I64x scheduled with dummy appearance profile"), ItElem->value());
	}
}

EProfileAction FITwinSchedulesImport::FImpl::ParseProfileAction(FString const& FromStr)
{
	if (FromStr.IsEmpty()) {
		check(false); return EProfileAction::Neutral;
	}
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
		check(false);
		break;
	}
	return EProfileAction::Neutral;
}

bool FITwinSchedulesImport::FImpl::ColorFromHexString(FString const& FromStr, FVector& Color)
{
	if (FromStr.Len() < 6)
		return false;
	uint64 const Clr = FCString::Strtoui64(*FromStr.Right(6), nullptr, /*base*/16);
	Color.X = ((Clr & 0xFF0000) >> 24) / 255.;
	Color.Y = ((Clr & 0x00FF00) >> 16) / 255.;
	Color.Z =  (Clr & 0x0000FF) / 255.;
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out)
{
	double coord;
	if (!JsonObj->TryGetNumberField("x", coord)) return false;
	Out.X = coord;
	if (!JsonObj->TryGetNumberField("y", coord)) return false;
	Out.Y = coord;
	if (!JsonObj->TryGetNumberField("z", coord)) return false;
	Out.Z = coord;
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseGrowthSimulationMode(FString const& FromStr,
															 EGrowthSimulationMode& Mode)
{
	if (FromStr.Len() < 2) {
		check(false); return false;
	}
	auto const Lower = FromStr.ToLower();
	switch (Lower[0])
	{
	case 'b':
		if (Lower[1] == 'o')
		{
			Mode = EGrowthSimulationMode::Bottom2Top;
			break;
		}
		else if (Lower[1] == 'a')
		{
			Mode = EGrowthSimulationMode::Back2Front;
			break;
		}
		else
		{
			check(false);
			return false;
		}
	case 't':
		Mode = EGrowthSimulationMode::Top2Bottom;
		break;
	case 'l':
		Mode = EGrowthSimulationMode::Left2Right;
		break;
	case 'r':
		Mode = EGrowthSimulationMode::Right2Left;
		break;
	case 'f':
		Mode = EGrowthSimulationMode::Front2Back;
		break;
	case 'c':
		Mode = EGrowthSimulationMode::Custom;
		break;
	case 'n':
		Mode = EGrowthSimulationMode::None;
		break;
	case 'u':
		Mode = EGrowthSimulationMode::Unknown;
		break;
	default:
		return false;
	}
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseSimpleAppearance(FSimpleAppearance& Appearance,
	bool const bBaseOfActiveApperance, TSharedPtr<FJsonObject> const& JsonObj)
{
	FString ColorStr;
	if (!JsonObj->TryGetStringField("color", ColorStr))
		return false;
	if (!ColorFromHexString(ColorStr, Appearance.Color))
		return false;
	if (!JsonObj->TryGetNumberField(bBaseOfActiveApperance ? "startTransparency" : "transparency",
									Appearance.Alpha))
		return false;
	Appearance.Alpha = std::clamp(1.f - Appearance.Alpha / 100.f, 0.f, 1.f);

	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool OrgCol = true, OrgTransp = true;
	if (!JsonObj->TryGetBoolField("useOriginalColor", OrgCol)
		|| !JsonObj->TryGetBoolField("useOriginalTransparency", OrgTransp))
	{
		return false;
	}
	Appearance.bUseOriginalColor = OrgCol;
	Appearance.bUseOriginalAlpha = OrgTransp;
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseActiveAppearance(FActiveAppearance& Appearance,
														 TSharedPtr<FJsonObject> const& JsonObj)
{
	if (!ParseSimpleAppearance(Appearance.Base, true, JsonObj))
		return false;
	if (!JsonObj->TryGetNumberField("finishTransparency", Appearance.FinishAlpha))
		return false;
	Appearance.FinishAlpha = std::clamp(1.f - Appearance.FinishAlpha / 100.f, 0.f, 1.f);

	auto GrowthObj = JsonObj->GetObjectField("growthSimulation");
	if (!GrowthObj)
		return false;
	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool GroPercent = true, GroPause = true, InvertGro = true;
	if (!GrowthObj->TryGetBoolField("adjustForTaskPercentComplete", GroPercent)
		|| !GrowthObj->TryGetBoolField("pauseDuringNonWorkingTime", GroPause)
		|| !GrowthObj->TryGetBoolField("simulateAsRemove", InvertGro))
	{
		return false;
	}
	Appearance.bGrowthSimulationBasedOnPercentComplete = GroPercent;
	Appearance.bGrowthSimulationPauseDuringNonWorkingTime = GroPause;
	Appearance.bInvertGrowth = InvertGro;
	FString GrowthModeStr;
	if (!GrowthObj->TryGetStringField("mode", GrowthModeStr))
		return false;
	if (!ParseGrowthSimulationMode(GrowthModeStr, Appearance.GrowthSimulationMode))
		return false;
	if (!ParseVector(GrowthObj->GetObjectField("direction"), Appearance.GrowthDirectionCustom))
		return false;
	return true;
}

void FITwinSchedulesImport::FImpl::RequestAppearanceProfiles(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, size_t const AnimIdx,
	FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto Known = Sched.KnownAppearanceProfiles.find(Sched.AnimationBindings[AnimIdx].AppearanceProfileId);
	if (Known != Sched.KnownAppearanceProfiles.end())
	{
		Schedules[SchedIdx].AnimationBindings[AnimIdx].AppearanceProfileInVec = Known->second;
		OnAnimationBindingAdded(Schedules[SchedIdx].AnimationBindings[AnimIdx],
								Schedules[SchedIdx].AppearanceProfiles[Known->second]);
	}
	else
	{
		Queries->StackRequest(Token, &Lock, EVerb::GET,
			{ Sched.Id, TEXT("appearanceProfiles"), Sched.AnimationBindings[AnimIdx].AppearanceProfileId },
			{},
			[this, SchedIdx, AnimIdx](TSharedPtr<FJsonObject> const& JsonObj)
			{
				FAppearanceProfile Profile;
				FString ProfileTypeStr;
				if (!JsonObj->TryGetStringField("action", ProfileTypeStr))
				{
					check(false); return;
				}
				Profile.ProfileType = ParseProfileAction(ProfileTypeStr);
				if (!ParseSimpleAppearance(Profile.StartAppearance, false,
										   JsonObj->GetObjectField("startAppearance"))
					|| !ParseActiveAppearance(Profile.ActiveAppearance,
											  JsonObj->GetObjectField("activeAppearance"))
					|| !ParseSimpleAppearance(Profile.FinishAppearance, false,
											  JsonObj->GetObjectField("endAppearance")))
				{
					FLock Lock(Mutex);
					auto& Sched = Schedules[SchedIdx];
					auto& Anim = Sched.AnimationBindings[AnimIdx];
					S4D_ERROR(TEXT("Error reading appearance profiles for task %s in schedule Id %s with "
								   "AnimatedEntityId %#x and AppearanceProfileId %s"),
							  *Anim.TaskName, *Sched.Id, Anim.AnimatedEntityId.value(),
							  *Anim.AppearanceProfileId);
					return;
				}
				FLock Lock(Mutex);
				auto& Sched = Schedules[SchedIdx];
				auto& Anim = Sched.AnimationBindings[AnimIdx];
				Anim.AppearanceProfileInVec = Sched.AppearanceProfiles.size();
				Sched.AppearanceProfiles.emplace_back(std::move(Profile));
				Sched.KnownAppearanceProfiles[Anim.AppearanceProfileId] = Anim.AppearanceProfileInVec;
				OnAnimationBindingAdded(Schedules[SchedIdx].AnimationBindings[AnimIdx],
					Schedules[SchedIdx].AppearanceProfiles[Anim.AppearanceProfileInVec]);
			});
	}
}

void FITwinSchedulesImport::FImpl::SetOnAnimationBindingAdded(FOnAnimationBindingAdded const& InCallback)
{
	FLock Lock(Mutex);
	OnAnimationBindingAdded = InCallback;
}

void FITwinSchedulesImport::FImpl::ResetConnection(TObjectPtr<AITwinServerConnection> InServerConn,
	FString const& ITwinAkaProjectAkaContextId, FString const& IModelId, bool const bInternalRetry)
{
	{	FLock Lock(Mutex);
		// I can imagine the URL or the token could need updating (new mirror, auth renew),
		// but not the iTwin nor the iModel
		check((!Queries && ITwinId.IsEmpty() && TargetedIModelId.IsEmpty())
			|| (ITwinAkaProjectAkaContextId == ITwinId && IModelId == TargetedIModelId));

		ReusableJsonQueries::FStackedBatches Batches;
		ReusableJsonQueries::FStackedRequests Requests;
		if (bInternalRetry)
		{
			if (Queries)
			{
				Queries->SwapQueues(Lock, Batches, Requests); // don't dump the existing queues!
			}
		}
		else
		{
			SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
		}
		ServerConnection = InServerConn;
		if (!Queries)
		{
			ITwinId = ITwinAkaProjectAkaContextId;
			TargetedIModelId = IModelId;
		}
		Queries = MakePimpl<FReusableJsonQueries<SimultaneousRequestsAllowed>>(
			GetSchedulesAPIBaseUrl(),
			[&CurrentToken=ServerConnection->AccessToken]()
			{
				static const FString AcceptJson(
					"application/json;odata.metadata=minimal;odata.streaming=true");
				const auto Request = FHttpModule::Get().CreateRequest();
				Request->SetHeader("Accept", AcceptJson);
				Request->SetHeader("Content-Type", AcceptJson);
				Request->SetHeader("Authorization", "Bearer " + CurrentToken);
				return Request;
			},
			std::bind(&AITwinServerConnection::CheckRequest, std::placeholders::_1, std::placeholders::_2,
					  std::placeholders::_3, std::placeholders::_4),
			Mutex);
		if (bInternalRetry)
		{
			// put back existing queues but only *after* RequestSchedules has been executed in priority
			Queries->SwapQueues(Lock, Batches, Requests,
				[this](ReusableJsonQueries::FStackingToken const& Token) { RequestSchedules(Token); });
		}
	} // end Lock

	if (!bInternalRetry)
	{
		Queries->NewBatch(
			[this](ReusableJsonQueries::FStackingToken const& Token) { RequestSchedules(Token); });
		// Wait for the completion of the initial request, assuming NextGen schedules: if nothing shows up,
		// switch to Legacy and try again.
		// TODO_GCO: If we ever need both, we could easily have two SchedulesApi, one for each server,
		// there's no hurry and we're supposed to query through a proxy anyway (see GetSchedulesAPIBaseUrl)
		Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
			{
				FLock Lock(Mutex);
				if (EITwinSchedulesGeneration::Unknown == SchedulesGeneration)
				{
					if (Schedules.empty())
					{
						SchedulesGeneration = EITwinSchedulesGeneration::Legacy;
						S4D_WARN(TEXT("No NextGen schedule found, trying Legacy..."));
						ResetConnection(ServerConnection, ITwinId, TargetedIModelId, true);
					}
					else
					{
						// found at least one next-gen schedule => all good
						SchedulesGeneration = EITwinSchedulesGeneration::NextGen;
					}
				}
				else
				{
					check(EITwinSchedulesGeneration::Legacy == SchedulesGeneration);
				}
			});
	}
}

std::pair<int, int> FITwinSchedulesImport::FImpl::HandlePendingQueries()
{
	if (!Queries) return std::make_pair(0, 0);
	Queries->HandlePendingQueries();
	auto const& QueueSize = Queries->QueueSize();
	// Avoid flooding the logs... Log only every ~10% more requests processed since last time
	std::pair<int, int> const DisplayedQueueSizeIncrements = {
		1, // batches could be of vastly different sizes :/
		//std::pow(10, std::floor(std::log10(std::max(1,  QueueSize.first)))),
		std::pow(10, std::floor(std::log10(std::max(10, QueueSize.second)))) };
	std::pair<int, int> const RoundedQueueSize = {
		QueueSize.first - (QueueSize.first % DisplayedQueueSizeIncrements.first),
		QueueSize.second - (QueueSize.second % DisplayedQueueSizeIncrements.second)
	};
	if (LastRoundedQueueSize != RoundedQueueSize
		|| LastDisplayedQueueSizeIncrements != DisplayedQueueSizeIncrements)
	{
		S4D_VERBOSE(TEXT("Remaining batches\\current batch requests: %d\\%d"),
					QueueSize.first, QueueSize.second, *ITwinId);
		LastRoundedQueueSize = RoundedQueueSize;
		LastDisplayedQueueSizeIncrements = DisplayedQueueSizeIncrements;
	}
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
				if (!Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty())
					RequestScheduleAnimationBindings(Token, SchedIdx, Lock, TimeRange);
		});
	// Not actually a new batch, just a way to have a function called upon completion
	// TODO_GCO: handle 'success' correctly by counting errors per batch in FReusableJsonQueries
	// TODO_GCO: don't count those pseudo-batches in FReusableJsonQueries::QueueSize
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); });
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
	if (ITwin::NOT_ELEMENT == ElementID)
	{
		if (OnQueriesCompleted) OnQueriesCompleted(false);
		check(false); return;
	}
	Queries->NewBatch(
		[this, ElementID]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			std::vector<ITwinElementID> ElementIDs(1, ElementID);
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
				if (!Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty())
					RequestScheduleAnimationBindings(Token, SchedIdx, Lock, {},
													 ElementIDs.begin(), ElementIDs.end());
		});

	Queries->NewBatch(
		[this, ElementID, MarginFromStart, MarginFromEnd]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			// Note: all Schedules currently merged in a single Timeline, hence the common extent
			// TODO_GCO => Schedules should be queried independently => one SchedulesApi per Schedule?
			auto const& MainTimeline = SchedulesInternals().GetTimeline();
			auto const Index = MainTimeline.GetElementTimelineIndex(ElementID);
			if (-1 == Index) return;
			auto ElemTimeRange = MainTimeline.GetElementTimelineByIndex(Index).GetTimeRange();
			if (ITwin::Time::Undefined() == ElemTimeRange)
			{
				return;
			}
			// Note: both margins are signed
			ElemTimeRange.first += ITwin::Time::FromTimespan(MarginFromStart);
			ElemTimeRange.second += ITwin::Time::FromTimespan(MarginFromEnd);
			if (ElemTimeRange.first >= ElemTimeRange.second)
			{
				return;
			}
			for (size_t SchedIdx = 0; SchedIdx < Schedules.size(); ++SchedIdx)
			{
				if (Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty())
					continue;
				RequestScheduleAnimationBindings(Token, SchedIdx, Lock, ElemTimeRange);
			}
		});

	// see comment in QueryEntireSchedules
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); });
}

void FITwinSchedulesImport::FImpl::QueryElementsTasks(std::vector<ITwinElementID>&& ElementIDs,
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
				if (Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty())
					continue;
				std::optional<FTimeRangeInSeconds> TimeRange;
				if (FromTime < UntilTime)
				{
					TimeRange.emplace(FTimeRangeInSeconds{
						ITwin::Time::FromDateTime(FromTime), ITwin::Time::FromDateTime(UntilTime) });
				}
				for (size_t RangeStart = 0; RangeStart < ElementIDs.size();
					 RangeStart += MaxElementIDsFilterSize)
				{
					RequestScheduleAnimationBindings(Token, SchedIdx, Lock, TimeRange,
						ElementIDs.begin() + RangeStart,
						ElementIDs.begin()
							+ std::min(ElementIDs.size() - RangeStart, MaxElementIDsFilterSize));
				}
			}
		});
	// see comment in QueryEntireSchedules
	if (OnQueriesCompleted)
		Queries->NewBatch([Callback=std::move(OnQueriesCompleted)]
			(ReusableJsonQueries::FStackingToken const&) { Callback(true); });
}

/// Needed in the CPP otherwise the default ctor impl complains about FITwinSchedulesConnector being unknown
FITwinSchedulesImport::FITwinSchedulesImport(UITwinSynchro4DSchedules const& InOwner,
		std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules)
	: Impl(MakePimpl<FImpl>(*this, Mutex, Schedules))
	, Owner(&InOwner)
{
}

FITwinSchedulesImport& FITwinSchedulesImport::operator=(FITwinSchedulesImport&& Other)
{
	Impl = std::move(Other.Impl);
	Impl->Owner = this;
	return *this;
}

bool FITwinSchedulesImport::IsReady() const
{
	return Impl->Queries.Get() != nullptr;
}

void FITwinSchedulesImport::ResetConnection(TObjectPtr<AITwinServerConnection> const ServerConnection,
	FString const& ITwinAkaProjectAkaContextId, FString const& IModelId)
{
	Impl->ResetConnection(ServerConnection, ITwinAkaProjectAkaContextId, IModelId, false);
}

void FITwinSchedulesImport::SetOnAnimationBindingAdded(FOnAnimationBindingAdded const& InOnAnimBindingAdded)
{
	Impl->SetOnAnimationBindingAdded(InOnAnimBindingAdded);
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

void FITwinSchedulesImport::QueryElementsTasks(std::vector<ITwinElementID>&& ElementIDs,
	FDateTime const FromTime/* = {}*/, FDateTime const UntilTime/* = {}*/,
	std::function<void(bool/*success*/)>&& OnQueriesCompleted/* = {}*/)
{
	Impl->QueryElementsTasks(std::move(ElementIDs), FromTime, UntilTime, std::move(OnQueriesCompleted));
}
