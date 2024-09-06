/*--------------------------------------------------------------------------------------+
|
|     $Source: Synchro4DQueriesTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Compil/SanitizedPlatformHeaders.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/SchedulesConstants.h>
#include <Timeline/SchedulesStructs.h>
#include <Timeline/TimeInSeconds.h>
#include <Containers/Map.h>
#include <EngineUtils.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Engine/GameViewportClient.h>
#include <HAL/PlatformFileManager.h>
#include <HAL/PlatformProcess.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>
#include <TimerManager.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// Copied from ITwinCesiumRuntime/Private/Tests/ITwinCesiumTestHelpers.h
template <typename T>
void WaitForImpl(
    const FDoneDelegate& done,
    UWorld* pWorld,
    T&& condition,
    FTimerHandle& timerHandle) {
  if (condition()) {
    pWorld->GetTimerManager().ClearTimer(timerHandle);
    done.Execute();
  } else if (pWorld->GetTimerManager().GetTimerRemaining(timerHandle) <= 0.0f) {
    // Timeout
    pWorld->GetTimerManager().ClearTimer(timerHandle);
    done.Execute();
  } else {
    pWorld->GetTimerManager().SetTimerForNextTick(
        [done, pWorld, condition, timerHandle]() mutable {
          WaitForImpl<T>(done, pWorld, std::move(condition), timerHandle);
        });
  }
}

// Copied from ITwinCesiumRuntime/Private/Tests/ITwinCesiumTestHelpers.h
/// <summary>
/// Waits for a provided lambda function to become true, ticking through render
/// frames in the meantime. If the timeout elapses before the condition becomes
/// true, an error is logged (which will cause a test failure) and the done
/// delegate is invoked anyway.
/// </summary>
/// <typeparam name="T"></typeparam>
/// <param name="done">The done delegate provided by a LatentIt or
/// LatentBeforeEach. It will be invoked when the condition is true or when the
/// timeout elapses.</param>
/// <param name="pWorld">The world in which to check the condition.</param>
/// <param name="timeoutSeconds">The maximum time to wait for the condition to
/// become true.</param>
/// <param name="condition">A lambda that is invoked each
/// frame. If this function returns false, waiting continues.</param>
template <typename T>
void WaitFor(
    const FDoneDelegate& done,
    UWorld* pWorld,
    float timeoutSeconds,
    T&& condition) {
  FTimerHandle timerHandle;
  pWorld->GetTimerManager().SetTimer(timerHandle, timeoutSeconds, false);
  WaitForImpl<T>(done, pWorld, std::forward<T>(condition), timerHandle);
}

/// Written before realizing Describes were run sequentially, not as individual tests.
/// Could still be useful if changing the test framework and tests are run in parallel, but in that case
/// the use of global variables in the ITwin_TestOverrides namespace should be secured or modified
class FSynchro4DQueriesTestHelper
{
	static std::mutex Mutex;
	static TObjectPtr<UITwinSynchro4DSchedules> FullSchedule;
	static std::atomic_bool bQueried;
	static bool bFullScheduleOK;
	TObjectPtr<AITwinIModel> DummyIModel;
	static TObjectPtr<AITwinIModel> IModel;///< Will need to use existing iModel/iTwin and connection details

	TObjectPtr<UITwinSynchro4DSchedules> TestSchedule;

public:
	void UseDummyIModel()
	{
		check(!DummyIModel);
		DummyIModel = NewObject<AITwinIModel>(GEngine, "DummyIModel");
		DummyIModel->ITwinId = TEXT("DummyITwinId");
		DummyIModel->IModelId = TEXT("DummyIModelId");
		DummyIModel->ChangesetId = TEXT("DummyChangesetId");
		DummyIModel->ResolvedChangesetId = DummyIModel->ChangesetId;
		DummyIModel->ServerConnection = NewObject<AITwinServerConnection>(DummyIModel, "DummyConnection");
		DummyIModel->ServerConnection->Environment = EITwinEnvironment::Prod;
	}

	static UWorld* GetWorldInPIE()
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetWorld())
		{
			FAIL_CHECK(TEXT("Not in PIE"));
			return nullptr;
		}
		return GEngine->GameViewport->GetWorld();
	}

	static void CreateScheduleWithOptions(AITwinIModel& ForIModel,
		TObjectPtr<UITwinSynchro4DSchedules>& Sched, FName const& Name,
		std::optional<int64_t> MaxElementIDsFilterSizeOverride = {},
		std::optional<int> RequestPaginationOverride = {},
		std::lock_guard<std::mutex>* Lock = nullptr)
	{
		std::optional<std::lock_guard<std::mutex>> optLock;
		if (!Lock) optLock.emplace(Mutex);

		// These swaps are only safe thanks to the Lock...
		if (RequestPaginationOverride)
			std::swap(ITwin_TestOverrides::RequestPagination, *RequestPaginationOverride);
		if (MaxElementIDsFilterSizeOverride)
			std::swap(ITwin_TestOverrides::MaxElementIDsFilterSize,
						*MaxElementIDsFilterSizeOverride);
		Sched = NewObject<UITwinSynchro4DSchedules>(&ForIModel, Name);
		if (RequestPaginationOverride)
			std::swap(ITwin_TestOverrides::RequestPagination, *RequestPaginationOverride);
		if (MaxElementIDsFilterSizeOverride) // restore
			std::swap(ITwin_TestOverrides::MaxElementIDsFilterSize,
						*MaxElementIDsFilterSizeOverride);
	}

	static void CreateSchedule(TObjectPtr<UITwinSynchro4DSchedules>& Sched, FName const& Name,
		std::optional<int64_t> MaxElementIDsFilterSizeOverride = {},
		std::optional<int> RequestPaginationOverride = {},
		std::lock_guard<std::mutex>* Lock = nullptr)
	{
		std::optional<std::lock_guard<std::mutex>> optLock;
		if (!Lock) optLock.emplace(Mutex);
		if (!IModel)
		{
			UWorld* World = GetWorldInPIE();
			if (!World) throw std::runtime_error("No running in PIE");
			for (TActorIterator<AITwinCesium3DTileset> Iter(World); Iter; ++Iter)
			{
				if ((IModel = (Cast<AITwinIModel>(Iter->GetOwner()))))
				{
					break;
				}
			}
		}
		if (Sched) { CHECK(false); }
		else CreateScheduleWithOptions(*IModel.Get(), Sched, Name, MaxElementIDsFilterSizeOverride,
									   RequestPaginationOverride, &(*optLock));
	}

	/// Avoid querying the full schedule in each test: this would support unit tests running in parallel
	/// in case we want to split the big test, for example reformulating as an "Automation Spec".
	static bool EnsureFullSchedule()
	{
		if (bQueried) return true;

		std::lock_guard<std::mutex> Lock(Mutex);
		if (FullSchedule)
		{
			GetInternals(*FullSchedule).GetSchedulesApiReadyForUnitTesting().HandlePendingQueries();
		}
		else
		{
			CreateSchedule(FullSchedule, "FullSchedule", 100, 100, &Lock);
			FITwinSynchro4DSchedulesInternals& Internals = GetInternals(*FullSchedule);
			Internals.GetSchedulesApiReadyForUnitTesting().QueryEntireSchedules({}, {},
				[&bQueried=FSynchro4DQueriesTestHelper::bQueried,
				 &bFullScheduleOK=FSynchro4DQueriesTestHelper::bFullScheduleOK] (bool bSuccess)
				{
					bQueried = true;
					bFullScheduleOK = bSuccess;
				});
		}
		return false;
	}

	static bool IsFullScheduleOK() { return bFullScheduleOK; }

	/// FullSchedule must exist and be bQueried
	static UITwinSynchro4DSchedules const& GetFullSchedule()
	{
		return *FullSchedule;
	}

	/// FullSchedule must exist and be bQueried
	static FITwinSynchro4DSchedulesInternals const& GetFullScheduleInternals()
	{
		return GetInternals(*FullSchedule);
	}

	UITwinSynchro4DSchedules& GetTestSchedule(FName const& Name = {},
		std::optional<int64_t> MaxElementIDsFilterSizeOverride = {},
		std::optional<int> RequestPaginationOverride = {})
	{
		if (!TestSchedule)
		{
			check(!Name.IsNone());
			if (DummyIModel)
				CreateScheduleWithOptions(*DummyIModel.Get(), TestSchedule, Name,
										  MaxElementIDsFilterSizeOverride, RequestPaginationOverride);
			else
				CreateSchedule(TestSchedule, Name, MaxElementIDsFilterSizeOverride, 
							   RequestPaginationOverride);
		}
		else
			check(!MaxElementIDsFilterSizeOverride);
		return *TestSchedule;
	}

private:
	/// \param TestDateStart If equal to TestDateEnd, Coverage is not tested
	void CheckExpectations(FITwinSchedule const& FullSched, FITwinSchedule const& TestSched,
		FDateTime const& TestDateStart, FDateTime const& TestDateEnd,
		std::optional<std::unordered_set<ITwinElementID>> const& optOnlyElements,
		std::optional<ITwinElementID> const optAllButElement, uint8_t const CoverageMask)
	{
		// Note: failure will 'interrupt' ie return from this function but not interrupt the whole test!
		if (optOnlyElements && optAllButElement)
		{
			CHECK(false); return;
		}
		// Count Element tasks in the full timeline that are 0: fully before, 1: partly before, 2: fully
		// inside, 3: partly after, 4: fully after and 5: fully includes the queried time range, to enforce
		// that all cases are covered.
		std::array<int, 6> Coverage;
		Coverage.fill(0);
		CHECK(CoverageMask == (CoverageMask & ((1 << Coverage.size()) - 1)));
		bool const bCheckCoverage = (TestDateStart != TestDateEnd) && (CoverageMask != 0);
		double const TestRangeStart = ITwin::Time::FromDateTime(TestDateStart);
		double const TestRangeEnd = ITwin::Time::FromDateTime(TestDateEnd);
		for (auto const& AnimBinding : FullSched.AnimationBindings)
		{
			auto&& AnimTask = FullSched.Tasks[AnimBinding.TaskInVec];
			if (AnimTask.TimeRange == ITwin::Time::Undefined()
				|| AnimTask.TimeRange.first == ITwin::Time::InitForMinMax().first
				|| AnimTask.TimeRange.second == ITwin::Time::InitForMinMax().second
				|| AnimTask.TimeRange.first >= AnimTask.TimeRange.second)
			{
				FAIL_CHECK(TEXT("Element timeline has invalid time range"));
				continue;
			}
			if (optOnlyElements
				&& optOnlyElements->find(std::get<0>(AnimBinding.AnimatedEntities)) == optOnlyElements->end())
			{
				continue; // OK.
			}
			if (optAllButElement && (*optAllButElement) == std::get<0>(AnimBinding.AnimatedEntities))
			{
				continue; // OK.
			}
			bool ShouldBeInFilteredTL = true;
			if (bCheckCoverage)
			{
				double const& Lower = AnimTask.TimeRange.first;
				double const& Upper = AnimTask.TimeRange.second;
				double const Epsilon = KEYFRAME_TIME_EPSILON;
				if (Upper < TestRangeStart)
				{
					ShouldBeInFilteredTL = false;
					++Coverage[0];
				}
				else if (Lower < TestRangeStart
					&& Upper > (TestRangeStart + Epsilon) && Upper < (TestRangeEnd - Epsilon))
				{
					++Coverage[1];
				}
				else if (Lower > (TestRangeStart + Epsilon) && Lower < (TestRangeEnd - Epsilon)
					&& Upper > (TestRangeStart + Epsilon) && Upper < (TestRangeEnd - Epsilon))
				{
					++Coverage[2];
				}
				else if (Lower >= (TestRangeStart + Epsilon) && Lower < (TestRangeEnd - Epsilon)
					&& Upper > TestRangeEnd)
				{
					++Coverage[3];
				}
				else if (Lower > TestRangeEnd)
				{
					++Coverage[4];
					ShouldBeInFilteredTL = false;
				}
				else if (Lower < TestRangeStart && Upper > TestRangeEnd)
				{
					++Coverage[5];
				}
				else
				{
					FAIL_CHECK(TEXT("Too close to a time boundary of the query, adjust it."));
				}
			}
			auto const Found = TestSched.KnownAnimationBindings.find(AnimBinding);
			if (ShouldBeInFilteredTL)
			{
				CHECK(Found != TestSched.KnownAnimationBindings.end());
			}
			else
			{
				CHECK(Found == TestSched.KnownAnimationBindings.end());
			}
		}
		if (bCheckCoverage)
		{
			uint8_t MaskBit = 0x1;
			for (int const CoveredCase : Coverage)
			{
				CHECK_MESSAGE(TEXT("Sparse coverage, adjust time query"),
								   (CoveredCase > 0) || ((CoverageMask & MaskBit) == 0));
				MaskBit <<= 1;
			}
		}
	}

	static bool FillWithRandomElementsToCapacity(FAutomationTestBase& Test, FITwinSchedule const& FromSched,
		size_t const MaxNeeded, std::unordered_set<ITwinElementID>& Elems, size_t const Pruning)
	{
		// Ensure we should at least be close to capacity() after the "random" pick
		if (FromSched.AnimBindingsFullyKnownForElem.size() < 2 * MaxNeeded)
		{
			return true; // try next schedule
		}
		size_t Seed = 4321;
		auto It = FromSched.AnimBindingsFullyKnownForElem.begin();
		auto const ItE = FromSched.AnimBindingsFullyKnownForElem.end();
		while (Elems.size() < MaxNeeded && It != ItE)
		{
			boost::hash_combine(Seed, It->first.getValue());
			Test.TestTrue("All Elements should be 'fully known'", It->second);
			if (Pruning <= 1 || (Seed % Pruning) == 0) Elems.insert(It->first);
			++It;
		}
		return false;
	}

public:
	void CheckExpectations(FDateTime const& TestRangeStart, FDateTime const& TestRangeEnd,
		std::optional<std::unordered_set<ITwinElementID>> const& optOnlyElements = {},
		std::optional<ITwinElementID> const optAllButElement = {},
		uint8_t const CoverageMask = 0x3F)
	{
		GetFullScheduleInternals().VisitSchedules(
			[&](FITwinSchedule const& FullSched)
			{
				GetInternals(GetTestSchedule()).VisitSchedules(
					[&](FITwinSchedule const& TestSched)
					{
						CheckExpectations(FullSched, TestSched, TestRangeStart, TestRangeEnd,
										  optOnlyElements, optAllButElement, CoverageMask);
						return true;
					});
				return true;
			});
	}

	static void FillWithRandomElementsToCapacity(FAutomationTestBase& Test,
		size_t const MaxNeeded, std::unordered_set<ITwinElementID>& Elems, size_t const Pruning)
	{
		GetFullScheduleInternals().VisitSchedules(
			[&Test, &Elems, MaxNeeded, Pruning](FITwinSchedule const& FullSched)
			{ return FillWithRandomElementsToCapacity(Test, FullSched, MaxNeeded, Elems, Pruning); });
	}
}; // class FSynchro4DQueriesTestHelper

/*static*/
std::mutex FSynchro4DQueriesTestHelper::Mutex;
TObjectPtr<UITwinSynchro4DSchedules> FSynchro4DQueriesTestHelper::FullSchedule;
std::atomic_bool FSynchro4DQueriesTestHelper::bQueried{ false };
bool FSynchro4DQueriesTestHelper::bFullScheduleOK = false;
TObjectPtr<AITwinIModel> FSynchro4DQueriesTestHelper::IModel;

constexpr double SECONDS_PER_DAY = 86400;

namespace TestSynchro4DQueries
{
	void MakeDummySchedule(FITwinSynchro4DSchedulesInternals& Internals)
	{
		Internals.MutateSchedules([](std::vector<FITwinSchedule>& Schedules)
			{
				check(Schedules.empty());
				Schedules.resize(1);
				FITwinSchedule& Sched = Schedules.back();
				Sched.Id = TEXT("<SchedId>");
				Sched.Name = TEXT("<SchedName>");
				Sched.AnimatedEntityUserFieldId = TEXT("<SchedAnimatedEntityUserFieldId>");
				FAnimationBinding Binding;
				Binding.AnimatedEntities = ITwinElementID(42);
				Binding.TaskId = TEXT("<TaskId>");
				Binding.TaskInVec = 0;
				Sched.Tasks.push_back(FScheduleTask{ {}, TEXT("<TaskName>"), FTimeRangeInSeconds{0., 12.} });
				Sched.KnownTasks[Binding.TaskId] = 0;
				Binding.AppearanceProfileId = TEXT("<AppearanceProfileId>");
				Binding.AppearanceProfileInVec = 0;
				check(Sched.AppearanceProfiles.empty());
				Sched.AppearanceProfiles.resize(1);
				Sched.KnownAppearanceProfiles[Binding.AppearanceProfileId] = 0;
				Sched.AnimBindingsFullyKnownForElem[std::get<0>(Binding.AnimatedEntities)] = true;
				//Binding.TransfoAssignmentId = TEXT("<TransformListId>");
				//Binding.TransfoAssignmentInVec = 0;
				//Sched.TransfoAssignments.push_back(FTransformAssignment{...});
				Sched.KnownAnimationBindings[Binding] = 0;
				Sched.AnimationBindings.emplace_back(std::move(Binding));
			});
	}
}

/* At the time of writing, the tasks for schedule Id 01456f3b-2cac-455c-bdb6-9f2ee8bb43d0
   in iTwin 2c7efcad-19b6-4ec6-959f-f36d49699071 (QA environment, "DO-4D-NextGen-internal-testing-E",
   iModel 4D-I95-for-LumenRT) have this unique set of time ranges:

	2020-02-26T09:00:00Z to 2020-03-10T17:00:00Z
	2020-03-10T09:00:00Z to 2020-03-23T17:00:00Z
	2020-03-10T09:00:00Z to 2020-03-30T17:00:00Z
	2020-03-31T09:00:00Z to 2020-04-20T17:00:00Z
	2020-04-21T09:00:00Z to 2020-05-04T17:00:00Z  <== see QueryAroundElementTasks
	2020-04-21T09:00:00Z to 2020-05-11T17:00:00Z
	2020-05-12T09:00:00Z to 2020-06-01T17:00:00Z
	2020-06-02T09:00:00Z to 2020-06-22T17:00:00Z
	2020-06-23T09:00:00Z to 2020-07-13T17:00:00Z
	2020-07-14T09:00:00Z to 2020-08-03T17:00:00Z
	2020-08-04T09:00:00Z to 2020-08-24T17:00:00Z
	2020-08-25T09:00:00Z to 2020-09-14T17:00:00Z
	2020-10-06T09:00:00Z to 2020-10-26T17:00:00Z

 There are 354 unique ElementIDs in the full schedule, a sparse collection ranging from 0x20000000146 to
 0x3000000017d.
*/
BEGIN_DEFINE_SPEC(Synchro4DImportSpec, "Bentley.ITwinForUnreal.ITwinRuntime.Schedules", \
				  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	std::shared_ptr<FSynchro4DQueriesTestHelper> Helper;
	void WaitFullSchedule(const FDoneDelegate& Done);
	void WaitTestSchedule(const FDoneDelegate& Done);
	void TestQueryElementsTasks(double const MultiRatio, FName const SchedName,
								std::optional<int64_t> MaxElementIDsFilterSizeOverride);
	void CheckEntireScheduleMatchesJson();
END_DEFINE_SPEC(Synchro4DImportSpec)
void Synchro4DImportSpec::WaitFullSchedule(const FDoneDelegate& Done)
{
	UWorld* World = FSynchro4DQueriesTestHelper::GetWorldInPIE();
	if (!World) throw std::runtime_error("No running in PIE");
	WaitFor(Done, World, 120, [this]()
		{
			if (Helper->EnsureFullSchedule())
			{
				if (!TestTrue("Something went wrong querying the full schedule",
								Helper->IsFullScheduleOK()))
				{
					throw std::runtime_error("Critical error");
				}
				return true;
			}
			else return false;
		});
}
void Synchro4DImportSpec::WaitTestSchedule(const FDoneDelegate& Done)
{
	UWorld* World = FSynchro4DQueriesTestHelper::GetWorldInPIE();
	if (!World) throw std::runtime_error("No running in PIE");
	WaitFor(Done, World, 120, [this]()
		{
			return GetInternals(Helper->GetTestSchedule()).GetSchedulesApiReadyForUnitTesting()
				.HandlePendingQueries() == std::make_pair(0, 0);
		});
}
void Synchro4DImportSpec::TestQueryElementsTasks(double const MultiRatio, FName const SchedName,
	std::optional<int64_t> MaxElementIDsFilterSizeOverride)
{
	auto ElemsSetKeptForCheck = std::make_shared<std::unordered_set<ITwinElementID>>();
	LatentBeforeEach(
		[this, SchedName, ElemsSetKeptForCheck, MultiRatio, MaxElementIDsFilterSizeOverride]
		(const FDoneDelegate& Done)
		{
			size_t const MaxElemsNeeded = static_cast<size_t>(
				(MaxElementIDsFilterSizeOverride ? *MaxElementIDsFilterSizeOverride
												 : ITwin_TestOverrides::MaxElementIDsFilterSize)
					* MultiRatio);
			FSynchro4DQueriesTestHelper::FillWithRandomElementsToCapacity(*this, MaxElemsNeeded,
																		  *ElemsSetKeptForCheck, 2);
			if (ElemsSetKeptForCheck->empty())
			{
				TestTrue("FillWithRandomElementsToCapacity failed", false);
				Done.Execute();
			}
			else
			{
				std::set<ITwinElementID> ElemSetWillBeEmptied;
				for (auto&& Elem : (*ElemsSetKeptForCheck))
					ElemSetWillBeEmptied.insert(Elem);
				GetInternals(Helper->GetTestSchedule(SchedName, MaxElementIDsFilterSizeOverride))
					.GetSchedulesApiReadyForUnitTesting().QueryElementsTasks(ElemSetWillBeEmptied);
				WaitTestSchedule(Done);
			}
		});
	It("should match expectations", [this, ElemsSetKeptForCheck]()
		{
			if (!ElemsSetKeptForCheck->empty()) // otherwise test already failed above
				Helper->CheckExpectations({}, {}, *ElemsSetKeptForCheck);
		});
}
void Synchro4DImportSpec::CheckEntireScheduleMatchesJson()
{
	FITwinSynchro4DSchedulesInternals const& FullSched = Helper->GetFullScheduleInternals();
	FString const TimelineAsJson = FullSched.GetTimeline().ToPrettyJsonString();
	if (TimelineAsJson.Len() < 8)
	{
		TestTrue("Full timeline is (probably) empty", false);
		return;
	}
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
	Path.Append("ITwinForUnreal/Content/ITwin_Tests/4D-I-95-for-LumenRT.json");
	FString RefJson;
	RefJson.Reserve(TimelineAsJson.Len());
	FFileHelper::LoadFileToString(RefJson, *Path);
	TestEqual("Entire schedule should match saved reference", RefJson, TimelineAsJson);
	if (RefJson != TimelineAsJson)
	{
		Path.Append(".differs");
		FFileHelper::SaveStringToFile(TimelineAsJson, *Path);
	}
}
void Synchro4DImportSpec::Define()
{
	BeforeEach([this]()
		{
			if (!Helper)
				Helper = std::make_shared<FSynchro4DQueriesTestHelper>();
		});
	AfterEach([this]()
		{
			Helper.reset(); // test structures are reused if you re-run a test!
		});

	// Also disabled: querying NextGen api errors out because it does not support itwin-platform scope yet
	// and apparently it randomly make other test fails because of output log's intermingling??
	// This test is a bit dummy anyway...
	xDescribe("Reset method", [this]()
		{
			It("should clear existing schedules", [this]()
				{
					Helper->UseDummyIModel();
					auto& TestSched = Helper->GetTestSchedule("DummySchedule");
					FITwinSynchro4DSchedulesInternals& Internals = GetInternals(TestSched);
					TestSynchro4DQueries::MakeDummySchedule(Internals);
					int Count = 0;
					Internals.VisitSchedules([this, &Count](FITwinSchedule const&)
						{ TestEqual("check size is 1", Count, 0); ++Count; return true; });
					// Requires the schedule to have an iModel owner!
					//TestSched.ResetSchedules(); <== now private, see what to do when re-enabling
					Internals.VisitSchedules([this](FITwinSchedule const&)
						{ TestTrue("should be empty", false); return false; });
				});
		});

	/*  All the rest cannot run in the Check pipeline: replace Describe by Describe to enable a test
		Later I may implement a mock server to enable it in the Check pipeline without having to get an
		authorization token nor also depend on remote server's availability and responsiveness. */

	/*  IMPORTANT: Currently the tests can only run in the PIE and AFTER loading the iModel mentioned above
		the BEGIN_DEFINE_SPEC declaration!! */

	// From now on, everything should be latent commands, because all tests need
	// the full schedule as a pre-requisite, and all queries need to be waited on for completion
	xDescribe("Querying the entire schedule", [this]()
		{
			LatentBeforeEach(std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1));

			// Just compare the FullSchedule against the reference file
			It("should match the stored json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));

			Describe("with different pagination setting", [this]()
				{
					LatentBeforeEach([this](const FDoneDelegate& Done)
						{
							auto& TestSched = Helper->GetTestSchedule("PaginatedEntire", {}, 6);
							GetInternals(TestSched)
								.GetSchedulesApiReadyForUnitTesting().QueryEntireSchedules();
							WaitTestSchedule(Done);
						});
					It("should not change the result", [this]() {
						Helper->CheckExpectations(FDateTime::MinValue(), FDateTime::MaxValue()); });
				});
		});

	xDescribe("Querying with time filtering", [this]()
		{
			LatentBeforeEach(std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1));

			auto TestRangeStart = std::make_shared<FDateTime>();
			auto TestRangeEnd = std::make_shared<FDateTime>();
			LatentBeforeEach([this, TestRangeStart, TestRangeEnd](const FDoneDelegate& Done)
				{
					auto const FullTimeRange =
						Helper->GetFullScheduleInternals().GetTimeline().GetTimeRange();
					// schedule should last more than 1 day:
					CHECK((FullTimeRange.first + SECONDS_PER_DAY) < FullTimeRange.second);
					// See comment above TEST_CASE_NAMED: I want to query a time range so that there are tasks
					// with all kinds of time ranges with respect to the query (see Coverage below to enforce
					// that in case of unexpected changes in the test schedule).
					if (!FDateTime::ParseIso8601(TEXT("2020-03-20T00.00.00Z"), *TestRangeStart)
						|| !FDateTime::ParseIso8601(TEXT("2020-06-22T17:00:00Z"), *TestRangeEnd))
					{
						TestTrue("Date parse error", false);
						*TestRangeStart = FDateTime();
						*TestRangeEnd = FDateTime();
						Done.Execute();
					}
					else
					{
						GetInternals(Helper->GetTestSchedule("WithTimeFiltering"))
							.GetSchedulesApiReadyForUnitTesting()
							.QueryEntireSchedules(*TestRangeStart, *TestRangeEnd);
						WaitTestSchedule(Done);
					}
				});
			It("should match expectations", [this, TestRangeStart, TestRangeEnd]()
				{
					if (*TestRangeStart != *TestRangeEnd) // otherwise test already failed above
						Helper->CheckExpectations(*TestRangeStart, *TestRangeEnd);
				});
		});

	xDescribe("Querying for the given Elements", [this]()
		{
			LatentBeforeEach(std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1));

			// take a random number of elements < ITwin_TestOverrides::MaxElementIDsFilterSize, to ensure a
			// single top-level query is launched
			Describe("using a single request",
				std::bind(&Synchro4DImportSpec::TestQueryElementsTasks, this, 0.5, "ElementsTasksSingle",
						  std::nullopt));
			// take a random number of elements > ITwin_TestOverrides::MaxElementIDsFilterSize, to ensure
			// multiple top-level queries are launched
			Describe("using multiple requests",
				std::bind(&Synchro4DImportSpec::TestQueryElementsTasks, this, 2.25, "ElementsTasksMulti",
						  25));
		});

	xDescribe("Querying around an Element", [this]()
		{
			LatentBeforeEach(std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1));

			auto ElementID = std::make_shared<ITwinElementID>(ITwin::ParseElementID(TEXT("0x2000000054f")));
			auto MarginFromStart = std::make_shared<FTimespan>(FTimespan::FromDays(-2));
			auto MarginFromEnd =
				std::make_shared<FTimespan>(FTimespan::FromDays(7) + FTimespan::FromHours(1));
			LatentBeforeEach(
				[this, ElementID, MarginFromStart, MarginFromEnd](const FDoneDelegate& Done)
				{
					GetInternals(Helper->GetTestSchedule("AroundElement"))
						.GetSchedulesApiReadyForUnitTesting()
						.QueryAroundElementTasks(*ElementID, *MarginFromStart, *MarginFromEnd);
					WaitTestSchedule(Done);
				});
			It("should match all expectations",
				[this, ElementID, MarginFromStart, MarginFromEnd]()
				{
					auto& TestSched = Helper->GetTestSchedule();
					FDateRange ElemTimeRange(FDateTime::MaxValue(), FDateTime::MinValue());
					GetInternals(TestSched).ForEachElementTimeline(*ElementID,
						[this, &ElemTimeRange](FITwinElementTimeline const& Timeline)
						{
							auto const& TimeRange = Timeline.GetDateRange();
							if (!TimeRange.HasLowerBound() || !TimeRange.HasUpperBound())
							{
								TestTrue("Invalid Element time range found", false);
								return;
							}
							if (TimeRange.GetLowerBoundValue() < ElemTimeRange.GetLowerBoundValue())
								ElemTimeRange.SetLowerBoundValue(TimeRange.GetLowerBoundValue());
							if (TimeRange.GetUpperBoundValue() > ElemTimeRange.GetUpperBoundValue())
								ElemTimeRange.SetUpperBoundValue(TimeRange.GetUpperBoundValue());
						});
					if (ElemTimeRange == FDateRange(FDateTime::MaxValue(), FDateTime::MinValue()))
					{
						TestTrue("Element timeline not found", false);
						return;
					}
					// Check we have all the tasks involving ElementID that the full schedule has
					Helper->CheckExpectations({}, {}, std::unordered_set<ITwinElementID>{ *ElementID });
					// Check that, besides ElementID, we have only the tasks in the expected range
					Helper->CheckExpectations(
						ElemTimeRange.GetLowerBoundValue() + (*MarginFromStart),
						ElemTimeRange.GetUpperBoundValue() + (*MarginFromEnd), {}, *ElementID, 0x1F);
				});
		});
}

#endif //WITH_TESTS
