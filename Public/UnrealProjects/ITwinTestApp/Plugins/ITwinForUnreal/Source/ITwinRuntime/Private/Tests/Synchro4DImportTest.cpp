/*--------------------------------------------------------------------------------------+
|
|     $Source: Synchro4DImportTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <ITwinServerConnection.h>
#include <ITwinUtilityLibrary.h>
#include <Tests/GenericHelpers.h>
#include <Timeline/SchedulesImport.h>
#include <Timeline/Timeline.h>

#include <Editor/EditorEngine.h>
#include <HAL/PlatformFileManager.h>
#include <Interfaces/IPluginManager.h>
#include <JsonObjectConverter.h>
#include <Misc/FileHelper.h>

#include <atomic>
#include <mutex>
#include <optional>

extern UNREALED_API class UEditorEngine* GEditor;

/// Written before realizing Describes were run sequentially, not as individual tests.
/// Could still be useful if changing the test framework and tests are run in parallel, but in that case
/// the use of global variables in the ITwin_TestOverrides namespace should be secured or modified
class FSynchro4DImportTestHelper
{
public:
	std::mutex s_Mutex;
	UWorld* EditorWorld = nullptr;
	std::recursive_mutex ScheduleMutex;
	std::vector<FITwinSchedule> Schedules;
	std::optional<FITwinScheduleTimelineBuilder> TimelineBuilder;
	/// Only pointed to, not copied, by TimelineBuilder, so persist it here:
	std::optional<FITwinCoordConversions> CoordConv;
	std::optional<bool> optUseAPIM;
	std::optional<int> optRequestPagination;
	std::optional<int> optBindingsRequestPagination;
	std::optional<int64_t> optMaxElementIDsFilterSize;

	std::unique_ptr<FITwinSchedulesImport> SchedulesApi;

	FSynchro4DImportTestHelper()
	{
		EditorWorld = GEditor->GetEditorWorldContext().World();
	}

	FString GetBaseTestFolder() const
	{
		return IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"))->GetBaseDir()
			+ TEXT("/Resources/Synchro4DTests/");
	}
	
	bool EnsureFullSchedule()
	{
		ensure(optUseAPIM && optRequestPagination && optBindingsRequestPagination
			&& optMaxElementIDsFilterSize);
		if (SchedulesApi)
		{
			SchedulesApi->HandlePendingQueries();
		}
		else
		{
			FString const CoordConvPath = GetBaseTestFolder() + TEXT("4D-testing.CoordConv.json");
			FString CoordConvStr;
			if (!ensure(FFileHelper::LoadFileToString(CoordConvStr, *CoordConvPath)))
				throw std::runtime_error("Critical error: could not read 4D-testing.CoordConv.json");
			CoordConv.emplace();
			if (!ensure(FJsonObjectConverter::JsonObjectStringToUStruct(CoordConvStr, &(*CoordConv))))
				throw std::runtime_error("Critical error: could not parse 4D-testing.CoordConv.json");
			FString const TestCacheFolder = GetBaseTestFolder()
				+ FString::Printf(TEXT("4D-testing-%s-%d-%d"), ((*optUseAPIM) ? TEXT("APIM") : TEXT("ES")),
					*optRequestPagination, *optBindingsRequestPagination)
				+ TEXT(".cache");
			if (!ensure(IFileManager::Get().DirectoryExists(*TestCacheFolder)))
				throw std::runtime_error("Critical error: missing or invalid cache folder");
			TimelineBuilder.emplace(FITwinScheduleTimelineBuilder::CreateForUnitTesting(*CoordConv));
			std::lock_guard<std::mutex> Lock(s_Mutex); // overrides are globals
			// Both are mandatory, even those not used, because of the way we instantiate SchedulesApi without
			// iModel not Schedules Component
			std::swap(ITwin_TestOverrides::RequestPagination, *optRequestPagination);
			std::swap(ITwin_TestOverrides::BindingsRequestPagination, *optBindingsRequestPagination);
			std::swap(ITwin_TestOverrides::MaxElementIDsFilterSize, *optMaxElementIDsFilterSize);
			// Note: neither make_unique (nor emplace) can obviously call the private ctor:
			Schedules.emplace_back(FITwinSchedule{
				// Note: schedule Id passed below is equal to project Id, as is often the case to this day
				TEXT("3497df55-60e9-44fd-91ec-3c86473884f5"),
				// Could be anything, cache.txt overwrite is skipped when unit testing
				TEXT("Exhaustive(~) test proj for 4D growth+transfos - FEET")
			});
			SchedulesApi.reset(new FITwinSchedulesImport(
				(*optUseAPIM) ? TEXT("https://qa-api.bentley.com/schedules")
							  : TEXT("https://qa-es-api.bentley.com/4dschedule/v1/schedules"),
				TimelineBuilder->Timeline(), TStrongObjectPtr<UObject>(EditorWorld), ScheduleMutex, 
				Schedules));
			std::swap(ITwin_TestOverrides::RequestPagination, *optRequestPagination);
			std::swap(ITwin_TestOverrides::BindingsRequestPagination, *optBindingsRequestPagination);
			std::swap(ITwin_TestOverrides::MaxElementIDsFilterSize, *optMaxElementIDsFilterSize);
			SchedulesApi->SetSchedulesImportConnectors(
				std::bind(&FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline, &(*TimelineBuilder),
						  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
				std::bind(&FITwinScheduleTimelineBuilder::UpdateAnimationGroupInTimeline, &(*TimelineBuilder),
						  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
				[](FGuid const&, ITwinElementID& OutElem) { OutElem = ITwin::NOT_ELEMENT; return false; });
			SchedulesApi->ResetConnectionForTesting(TEXT("3497df55-60e9-44fd-91ec-3c86473884f5"),
				TEXT("82aeb38a-81cd-4fc6-9244-5d6244cfd21b"), TEXT("657d00da87c8cfe932a403a378ae2099d2ad1c7a"),
				TestCacheFolder);
		}
		return SchedulesApi->HasFinishedPrefetching();
	}
}; // class FSynchro4DImportTestHelper

BEGIN_DEFINE_SPEC(Synchro4DImportSpec, "Bentley.ITwinForUnreal.ITwinRuntime.SchedImport", \
				  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	std::shared_ptr<FSynchro4DImportTestHelper> Helper;
void WaitFullSchedule(const FDoneDelegate& Done,
	std::function<void(std::shared_ptr<FSynchro4DImportTestHelper> Helper)> SetupFnc);
void CheckEntireScheduleMatchesJson();
END_DEFINE_SPEC(Synchro4DImportSpec)

void Synchro4DImportSpec::WaitFullSchedule(const FDoneDelegate& Done,
	std::function<void(std::shared_ptr<FSynchro4DImportTestHelper> Helper)> SetupFnc)
{
	SetupFnc(Helper);
	WaitFor(Done, Helper->EditorWorld, 120, [this]()
		{
			if (!Helper) // happened once, probably released because of test error?
				return true; // stop waiting, test probably already failed
			if (Helper->EnsureFullSchedule())
			{
				if (!TestTrue("Something went wrong querying the full schedule",
					Helper->SchedulesApi && Helper->SchedulesApi->HasFinishedPrefetching()
					&& !Helper->SchedulesApi->HasFetchingErrors() && !Helper->Schedules.empty()))
				{
					throw std::runtime_error("Critical error");
				}
				return true;
			}
			else return false;
		});
}

void Synchro4DImportSpec::CheckEntireScheduleMatchesJson()
{
	FString const TimelineAsJson = Helper->TimelineBuilder->GetTimeline().ToPrettyJsonString();
	if (TimelineAsJson.Len() < 8)
	{
		TestTrue("Full timeline is (probably) empty", false);
		return;
	}
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString RefJsonPath = Helper->GetBaseTestFolder() + TEXT("4D-testing")
		// Reference has slightly different values for all transforms because APIM uses more decimals
		// (hidden from SynchroPro UI anyway because its precision is very low! for example a 12.20 angle
		// in SynchroPro is actually 12.198338508605957 in APIM and 12.1983385 in ES-API, which can lead
		// large-ish keyframe position differences of several centimeters!)
		+ ((*Helper->optUseAPIM) ? TEXT("-APIM") : TEXT("")) + TEXT(".json");
	FString RefJson;
	RefJson.Reserve(TimelineAsJson.Len());
	FFileHelper::LoadFileToString(RefJson, *RefJsonPath);
	TestEqual("Entire schedule should match saved reference", RefJson, TimelineAsJson);
	if (RefJson != TimelineAsJson)
	{
		RefJsonPath = RefJsonPath.LeftChop(4) + TEXT("differs.json");
		FFileHelper::SaveStringToFile(TimelineAsJson, *RefJsonPath, FFileHelper::EEncodingOptions::ForceUTF8);
	}
}

void Synchro4DImportSpec::Define()
{
	BeforeEach([this]()
		{
			if (!Helper)
				Helper = std::make_shared<FSynchro4DImportTestHelper>();
			TestTrue("Need EditorWorld", nullptr != Helper->EditorWorld);
			Helper->optMaxElementIDsFilterSize = 500; // unused
		});
	AfterEach([this]()
		{
			Helper.reset(); // test structures are reused if you re-run a test!
		});

	// Disabled because of "latent command timeout" in the Check pipeline, even though the exact same
	// msbuild command (with several Editors in //) works perfectly on parcluster38511...
	xDescribe("Querying the schedule from ES-API, without pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->optUseAPIM = false;
					Helper->optRequestPagination = 10'000; // small test project => no pagination
					Helper->optBindingsRequestPagination = 10'000; // small test project => no pagination
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	xDescribe("Querying the schedule from ES-API, with pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->optUseAPIM = false;
					Helper->optRequestPagination = 2; // force pagination even on the very small test project
					Helper->optBindingsRequestPagination = 3; // same, for animation bindings only
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	xDescribe("Querying the schedule from APIM, without pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->optUseAPIM = true;
					Helper->optRequestPagination = 10'000; // small test project => no pagination
					Helper->optBindingsRequestPagination = 10'000; // small test project => no pagination
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	xDescribe("Querying the schedule from APIM, with pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->optUseAPIM = true;
					Helper->optRequestPagination = 2; // force pagination even on the very small test project
					Helper->optBindingsRequestPagination = 3; // same, for animation bindings only
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
}

#endif // WITH_TESTS && WITH_EDITOR
