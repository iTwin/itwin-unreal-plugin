/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinComponentAnimationWaitTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Components/SplineComponent.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Math/UEMathConversion.h>
#include <PathAnimation/ITwinAnimPathHelper.h>
#include <PathAnimation/ITwinPathAnimTool.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/TestWorldHelpers.h>
#include <UObject/UObjectHash.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#	include <SDK/Core/Visualization/PathAnimation.h>
#	include <SDK/Core/Visualization/PopulationPersistence.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <Compil/AfterNonUnrealIncludes.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentAnimationWaitTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.AnimationWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentAnimationWaitTest::RunTest(const FString& Parameters)
{
	using namespace AdvViz::SDK;
	ITwinTests::FScopedTestWorld world;
	auto* helper = world.SpawnActorOfClass<AITwinDecorationHelper>();
	auto* populations = world.SpawnActorOfClass<AITwinPopulationTool>();
	auto* animation = world.SpawnActorOfClass<AITwinPathAnimTool>();
	if (!TestNotNull(TEXT("Decoration helper"), helper)
		|| !TestNotNull(TEXT("Population tool"), populations)
		|| !TestNotNull(TEXT("Animation tool"), animation))
		return false;
	helper->SetLoadedITwinId(TEXT("animation_wait_test"));
	populations->SetDecorationHelper(helper);
	animation->SetDecorationHelper(helper);
	animation->ConnectPopulationTool(populations);
	std::shared_ptr<IPathAnimManager> pathManager(IPathAnimManager::New());
	animation->SetPathAnimManager(pathManager);

	AITwinSplineHelper* spline = nullptr;
	{
		AITwinSplineHelper::FSpawnContext context(EITwinSplineUsage::AnimPathTraffic);
		spline = world.SpawnActorOfClass<AITwinSplineHelper>();
	}
	if (!TestNotNull(TEXT("Animation spline"), spline))
		return false;
	auto coreSpline = Tools::MakeSharedLockableDataPtr(ISpline::New());
	{
		auto locked = coreSpline->GetAutoLock();
		locked->SetUsage(ESplineUsage::AnimPathTraffic);
	}
	spline->GetSplineComponent()->SetSplinePoints({ FVector::ZeroVector, FVector(1000, 0, 0) },
		ESplineCoordinateSpace::Local);
	spline->Initialize(spline->GetSplineComponent(), coreSpline);
	animation->OnSplineHelperAdded(spline);
	if (!TestEqual(TEXT("Traffic animation registered"), animation->NumPaths(EITwinAnimPathType::Traffic), 1))
		return false;

	TArray<UObject*> children;
	GetObjectsWithOuter(animation, children, false);
	UITwinAnimPathHelper* path = nullptr;
	for (UObject* child : children)
		if ((path = Cast<UITwinAnimPathHelper>(child)))
			break;
	if (!TestNotNull(TEXT("Animation path helper"), path))
		return false;

	const FString rejected(TEXT("/CC/aaaaaaaa/bbbbbbbb/Rejected"));
	const FString pending(TEXT("/CC/aaaaaaaa/cccccccc/Pending"));
	const FString rejectedAsset = rejected + TEXT("/Game/Test/Rejected");
	const FString pendingAsset = pending + TEXT("/Game/Test/Pending");
	int32 requests = 0;
	helper->OnDownloadRequest.BindLambda([&requests](const FString&) { ++requests; return true; });
	const FAnimPathIdentifier handle(EITwinAnimPathType::Traffic, 0);
	animation->Set3DObjects(handle, { rejectedAsset, pendingAsset });
	animation->Tick(0.01f);
	TestEqual(TEXT("Both component requests start"), requests, 2);
	helper->ReportComponentLoadFailure(rejected, helper->GetComponentLoadGeneration(rejected),
		EITwinComponentLoadFailure::Terminal, TEXT("invalid artifact"));
	animation->Tick(0.01f);
	TestTrue(TEXT("Animation still waits for unrelated in-flight work"),
		path->GetUpdateState().ShouldStartUpdate(1000.f));
	TestFalse(TEXT("Waiting animation is not ready"), path->IsReady());
	TestEqual(TEXT("No animation instances created for the rejected set"), animation->GetInstancesNum(handle), 0);

	TestTrue(TEXT("Other component can complete"), helper->MountPak("", TCHAR_TO_UTF8(*pending)));
	animation->Tick(0.01f);
	TestFalse(TEXT("Failed update stops polling"), path->GetUpdateState().ShouldStartUpdate(1000.f));
	TestFalse(TEXT("Failed update remains dirty, not ready"), path->IsReady());
	TestEqual(TEXT("Rejection does not signal animation success"), animation->GetInstancesNum(handle), 0);
	TestEqual(TEXT("Failure did not re-request either component"), requests, 2);
	animation->Set3DObjects(handle, { pendingAsset });
	TestTrue(TEXT("An explicit replacement re-arms the update"), path->GetUpdateState().ShouldStartUpdate(1000.f));
	helper->OnDownloadRequest.Unbind();
	return true;
}

namespace ITwinComponentSplineWaitTests
{
	const FString Rejected(TEXT("/CC/aaaaaaaa/bbbbbbbb/Rejected"));
	const FString Pending(TEXT("/CC/aaaaaaaa/cccccccc/Pending"));

	struct FState
	{
		TSharedPtr<ITwinTests::FScopedTestWorld> World;
		std::shared_ptr<AdvViz::SDK::IPopulationManager> Manager;
		AITwinDecorationHelper* Helper = nullptr;
		AITwinPopulationTool* Tool = nullptr;
		AITwinSplineHelper* Spline = nullptr;
		FAutomationTestBase* Test = nullptr;
		int32 Requests = 0;
		bool bSawPartialJob = false;
		int32 Phase = 0;
		double CheckAt = 0.0;
		double Deadline = 0.0;

		~FState()
		{
			if (Helper)
				Helper->OnDownloadRequest.Unbind();
			World.Reset();
		}
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckComponentSplineWait,
	TSharedPtr<ITwinComponentSplineWaitTests::FState>, State);

bool FCheckComponentSplineWait::Update()
{
	using namespace ITwinComponentSplineWaitTests;
	auto& test = *State->Test;
	if (FPlatformTime::Seconds() > State->Deadline)
	{
		test.AddError(TEXT("Timed out waiting for the partial spline population"));
		return true;
	}
	if (State->Phase == 0)
	{
		if (FPlatformTime::Seconds() < State->CheckAt)
			return false;
		test.TestTrue(TEXT("Spline keeps waiting for the other in-flight asset"),
			State->Helper->GetComponentLoadWait({ Rejected, Pending }) == EITwinComponentLoadWait::InFlight);
		test.TestFalse(TEXT("No spline job while the other component is pending"),
			State->Tool->IsPopulatingSpline(*State->Spline));
		test.TestTrue(TEXT("Other component can finish"),
			State->Helper->MountPak("", TCHAR_TO_UTF8(*Pending)));
		State->Phase = 1;
		return false;
	}
	if (State->Tool->IsPopulatingSpline(*State->Spline))
		State->bSawPartialJob = true;
	static_cast<AActor*>(State->Tool)->Tick(0.01f);
	if (!State->bSawPartialJob || State->Tool->IsPopulatingSpline(*State->Spline))
		return false;
	const auto group = State->Helper->GetInstancesGroupIdForSpline(*State->Spline);
	test.TestNotNull(TEXT("Successful mixed-set actor is available"),
		State->Helper->GetPopulation(Pending + TEXT("/Game/Test/Pending"), group));
	test.TestNull(TEXT("Rejected mixed-set actor is not created"),
		State->Helper->GetPopulation(Rejected + TEXT("/Game/Test/Rejected"), group));
	test.TestEqual(TEXT("Spline did not re-request the rejection"), State->Requests, 2);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentSplineWaitTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.SplineWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentSplineWaitTest::RunTest(const FString& Parameters)
{
	using namespace AdvViz::SDK;
	using namespace ITwinComponentSplineWaitTests;
	auto state = MakeShared<FState>();
	state->Test = this;
	state->World = MakeShared<ITwinTests::FScopedTestWorld>();
	state->Helper = state->World->SpawnActorOfClass<AITwinDecorationHelper>();
	state->Tool = state->World->SpawnActorOfClass<AITwinPopulationTool>();
	if (!TestNotNull(TEXT("Decoration helper"), state->Helper)
		|| !TestNotNull(TEXT("Population tool"), state->Tool))
		return false;
	state->Helper->SetLoadedITwinId(TEXT("spline_wait_test"));
	state->Tool->SetDecorationHelper(state->Helper);
	state->Manager.reset(IPopulationManager::New());
	state->Tool->SetPopulationManager(state->Manager);
	{
		AITwinSplineHelper::FSpawnContext context(EITwinSplineUsage::PopulationPath);
		state->Spline = state->World->SpawnActorOfClass<AITwinSplineHelper>();
	}
	if (!TestNotNull(TEXT("Population spline"), state->Spline))
		return false;
	auto coreSpline = Tools::MakeSharedLockableDataPtr(ISpline::New());
	{
		auto locked = coreSpline->GetAutoLock();
		locked->SetUsage(ESplineUsage::PopulationPath);
	}
	state->Spline->GetSplineComponent()->SetSplinePoints({ FVector::ZeroVector, FVector(1000, 0, 0) },
		ESplineCoordinateSpace::Local);
	state->Spline->Initialize(state->Spline->GetSplineComponent(), coreSpline);
	state->Tool->OnSplineHelperAdded(state->Spline);
	if (!TestEqual(TEXT("Path population registered"), state->Tool->NumPopulations(EITwinPopulationType::Path), 1))
		return false;

	FState* raw = &state.Get();
	state->Helper->OnDownloadRequest.BindLambda([raw](const FString&) { ++raw->Requests; return true; });
	const FPopulationIdentifier handle(EITwinPopulationType::Path, 0);
	state->Tool->SetDistance(handle, 2.f);
	state->Tool->Set3DObjects(handle, { Rejected + TEXT("/Game/Test/Rejected"), Pending + TEXT("/Game/Test/Pending") });
	TestEqual(TEXT("Both spline assets were requested"), state->Requests, 2);
	state->Helper->ReportComponentLoadFailure(Rejected, state->Helper->GetComponentLoadGeneration(Rejected),
		EITwinComponentLoadFailure::Terminal, TEXT("invalid artifact"));
	state->CheckAt = FPlatformTime::Seconds() + 0.4;
	state->Deadline = FPlatformTime::Seconds() + 8.0;
	ADD_LATENT_AUTOMATION_COMMAND(FCheckComponentSplineWait(state));
	return true;
}

namespace ITwinComponentSceneAnimationTests
{
	struct FState
	{
		TSharedPtr<ITwinTests::FScopedTestWorld> World;
		std::shared_ptr<AdvViz::SDK::IPathAnimManager> Manager;
		AITwinDecorationHelper* Helper = nullptr;
		AITwinPathAnimTool* Tool = nullptr;
		UITwinAnimPathHelper* Path = nullptr;
		FAutomationTestBase* Test = nullptr;
		double CheckAt = 0.0;

		~FState()
		{
			if (Helper)
				Helper->OnDownloadRequest.Unbind();
			World.Reset();
		}
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckSceneAnimationWait,
	TSharedPtr<ITwinComponentSceneAnimationTests::FState>, State);

bool FCheckSceneAnimationWait::Update()
{
	if (FPlatformTime::Seconds() < State->CheckAt)
		return false;
	State->Tool->Tick(0.01f);
	State->Test->TestFalse(TEXT("Rejected restored animation is not ready"), State->Path->IsReady());
	State->Test->TestFalse(TEXT("Rejected restored animation stopped polling"),
		State->Path->GetUpdateState().ShouldStartUpdate(1000.f));
	State->Test->TestEqual(TEXT("No animation instances for a rejected restored path"),
		State->Tool->GetInstancesNum(FAnimPathIdentifier(EITwinAnimPathType::Traffic, 0)), 0);
	const FString replacement(TEXT("/CC/aaaaaaaa/cccccccc/Pending/Game/Test/Pending"));
	State->Tool->Set3DObjects(FAnimPathIdentifier(EITwinAnimPathType::Traffic, 0), { replacement });
	State->Test->TestTrue(TEXT("Explicit restored-path replacement re-arms the update"),
		State->Path->GetUpdateState().ShouldStartUpdate(1000.f));
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentSceneAnimationWaitTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.SceneAnimationWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentSceneAnimationWaitTest::RunTest(const FString& Parameters)
{
	using namespace AdvViz::SDK;
	using namespace ITwinComponentSceneAnimationTests;
	auto state = MakeShared<FState>();
	state->Test = this;
	state->World = MakeShared<ITwinTests::FScopedTestWorld>();
	state->Helper = state->World->SpawnActorOfClass<AITwinDecorationHelper>();
	state->Tool = state->World->SpawnActorOfClass<AITwinPathAnimTool>();
	auto* populations = state->World->SpawnActorOfClass<AITwinPopulationTool>();
	if (!TestNotNull(TEXT("Decoration helper"), state->Helper)
		|| !TestNotNull(TEXT("Animation tool"), state->Tool)
		|| !TestNotNull(TEXT("Population tool"), populations))
		return false;
	state->Helper->SetLoadedITwinId(TEXT("scene_animation_wait_test"));
	populations->SetDecorationHelper(state->Helper);
	state->Tool->SetDecorationHelper(state->Helper);
	state->Tool->ConnectPopulationTool(populations);
	state->Manager.reset(IPathAnimManager::New());
	state->Tool->SetPathAnimManager(state->Manager);

	AITwinSplineHelper* spline = nullptr;
	{
		AITwinSplineHelper::FSpawnContext context(EITwinSplineUsage::AnimPathTraffic);
		spline = state->World->SpawnActorOfClass<AITwinSplineHelper>();
	}
	if (!TestNotNull(TEXT("Restored animation spline"), spline))
		return false;
	auto coreSpline = Tools::MakeSharedLockableDataPtr(ISpline::New());
	{
		auto locked = coreSpline->GetAutoLock();
		locked->SetUsage(ESplineUsage::AnimPathTraffic);
	}
	spline->GetSplineComponent()->SetSplinePoints({ FVector::ZeroVector, FVector(1000, 0, 0) },
		ESplineCoordinateSpace::Local);
	spline->Initialize(spline->GetSplineComponent(), coreSpline);
	const FString rejected(TEXT("/CC/aaaaaaaa/bbbbbbbb/Rejected"));
	const FString pending(TEXT("/CC/aaaaaaaa/cccccccc/Pending"));
	const FString rejectedAsset = rejected + TEXT("/Game/Test/Rejected");
	const FString pendingAsset = pending + TEXT("/Game/Test/Pending");
	{
		auto info = state->Manager->AddAnimationPathInfo();
		auto locked = info->GetAutoLock();
		locked->SetSplineId(spline->GetAVizSplineId());
		locked->SetObjects({ TCHAR_TO_UTF8(*rejectedAsset), TCHAR_TO_UTF8(*pendingAsset) });
	}
	state->Helper->OnDownloadRequest.BindLambda([](const FString&) { return true; });
	const auto group = state->Helper->GetInstancesGroupIdForSpline(*spline);
	state->Helper->GetOrCreatePopulation(rejectedAsset, group);
	state->Helper->GetOrCreatePopulation(pendingAsset, group);
	state->Helper->ReportComponentLoadFailure(rejected, state->Helper->GetComponentLoadGeneration(rejected),
		EITwinComponentLoadFailure::Terminal, TEXT("invalid artifact"));
	state->Tool->LoadAnimationPaths();
	TArray<UObject*> children;
	GetObjectsWithOuter(state->Tool, children, false);
	for (UObject* child : children)
		if ((state->Path = Cast<UITwinAnimPathHelper>(child)))
			break;
	if (!TestNotNull(TEXT("Restored animation path helper"), state->Path))
		return false;
	TestFalse(TEXT("Restored path is not ready while pending"), state->Path->IsReady());
	TestTrue(TEXT("Other component can finish"),
		state->Helper->MountPak("", TCHAR_TO_UTF8(*pending)));
	state->CheckAt = FPlatformTime::Seconds() + 0.6;
	ADD_LATENT_AUTOMATION_COMMAND(FCheckSceneAnimationWait(state));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckSceneAnimationSuccess,
	TSharedPtr<ITwinComponentSceneAnimationTests::FState>, State);

bool FCheckSceneAnimationSuccess::Update()
{
	if (!State->Path->IsReady() && FPlatformTime::Seconds() < State->CheckAt)
		return false;
	State->Test->TestTrue(TEXT("A successfully restored animation becomes ready"), State->Path->IsReady());
	State->Test->TestEqual(TEXT("The existing animated object is retained"),
		State->Tool->GetInstancesNum(FAnimPathIdentifier(EITwinAnimPathType::Object, 0)), 1);
	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinComponentSceneAnimationSuccessTest, FITwinAutomationTestBaseNoLogs,
	"Bentley.ITwinForUnreal.ITwinRuntime.ComponentLoad.SceneAnimationSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinComponentSceneAnimationSuccessTest::RunTest(const FString& Parameters)
{
	using namespace AdvViz::SDK;
	using namespace ITwinComponentSceneAnimationTests;
	auto state = MakeShared<FState>();
	state->Test = this;
	state->World = MakeShared<ITwinTests::FScopedTestWorld>();
	state->Helper = state->World->SpawnActorOfClass<AITwinDecorationHelper>();
	state->Tool = state->World->SpawnActorOfClass<AITwinPathAnimTool>();
	auto* populations = state->World->SpawnActorOfClass<AITwinPopulationTool>();
	if (!TestNotNull(TEXT("Decoration helper"), state->Helper)
		|| !TestNotNull(TEXT("Animation tool"), state->Tool)
		|| !TestNotNull(TEXT("Population tool"), populations))
		return false;
	state->Helper->SetLoadedITwinId(TEXT("scene_animation_success_test"));
	populations->SetDecorationHelper(state->Helper);
	state->Tool->SetDecorationHelper(state->Helper);
	state->Tool->ConnectPopulationTool(populations);
	state->Manager.reset(IPathAnimManager::New());
	state->Tool->SetPathAnimManager(state->Manager);

	AITwinSplineHelper* spline = nullptr;
	{
		AITwinSplineHelper::FSpawnContext context(EITwinSplineUsage::AnimPathObject);
		spline = state->World->SpawnActorOfClass<AITwinSplineHelper>();
	}
	if (!TestNotNull(TEXT("Restored animation spline"), spline))
		return false;
	auto coreSpline = Tools::MakeSharedLockableDataPtr(ISpline::New());
	{
		auto locked = coreSpline->GetAutoLock();
		locked->SetUsage(ESplineUsage::AnimPathObject);
	}
	spline->GetSplineComponent()->SetSplinePoints({ FVector::ZeroVector, FVector(1000, 0, 0) },
		ESplineCoordinateSpace::Local);
	spline->Initialize(spline->GetSplineComponent(), coreSpline);
	const FString id(TEXT("/CC/aaaaaaaa/cccccccc/Bus"));
	const FString asset = id + TEXT("/Game/CarrotLibrary/Vehicles/Bus");
	{
		auto info = state->Manager->AddAnimationPathInfo();
		auto locked = info->GetAutoLock();
		locked->SetSplineId(spline->GetAVizSplineId());
		locked->SetObjects({ TCHAR_TO_UTF8(*asset) });
	}
	state->Helper->OnDownloadRequest.BindLambda([](const FString&) { return true; });
	const auto group = state->Helper->GetInstancesGroupIdForSpline(*spline);
	state->Helper->GetOrCreatePopulation(asset, group);
	auto instances = state->Helper->GetInstancesManager();
	if (!TestTrue(TEXT("Scene instance manager"), static_cast<bool>(instances)))
		return false;
	auto instance = instances->AddInstance(TCHAR_TO_UTF8(*asset), group);
	if (!TestTrue(TEXT("Saved scene instance"), static_cast<bool>(instance)))
		return false;
	{
		auto locked = instance->GetAutoLock();
		locked->SetName("restored");
		locked->SetTransform(FITwinMathConversion::UEtoSDK(FTransform::Identity));
	}
	state->Tool->LoadAnimationPaths();
	TArray<UObject*> children;
	GetObjectsWithOuter(state->Tool, children, false);
	for (UObject* child : children)
		if ((state->Path = Cast<UITwinAnimPathHelper>(child)))
			break;
	if (!TestNotNull(TEXT("Restored animation path helper"), state->Path))
		return false;
	TestFalse(TEXT("Successful path waits until the component is ready"), state->Path->IsReady());
	if (!TestTrue(TEXT("Component registers"), state->Helper->MountPak("", TCHAR_TO_UTF8(*id))))
		return false;
	state->CheckAt = FPlatformTime::Seconds() + 5.0;
	ADD_LATENT_AUTOMATION_COMMAND(FCheckSceneAnimationSuccess(state));
	return true;
}

#endif
