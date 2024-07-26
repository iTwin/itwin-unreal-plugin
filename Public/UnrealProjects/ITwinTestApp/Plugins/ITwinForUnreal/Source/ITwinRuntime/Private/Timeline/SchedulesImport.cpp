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
#include "TimeInSeconds.h"
#include "Timeline.h"
#include <Math/UEMathExts.h> // for RandomFloatColorFromIndex

#include <Dom/JsonObject.h>
#include <HttpModule.h>
#include <Input/Reply.h>
#include <ITwinServerConnection.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Logging/LogMacros.h>
#include <Math/UnrealMathUtility.h>
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

DECLARE_LOG_CATEGORY_EXTERN(ITwinS4DImport, Log, All);
DEFINE_LOG_CATEGORY(ITwinS4DImport);
#define S4D_VERBOSE(FORMAT, ...) UE_LOG(ITwinS4DImport, Verbose, FORMAT, ##__VA_ARGS__)
#define S4D_LOG(FORMAT, ...) UE_LOG(ITwinS4DImport, Display, FORMAT, ##__VA_ARGS__)
#define S4D_WARN(FORMAT, ...) UE_LOG(ITwinS4DImport, Warning, FORMAT, ##__VA_ARGS__)
#define S4D_ERROR(FORMAT, ...) UE_LOG(ITwinS4DImport, Error, FORMAT, ##__VA_ARGS__)

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

constexpr bool s_bDebugNoPartialTransparencies = false;
constexpr bool s_bDebugForcePartialTransparencies = false; // will extract EVERYTHING! SLOW!!

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
		[](FITwinSchedule const&, size_t const/*AnimIdx*/, FLock&) {};
	FOnAnimationGroupModified OnAnimationGroupModified =
		[](size_t const/*GroupIdx*/, std::set<ITwinElementID> const& /*GroupElements*/, FLock&) {};
	TObjectPtr<AITwinServerConnection> ServerConnection;
	ReusableJsonQueries::FMutex& Mutex;///< TODO_GCO: use a per-Schedule mutex?
	const int RequestPagination;///< pageSize for paginated requests
	/// When passing a collection of ElementIDs to filter a request, we need to cap the size for performance
	/// reasons. Julius suggested to cap to 1000 on the server.
	const size_t MaxElementIDsFilterSize;
	std::pair<int, int> LastDisplayedQueueSizeIncrements = { -1, -1 };
	std::pair<int, int> LastRoundedQueueSize = { -1, -1 };
	double LastCheckTotalBindings = 0.;
	size_t LastTotalBindingsFound = 0;
	int SchedApiSession = -1;
	static int s_NextSchedApiSession;
	FString ITwinId, TargetedIModelId; ///< Set in FITwinSchedulesImport::ResetConnection
	EITwinSchedulesGeneration SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
	std::vector<FITwinSchedule>& Schedules;
	TPimplPtr<FReusableJsonQueries<SimultaneousRequestsAllowed>> Queries;

public:
	FImpl(FITwinSchedulesImport const& InOwner, ReusableJsonQueries::FMutex& InMutex,
			std::vector<FITwinSchedule>& InSchedules, const int InRequestPagination = 100,
			const size_t InMaxElementIDsFilterSize = 900)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, RequestPagination(ITwin_TestOverrides::RequestPagination > 0
			? ITwin_TestOverrides::RequestPagination : InRequestPagination)
		, MaxElementIDsFilterSize(ITwin_TestOverrides::MaxElementIDsFilterSize > 0
			? (size_t)ITwin_TestOverrides::MaxElementIDsFilterSize : InMaxElementIDsFilterSize)
		//, SchedApiSession(s_NextSchedApiSession++) <== (re-)init by each call to ResetConnection
		, Schedules(InSchedules)
	{}
	FImpl(FImpl const&) = delete;
	FImpl& operator=(FImpl const&) = delete;

	void ResetConnection(TObjectPtr<AITwinServerConnection> ServerConnection,
		FString const& ITwinAkaProjectAkaContextId, FString const& IModelId);
	void SetSchedulesImportObservers(FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
									 FOnAnimationGroupModified const& InOnAnimationGroupModified);
	std::pair<int, int> HandlePendingQueries();
	void QueryEntireSchedules(FDateTime const FromTime, FDateTime const UntilTime,
							  std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryAroundElementTasks(ITwinElementID const ElementID, FTimespan const MarginFromStart,
		FTimespan const MarginFromEnd, std::function<void(bool/*success*/)>&& OnQueriesCompleted);
	void QueryElementsTasks(std::set<ITwinElementID>&& ElementIDs, FDateTime const FromTime,
		FDateTime const UntilTime, std::function<void(bool/*success*/)>&& OnQueriesCompleted);

private:
	UITwinSynchro4DSchedules const& SchedulesComponent() const
		{ return *Owner->Owner; }
	FITwinSynchro4DSchedulesInternals const& SchedulesInternals() const
		{ return GetInternals(SchedulesComponent()); }
	void RequestSchedules(ReusableJsonQueries::FStackingToken const&,
		std::optional<FString> const PageToken = {}, FLock* optLock = nullptr);
	void RequestAnimatedEntityUserFieldId(ReusableJsonQueries::FStackingToken const&,
										  size_t const SchedStartIdx, size_t const SchedEndIdx, FLock&);
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
		std::optional<FString> const PageToken = {}, std::optional<FString> JsonPostString = {});
	/// \param CreatedProperties When additional information for the property needs to be queried, the
	///		AnimIdx where to find the PropertyId (possibly in a nested property) is inserted in this set.
	///		The Insertable type need only support 'void insert(size_t)'
	/// \return A pair with the iterator to the created or existing property, and a flag telling whether
	///		the property either has a pending query, or needs to be queried (see CreatedProperties parameter
	///		for that case)
	template<typename Property, class Insertable>
	std::pair<Property*, bool> EmplaceProperty(size_t const AnimIdx, FString const& PropertyId,
		size_t& PropertyInVec, std::vector<Property>& SchedProperties,
		std::unordered_map<FString, size_t>& SchedKnownPropertys,
		Insertable& CreatedPropertys, FLock&);
	void CompletedProperty(FITwinSchedule& Schedule, std::vector<size_t>& Bindings, FLock& Lock,
						   FString const& From);
	void RequestTask(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx, size_t const AnimIdx,
					 FLock&);
	void ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
		TSharedPtr<FJsonObject> const& responseJson, size_t const SchedIdx, size_t const AnimIdx,
		FLock* Lock = nullptr);
	void RequestAppearanceProfile(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								  size_t const AnimIdx, FLock&);
	bool Parse3DPathAlignment(FString const& Alignment,
							  std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor);
	void RequestTransfoAssignment(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								  size_t const AnimIdx, FLock&);
	void Request3DPath(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
					   size_t const TransfoAssignmentIdx, std::optional<FString> const PageToken, FLock&);

	//void RequestSchedulesTasks(...);
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

	bool SetAnimatedEntityUserFieldId(TSharedRef<FJsonObject> JsonObj, FITwinSchedule const& Schedule) const;
	bool SupportsAnimationBindings(size_t const SchedIdx, FLock&) const;

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

}; // class FITwinSchedulesImport::FImpl

int FITwinSchedulesImport::FImpl::s_NextSchedApiSession = 0;

// conflicts with ITwinWebServices.cpp
#undef JSON_GETOBJ_OR
#undef JSON_GETOBJ_OR_CUSTOM

/// Get a non-empty string from the Json object passed, or log an error and do something (typically continue
/// or return)
#define JSON_GETSTR_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetStringField(TEXT(Field), Dest) || Dest.IsEmpty()) { \
		S4D_ERROR(TEXT("Parsing error or empty string field %s in Json response"), TEXT(Field)); \
		WhatToDo; \
	}}
/// Get a number from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETNUMBER_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetNumberField(TEXT(Field), Dest)) { \
		S4D_ERROR(TEXT("Parsing error for number field %s in Json response"), TEXT(Field)); \
		WhatToDo; \
	}}
/// Get a boolean from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETBOOL_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetBoolField(TEXT(Field), Dest)) { \
		S4D_ERROR(TEXT("Parsing error for boolean field %s in Json response"), TEXT(Field)); \
		WhatToDo; \
	}}
/// Get an Object from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETOBJ_OR(JsonObj, Field, Dest, WhatToDo) \
	{ Dest = nullptr; \
		if (!(JsonObj)->TryGetObjectField(TEXT(Field), Dest) || !Dest) { \
		S4D_ERROR(TEXT("Parsing error for object field %s in Json response"), TEXT(Field)); \
		WhatToDo; \
	}}

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
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	}
	Queries->StackRequest(Token, optLock, EVerb::GET, {}, std::move(RequestArgList),
		[this, &Token](TSharedPtr<FJsonObject> const& responseJson)
		{
			auto NewScheds = responseJson->GetArrayField(TEXT("items"));
			S4D_LOG(TEXT("Received %d schedules for iTwin %s"), (int)NewScheds.Num(), *ITwinId);
			if (0 == NewScheds.Num())
				return;
			FLock Lock(Mutex);
			size_t const SchedStartIdx = Schedules.size();
			Schedules.reserve(SchedStartIdx + NewScheds.Num());
			for (const auto& SchedVal : NewScheds)
			{
				const auto& SchedObj = SchedVal->AsObject();
				FString IModelId;
				JSON_GETSTR_OR(SchedObj, "iModelId", IModelId, continue)
				if (IModelId == TargetedIModelId)
				{
					Schedules.emplace_back(
						FITwinSchedule{ SchedObj->GetStringField(TEXT("id")),
										SchedObj->GetStringField(TEXT("name")),
										SchedulesGeneration });
					Schedules.back().Reserve(200);
					S4D_LOG(TEXT("Added schedule Id %s named '%s' to iModel %s"), *Schedules.back().Id,
							*Schedules.back().Name, *TargetedIModelId);
				}
			}
			FString NextPageToken;
			if (responseJson->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
				RequestSchedules(Token, NextPageToken, &Lock);
			else
				Queries->StatsResetActiveTime();
			if (EITwinSchedulesGeneration::Legacy != SchedulesGeneration)
				RequestAnimatedEntityUserFieldId(Token, SchedStartIdx, Schedules.size(), Lock);
		});
}

void FITwinSchedulesImport::FImpl::RequestAnimatedEntityUserFieldId(
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
				auto const& Items = responseJson->GetArrayField(TEXT("items"));
				if (Items.Num() == 0)
					return;
				FLock Lock(Mutex);
				auto&& Sched = Schedules[SchedIdx];
				for (auto&& Item : Items)
				{
					const auto& JsonObj = Item->AsObject();
					// The 'name' filter "[matches] user fields with specified name or part of it", so we
					// need to check equality:
					FString Name;
					JSON_GETSTR_OR(JsonObj, "name", Name, continue)
					if (Name == AnimatedEntityUserField)
					{
						JSON_GETSTR_OR(JsonObj, "id", Sched.AnimatedEntityUserFieldId, return)
						S4D_VERBOSE(TEXT("Found AnimatedEntityUserFieldId %s for schedule Id %s"),
									*Sched.AnimatedEntityUserFieldId, *Sched.Id);
						Queries->StatsResetActiveTime();
						break;
					}
				}
			});
	}
}

bool FITwinSchedulesImport::FImpl::SetAnimatedEntityUserFieldId(TSharedRef<FJsonObject> JsonObj,
																FITwinSchedule const& Schedule) const
{
	if (!Schedule.AnimatedEntityUserFieldId.IsEmpty())
	{
		JsonObj->SetStringField(TEXT("animatedEntityUserFieldId"), Schedule.AnimatedEntityUserFieldId);
		return true;
	}
	else
	{
		// Legacy endpoint exists but does not use an AnimatedEntityUserFieldId
		return EITwinSchedulesGeneration::Legacy == SchedulesGeneration;
	}
}

bool FITwinSchedulesImport::FImpl::SupportsAnimationBindings(size_t const SchedIdx, FLock&) const
{
	if (EITwinSchedulesGeneration::NextGen == SchedulesGeneration)
		// Note: some empty NextGen schedules do not even have the required user field, let's not assert on
		// that - it's actly the case for schedule 75c8ecfb-fa5d-4669-b68d-33b1bd29a69e in our only NextGen
		// iTwin so far (2c7efcad-19b6-4ec6-959f-f36d49699071)
		return /*ensure*/(!Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty());
	else
		return EITwinSchedulesGeneration::Legacy == SchedulesGeneration;
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
		if (ensure(FromStr.FindLastChar(TCHAR(']'), IdxClose) && IdxClose > (IdxOpen + 1)))
			FromStr.MidInline(IdxOpen + 1, IdxClose - IdxOpen - 1);
		else
			FromStr.RightChopInline(IdxOpen);
	}
	uint64 Parsed = ITwin::NOT_ELEMENT.value();
	errno = 0;
	if (FromStr.StartsWith(TEXT("0x")))//Note: StartsWith ignores case by default!
		Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/16);
	else
		Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/10);
	return (errno == 0) ? ITwinElementID(Parsed) : ITwin::NOT_ELEMENT;
}

/// Type suitable for the Insertable template parameter to FITwinSchedulesImport::FImpl::EmplaceProperty,
/// supporting insertion of a single element (replaces unordered_set to avoid useless heap allocation)
class FMonoIndexSet
{
	size_t Value = INVALID_IDX;
public:
	void insert(size_t const& InValue) { ensure(empty()); Value = InValue; }
	/// Just tells if "something" was inserted: we already know which anyway...
	bool empty() const { return Value == INVALID_IDX; }
};

} // ns ITwin

template<typename TProperty, class Insertable>
std::pair<TProperty*, bool> FITwinSchedulesImport::FImpl::EmplaceProperty(
	size_t const AnimIdx, FString const& PropertyId, size_t& PropertyInVec,
	std::vector<TProperty>& SchedProperties, std::unordered_map<FString, size_t>& SchedKnownPropertys,
	Insertable& CreatedPropertys, FLock&)
{
	if (PropertyId.IsEmpty()) // could be optional (tested elsewhere)
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
			SchedProperties.back().Bindings.emplace_back(AnimIdx);
			CreatedPropertys.insert(AnimIdx);
			return std::make_pair(&SchedProperties.back(), true);
		}
		else
		{
			auto& Property = SchedProperties[PropertyInVec];
			// already present + Bindings empty = its query was already completed.
			// otherwise, add this binding to the list that needs to be notified
			if (!Property.Bindings.empty())
			{
				Property.Bindings.emplace_back(AnimIdx);
				return std::make_pair(&Property, true);
			}
			else return std::make_pair(&Property, false);
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
	std::set<ITwinElementID>::const_iterator const ElementsEnd/* = {}*/,
	int64_t* InOutElemCount/* = nullptr*/, std::optional<FString> const PageToken/* = {}*/,
	std::optional<FString> JsonPostString/* = {}*/)
{
	bool bHasTimeRange = false;
	auto ElementsIt = ElementsBegin;
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
		ensure(SetAnimatedEntityUserFieldId(JsonObj, Sched));
		if (ElementsBegin != ElementsEnd)
		{
			TArray<TSharedPtr<FJsonValue>> AnimatedEntityIDs;
			if (InOutElemCount && (*InOutElemCount) > 0)
				AnimatedEntityIDs.Reserve(std::min(*InOutElemCount, (int64_t)MaxElementIDsFilterSize));
			if (TimeRange)
			{
				for ( ; ElementsIt != ElementsEnd && AnimatedEntityIDs.Num() <= MaxElementIDsFilterSize;
					 ++ElementsIt)
				{
					// Do not insert anything: the query is only for a specific time range...
					if (Sched.AnimBindingsFullyKnownForElem.end()
						== Sched.AnimBindingsFullyKnownForElem.find(*ElementsIt))
					{
						AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(
							FString::Printf(TEXT("0x%I64x"), ElementsIt->value())));
					}
					if (InOutElemCount) --(*InOutElemCount);
				}
			}
			else
			{
				for ( ; ElementsIt != ElementsEnd && AnimatedEntityIDs.Num() <= MaxElementIDsFilterSize;
					 ++ElementsIt)
				{
					if (Sched.AnimBindingsFullyKnownForElem.try_emplace(*ElementsIt, VersionToken::None)
						.second)
					{
						// not known => was inserted
						AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(
							FString::Printf(TEXT("0x%I64x"), ElementsIt->value())));
					}
					if (InOutElemCount) --(*InOutElemCount);
				}
			}
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
		FJsonSerializer::Serialize(JsonObj, JsonWriter);
	}
	FUrlArgList RequestArgList = {
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	if (PageToken)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	Queries->StackRequest(Token, &Lock, EVerb::POST,
		{ Schedules[SchedIdx].Id, TEXT("animationBindings/query") }, std::move(RequestArgList),
		[this, SchedIdx, TimeRange, JsonPostString, bHasTimeRange, &Token]
		(TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("items"));
			if (Items.IsEmpty())
				return;
			FLock Lock(Mutex);
			FString NextPageToken;
			if (Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
			{
				// No need to repeat the TimeRange and ElementIDs parameters, they are already included in
				// the JsonPostString content
				RequestAnimationBindings(Token, SchedIdx, Lock, {}, {}, {}, {},
										 NextPageToken, JsonPostString);
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

				FString AnimElemIdHexa;
				JSON_GETSTR_OR(BindingObj, "animatedEntityId", AnimElemIdHexa, continue)
				ITwinElementID const ElementID = ITwin::ParseElementID(AnimElemIdHexa);
				// TODO_GCO: add some error handling/flagging about everywhere...
				if (ElementID == ITwin::NOT_ELEMENT)
				{
					continue;
				}
				// If there is a time range, only read from AnimBindingsFullyKnownForElem. If not, we'll want
				// to insert it: if there was no filtering by Elements (ElementsIt == ElementsEnd),
				// nothing was inserted yet (see above the call to StackRequest, when creating JsonPostString)
				// so we need to do it now since we will be parsing the reply to fill the binding details.
				// Note: not to be confused with FAnimationBinding::NotifiedVersion, which only switches to
				// InitialVersion later, when notifying the observer (timeline builder), as the name implies
				auto Known = bHasTimeRange
					? Sched.AnimBindingsFullyKnownForElem.find(ElementID)
					: Sched.AnimBindingsFullyKnownForElem.try_emplace(ElementID, VersionToken::None)
						.first;
				if (Sched.AnimBindingsFullyKnownForElem.end() != Known)
				{
					if (Known->second == VersionToken::InitialVersion)
					{
						continue; // already fully known, can skip
					}
					Known->second = VersionToken::InitialVersion;
				}
				//else: no-op, query was only on a specific time range

				FAnimationBinding Tmp;
				JSON_GETSTR_OR(BindingObj, "taskId", Tmp.TaskId, continue)
				JSON_GETSTR_OR(BindingObj, "appearanceProfileId", Tmp.AppearanceProfileId, continue)
				FString AnimatedEntitiesAsGroup;
				// Laurynas confirmed to check resourceGroupId first, then resourceId - both can be present
				if (BindingObj->TryGetStringField(TEXT("resourceGroupId"), AnimatedEntitiesAsGroup))
					Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
				else if (BindingObj->TryGetStringField(TEXT("resourceId"), AnimatedEntitiesAsGroup))
					Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
				else
					Tmp.AnimatedEntities = ElementID;
			#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
				if (BindingObj->TryGetStringField(TEXT("transformId"), Tmp.TransfoAssignmentId))
				{
					Tmp.bStaticTransform = true;
					ensure(!BindingObj->HasTypedField<EJson::String>(TEXT("pathAssignmentId")));
				}
				else if (BindingObj->TryGetStringField(TEXT("pathAssignmentId"), Tmp.TransfoAssignmentId))
				{
					Tmp.bStaticTransform = false;
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
						auto KnownGroup = Sched.KnownGroups.try_emplace(std::get<1>(Anim.AnimatedEntities), 
																		Sched.Groups.size());
						Anim.GroupInVec = KnownGroup.first->second;
						if (KnownGroup.second) // was inserted => need to create it
							Sched.Groups.emplace_back();
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
					EmplaceProperty(AnimIdx, Anim.TransfoAssignmentId, Anim.TransfoAssignmentInVec,
									Sched.TransfoAssignments, Sched.KnownTransfoAssignments,
									CreatedTransfoAssignments, Lock);
				// Just check nested Animation3DPath property to see if binding is actually fully known
				if (TransfoAssignmentIncomplete.second)
				{
					bIncomplete = true;
				}
				else if (TransfoAssignmentIncomplete.first)
				{
					// TransfoAssignment's properties are known, but its Animation3DPath details may not
					// (no such case with a static transform), but in that case it was created and requested
					// when the transfo-assignment query completed
					auto& TransfoAssignment = Sched.TransfoAssignments[Anim.TransfoAssignmentInVec];
					if (!Anim.bStaticTransform
						&& ensure(std::holds_alternative<FPathAssignment>(TransfoAssignment.Transformation)))
					{
						FPathAssignment& Assignment = std::get<1>(TransfoAssignment.Transformation);
						if (ensure(ITwin::INVALID_IDX != Assignment.Animation3DPathInVec)
							&& !Sched.Animation3DPaths[Assignment.Animation3DPathInVec].Bindings.empty())
						{
							// here it is still pending, so this whole binding it not fully known
							bIncomplete = true;
						}
					}
				}
				//else: there is no TransfoAssignment at all (Id was empty)
			#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()

				// mostly redundant with tests in EmplaceProperty, hence the flag computed above
				//if (!Anim.FullyDefined(Sched, false, Lock))
				if (!bIncomplete)
					FullyDefinedBindingsToNotify.insert(AnimIdx);
			}
			// Note: see comment above the loop and the unordered_set Created*** definition to understand why
			// the calls below are indeed unique by PropertyId!
			// TODO_GCO: this ordering means until all sub-queries are processed, none of the bindings will
			// probably be fully known. On the other hand, it might mean better data locality on the server...
			for (auto&& AnimIdx : CreatedTasks)
				RequestTask(Token, SchedIdx, AnimIdx, Lock);
			for (auto&& AnimIdx : CreatedAppearanceProfiles)
				RequestAppearanceProfile(Token, SchedIdx, AnimIdx, Lock);
			for (auto&& AnimIdx : CreatedTransfoAssignments)
				RequestTransfoAssignment(Token, SchedIdx, AnimIdx, Lock);
			for (auto&& GroupInVec : UpdatedGroups)
			{
				OnAnimationGroupModified(GroupInVec, Sched.Groups[GroupInVec], Lock);
			}
			for (auto&& Binding : FullyDefinedBindingsToNotify)
			{
				OnAnimationBindingAdded(Sched, Binding, Lock);
				Sched.AnimationBindings[Binding].NotifiedVersion = VersionToken::InitialVersion;
				S4D_VERBOSE(TEXT("Complete binding notified: %s"),
							*Sched.AnimationBindings[Binding].ToString());
			}
		},
		FString(*JsonPostString));

	return ElementsIt;
}

void FITwinSchedulesImport::FImpl::CompletedProperty(FITwinSchedule& Schedule, std::vector<size_t>& Bindings,
													 FLock& Lock, FString const& From)
{
	std::vector<size_t> Swapped;
	Swapped.swap(Bindings);
	for (size_t AnimIdx : Swapped)
	{
		auto&& AnimationBinding = Schedule.AnimationBindings[AnimIdx];
		if (AnimationBinding.FullyDefined(Schedule, false, Lock))
		{
			if (AnimationBinding.NotifiedVersion != VersionToken::InitialVersion)
			{
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

// Separate method from the time RequestSchedulesTasks was used for Legacy projects
void FITwinSchedulesImport::FImpl::ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
	TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx, size_t const AnimIdx,
	FLock* MaybeLock /*= nullptr*/)
{
	FString Name = JsonObj->GetStringField(TEXT("name"));
	// Using "Planned" ATM (Laurynas said timerange filtering does that too), but we should probably also
	// support the notion of "Best date" (Actual if any, Planned otw) like Synchro/Pineapple (TODO_GCO)
	FString PlannedStartStr, PlannedFinishStr;
	JSON_GETSTR_OR(JsonObj, "plannedStart", PlannedStartStr, return)
	JSON_GETSTR_OR(JsonObj, "plannedFinish", PlannedFinishStr, return)
	FDateTime PlannedStart, PlannedFinish;
	bool const bCouldParseDates = FDateTime::ParseIso8601(*PlannedStartStr, PlannedStart)
							   && FDateTime::ParseIso8601(*PlannedFinishStr, PlannedFinish);
	std::optional<FLock> OptLockDontUse;
	if (!MaybeLock) OptLockDontUse.emplace(Mutex);
	FLock& Lock = MaybeLock ? (*MaybeLock) : (*OptLockDontUse);
	auto& Sched = Schedules[SchedIdx];
	auto& Anim = Sched.AnimationBindings[AnimIdx];
	auto& Task = Sched.Tasks[Anim.TaskInVec];
	Task.Name = std::move(Name);
	if (ensure(bCouldParseDates))
	{
		Task.TimeRange.first = ITwin::Time::FromDateTime(PlannedStart);
		Task.TimeRange.second = ITwin::Time::FromDateTime(PlannedFinish);
		S4D_VERBOSE(TEXT("Task %s named '%s' for schedule Id %s spans %s to %s"),
					*Anim.TaskId, *Task.Name, *Sched.Id, *PlannedStartStr, *PlannedFinishStr);
		CompletedProperty(Sched, Task.Bindings, Lock, TEXT("TaskDetails"));
	}
	else
	{
		Task.TimeRange = ITwin::Time::Undefined();
		S4D_ERROR(TEXT("Task %s named '%s' for schedule Id %s has invalid date(s)!"),
				  *Anim.TaskId, *Task.Name, *Sched.Id);
	}
}

void FITwinSchedulesImport::FImpl::RequestTask(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	Queries->StackRequest(Token, &Lock, EVerb::GET,
		{ Schedules[SchedIdx].Id, TEXT("tasks"), Schedules[SchedIdx].AnimationBindings[AnimIdx].TaskId },
		{},
		std::bind(&FITwinSchedulesImport::FImpl::ParseTaskDetails, this, std::cref(Token),
				  std::placeholders::_1, SchedIdx, AnimIdx, nullptr));
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
	size_t Seed = std::holds_alternative<ITwinElementID>(AnimationBinding.AnimatedEntities)
		? std::get<0>(AnimationBinding.AnimatedEntities).value()
		: GetTypeHash(std::get<1>(AnimationBinding.AnimatedEntities));
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

bool FITwinSchedulesImport::FImpl::ColorFromHexString(FString const& FromStr, FVector& Color)
{
	if (FromStr.Len() < 6)
		return false;
	uint64 const Clr = FCString::Strtoui64(*FromStr.Right(6), nullptr, /*base*/16);
	Color.X = ((Clr & 0xFF0000) >> 16) / 255.;
	Color.Y = ((Clr & 0x00FF00) >> 8) / 255.;
	Color.Z =  (Clr & 0x0000FF) / 255.;
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out)
{
	JSON_GETNUMBER_OR(JsonObj, "x", Out.X, return false)
	JSON_GETNUMBER_OR(JsonObj, "y", Out.Y, return false)
	JSON_GETNUMBER_OR(JsonObj, "z", Out.Z, return false)
	return true;
}

/// Note: direction of growth kept in iTwin reference system
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
	bool const bBaseOfActiveApperance, TSharedPtr<FJsonObject> const& JsonObj)
{
	FString ColorStr;
	JSON_GETSTR_OR(JsonObj, "color", ColorStr, return false)
	if (!ColorFromHexString(ColorStr, Appearance.Color))
		return false;

	if (bBaseOfActiveApperance)
		JSON_GETNUMBER_OR(JsonObj, "startTransparency", Appearance.Alpha, return false)
	else
		JSON_GETNUMBER_OR(JsonObj, "transparency", Appearance.Alpha, return false)

	if constexpr (s_bDebugNoPartialTransparencies)
		Appearance.Alpha = (Appearance.Alpha == 0.f) ? 1.f : 0.f;
	else if constexpr (s_bDebugForcePartialTransparencies)
		Appearance.Alpha = 0.3f;
	else
		Appearance.Alpha = std::clamp(1.f - Appearance.Alpha / 100.f, 0.f, 1.f);

	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool OrgCol = true, OrgTransp = true;
	JSON_GETBOOL_OR(JsonObj, "useOriginalColor", OrgCol, return false)
	JSON_GETBOOL_OR(JsonObj, "useOriginalTransparency", OrgTransp, return false)
	Appearance.bUseOriginalColor = OrgCol;
	Appearance.bUseOriginalAlpha = OrgTransp;
	return true;
}

bool FITwinSchedulesImport::FImpl::ParseActiveAppearance(FActiveAppearance& Appearance,
														 TSharedPtr<FJsonObject> const& JsonObj)
{
	if (!ParseSimpleAppearance(Appearance.Base, true, JsonObj))
		return false;
	JSON_GETNUMBER_OR(JsonObj, "finishTransparency", Appearance.FinishAlpha, return false)
	if constexpr (s_bDebugNoPartialTransparencies)
		Appearance.FinishAlpha = (Appearance.FinishAlpha == 0.f) ? 1.f : 0.f;
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

void FITwinSchedulesImport::FImpl::RequestAppearanceProfile(
	ReusableJsonQueries::FStackingToken const& Token, size_t const SchedIdx, size_t const AnimIdx,
	FLock& Lock)
{
	if (SchedulesComponent().bDebugWithRandomProfiles)
	{
		CreateRandomAppearanceProfile(SchedIdx, AnimIdx, Lock);
		return;
	}
	auto&& Sched = Schedules[SchedIdx];
	Queries->StackRequest(Token, &Lock, EVerb::GET,
		{ Sched.Id, TEXT("appearanceProfiles"), Sched.AnimationBindings[AnimIdx].AppearanceProfileId },
		{},
		[this, SchedIdx, AnimIdx](TSharedPtr<FJsonObject> const& JsonObj)
		{
			FAppearanceProfile Parsed;
			FString ProfileTypeStr;
			JSON_GETSTR_OR(JsonObj, "action", ProfileTypeStr, return)
			Parsed.ProfileType = ParseProfileAction(ProfileTypeStr);
			TSharedPtr<FJsonObject> const *StartObj, *ActiveObj, *EndObj;
			JSON_GETOBJ_OR(JsonObj, "startAppearance", StartObj, return)
			JSON_GETOBJ_OR(JsonObj, "activeAppearance", ActiveObj, return)
			JSON_GETOBJ_OR(JsonObj, "endAppearance", EndObj, return)
			if (!ParseSimpleAppearance(Parsed.StartAppearance, false, *StartObj)
				|| !ParseActiveAppearance(Parsed.ActiveAppearance, *ActiveObj)
				|| !ParseSimpleAppearance(Parsed.FinishAppearance, false, *EndObj))
			{
				FLock Lock(Mutex);
				S4D_ERROR(TEXT("Error reading appearance profiles for %s"),
						  *Schedules[SchedIdx].AnimationBindings[AnimIdx].ToString());
				return;
			}
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
			auto& AppearanceProfile =
				Sched.AppearanceProfiles[Sched.AnimationBindings[AnimIdx].AppearanceProfileInVec];
			// swap with the empty Parsed.Bindings so that we can move the whole thing
			Parsed.Bindings.swap(AppearanceProfile.Bindings); // don't lose this
			AppearanceProfile = std::move(Parsed);
			CompletedProperty(Sched, AppearanceProfile.Bindings, Lock, TEXT("Appearance"));
		});
}

/// Note: anchor point kept in iTwin reference system
bool FITwinSchedulesImport::FImpl::Parse3DPathAlignment(FString const& FromStr,
	std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor)
{
	if (ensure(FromStr.Len() >= 2))
	{
		auto const Lower = FromStr.ToLower();
		switch (Lower[0])
		{
		case 'c':
			if (Lower[1] == 'u')
				Anchor = FVector::Zero(); // make an FVector variant - all other cases use the enum
			else if (Lower[1] == 'e')
				Anchor = ITwin::Timeline::EAnchorPoint::Center;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 'b':
			if (Lower[1] == 'o')
				Anchor = ITwin::Timeline::EAnchorPoint::Bottom;
			else if (Lower[1] == 'a')
				Anchor = ITwin::Timeline::EAnchorPoint::Back;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 't': Anchor = ITwin::Timeline::EAnchorPoint::Top; break;
		case 'f': Anchor = ITwin::Timeline::EAnchorPoint::Front; break;
		case 'l': Anchor = ITwin::Timeline::EAnchorPoint::Left; break;
		case 'r': Anchor = ITwin::Timeline::EAnchorPoint::Right; break;
		case 'o': Anchor = ITwin::Timeline::EAnchorPoint::Original; break;
		default: return false;
		}
		return true;
	}
	return false;
}

void FITwinSchedulesImport::FImpl::RequestTransfoAssignment(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto&& AnimationBinding = Sched.AnimationBindings[AnimIdx];
	Queries->StackRequest(Token, &Lock, EVerb::GET,
		{ Sched.Id,
		  AnimationBinding.bStaticTransform ? TEXT("animation3dTransforms")
											: TEXT("animation3dPathAssignments"),
		  AnimationBinding.TransfoAssignmentId
		},
		{},
		[this, SchedIdx, AnimIdx, &Token, bStaticTransform = AnimationBinding.bStaticTransform]
		(TSharedPtr<FJsonObject> const& JsonObj)
		{
			std::optional<FTransform> StaticTransform;
			std::optional<FPathAssignment> PathAssignment;
			if (bStaticTransform)
			{
				auto&& TransfoArray = JsonObj->GetArrayField(TEXT("transform"));
				if (ensure(TransfoArray.Num() == 16))
				{
					FMatrix Mat;
					for (int Row = 0; Row < 4; ++Row)
						for (int Col = 0; Col < 4; ++Col)
						{
							if (!TransfoArray[4 * Row + Col]->TryGetNumber(Mat.M[Row][Col]))
							{
								ensure(false); return;
							}
						}
					StaticTransform.emplace(Mat);
				}
				else return;
			}
			else
			{
				PathAssignment.emplace();
				JSON_GETSTR_OR(JsonObj, "pathId", PathAssignment->Animation3DPathId, return);
				FString Alignment;
				JSON_GETSTR_OR(JsonObj, "alignment", Alignment, return);
				if (!Parse3DPathAlignment(Alignment, PathAssignment->TransformAnchor)) return;
				if (std::holds_alternative<FVector>(PathAssignment->TransformAnchor))
				{
					TSharedPtr<FJsonObject> const* CenterObj;
					JSON_GETOBJ_OR(JsonObj, "center", CenterObj, return);
					if (!ParseVector(*CenterObj, std::get<1>(PathAssignment->TransformAnchor))) return;
				}
				JSON_GETBOOL_OR(JsonObj, "reverseDirection", PathAssignment->b3DPathReverseDirection, return);
			}
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
			size_t const& TransfoAssignmentIndex = Sched.AnimationBindings[AnimIdx].TransfoAssignmentInVec;
			auto& TransformAssignment = Sched.TransfoAssignments[TransfoAssignmentIndex];
			if (bStaticTransform)
			{
				TransformAssignment.Transformation = std::move(*StaticTransform);
				CompletedProperty(Sched, TransformAssignment.Bindings, Lock, TEXT("StaticTransfoAssign"));
			}
			else
			{
				TransformAssignment.Transformation = std::move(*PathAssignment);
				auto& TransfoAsPath = std::get<1>(TransformAssignment.Transformation);
				ITwin::FMonoIndexSet Created;
				auto&& TransformListIncomplete = EmplaceProperty(AnimIdx, TransfoAsPath.Animation3DPathId,
					TransfoAsPath.Animation3DPathInVec, Sched.Animation3DPaths, Sched.KnownAnimation3DPaths,
					Created, Lock);
				if (!Created.empty()) // need to query the newly created/discovered 3D Path
				{
					// Transfer the responsibility of checking and notifiyng the completed bindings (since
					// there is no other sub-property the TransfoAssignment depends on)
					Sched.Animation3DPaths[TransfoAsPath.Animation3DPathInVec].Bindings =
						std::move(TransformAssignment.Bindings);
					Request3DPath(Token, SchedIdx, TransfoAssignmentIndex, {}, Lock);
				}
				else if (TransformListIncomplete.second == false)
					CompletedProperty(Sched, TransformAssignment.Bindings, Lock, TEXT("Path3dAssign"));
				//else: incomplete but already queried, just wait for completion
			}
		});
}

/// \param TransfoAssignmentIdx Index of one (of possibly several) FTransformAssignment pointing at this path:
///		easier than passing the path Id and path index in the schedule's vector
void FITwinSchedulesImport::FImpl::Request3DPath(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const TransfoAssignmentIdx, std::optional<FString> const PageToken,
	FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	FUrlArgList RequestArgList = {
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	bool bFirstPage = true;
	if (PageToken)
	{
		bFirstPage = false;
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	}
	Queries->StackRequest(Token, &Lock, EVerb::GET,
		{ Sched.Id, TEXT("animation3dPaths"),
		  std::get<1>(Sched.TransfoAssignments[TransfoAssignmentIdx].Transformation).Animation3DPathId,
		  TEXT("keyframes")
		},
		std::move(RequestArgList),
		[this, SchedIdx, TransfoAssignmentIdx, bFirstPage, &Token] (TSharedPtr<FJsonObject> const& JsonObj)
		{
			auto&& KeyframesArray = JsonObj->GetArrayField(TEXT("items"));
			if (KeyframesArray.IsEmpty())
				return;
			FAnimation3DPath Parsed;
			Parsed.Keyframes.reserve(KeyframesArray.Num());
			for (auto&& Entry : KeyframesArray)
			{
				auto&& KeyframeObj = Entry->AsObject();
				auto& Keyframe = Parsed.Keyframes.emplace_back();
				JSON_GETNUMBER_OR(KeyframeObj, "time", Keyframe.RelativeTime, return)
				TSharedPtr<FJsonObject> const *PosObj, *RotObj;
				JSON_GETOBJ_OR(KeyframeObj, "position", PosObj, return)
				FVector Pos, RotAxis;
				if (!ParseVector(*PosObj, Pos)) return;
				Keyframe.Transform = FTransform(Pos);
				JSON_GETOBJ_OR(KeyframeObj, "rotation", RotObj, continue) // support optional rotation
				if (!ParseVector(*RotObj, RotAxis)) continue;
				double AngleDegrees;
				JSON_GETNUMBER_OR(*RotObj, "angle", AngleDegrees, continue)
				Keyframe.Transform.SetRotation(FQuat(RotAxis, FMath::DegreesToRadians(AngleDegrees)));
			}
			FLock Lock(Mutex);
			auto& Sched = Schedules[SchedIdx];
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
			FString NextPageToken;
			if (JsonObj->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
				Request3DPath(Token, SchedIdx, TransfoAssignmentIdx, std::move(NextPageToken), Lock);
			else
				CompletedProperty(Sched, Path3D.Bindings, Lock, TEXT("Path3d"));
		});
}

void FITwinSchedulesImport::FImpl::SetSchedulesImportObservers(
	FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
	FOnAnimationGroupModified const& InOnAnimationGroupModified)
{
	FLock Lock(Mutex);
	if (ensure(InOnAnimationBindingAdded))
		OnAnimationBindingAdded = InOnAnimationBindingAdded;
	if (ensure(InOnAnimationGroupModified))
		OnAnimationGroupModified = InOnAnimationGroupModified;
}

void FITwinSchedulesImport::FImpl::ResetConnection(TObjectPtr<AITwinServerConnection> InServerConn,
	FString const& ITwinAkaProjectAkaContextId, FString const& IModelId)
{
	{	FLock Lock(Mutex);
		// I can imagine the URL or the token could need updating (new mirror, auth renew),
		// but not the iTwin nor the iModel
		ensure((!Queries && ITwinId.IsEmpty() && TargetedIModelId.IsEmpty())
			|| (ITwinAkaProjectAkaContextId == ITwinId && IModelId == TargetedIModelId));

		SchedApiSession = s_NextSchedApiSession++;
		ReusableJsonQueries::FStackedBatches Batches;
		ReusableJsonQueries::FStackedRequests Requests;
		SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
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
			Mutex,
			Owner->Owner->DebugRecordSessionQueries.IsEmpty()
				? nullptr : (*Owner->Owner->DebugRecordSessionQueries),
			SchedApiSession, 
			Owner->Owner->DebugSimulateSessionQueries.IsEmpty()
				? nullptr : (*Owner->Owner->DebugSimulateSessionQueries),
			ReusableJsonQueries::EReplayMode::OnDemandSimulation);
	} // end Lock

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
					S4D_WARN(TEXT("No NextGen schedule found, trying Legacy..."));
					SchedulesGeneration = EITwinSchedulesGeneration::Legacy;
					Queries->ChangeRemoteUrl(GetSchedulesAPIBaseUrl());
					RequestSchedules(Token);
				}
				else
				{
					// found at least one next-gen schedule => all good
					SchedulesGeneration = EITwinSchedulesGeneration::NextGen;
				}
			}
			else
			{
				ensure(EITwinSchedulesGeneration::Legacy == SchedulesGeneration);
			}
		});
}

std::pair<int, int> FITwinSchedulesImport::FImpl::HandlePendingQueries()
{
	if (!Queries) return std::make_pair(0, 0);
	Queries->HandlePendingQueries();
	auto const& QueueSize = Queries->QueueSize();
	if (QueueSize.first == 0 && QueueSize.second == 0)
	{
		FLock Lock(Mutex);
		if (LastCheckTotalBindings != 0. && (LastCheckTotalBindings + 1.) > FPlatformTime::Seconds())
			return { 0, 0 }; // Checked less than one second ago
		size_t NewTotalBindings = 0;
		for (auto&& Sched : Schedules)
			NewTotalBindings += Sched.AnimationBindings.size();
		if (NewTotalBindings == LastTotalBindingsFound)
			return { 0, 0 }; // no new binding since we last checked
		LastTotalBindingsFound = 0;
		for (auto&& Sched : Schedules)
		{
			if (!Sched.AnimationBindings.empty())
			{
				LastTotalBindingsFound += Sched.AnimationBindings.size();
				S4D_LOG(TEXT("Current Schedules: %s\nQuerying statistics: %s"),
					*Sched.ToString(), Queries ? (*Queries->Stats()) : TEXT("na."));
			}
		}
		LastCheckTotalBindings = FPlatformTime::Seconds();
		return { 0, 0 };
	}
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
		S4D_LOG(TEXT("Still %d pending batches, and %d requests in current batch..."), QueueSize.first, QueueSize.second);
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
				if (SupportsAnimationBindings(SchedIdx, Lock))
					RequestAnimationBindings(Token, SchedIdx, Lock, TimeRange);
				//else RequestSchedulesTasks(...) <== removed so as not to refactor it...
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
				if (SupportsAnimationBindings(SchedIdx, Lock))
					RequestAnimationBindings(Token, SchedIdx, Lock, {}, ElementIDs.begin(), ElementIDs.end());
		});

	Queries->NewBatch(
		[this, ElementID, MarginFromStart, MarginFromEnd]
		(ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			// Note: all Schedules currently merged in a single Timeline, hence the common extent
			// TODO_GCO => Schedules should be queried independently => one SchedulesApi per Schedule?
			auto const& MainTimeline = SchedulesInternals().GetTimeline();
			FTimeRangeInSeconds ElemTimeRange = ITwin::Time::Undefined();
			MainTimeline.ForEachElementTimeline(ElementID,
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
				if (SupportsAnimationBindings(SchedIdx, Lock))
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
				if (!SupportsAnimationBindings(SchedIdx, Lock))
					continue;
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
	Impl->ResetConnection(ServerConnection, ITwinAkaProjectAkaContextId, IModelId);
}

void FITwinSchedulesImport::SetSchedulesImportObservers(
	FOnAnimationBindingAdded const& InOnAnimationBindingAdded,
	FOnAnimationGroupModified const& InOnAnimationGroupModified)
{
	Impl->SetSchedulesImportObservers(InOnAnimationBindingAdded, InOnAnimationGroupModified);
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
									 ReusableJsonQueries::FLock&) const
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

FString FAnimationBinding::ToString(const TCHAR* SpecificElementID/* = nullptr*/) const
{
	return FString::Printf(TEXT("binding for ent. %s%s, appear. %s%s%s"),
		//(Name.IsEmpty() ? (*(FString(" Id ") + TaskId)) : *Name),
		SpecificElementID ? SpecificElementID : TEXT(""),
		std::holds_alternative<ITwinElementID>(AnimatedEntities)
			? (*FString::Printf(TEXT("%#x"), std::get<0>(AnimatedEntities).value()))
			: *(TEXT("in group ") + std::get<1>(AnimatedEntities)),
		*AppearanceProfileId,
		TransfoAssignmentId.IsEmpty() ? TEXT("") : *(TEXT(", transf. ") + TransfoAssignmentId),
		TransfoAssignmentId.IsEmpty() ? TEXT("")
									  : (bStaticTransform ? TEXT(" (static)") : TEXT(" (3D path)")));
}

FString FITwinSchedule::ToString() const
{
	return FString::Printf(TEXT("%s Schedule %s (\"%s\"), with:\n" \
		"\t%llu bindings, %llu tasks, %llu groups, %llu appearance profiles,\n" \
		"\t%llu transfo. assignments (incl. %llu 3D paths).\n" \
		"\t%llu ouf of %llu Elements are bound to a task."),
		(EITwinSchedulesGeneration::Unknown == Generation) ? TEXT("<?>")
			: ((EITwinSchedulesGeneration::Legacy == Generation) ? TEXT("Legacy") : TEXT("NextGen")),
		*Id, *Name, AnimationBindings.size(), Tasks.size(), Groups.size(), AppearanceProfiles.size(),
		TransfoAssignments.size(), Animation3DPaths.size(),
		std::count_if(AnimBindingsFullyKnownForElem.begin(), AnimBindingsFullyKnownForElem.end(),
			[](auto&& Known) { return VersionToken::InitialVersion == Known.second; }),
		AnimBindingsFullyKnownForElem.size());
}
