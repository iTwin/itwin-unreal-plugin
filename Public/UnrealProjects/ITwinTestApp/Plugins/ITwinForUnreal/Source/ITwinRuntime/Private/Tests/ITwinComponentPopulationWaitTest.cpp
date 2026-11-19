/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinComponentPopulationWaitTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Decoration/ITwinDecorationHelper.h>
#include <Population/ITwinPopulation.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/TestWorldHelpers.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwinComponentPopulationWaitTests
{
	const FString Rejected(TEXT("/CC/aaaaaaaa/bbbbbbbb/Rejected"));
	const FString Pending(TEXT("/CC/aaaaaaaa/cccccccc/Pending"));
	const FString Ready(TEXT("/CC/aaaaaaaa/dddddddd/Ready"));
	const FString Ordinary(TEXT("/Game/Test/Ordinary"));

	struct FState
	{
		TSharedPtr<ITwinTests::FScopedTestWorld> World;
		AITwinDecorationHelper* Helper = nullptr;
		FAutomationTestBase* Test = nullptr;
		AdvViz::SDK::RefID Group;
		int32 Requests = 0;
		int32 Phase = 0;
		double NextCheck = 0.0;
		double Deadline = 0.0;

		~FState()
		{
			if (Helper)
				Helper->OnDownloadRequest.Unbind();
			World.Reset();
		}
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckComponentPopulationWait,
	TSharedPtr<ITwinComponentPopulationWaitTests::FState>, State);

bool FCheckComponentPopulationWait::Update()
{
	using namespace ITwinComponentPopulationWaitTests;
	auto& test = *State->Test;
	auto* helper = State->Helper;
	if (FPlatformTime::Seconds() > State->Deadline)
	{
		test.AddError(TEXT("Timed out waiting for population creation"));
		return true;
	}
	if (State->Phase == 0)
	{
		if (FPlatformTime::Seconds() < State->NextCheck)
			return false;
		test.TestNull(TEXT("Rejected population was never created"),
			helper->GetPopulation(Rejected + TEXT("/Game/Test"), State->Group));
		test.TestTrue(TEXT("Unrelated component is still in flight"),
			helper->GetComponentLoadState(Pending) == EITwinComponentLoadState::InFlight);
		test.TestTrue(TEXT("A replacement can register the rejected component"),
			helper->MountPak("", TCHAR_TO_UTF8(*Rejected)));
		test.TestTrue(TEXT("The other component can independently complete"),
			helper->MountPak("", TCHAR_TO_UTF8(*Pending)));
		State->Phase = 1;
		return false;
	}
	if (!helper->GetPopulation(Pending + TEXT("/Game/Test"), State->Group))
		return false;
	test.TestNull(TEXT("Stopped rejection timer did not create an actor after replacement"),
		helper->GetPopulation(Rejected + TEXT("/Game/Test"), State->Group));
	test.TestNotNull(TEXT("Already-ready asset remains usable"),
		helper->GetPopulation(Ready + TEXT("/Game/Test"), State->Group));
	test.TestNotNull(TEXT("Ordinary asset remains usable"), helper->GetPopulation(Ordinary, State->Group));
	test.TestNotNull(TEXT("The replaced component can be explicitly recreated"),
		helper->GetOrCreatePopulation(Rejected + TEXT("/Game/Test"), State->Group));
	test.TestEqual(TEXT("No failure caused an automatic re-request"), State->Requests, 2);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentPopulationWaitTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.PopulationWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentPopulationWaitTest::RunTest(const FString& Parameters)
{
	using namespace ITwinComponentPopulationWaitTests;
	auto state = MakeShared<FState>();
	state->Test = this;
	state->World = MakeShared<ITwinTests::FScopedTestWorld>();
	state->Helper = state->World->SpawnActorOfClass<AITwinDecorationHelper>();
	if (!TestNotNull(TEXT("Decoration helper"), state->Helper))
		return false;
	state->Helper->SetLoadedITwinId(TEXT("population_wait_test"));
	state->Group = state->Helper->AddNewCustomInstancesGroup("component-wait-test");
	if (!TestTrue(TEXT("Instance group exists"), state->Group.IsValid()))
		return false;
	FState* raw = &state.Get();
	state->Helper->OnDownloadRequest.BindLambda([raw](const FString&) { ++raw->Requests; return true; });
	TestTrue(TEXT("Register already-ready component"), state->Helper->MountPak("", TCHAR_TO_UTF8(*Ready)));
	if (!TestNotNull(TEXT("Already-ready population created"),
		state->Helper->GetOrCreatePopulation(Ready + TEXT("/Game/Test"), state->Group))
		|| !TestNotNull(TEXT("Ordinary population created"), state->Helper->GetOrCreatePopulation(Ordinary, state->Group)))
		return false;
	TestNull(TEXT("Rejected component initially waits"),
		state->Helper->GetOrCreatePopulation(Rejected + TEXT("/Game/Test"), state->Group));
	TestNull(TEXT("Other component independently waits"),
		state->Helper->GetOrCreatePopulation(Pending + TEXT("/Game/Test"), state->Group));
	const uint64 generation = state->Helper->GetComponentLoadGeneration(Rejected);
	state->Helper->ReportComponentLoadFailure(Rejected, generation,
		EITwinComponentLoadFailure::Terminal, TEXT("invalid artifact"));
	TestNull(TEXT("Rejected population does not retry"),
		state->Helper->GetOrCreatePopulation(Rejected + TEXT("/Game/Test"), state->Group));
	TestTrue(TEXT("Mixed aggregate preserves pending work"), state->Helper->GetComponentLoadWait({
		Rejected + TEXT("/Game/Test"), Pending + TEXT("/Game/Test"), Ready + TEXT("/Game/Test"), Ordinary })
		== EITwinComponentLoadWait::InFlight);
	state->NextCheck = FPlatformTime::Seconds() + 0.4;
	state->Deadline = FPlatformTime::Seconds() + 5.0;
	ADD_LATENT_AUTOMATION_COMMAND(FCheckComponentPopulationWait(state));
	return true;
}

#endif
