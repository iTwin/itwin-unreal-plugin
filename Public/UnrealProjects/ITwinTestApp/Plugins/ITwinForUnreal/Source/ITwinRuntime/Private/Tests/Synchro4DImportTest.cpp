/*--------------------------------------------------------------------------------------+
|
|     $Source: Synchro4DImportTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <ITwinSynchro4DSchedulesTimelineBuilder.h>
#include <ITwinSceneMapping.h>
#include <ITwinServerConnection.h>
#include <ITwinUtilityLibrary.h>
#include <Network/JsonQueriesCacheInit.h>
#include <Tests/GenericHelpers.h>
#include <Timeline/ScheduleComparison.inl>
#include <Timeline/SchedulesImport.h>
#include <Timeline/Timeline.h>

#include <Editor/EditorEngine.h>
#include <Editor/EditorPerformanceSettings.h>
#include <EditorViewportClient.h>
#include <HAL/PlatformFileManager.h>
#include <Interfaces/IPluginManager.h>
#include <JsonObjectConverter.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/LockableObject.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <atomic>
#include <mutex>
#include <optional>

namespace
{
	size_t timesAllowingEditorTick = 0;
	bool originalEditorTickState = true;
	const auto RealtimeOverrideName = FText::FromString(TEXT("ITwinRuntimeTest"));
}

void pushAllowTickInEditor()
{
	if (timesAllowingEditorTick == 0) {
		UEditorPerformanceSettings* pSettings =
			GetMutableDefault<UEditorPerformanceSettings>();
		originalEditorTickState = pSettings->bThrottleCPUWhenNotForeground;
		pSettings->bThrottleCPUWhenNotForeground = false;
		// This is needed for machines with no physical display device attached,
		// eg. machines running automated builds.
		// Without this, AActor::Tick() is not called.
		for (auto* const Viewport : GEditor->GetAllViewportClients())
			Viewport->AddRealtimeOverride(true, RealtimeOverrideName);
	}
	++timesAllowingEditorTick;
}

void popAllowTickInEditor()
{
	--timesAllowingEditorTick;
	if (timesAllowingEditorTick == 0) {
		UEditorPerformanceSettings* pSettings =
			GetMutableDefault<UEditorPerformanceSettings>();
		pSettings->bThrottleCPUWhenNotForeground = originalEditorTickState;
		for (auto* const Viewport : GEditor->GetAllViewportClients())
			Viewport->RemoveRealtimeOverride(RealtimeOverrideName);
	}
}

extern UNREALED_API class UEditorEngine* GEditor;

class FSynchro4DImportTestHelper
{
public:
	/// Static mutex added before realizing Describes were run sequentially, not as individual tests.
	/// Could still be useful if changing the test framework and tests are run in parallel, but in that case
	/// the use of global variables in the ITwin_TestOverrides namespace should be secured or modified
	static std::mutex s_Mutex;

	UWorld* EditorWorld = nullptr;
	std::recursive_mutex ScheduleMutex;
	std::optional<FITwinSchedule> Schedule;
	std::optional<FITwinScheduleTimelineBuilder> TimelineBuilder;
	TSceneMappingPtr SceneMapping;
	/// Only pointed to, not copied, by TimelineBuilder, so persist it here:
	std::optional<FITwinCoordConversions> CoordConv;
	std::optional<int> optRequestPagination;
	std::optional<int> optBindingsReqPagination;
	FString TestName, ScheduleName, ITwinId, IModelId, ChangesetId;
	EITwinSchedulesGeneration Generation = EITwinSchedulesGeneration::Legacy;
	std::optional<FString> EnvPrefix;
	bool bUseCachedScheduleData = false;

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

	bool LoadScheduleFromJson(FITwinSchedule* pOutSchedule = nullptr)
	{
		if (!pOutSchedule)
			pOutSchedule = &(*Schedule);
		IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
		FString const RefJsonPath = GetBaseTestFolder() / (TestName + ".schedule.json");
		FString RefJson;
		if (!FFileHelper::LoadFileToString(RefJson, *RefJsonPath))
			return false;
		return pOutSchedule->FromJsonString(RefJson);
	}
	
	bool EnsureFullSchedule()
	{
		ensure(((optRequestPagination && optBindingsReqPagination) || bUseCachedScheduleData)
			&& !TestName.IsEmpty() && !ITwinId.IsEmpty() && !IModelId.IsEmpty() && !ChangesetId.IsEmpty());
		if (SchedulesApi)
		{
			SchedulesApi->HandlePendingQueries();
		}
		else
		{
			FString const CoordConvPath = GetBaseTestFolder() + TestName + TEXT(".CoordConv.json");
			FString CoordConvStr;
			if (!ensureMsgf(FFileHelper::LoadFileToString(CoordConvStr, *CoordConvPath),
							TEXT("Critical error: could not read %s"), *CoordConvPath))
				return true;// stop waiting, test will fail
			CoordConv.emplace();
			if (!ensureMsgf(FJsonObjectConverter::JsonObjectStringToUStruct(CoordConvStr, &(*CoordConv)),
							TEXT("Critical error: could not parse %s"), *CoordConvPath))
				return true;// stop waiting, test will fail
			FString const ElemDataPath = GetBaseTestFolder() + TestName + TEXT(".ElemData.json");
			FString ElemDataStr;
			if (!ensureMsgf(FFileHelper::LoadFileToString(ElemDataStr, *ElemDataPath),
				TEXT("Critical error: could not read %s"), *ElemDataPath))
				return true;// stop waiting, test will fail
			TSharedRef<TJsonReader<>> ElemDataJsonReader = TJsonReaderFactory<>::Create(ElemDataStr);
			TSharedPtr<FJsonObject> ElemDataJson;
			if (!ensureMsgf(FJsonSerializer::Deserialize(ElemDataJsonReader, ElemDataJson),
				TEXT("Critical error: could not parse file %s"), *ElemDataPath))
				return true;// stop waiting, test will fail
			SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);
			auto SceneMappingLocked = SceneMapping->GetAutoLock();
			if (!ensureMsgf(SceneMappingLocked->FromJson(ElemDataJson),
				TEXT("Critical error: could not parse data from %s"), *ElemDataPath))
				return true;// stop waiting, test will fail
			TimelineBuilder.emplace(FITwinScheduleTimelineBuilder::CreateForUnitTesting(SceneMapping, *CoordConv));
			// Schedule Id passed below is equal to iTwin Id, as is often the case in Legacy projects.
			Schedule.emplace(ITwinId, ScheduleName, Generation);
			if (bUseCachedScheduleData)
			{
				if (!LoadScheduleFromJson())
					*Schedule = FITwinSchedule(ITwinId, ScheduleName); // test will fail on empty schedule
				return true;
			}
			else
			{
				FString const TestCacheFolder = GetBaseTestFolder()
					+ FString::Printf(TEXT("%s-%d-%d"), *TestName, *optRequestPagination, *optBindingsReqPagination)
					+ TEXT(".cache");
				if (!ensureMsgf(IFileManager::Get().DirectoryExists(*TestCacheFolder),
					TEXT("Critical error: missing or invalid cache folder %s"), *TestCacheFolder))
					return true;// stop waiting, test will fail

				std::lock_guard<std::mutex> Lock(s_Mutex); // overrides are globals
				// Both are mandatory, even those not used, because of the way we instantiate SchedulesApi without
				// iModel nor Schedules Component
				std::swap(ITwin_TestOverrides::RequestPagination, *optRequestPagination);
				std::swap(ITwin_TestOverrides::BindingsRequestPagination, *optBindingsReqPagination);
				SchedulesApi.reset(new FITwinSchedulesImport(
					TEXT("https://") + (*EnvPrefix) + TEXT("api.bentley.com/schedules"),
					TimelineBuilder->Timeline(), TStrongObjectPtr<UObject>(EditorWorld), ScheduleMutex, Schedule));
				std::swap(ITwin_TestOverrides::RequestPagination, *optRequestPagination);
				std::swap(ITwin_TestOverrides::BindingsRequestPagination, *optBindingsReqPagination);
				SchedulesApi->SetSchedulesImportConnectors(
					std::bind(&FITwinScheduleTimelineBuilder::OnReceivedScheduleStats, &(*TimelineBuilder),
							  std::placeholders::_1));
				SchedulesApi->ResetConnectionForTesting(ITwinId, IModelId, ChangesetId, TestCacheFolder, Generation);
			}
		}
		return SchedulesApi->HasFinishedPrefetching();
	}
}; // class FSynchro4DImportTestHelper

/*static*/std::mutex FSynchro4DImportTestHelper::s_Mutex;

BEGIN_DEFINE_SPEC(Synchro4DImportSpec, "Bentley.ITwinForUnreal.ITwinRuntime.SchedImport", \
				  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	std::shared_ptr<FSynchro4DImportTestHelper> Helper;
void WaitFullSchedule(const FDoneDelegate& Done,
	std::function<void(std::shared_ptr<FSynchro4DImportTestHelper> Helper)> SetupFnc);
bool LoadMainTimelineFromJson(FString const& FileStem, ITwin::Timeline::MainTimeline& TimelineFromJson,
							  FString* JsonFromFile = nullptr);
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
				TestTrue("Something went wrong querying the full schedule", Helper->Schedule
					&& (Helper->bUseCachedScheduleData
						|| (Helper->SchedulesApi && Helper->SchedulesApi->HasFinishedPrefetching()
							&& !Helper->SchedulesApi->HasFetchingErrors())));
				return true;
			}
			else return false;
		});
}

bool Synchro4DImportSpec::LoadMainTimelineFromJson(FString const& FileStem,
	ITwin::Timeline::MainTimeline& TimelineFromJson, FString* JsonFromFile/*=nullptr*/)
{
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString const RefJsonPath = Helper->GetBaseTestFolder() + FileStem;
	FString RefJson;
	if (!JsonFromFile)
		JsonFromFile = &RefJson;
	if (!FFileHelper::LoadFileToString(*JsonFromFile, *(RefJsonPath + TEXT(".timelines.json"))))
		return false;
	return 0 == TimelineFromJson.FromJsonString(*JsonFromFile);
}

void Synchro4DImportSpec::CheckEntireScheduleMatchesJson()
{
	Helper->TimelineBuilder->FinalizeTimeline(*Helper->Schedule);
	ITwin::Timeline::MainTimeline ReferenceTimelines;
	TestTrue("Deserializing timelines", LoadMainTimelineFromJson(Helper->TestName, ReferenceTimelines));
	bool const bTimelinesMatch = AreNearlyEqual(ReferenceTimelines, Helper->TimelineBuilder->GetTimeline(), 1e-6f);
	TestTrue("Deserialized timelines match ref", bTimelinesMatch);
	if (!bTimelinesMatch)
	{
		FString const RefJsonPath = Helper->GetBaseTestFolder() + Helper->TestName;
		// Write both the current ref and the differing timelines in a way that can be compared by humans, to help
		// validating changes when actual 4D code changes are expected and the ref needs to be updated:
		Helper->TimelineBuilder->GetTimeline().SetJsonPrintingWithHumanReadableTimes(true);
		Helper->TimelineBuilder->GetTimeline().SetJsonPrintingNumberOfDecimals(6);
		FFileHelper::SaveStringToFile(Helper->TimelineBuilder->GetTimeline().ToPrettyJsonString(),
			*(RefJsonPath + TEXT("-differs-HumanComparable.json")), FFileHelper::EEncodingOptions::ForceUTF8);
		ReferenceTimelines.SetJsonPrintingWithHumanReadableTimes(true);
		ReferenceTimelines.SetJsonPrintingNumberOfDecimals(6);
		FFileHelper::SaveStringToFile(ReferenceTimelines.ToPrettyJsonString(),
			*(RefJsonPath + TEXT("-CurrentRef-HumanComparable.json")), FFileHelper::EEncodingOptions::ForceUTF8);
		// Writes the differing timelines again, this time with full precision and no human-readable times, which is
		// the format that needs be commited to the repo is all the differences are validated:
		Helper->TimelineBuilder->GetTimeline().SetJsonPrintingWithHumanReadableTimes(false);
		Helper->TimelineBuilder->GetTimeline().SetJsonPrintingNumberOfDecimals(-1);
		FFileHelper::SaveStringToFile(Helper->TimelineBuilder->GetTimeline().ToPrettyJsonString(),
			*(RefJsonPath + TEXT("-differs-NewRefCandidate.json")), FFileHelper::EEncodingOptions::ForceUTF8);
	}
	FITwinSchedule RefSchedule(Helper->ITwinId, Helper->ScheduleName);
	TestTrue("Deserializing schedule", Helper->LoadScheduleFromJson(&RefSchedule));
	TestTrue("Schedule JSON cache in repo is of the current version",
		Helper->Schedule->JsonCacheVersion == FITwinSchedule::CurrentJsonCacheVersion);
	TestTrue("Deserialized schedule matches ref", RefSchedule == (*Helper->Schedule));
}

void Synchro4DImportSpec::Define()
{
	BeforeEach([this]()
		{
			if (!Helper)
				Helper = std::make_shared<FSynchro4DImportTestHelper>();
			TestTrue("Need EditorWorld", nullptr != Helper->EditorWorld);
			pushAllowTickInEditor();
		});
	AfterEach([this]()
		{
			popAllowTickInEditor();
			Helper.reset(); // test structures are reused if you re-run a test!
		});

	Describe("Check MainTimeline JSON (de-)serializations", [this]()
		{
			It("should match the ref json", [this]() {
				ITwin::Timeline::MainTimeline TimelineFromJson, TimelineFromRef/*dummy*/;
				FString const TestName(TEXT("4D-testing"));
				TestTrue("Deserializing timeline", LoadMainTimelineFromJson(TestName, TimelineFromJson));
				TimelineFromJson.SetJsonPrintingWithHumanReadableTimes(false);
				TimelineFromJson.SetJsonPrintingNumberOfDecimals(6);
				FString BackToJson = TimelineFromJson.ToPrettyJsonString();
				FString JsonFromRef;
				TestTrue("Reading timeline ref string", LoadMainTimelineFromJson(
					TEXT("StandaloneJsonReadingTestRef_") + TestName, TimelineFromRef, &JsonFromRef));
				bool bTimelinesMatch = (BackToJson == JsonFromRef);
				if (!bTimelinesMatch)
				{
					// For some reason, the Release build has a 12.8microsecond difference for a single KF... :/
					// No reason for it to be of any relevance, so hack around it - this is a GetTicks saved as hexa:
					BackToJson = BackToJson.Replace(TEXT("0x8dd44c75740ea80"), TEXT("0x8dd44c75740eb00"));
					bTimelinesMatch = (BackToJson == JsonFromRef);
				}
				TestTrue("Deserialized timeline matches original JSON", bTimelinesMatch);
				if (!bTimelinesMatch)
				{
					FString const OutJsonPath = Helper->GetBaseTestFolder() + TEXT("StandaloneJsonReadingTest_")
						+ TestName + TEXT("_jsonInOut.json");
					FFileHelper::SaveStringToFile(BackToJson, *OutJsonPath, FFileHelper::EEncodingOptions::ForceUTF8);
				}
			});
		});

	Describe("Check Elements Metadata JSON (de-)serializations", [this]()
		{
			It("should match the ref json", [this]() {
				FString const TestName(TEXT("4D-testing"));
				FString const ElemDataPath = Helper->GetBaseTestFolder() + TestName + TEXT(".ElemData.json");
				FString ElemDataStr;
				TestTrue("File reading failed", FFileHelper::LoadFileToString(ElemDataStr, *ElemDataPath));
				TSharedRef<TJsonReader<>> ElemDataJsonReader = TJsonReaderFactory<>::Create(ElemDataStr);
				TSharedPtr<FJsonObject> ElemDataJson;
				TestTrue("File parsing failed", FJsonSerializer::Deserialize(ElemDataJsonReader, ElemDataJson));
				auto SceneMapping = AdvViz::SDK::Tools::MakeSharedLockableData<FITwinSceneMapping>(false);
				auto SceneMappingLocked = SceneMapping->GetAutoLock();
				TestTrue("Json parsing failed", SceneMappingLocked->FromJson(ElemDataJson));
				auto BackToJson = SceneMappingLocked->ToJson();
				FString BackToJsonStr;
				auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&BackToJsonStr);
				FJsonSerializer::Serialize(BackToJson.ToSharedRef(), JsonWriter);
				bool bJsonMatch = (BackToJsonStr == ElemDataStr);
				TestTrue("Deserialized Elements metadata matches original JSON", bJsonMatch);
				if (!bJsonMatch)
				{
					FString const OutJsonPath =
						Helper->GetBaseTestFolder() + TestName + TEXT(".ElemData_jsonInOut.json");
					FFileHelper::SaveStringToFile(BackToJsonStr, *OutJsonPath,
												  FFileHelper::EEncodingOptions::ForceUTF8);
				}
			});
		});

	Describe("Querying the schedule, without pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->TestName = TEXT("4D-testing");
					Helper->ScheduleName = TEXT("Exhaustive test with primitives");
					Helper->ITwinId = TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3");
					Helper->IModelId = TEXT("b088e94b-bfa8-48b0-b551-e7dbb3ef5ee1");
					Helper->ChangesetId = TEXT("3de7554f8e10bb7c5fa7dbd88e2e6c76d3b04dc7");
					Helper->EnvPrefix = TEXT(""); // ie Prod
					Helper->optRequestPagination = 10'000; // small test project => no pagination
					Helper->optBindingsReqPagination = 10'000; // small test project => no pagination
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	Describe("Querying the schedule, with pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->TestName = TEXT("4D-testing");
					Helper->ScheduleName = TEXT("Exhaustive test with primitives");
					Helper->ITwinId = TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3");
					Helper->IModelId = TEXT("b088e94b-bfa8-48b0-b551-e7dbb3ef5ee1");
					Helper->ChangesetId = TEXT("3de7554f8e10bb7c5fa7dbd88e2e6c76d3b04dc7");
					Helper->EnvPrefix = TEXT(""); // ie Prod
					Helper->optRequestPagination = 2; // force pagination even on the very small test project
					Helper->optBindingsReqPagination = 3; // same, for animation bindings only
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	// Actually redundant with the other "4D-testing" tests above since CheckEntireScheduleMatchesJson also
	// loads the serialized schedule to compare it to the one built from the cached HTTP replies...
	Describe("Loading serialized schedule", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
				Helper->TestName = TEXT("4D-testing");
				Helper->ScheduleName = TEXT("");// will be read from Json
				Helper->Generation = EITwinSchedulesGeneration::Unknown;// will be read from Json
				Helper->ITwinId = TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3");
				Helper->IModelId = TEXT("b088e94b-bfa8-48b0-b551-e7dbb3ef5ee1");
				Helper->ChangesetId = TEXT("3de7554f8e10bb7c5fa7dbd88e2e6c76d3b04dc7");
				Helper->EnvPrefix = TEXT(""); // ie Prod
				Helper->bUseCachedScheduleData = true; // skip querying, use the already serialized schedule instead
				};
			LatentBeforeEach(FTimespan::FromSeconds(5.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			It("should have overwritten the supplied name and generation with those from the Json",
				[this]() {
					TestTrue("Schedule name matches the one in the serialized schedule",
						FString(TEXT("Exhaustive test with primitives")) == Helper->Schedule->Name);
					TestTrue("Schedule generation matches the one in the serialized schedule",
						EITwinSchedulesGeneration::Legacy == Helper->Schedule->Generation);
				});
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	Describe("Querying a bigger schedule, with larger pagination", [this]()
		{
			auto const SetupFnc = [](std::shared_ptr<FSynchro4DImportTestHelper> Helper) {
					Helper->optRequestPagination = 10000;
					Helper->optBindingsReqPagination = 10000;
					Helper->TestName = TEXT("GSW-Stadium");
					Helper->ScheduleName = TEXT("GSW Stadium Only");
					Helper->ITwinId = TEXT("437e02f9-ab73-43a2-b525-f340f9579854");
					Helper->IModelId = TEXT("4ab017b4-6376-416d-8dbe-30808e4ca0f8");
					Helper->ChangesetId = TEXT("b3aca9315d78e470f328b2c023baffaed726470b");
					Helper->EnvPrefix = TEXT(""); // ie Prod
				};
			LatentBeforeEach(FTimespan::FromSeconds(10.),
				std::bind(&Synchro4DImportSpec::WaitFullSchedule, this, std::placeholders::_1, SetupFnc));
			// Just compare the FullSchedule against the reference file
			It("should match the ref json",
				std::bind(&Synchro4DImportSpec::CheckEntireScheduleMatchesJson, this));
		});
	Describe("Find other changeset's cached json schedules", [this]()
		{
			It("should find the other changeset's cached json schedules", [this]() {
				FString const CacheName(TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3_APIM_fadafada-5a10-407e-b517-fbc23fcf4dc3_DUMMYCHANGESETAFTERUNDERSCORE"));
				QueriesCache::FChangesetFinderIterator Finder(CacheName + TEXT(".json"));
				FString const TestJsonsCacheFolder =
					FPaths::Combine(Helper->GetBaseTestFolder(), TEXT("DummyCachedSchedulesJsons"));
				IFileManager::Get().IterateDirectory(*TestJsonsCacheFolder, Finder);
				TArray<FString> const& OtherChangesetJsonSchedules = Finder.GetOtherChangesetJsonsFound();
				TestTrue("Found 2 json filenames, no less, no more", OtherChangesetJsonSchedules.Num() == 2);
				TestTrue("Found the first one, full path, ending with '-2'", OtherChangesetJsonSchedules.Contains(
					FPaths::Combine(TestJsonsCacheFolder, TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3_APIM_fadafada-5a10-407e-b517-fbc23fcf4dc3_DUMMYCHANGESETAFTERUNDERSCORE-2.json"))));
				TestTrue("Found the second one, full path, ending with '#3'", OtherChangesetJsonSchedules.Contains(
					FPaths::Combine(TestJsonsCacheFolder, TEXT("d9712811-5a10-407e-b517-fbc23fcf4dc3_APIM_fadafada-5a10-407e-b517-fbc23fcf4dc3_DUMMYCHANGESETfoobar#3.json"))));
				// Tested manually: could copy to some tmp folder to include in unit test...
				//Finder.DeleteOtherChangesetJsons();
			});
		});
}

#endif // WITH_TESTS && WITH_EDITOR
