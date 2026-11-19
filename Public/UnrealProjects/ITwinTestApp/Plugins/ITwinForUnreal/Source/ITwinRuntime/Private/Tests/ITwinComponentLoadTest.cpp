/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinComponentLoadTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Content/ITwinContentManager.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/TestWorldHelpers.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentAttemptTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.Attempts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentAttemptTest::RunTest(const FString& Parameters)
{
	ITwinTests::FScopedTestWorld world;
	auto* helper = world.SpawnActorOfClass<AITwinDecorationHelper>();
	if (!TestNotNull(TEXT("Decoration helper"), helper))
		return false;
	auto* manager = NewObject<UITwinContentManager>(helper);
	helper->iTwinContentManager = manager;
	manager->DecorationHelperPtr = helper;

	const FString id(TEXT("/CC/aaaaaaaa/bbbbbbbb/Bus"));
	const FString upperId(TEXT("/CC/AAAAAAAA/BBBBBBBB/Bus"));
	const FString asset = id + TEXT("/Game/Test/Bus");
	const FString other(TEXT("/CC/cccccccc/dddddddd/Tree"));
	const FString ready(TEXT("/CC/eeeeeeee/ffffffff/Ready"));
	const FString ordinary(TEXT("/Game/CarrotLibrary/Tree"));
	int32 requests = 0;
	helper->OnDownloadRequest.BindLambda([&requests](const FString&) { ++requests; return true; });

	TestTrue(TEXT("Bare ID initially not requested"),
		helper->GetComponentLoadState(id) == EITwinComponentLoadState::NotRequested);
	TestTrue(TEXT("Ordinary path remains ordinary"),
		helper->GetComponentLoadState(ordinary) == EITwinComponentLoadState::NotComponent);
	TestTrue(TEXT("Request accepted"), helper->RequestComponentDownload(upperId));
	const uint64 first = helper->GetComponentLoadGeneration(asset);
	TestTrue(TEXT("Asset path and bare ID share an in-flight attempt"),
		first != 0 && helper->GetComponentLoadState(id) == EITwinComponentLoadState::InFlight);
	TestTrue(TEXT("Duplicate request acknowledged"), helper->RequestComponentDownload(asset));
	TestEqual(TEXT("Duplicate did not invoke the delegate"), requests, 1);
	TestTrue(TEXT("Unrelated request accepted"), helper->RequestComponentDownload(other));
	TestEqual(TEXT("Unrelated request is not deduplicated"), requests, 2);

	helper->ReportComponentLoadFailure(id, first, EITwinComponentLoadFailure::Terminal, TEXT("invalid artifact"));
	TestTrue(TEXT("Terminal failure is typed"),
		helper->GetComponentLoadState(asset) == EITwinComponentLoadState::TerminalFailure);
	TestEqual(TEXT("Failure reason retained"), helper->GetComponentLoadFailureReason(id), FString(TEXT("invalid artifact")));
	TestTrue(TEXT("Legacy needs-download query still accepts a bare ID"), helper->IsComponentDownloadPending(id));
	TestFalse(TEXT("Ordinary requests do not retry a rejected artifact"), helper->RequestComponentDownload(asset));
	TestEqual(TEXT("No repeated delegate request"), requests, 2);

	TestTrue(TEXT("An explicit retry is accepted"), helper->RequestComponentDownload(id, true));
	const uint64 second = helper->GetComponentLoadGeneration(id);
	TestTrue(TEXT("Retry has a new generation"), second > first);
	helper->ReportComponentLoadFailure(id, first, EITwinComponentLoadFailure::Terminal, TEXT("stale"));
	TestTrue(TEXT("Old failure cannot override a newer request"),
		helper->GetComponentLoadState(id) == EITwinComponentLoadState::InFlight);
	helper->ReportComponentLoadFailure(id, second, EITwinComponentLoadFailure::Retryable, TEXT("transient"));
	TestTrue(TEXT("Transient failure remains distinct"),
		helper->GetComponentLoadState(id) == EITwinComponentLoadState::RetryableFailure);
	TestTrue(TEXT("Retry transient failure explicitly"), helper->RequestComponentDownload(id, true));
	const uint64 third = helper->GetComponentLoadGeneration(id);
	TestTrue(TEXT("Successful registration"), helper->MountPak("", TCHAR_TO_UTF8(*id)));
	helper->ReportComponentLoadFailure(id, third, EITwinComponentLoadFailure::Terminal, TEXT("late"));
	TestTrue(TEXT("Late failure cannot override a successful registration"),
		helper->GetComponentLoadState(asset) == EITwinComponentLoadState::Ready);
	TestTrue(TEXT("Ready request remains usable"), helper->RequestComponentDownload(id));
	TestEqual(TEXT("Ready component is not downloaded again"), requests, 4);

	TestTrue(TEXT("Register another ready component"), helper->MountPak("", TCHAR_TO_UTF8(*ready)));
	TestTrue(TEXT("Start a failure for the aggregate"), helper->RequestComponentDownload(
		TEXT("/CC/11111111/22222222/Rejected")));
	const FString rejected(TEXT("/CC/11111111/22222222/Rejected"));
	helper->ReportComponentLoadFailure(rejected, helper->GetComponentLoadGeneration(rejected),
		EITwinComponentLoadFailure::Terminal, TEXT("invalid"));
	const TArray<FString> mixed{ rejected + TEXT("/Game/A"), other + TEXT("/Game/B"),
		ready + TEXT("/Game/C"), ordinary };
	TestTrue(TEXT("Spline/animation aggregate waits for unrelated in-flight work"),
		helper->GetComponentLoadWait(mixed) == EITwinComponentLoadWait::InFlight);
	TestTrue(TEXT("Other component can still complete"), helper->MountPak("", TCHAR_TO_UTF8(*other)));
	TestTrue(TEXT("Aggregate is failure, not all-ready, once the other attempt settles"),
		helper->GetComponentLoadWait(mixed) == EITwinComponentLoadWait::Failed);
	TestTrue(TEXT("Independent ready asset is still ready"),
		helper->GetComponentLoadState(ready) == EITwinComponentLoadState::Ready);

	const FString untracked(TEXT("/CC/33333333/44444444/Untracked"));
	TestTrue(TEXT("Start an attempt that can be cleared"), helper->RequestComponentDownload(untracked));
	const uint64 firstUntrackedGeneration = helper->GetComponentLoadGeneration(untracked);
	TestTrue(TEXT("Clear only the current transient attempt"),
		helper->ClearComponentLoadAttempt(untracked, firstUntrackedGeneration));
	TestTrue(TEXT("Clearing returns the component to not requested"),
		helper->GetComponentLoadState(untracked) == EITwinComponentLoadState::NotRequested);
	TestEqual(TEXT("Clearing removes the active generation"),
		helper->GetComponentLoadGeneration(untracked), uint64(0));
	TestTrue(TEXT("A later attempt can be started"), helper->RequestComponentDownload(untracked));
	const uint64 secondUntrackedGeneration = helper->GetComponentLoadGeneration(untracked);
	TestTrue(TEXT("Later attempt has a new generation"), secondUntrackedGeneration > firstUntrackedGeneration);
	TestFalse(TEXT("A stale clear cannot remove a newer attempt"),
		helper->ClearComponentLoadAttempt(untracked, firstUntrackedGeneration));
	TestTrue(TEXT("Newer attempt remains in flight"),
		helper->GetComponentLoadState(untracked) == EITwinComponentLoadState::InFlight);
	TestTrue(TEXT("Current generation can be cleared"),
		helper->ClearComponentLoadAttempt(untracked, secondUntrackedGeneration));
	TestTrue(TEXT("Final state remains unrequested, not ready"),
		helper->GetComponentLoadState(untracked) == EITwinComponentLoadState::NotRequested);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinLegacyComponentDownloadTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.LegacyDownload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinLegacyComponentDownloadTest::RunTest(const FString& Parameters)
{
	ITwinTests::FScopedTestWorld world;
	auto* helper = world.SpawnActorOfClass<AITwinDecorationHelper>();
	if (!TestNotNull(TEXT("Decoration helper"), helper))
		return false;
	auto* manager = NewObject<UITwinContentManager>(helper);
	helper->iTwinContentManager = manager;
	manager->DecorationHelperPtr = helper;

	const FString id(TEXT("/CC/aaaaaaaa/bbbbbbbb/Bus"));
	const FString upperId(TEXT("/CC/AAAAAAAA/BBBBBBBB/Bus"));
	int32 requests = 0;
	FString requestedId;
	helper->OnDownloadRequest.BindLambda([&requests, &requestedId](const FString& requested)
	{
		++requests;
		requestedId = requested;
		return true;
	});

	TestFalse(TEXT("Empty ID is not downloaded"), manager->IsComponentDownloaded(FString()));
	TestFalse(TEXT("Empty ID is not requested"), manager->RequestComponentDownload(FString()));
	TestFalse(TEXT("Component starts undownloaded"), manager->IsComponentDownloaded(id));
	TestTrue(TEXT("Legacy request starts the download"), manager->RequestComponentDownload(upperId));
	TestTrue(TEXT("Legacy request waits for the in-flight download"), manager->RequestComponentDownload(id));
	TestEqual(TEXT("In-flight request is not repeated"), requests, 1);
	TestFalse(TEXT("In-flight component is not downloaded"), manager->IsComponentDownloaded(id));

	TestTrue(TEXT("Register downloaded component"), manager->MountPak(TEXT(""), id));
	TestTrue(TEXT("Downloaded lookup normalizes identifiers"), manager->IsComponentDownloaded(upperId));
	TestTrue(TEXT("Downloaded lookup accepts asset paths"),
		manager->IsComponentDownloaded(upperId + TEXT("/Game/Test/Bus")));
	TestFalse(TEXT("Legacy request does not wait for a ready component"),
		manager->RequestComponentDownload(upperId));
	TestTrue(TEXT("State-aware request accepts a ready component"),
		manager->RequestComponentDownload(upperId, false));
	TestEqual(TEXT("Ready component is not downloaded again"), requests, 1);

	TestTrue(TEXT("Register a legacy opaque identifier"), manager->MountPak(TEXT(""), TEXT("LegacyId")));
	TestTrue(TEXT("Legacy opaque identifier remains discoverable"), manager->IsComponentDownloaded(TEXT("LegacyId")));
	TestFalse(TEXT("Legacy opaque identifier is not requested again"),
		manager->RequestComponentDownload(TEXT("LegacyId")));
	TestTrue(TEXT("Legacy requests forward undownloaded opaque identifiers"),
		manager->RequestComponentDownload(TEXT("LegacyPendingId")));
	TestEqual(TEXT("Opaque identifier is forwarded unchanged"), requestedId, FString(TEXT("LegacyPendingId")));
	TestEqual(TEXT("Opaque identifier triggers the download delegate"), requests, 2);
	TestFalse(TEXT("State-aware requests still reject opaque identifiers"),
		manager->RequestComponentDownload(TEXT("LegacyPendingId"), false));
	TestEqual(TEXT("Rejected state-aware request does not trigger the delegate"), requests, 2);
	return true;
}

#endif
