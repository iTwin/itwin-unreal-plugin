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

#include <algorithm>
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

namespace ITwin_TestOverrides
{
	// See comment on declaration in SchedulesConstants.h
	int RequestPagination = -1;
	int64_t MaxElementIDsFilterSize = -1;
}

constexpr bool s_bDebugNoPartialTransparencies = false;
constexpr bool s_bDebugForcePartialTransparencies = false; // will extract EVERYTHING! SLOW!!

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
	ITwinHttp::FMutex& Mutex;///< TODO_GCO: use a per-Schedule mutex?
	const int RequestPagination;///< pageSize for paginated requests EXCEPT animation bindings
	/// When passing a collection of ElementIDs to filter a request, we need to cap the size for performance
	/// reasons. Julius suggested to cap to 1000 on the server.
	const size_t MaxElementIDsFilterSize;
	bool bHasFullSchedule = false;
	std::pair<int, int> LastDisplayedQueueSizeIncrements = { -1, -1 };
	std::pair<int, int> LastRoundedQueueSize = { -1, -1 };
	double LastCheckTotalBindings = 0.;
	size_t LastTotalBindingsFound = 0;
	int SchedApiSession = -1;
	static int s_NextSchedApiSession;
	FString ITwinId, TargetedIModelId, ChangesetId; ///< Set in FITwinSchedulesImport::ResetConnection
	EITwinSchedulesGeneration SchedulesGeneration = EITwinSchedulesGeneration::Unknown;
	std::vector<FITwinSchedule>& Schedules;
	TPimplPtr<FReusableJsonQueries> Queries;

public:
	FImpl(FITwinSchedulesImport const& InOwner, ITwinHttp::FMutex& InMutex,
			std::vector<FITwinSchedule>& InSchedules)
		: Owner(&InOwner)
		, Mutex(InMutex)
		, RequestPagination(ITwin_TestOverrides::RequestPagination > 0
			? ITwin_TestOverrides::RequestPagination : InOwner.Owner->ScheduleQueriesServerPagination)
		, MaxElementIDsFilterSize(ITwin_TestOverrides::MaxElementIDsFilterSize > 0
			? (size_t)ITwin_TestOverrides::MaxElementIDsFilterSize
			: InOwner.Owner->ScheduleQueriesMaxElementIDsFilterSize)
		//, SchedApiSession(s_NextSchedApiSession++) <== (re-)init by each call to ResetConnection
		, Schedules(InSchedules)
	{}
	FImpl(FImpl const&) = delete;
	FImpl& operator=(FImpl const&) = delete;

	void ResetConnection(FString const& ITwinAkaProjectAkaContextId, FString const& IModelId,
						 FString const& InChangesetId);
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
	UITwinSynchro4DSchedules const& SchedulesComponent() const { return *Owner->Owner; }
	UITwinSynchro4DSchedules& SchedulesComponent() { return *Owner->Owner; }
	TObjectPtr<AITwinServerConnection> const& GetServerConnection() const
		{ return Cast<AITwinIModel>(SchedulesComponent().GetOwner())->ServerConnection; }
	FITwinSynchro4DSchedulesInternals const& SchedulesInternals() const
		{ return GetInternals(SchedulesComponent()); }
	FITwinSynchro4DSchedulesInternals& SchedulesInternals() { return GetInternals(SchedulesComponent()); }
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
		std::optional<size_t> const BeginTaskIdx = {}, std::optional<size_t> const EndTaskIdx = {},
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
	void RequestAllTasks(ReusableJsonQueries::FStackingToken const&, size_t const SchedStartIdx,
		size_t const SchedEndIdx, FLock&, std::optional<FString> const PageToken = {});
	void RequestTask(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx, size_t const AnimIdx,
					 FLock&);
	void ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
		TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx, size_t const TaskInVec,
		FLock* MaybeLock = nullptr);
	void ParseAppearanceProfileDetails(ReusableJsonQueries::FStackingToken const& Token,
		TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx, FString const AppearanceProfileId,
		size_t const AppearanceProfileInVec, FLock* MaybeLock = nullptr);
	void RequestAllAppearanceProfiles(ReusableJsonQueries::FStackingToken const&, size_t const SchedStartIdx,
		size_t const SchedEndIdx, FLock&, std::optional<FString> const PageToken = {});
	void RequestAppearanceProfile(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								  size_t const AnimIdx, FLock&);
	bool Parse3DPathAlignment(FString const& Alignment,
							  std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor);
	void RequestTransfoAssignment(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
								  size_t const AnimIdx, FLock&);
	void Request3DPath(ReusableJsonQueries::FStackingToken const&, size_t const SchedIdx,
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

	bool SetAnimatedEntityUserFieldId(TSharedRef<FJsonObject> JsonObj, FITwinSchedule const& Schedule) const;
	bool SupportsAnimationBindings(size_t const SchedIdx, FLock&) const;

	int GetSchedulesAPIVersion() const
	{
		return 1;
		//switch (ServerConnection->SchedulesGeneration)
		//{
		//case EITwinSchedulesGeneration::Unknown:
		//case EITwinSchedulesGeneration::Legacy:
		//	return 1;
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
		case EITwinSchedulesGeneration::NextGen:
			return FString::Printf(TEXT("https://%ssynchro4dschedulesapi-eus.bentley.com/api/v%d/schedules"),
								   *GetServerConnection()->UrlPrefix(), GetSchedulesAPIVersion());
		// not yet known: try Legacy first: same assumption should also be enforced in GetIdToQuerySchedules 
		// below, as well as in FImpl::ResetConnection and FImpl::RequestSchedules!
		case EITwinSchedulesGeneration::Unknown:
			[[fallthrough]];
		case EITwinSchedulesGeneration::Legacy:
			return FString::Printf(TEXT("https://%ses-api.bentley.com/4dschedule/v%d/schedules"),
								   *GetServerConnection()->UrlPrefix(), GetSchedulesAPIVersion());
		}
		check(false);
		return "<invalidUrl>";
	}

	FString GetIdToQuerySchedules() const
	{
		switch (SchedulesGeneration)
		{
		case EITwinSchedulesGeneration::Unknown:
			// not yet known: we'll try Legacy first (see important comment on GetSchedulesAPIBaseUrl!)
			[[fallthrough]];
		case EITwinSchedulesGeneration::Legacy:
			return "projectId";
		case EITwinSchedulesGeneration::NextGen:
			return "contextId";
		}
		check(false);
		return "<invalId>";
	}

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
	Queries->StackRequest(Token, optLock, ITwinHttp::EVerb::Get, {}, std::move(RequestArgList),
		[this, &Token] (TSharedPtr<FJsonObject> const& responseJson)
		{
			auto NewScheds = responseJson->GetArrayField(TEXT("items"));
			S4D_LOG(TEXT("Received %d schedules for iTwin %s"), (int)NewScheds.Num(), *ITwinId);
			if (0 == NewScheds.Num())
			{
				if (EITwinSchedulesGeneration::Unknown != SchedulesGeneration)
				{
					SchedulesInternals().SetScheduleTimeRangeIsKnown();
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
					// caching uses a single folder: I probably need to either replace Schedules by a single
					// optional<Schedule>, OR have an array of cache folders and pass the schedule index (aka
					// cache folder index) to the Queries for each request - TODO_GCO.
					ensure(Schedules.empty());
					if (EITwinSchedulesGeneration::Unknown == SchedulesGeneration)
					{
						// was tried first (see important comment on GetSchedulesAPIBaseUrl!)
						SchedulesGeneration = EITwinSchedulesGeneration::Legacy;
					}
					FString Id;
					if (!ensure(SchedObj->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty()))
						continue;
					Schedules.emplace_back(
						FITwinSchedule{ Id, SchedObj->GetStringField(TEXT("name")), SchedulesGeneration });
					Schedules.back().Reserve(200);
					// Set up the cache folder for the next requests
					if (!Owner->Owner->bDisableCaching
						// superceded by the special simulation mode
						&& Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
					{
						FString const CacheFolder = QueriesCache::GetCacheFolder(
							QueriesCache::ESubtype::Schedules, GetServerConnection()->Environment,
							ITwinId, IModelId, ChangesetId, Id);
						if (ensure(!CacheFolder.IsEmpty()))
						{
							Queries->InitializeCache(CacheFolder, GetServerConnection()->Environment,
													 Schedules.back().Name);
						}
					}
					S4D_LOG(TEXT("Added schedule Id %s named '%s' to iModel %s"), *Id,
							*Schedules.back().Name, *TargetedIModelId);
				}
			}
			FString NextPageToken;
			if (responseJson->TryGetStringField(TEXT("nextPageToken"), NextPageToken))
			{
				RequestSchedules(Token, NextPageToken, &Lock);
			}
			else
			{
				Queries->StatsResetActiveTime();
				if (Schedules.empty() && EITwinSchedulesGeneration::Unknown != SchedulesGeneration)
				{
					SchedulesInternals().SetScheduleTimeRangeIsKnown();
				}
			}
			if (SchedStartIdx != Schedules.size())
			{
				if (EITwinSchedulesGeneration::Legacy != SchedulesGeneration)
					RequestAnimatedEntityUserFieldId(Token, SchedStartIdx, Schedules.size(), Lock);
				if (Owner->Owner->bPrefetchAllTasksAndAppearanceProfiles)
				{
					RequestAllTasks(Token, SchedStartIdx, Schedules.size(), Lock);
					RequestAllAppearanceProfiles(Token, SchedStartIdx, Schedules.size(), Lock);
				}
			}
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
		Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedules[SchedIdx].Id, TEXT("userFields") },
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
	ensure(EITwinSchedulesGeneration::Unknown != SchedulesGeneration);
	if (EITwinSchedulesGeneration::NextGen == SchedulesGeneration)
		// Note: some empty NextGen schedules do not even have the required user field, let's not assert on
		// that - it's actly the case for schedule 75c8ecfb-fa5d-4669-b68d-33b1bd29a69e in our only NextGen
		// iTwin so far (2c7efcad-19b6-4ec6-959f-f36d49699071)
		return /*ensure*/(!Schedules[SchedIdx].AnimatedEntityUserFieldId.IsEmpty());
	else
		return EITwinSchedulesGeneration::Legacy == SchedulesGeneration;
}

void FITwinSchedulesImport::FImpl::RequestAllTasks(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedStartIdx, size_t const SchedEndIdx, FLock& Lock,
	std::optional<FString> const PageToken /*= {}*/)
{
	// Cannot have a page token common to several Schedules tasks queries...
	check(!PageToken || SchedEndIdx == (SchedStartIdx + 1));
	FUrlArgList RequestArgList = {
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	if (PageToken)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	for (auto SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
	{
		Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedules[SchedIdx].Id, TEXT("tasks") },
							  FUrlArgList(RequestArgList),
			[this, SchedIdx, &Token](TSharedPtr<FJsonObject> const& responseJson)
			{
				auto const& Items = responseJson->GetArrayField("items");
				if (Items.IsEmpty())
				{
					S4D_WARN(TEXT("Did not receive any task for schedule '%s'!"), *Schedules[SchedIdx].Name);
				}
				else
				{
					FLock Lock(Mutex);
					bool const bMoreToCome = responseJson->HasTypedField<EJson::String>("nextPageToken");
					auto&& Sched = Schedules[SchedIdx];
					S4D_LOG(TEXT("Received %d tasks (total %d%s) for schedule '%s'"), Items.Num(),
						(int)Sched.Tasks.size() + Items.Num(),
						bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
						*Schedules[SchedIdx].Name);
					if (bMoreToCome)
					{
						RequestAllTasks(Token, SchedIdx, SchedIdx + 1, Lock,
										responseJson->GetStringField("nextPageToken"));
					}
					Sched.Tasks.reserve(Sched.Tasks.size() + Items.Num());
					auto& MainTimeline = SchedulesInternals().Timeline();
					for (auto&& Item : Items)
					{
						const auto& TaskObj = Item->AsObject();
						FString TaskId = TaskObj->GetStringField(TEXT("id"));
						auto const KnownTask = Sched.KnownTasks.try_emplace(TaskId, Sched.Tasks.size());
						if (KnownTask.second) // was inserted
						{
							Sched.Tasks.emplace_back();
						}
						Sched.Tasks[KnownTask.first->second].Id = std::move(TaskId);
						ParseTaskDetails(Token, TaskObj, SchedIdx, KnownTask.first->second, &Lock);
						MainTimeline.IncludeTimeRange(Sched.Tasks[KnownTask.first->second].TimeRange);
					}
					if (!bMoreToCome)
					{
						SchedulesInternals().SetScheduleTimeRangeIsKnown();
						if (SchedulesInternals().PrefetchAllElementAnimationBindings())
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
	FUrlArgList RequestArgList = {
		{ TEXT("pageSize"), FString::Printf(TEXT("%d"), RequestPagination) } };
	if (PageToken)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	for (auto SchedIdx = SchedStartIdx; SchedIdx < SchedEndIdx; ++SchedIdx)
	{
		Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get, { Schedules[SchedIdx].Id, TEXT("appearanceProfiles") },
							  FUrlArgList(RequestArgList),
			[this, SchedIdx, &Token](TSharedPtr<FJsonObject> const& responseJson)
			{
				auto const& Items = responseJson->GetArrayField("items");
				if (Items.IsEmpty())
				{
					S4D_WARN(TEXT("Did not receive any appearance profile for schedule '%s'!"),
							 *Schedules[SchedIdx].Name);
				}
				else
				{
					FLock Lock(Mutex);
					bool const bMoreToCome = responseJson->HasTypedField<EJson::String>("nextPageToken");
					auto&& Sched = Schedules[SchedIdx];
					S4D_LOG(TEXT("Received %d appearance profiles (total %d%s) for schedule '%s'"),
						Items.Num(), (int)Sched.AppearanceProfiles.size() + Items.Num(),
						bMoreToCome ? TEXT(", more to come") : TEXT(", final reply"),
						*Schedules[SchedIdx].Name);
					if (bMoreToCome)
					{
						RequestAllAppearanceProfiles(Token, SchedIdx, SchedIdx + 1, Lock,
													 responseJson->GetStringField("nextPageToken"));
					}
					Sched.AppearanceProfiles.reserve(Sched.AppearanceProfiles.size() + Items.Num());
					for (auto&& Item : Items)
					{
						const auto& AppearanceObj = Item->AsObject();
						FString const AppearanceId = AppearanceObj->GetStringField(TEXT("id"));
						auto const KnownAppearance = Sched.KnownAppearanceProfiles.try_emplace(
							AppearanceId,  Sched.AppearanceProfiles.size());
						if (KnownAppearance.second) // was inserted
						{
							Sched.AppearanceProfiles.emplace_back();
						}
						ParseAppearanceProfileDetails(Token, AppearanceObj, SchedIdx, AppearanceId,
													  KnownAppearance.first->second, &Lock);
					}
				}
			});
	}
}

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
	std::set<ITwinElementID>::const_iterator const ElementsEnd/* = {}*/, int64_t* InOutElemCount/*=nullptr*/,
	std::optional<size_t> const BeginTaskIdx/*= {}*/, std::optional<size_t> const EndTaskIdx/*= {}*/,
	std::optional<FString> const PageToken/*= {}*/, std::optional<FString> JsonPostString/*= {}*/)
{
	bool bHasTimeRange = false;
	auto ElementsIt = ElementsBegin;
	// See comment below about AnimBindingsFullyKnownForElem "optim": it could be fixed in the non-prefetched
	// case by switching from 'None' to 'InitialVersion' only after the last page of a given query is fully
	// processed, which probably means keeping the ElementsBegin/End range alive for the whole duration, or
	// using an intermediate 'BeingQueried' state between 'None' and 'InitialVersion'.
	ensure(SchedulesInternals().PrefetchAllElementAnimationBindings());
	if (JsonPostString)
	{
		// Parameters were not forwarded (they shouldn't be: they were deallocated by now)
		// so rely on post string content
		bHasTimeRange = JsonPostString->Contains("startTime") || JsonPostString->Contains("endTime");
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
						AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(ITwin::ToString(*ElementsIt)));
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
						AnimatedEntityIDs.Add(MakeShared<FJsonValueString>(ITwin::ToString(*ElementsIt)));
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
		if (BeginTaskIdx
			&& ensure(EndTaskIdx && BeginTaskIdx < Sched.Tasks.size() && EndTaskIdx <= Sched.Tasks.size()
					  && (*BeginTaskIdx) != (*EndTaskIdx)
					  && ((*EndTaskIdx) - (*BeginTaskIdx)) <= Owner->Owner->ScheduleQueriesMaxTaskIDsFilterSize))
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
	FUrlArgList RequestArgList = { { TEXT("pageSize"),
		FString::Printf(TEXT("%llu"), Owner->Owner->ScheduleQueriesBindingsPagination) } };
	if (PageToken)
		RequestArgList.push_back({ TEXT("pageToken"), *PageToken });
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Post,
		{ Schedules[SchedIdx].Id, TEXT("animationBindings/query") }, std::move(RequestArgList),
		[this, SchedIdx, TimeRange, JsonPostString, bHasTimeRange, &Token]
		(TSharedPtr<FJsonObject> const& Reply)
		{
			auto const& Items = Reply->GetArrayField(TEXT("items"));
			if (Items.IsEmpty())
				return;
			FLock Lock(Mutex);
			FString NextPageToken;
			bool const bHasNextPage = Reply->TryGetStringField(TEXT("nextPageToken"), NextPageToken);
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

				FString AnimElemIdHexa;
				JSON_GETSTR_OR(BindingObj, "animatedEntityId", AnimElemIdHexa, continue)
				ITwinElementID const ElementID = ITwin::ParseElementID(AnimElemIdHexa);
				// TODO_GCO: add some error handling/flagging about everywhere...
				if (ElementID == ITwin::NOT_ELEMENT)
				{
					continue;
				}
				// Optim based on AnimBindingsFullyKnownForElem was buggy: it skipped all but the first binding
				// for any given Element, because setting to VersionToken::InitialVersion too early
				// => the optim was useful only before pre-fetching all bindings

				// If there is a time range, only read from AnimBindingsFullyKnownForElem. If not, we'll want
				// to insert it: if there was no filtering by Elements (ElementsIt == ElementsEnd),
				// nothing was inserted yet (see above the call to StackRequest, when creating JsonPostString)
				// so we need to do it now since we will be parsing the reply to fill the binding details.
				// Note: not to be confused with FAnimationBinding::NotifiedVersion, which only switches to
				// InitialVersion later, when notifying the observer (timeline builder), as the name implies
				//auto Known = bHasTimeRange
				//	? Sched.AnimBindingsFullyKnownForElem.find(ElementID)
				//	: Sched.AnimBindingsFullyKnownForElem.try_emplace(ElementID, VersionToken::None)
				//		.first;
				//if (Sched.AnimBindingsFullyKnownForElem.end() != Known)
				//{
				//	if (Known->second == VersionToken::InitialVersion)
				//	{
				//		continue; // already fully known, can skip
				//	}
				//	Known->second = VersionToken::InitialVersion;
				//}
				//else: no-op, query was only on a specific time range

				// => so let's just keep the map for counting Elements bound, because it could be useful as a
				// measure of progress tracking until we can get some statistics from the 4D api directly
				Sched.AnimBindingsFullyKnownForElem.try_emplace(ElementID, VersionToken::None);

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

			} // for NewBindings

			S4D_LOG(TEXT("Received %d Bindings, total Elements bound: %llu"), Items.Num(),
				Sched.AnimBindingsFullyKnownForElem.size()
				// Map value no longer set to InitialVersion, see comments about AnimBindingsFullyKnownForElem
				/*std::count_if(Sched.AnimBindingsFullyKnownForElem.begin(), 
								Sched.AnimBindingsFullyKnownForElem.end(),
					[](auto&& Known) { return VersionToken::InitialVersion == Known.second; })*/);

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

void FITwinSchedulesImport::FImpl::ParseTaskDetails(ReusableJsonQueries::FStackingToken const& Token,
	TSharedPtr<FJsonObject> const& JsonObj, size_t const SchedIdx,
	size_t const TaskInVec, FLock* MaybeLock /*= nullptr*/)
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
	auto& Task = Sched.Tasks[TaskInVec];
	Task.Name = std::move(Name);
	if (ensure(bCouldParseDates))
	{
		Task.TimeRange.first = ITwin::Time::FromDateTime(PlannedStart);
		Task.TimeRange.second = ITwin::Time::FromDateTime(PlannedFinish);
		S4D_VERBOSE(TEXT("Task %s named '%s' for schedule Id %s spans %s to %s"),
					*Task.Id, *Task.Name, *Sched.Id, *PlannedStartStr, *PlannedFinishStr);
		CompletedProperty(Sched, Task.Bindings, Lock, TEXT("TaskDetails"));
	}
	else
	{
		Task.TimeRange = ITwin::Time::Undefined();
		S4D_ERROR(TEXT("Task %s named '%s' for schedule Id %s has invalid date(s)!"),
				  *Task.Id, *Task.Name, *Sched.Id);
	}
}

void FITwinSchedulesImport::FImpl::RequestTask(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto& Sched = Schedules[SchedIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, TEXT("tasks"), Schedules[SchedIdx].AnimationBindings[AnimIdx].TaskId },
		{},
		std::bind(&FITwinSchedulesImport::FImpl::ParseTaskDetails, this, std::cref(Token),
				  std::placeholders::_1, SchedIdx, Sched.AnimationBindings[AnimIdx].TaskInVec, nullptr));
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

void FITwinSchedulesImport::FImpl::ParseAppearanceProfileDetails(
	ReusableJsonQueries::FStackingToken const& Token, TSharedPtr<FJsonObject> const& JsonObj,
	size_t const SchedIdx, FString const AppearanceProfileId, size_t const AppearanceProfileInVec,
	FLock* MaybeLock /*= nullptr*/)
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

	std::optional<FLock> OptLockDontUse;
	if (!MaybeLock) OptLockDontUse.emplace(Mutex);
	FLock& Lock = MaybeLock ? (*MaybeLock) : (*OptLockDontUse);
	auto& Sched = Schedules[SchedIdx];
	auto& AppearanceProfile = Sched.AppearanceProfiles[AppearanceProfileInVec];
	if ((bNeedStartAppearance && !ParseSimpleAppearance(Parsed.StartAppearance, false, *StartObj))
		|| !ParseActiveAppearance(Parsed.ActiveAppearance, *ActiveObj)
		|| (bNeedFinishAppearance && !ParseSimpleAppearance(Parsed.FinishAppearance, false, *EndObj)))
	{
		S4D_ERROR(TEXT("Error reading appearance profile %s"), *AppearanceProfileId);
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
	if (SchedulesComponent().bDebugWithRandomProfiles)
	{
		CreateRandomAppearanceProfile(SchedIdx, AnimIdx, Lock);
		return;
	}
	auto&& Sched = Schedules[SchedIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
		{ Sched.Id, TEXT("appearanceProfiles"), Sched.AnimationBindings[AnimIdx].AppearanceProfileId },
		{},
		std::bind(&FITwinSchedulesImport::FImpl::ParseAppearanceProfileDetails, this, std::cref(Token),
				  std::placeholders::_1, SchedIdx, Sched.AnimationBindings[AnimIdx].AppearanceProfileId,
				  Sched.AnimationBindings[AnimIdx].AppearanceProfileInVec, nullptr));
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

void FITwinSchedulesImport::FImpl::RequestTransfoAssignment(ReusableJsonQueries::FStackingToken const& Token,
	size_t const SchedIdx, size_t const AnimIdx, FLock& Lock)
{
	auto&& Sched = Schedules[SchedIdx];
	auto&& AnimationBinding = Sched.AnimationBindings[AnimIdx];
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
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
				if (!Parse3DPathAlignment(Alignment, PathAssignment->TransformAnchor))
				{
					S4D_ERROR(TEXT("Parsing error for 3D path 'alignment', with value: %s"), *Alignment);
					return;
				}
				if (std::holds_alternative<FVector>(PathAssignment->TransformAnchor))
				{
					TSharedPtr<FJsonObject> const* CenterObj;
					JSON_GETOBJ_OR(JsonObj, "center", CenterObj, return);
					if (!ParseVector(*CenterObj, std::get<1>(PathAssignment->TransformAnchor)))
					{
						S4D_ERROR(TEXT("Parsing error for 3D path custom alignment, from: '%s'"),
								  *ToString(*CenterObj));
						return;
					}
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
					// Transfer the responsibility of checking and notifying the completed bindings (since
					// there is no other sub-property the TransfoAssignment depends on)
					Sched.Animation3DPaths[TransfoAsPath.Animation3DPathInVec].Bindings =
						std::move(TransformAssignment.Bindings);
					Request3DPath(Token, SchedIdx, TransfoAssignmentIndex, {}, Lock);
				}
				else if (TransformListIncomplete.second == false)
					CompletedProperty(Sched, TransformAssignment.Bindings, Lock, TEXT("Path3dAssign"));
				else
				{
					// incomplete but already queried: wait for completion, but also transfer responsibility
					// of checking and notifying the completed bindings (same reason as above)
					std::copy(TransformAssignment.Bindings.cbegin(), TransformAssignment.Bindings.cend(),
						std::back_inserter(
							Sched.Animation3DPaths[TransfoAsPath.Animation3DPathInVec].Bindings));
					TransformAssignment.Bindings.clear();
				}
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
	Queries->StackRequest(Token, &Lock, ITwinHttp::EVerb::Get,
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
				double AngleDegrees;
				JSON_GETNUMBER_OR(*RotObj, "angle", AngleDegrees, continue)
				if (AngleDegrees == 0. || !ParseVector(*RotObj, RotAxis)) continue;
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
			{
				// A bit overkill but probably safer to sort the whole vector even though the only requirement
				// is that Add3DPathTransformToTimeline needs the first frame of the list (usually where t=0)
				std::sort(Path3D.Keyframes.begin(), Path3D.Keyframes.end(),
					[](FTransformKey const& J, FTransformKey const& K)
						{ return J.RelativeTime < K.RelativeTime; });
				CompletedProperty(Sched, Path3D.Bindings, Lock, TEXT("Path3d"));
			}
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

void FITwinSchedulesImport::FImpl::ResetConnection(FString const& ITwinAkaProjectAkaContextId,
												   FString const& IModelId, FString const& InChangesetId)
{
	{	FLock Lock(Mutex);
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
			if (SchedulesInternals().PrefetchAllElementAnimationBindings())
				Owner->Owner->OnScheduleQueryingStatusChanged.Broadcast(true);
		}
		auto const GetBearerToken = [SchedComp=Owner->Owner]() -> FString
			{
				AITwinIModel const& IModel = *Cast<AITwinIModel const>(SchedComp->GetOwner());
				if (ensure(IModel.ServerConnection))
					return IModel.ServerConnection->GetAccessToken();
				else
					return TEXT("_TokenError_");
			};
		Queries = MakePimpl<FReusableJsonQueries>(
			*Owner->Owner,
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
			std::bind(&AITwinServerConnection::CheckRequest, std::placeholders::_1, std::placeholders::_2,
					  std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
			Mutex,
			(Owner->Owner->DebugRecordSessionQueries.IsEmpty()
					|| !Owner->Owner->DebugSimulateSessionQueries.IsEmpty())
				? nullptr : (*Owner->Owner->DebugRecordSessionQueries),
			SchedApiSession, 
			Owner->Owner->DebugSimulateSessionQueries.IsEmpty()
				? nullptr : (*Owner->Owner->DebugSimulateSessionQueries),
			SchedulesInternals().PrefetchAllElementAnimationBindings()
				? (&Owner->Owner->OnScheduleQueryingStatusChanged) : nullptr,
			GetBearerToken);
	} // end Lock

	// bHasFullSchedule = false; No, ResetConnection doesn't scratch the structures...
	Queries->NewBatch(
		[this](ReusableJsonQueries::FStackingToken const& Token) { RequestSchedules(Token); });
	// Wait for the completion of the initial request or batch, assuming Legacy schedules: if none received,
	// switch to NextGen and try again (see important comment on this assumption in GetSchedulesAPIBaseUrl!)
	// TODO_GCO: If we ever need both, we could easily have two SchedulesApi, one for each server,
	// there's no hurry and we're supposed to query through a proxy anyway (see GetSchedulesAPIBaseUrl)
	Queries->NewBatch([this](ReusableJsonQueries::FStackingToken const& Token)
		{
			FLock Lock(Mutex);
			if (Schedules.empty())
			{
				// TODO_GCO: incomplete error handling means as long as NextGen call errors out,
				// SetScheduleTimeRangeIsKnown never got called => disable NextGen entirely for the time being
				//S4D_WARN(TEXT("No Legacy schedule found, trying NextGen..."));
				//SchedulesGeneration = EITwinSchedulesGeneration::NextGen;
				//Queries->ChangeRemoteUrl(GetSchedulesAPIBaseUrl());
				//RequestSchedules(Token);
				SchedulesInternals().SetScheduleTimeRangeIsKnown();
			}
			else
			{
				ensure(EITwinSchedulesGeneration::Legacy == SchedulesGeneration);
				if (SchedulesInternals().PrefetchAllElementAnimationBindings())
				{
					bHasFullSchedule = true;
					Queries->ClearCacheFromMemory();
					Owner->Owner->LogStatisticsUponQueryLoopStatusChange(false);
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
		S4D_LOG(TEXT("Still %d pending batches, and %d requests in current batch..."), QueueSize.first, QueueSize.second);
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
				if (SupportsAnimationBindings(SchedIdx, Lock))
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
FITwinSchedulesImport::FITwinSchedulesImport(UITwinSynchro4DSchedules& InOwner,
		std::recursive_mutex& Mutex, std::vector<FITwinSchedule>& Schedules)
	: Owner(&InOwner)
	, Impl(MakePimpl<FImpl>(*this, Mutex, Schedules))
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

bool FITwinSchedulesImport::HasFullSchedule() const
{
	return Impl->bHasFullSchedule;
}

void FITwinSchedulesImport::ResetConnection(FString const& ITwinAkaProjectAkaCtextId, FString const& IModelId,
											FString const& InChangesetId)
{
	Impl->ResetConnection(ITwinAkaProjectAkaCtextId, IModelId, InChangesetId);
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
	if (Impl->SchedulesInternals().PrefetchAllElementAnimationBindings() && !HasFullSchedule())
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
		"\t%llu transfo. assignments (%llu static, %llu along %llu 3D paths).\n" \
		"\t%llu Elements are bound to a task."),
		(EITwinSchedulesGeneration::Unknown == Generation) ? TEXT("<Gen?>")
			: ((EITwinSchedulesGeneration::Legacy == Generation) ? TEXT("Legacy") : TEXT("NextGen")),
		*Id, *Name, AnimationBindings.size(), Tasks.size(), Groups.size(), AppearanceProfiles.size(),
		TransfoAssignments.size(),
		std::count_if(TransfoAssignments.begin(), TransfoAssignments.end(),
			[](auto&& Known) { return Known.Transformation.index() == 0; }),
		std::count_if(TransfoAssignments.begin(), TransfoAssignments.end(),
			[](auto&& Known) { return Known.Transformation.index() == 1; }),
		Animation3DPaths.size(),
		AnimBindingsFullyKnownForElem.size()
		// Map value no longer set to InitialVersion, see comments about AnimBindingsFullyKnownForElem
		//std::count_if(AnimBindingsFullyKnownForElem.begin(), AnimBindingsFullyKnownForElem.end(),
		//	[](auto&& Known) { return VersionToken::InitialVersion == Known.second; })
	);
}
