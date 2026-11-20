/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationRemovalTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Components/HierarchicalInstancedStaticMeshComponent.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Population/ITwinPopulation.h>
#include <Tests/TestWorldHelpers.h>

#include <Engine/StaticMesh.h>
#include <HAL/IConsoleManager.h>
#include <Misc/AutomationTest.h>
#include <StaticMeshCompiler.h>
#include <Templates/Greater.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinPopulationRemovalTest,
	"Bentley.ITwinForUnreal.ITwinRuntime.Population.RepeatedRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinPopulationRemovalTest::RunTest(const FString& Parameters)
{
	ITwinTests::FScopedTestWorld World;
	auto* Helper = World.SpawnActorOfClass<AITwinDecorationHelper>();
	if (!TestNotNull(TEXT("Decoration helper"), Helper))
		return false;
	Helper->SetLoadedITwinId(TEXT("population-removal-test"));
	const auto Group = Helper->AddNewCustomInstancesGroup("population-removal-test");
	if (!TestTrue(TEXT("Instance group"), Group.IsValid()))
		return false;
	auto* Population = Helper->GetOrCreatePopulation(TEXT("/Engine/BasicShapes/Cube.Cube"), Group);
	if (!TestNotNull(TEXT("Population"), Population))
		return false;
	Population->DispatchBeginPlay();
	auto* Foliage = Population->FindComponentByClass<UHierarchicalInstancedStaticMeshComponent>();
	auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Foliage"), Foliage) || !TestNotNull(TEXT("Mesh"), Mesh))
		return false;
	FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });
	// CreatePopulation uses an empty mesh with NullRHI; use real bounds/vertices for this tree test.
	Foliage->SetStaticMesh(Mesh);
	Foliage->bEnableDensityScaling = false;
	auto* SplitVerts = IConsoleManager::Get().FindConsoleVariable(TEXT("foliage.MinVertsToSplitNode"));
	if (!TestNotNull(TEXT("Tree split setting"), SplitVerts))
		return false;
	struct FRestoreSplit
	{
		IConsoleVariable* Variable;
		int32 Value;
		EConsoleVariableFlags Flags;
		~FRestoreSplit()
		{
			Variable->Set(Value, ECVF_SetByCode);
			Variable->SetFlags(Flags);
		}
	} RestoreSplit{ SplitVerts, SplitVerts->GetInt(), SplitVerts->GetFlags() };
	SplitVerts->Set(1, ECVF_SetByCode);

	TArray<AdvViz::SDK::RefID> Ids;
	TArray<FTransform> Transforms;
	TArray<FVector> Colors;
	const int32 Positions[] = { 6, 0, 5, 1, 4, 2, 7, 3 };
	for (int32 Position : Positions)
	{
		// Exercise the single-placement path, including a moved preview and its final transform.
		AITwinPopulation::FInteractiveTransformationScope Placement(*Population);
		const int32 Index = Population->AddInstance(FTransform::Identity,
			AITwinPopulation::EAddInstanceContext::InteractivePlacement);
		const FTransform Transform(FVector(Position * 1000.0, 0.0, 0.0));
		Population->FinalizeAddedInstance(Index, &Transform);
		Ids.Add(Population->GetInstanceRefId(Index));
		Transforms.Add(Transform);
		Colors.Add(Population->GetInstanceColorVariation(Index));
	}
	Foliage->BuildTreeIfOutdated(false, true);
	bool bNonIdentityOrder = false;
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
		bNonIdentityOrder |= Foliage->GetRenderIndex(Index) != Index;
	TestTrue(TEXT("Spatial ordering differs from placement ordering"), bNonIdentityOrder);

	auto Remove = [&](TArray<int32> Indices, bool bSingle = false)
	{
		Indices.Sort(TGreater<int32>());
		for (int32 Index : Indices)
		{
			Ids.RemoveAtSwap(Index);
			Transforms.RemoveAtSwap(Index);
			Colors.RemoveAtSwap(Index);
		}
		if (bSingle)
			Population->RemoveInstance(Indices[0]);
		else
			Population->RemoveInstances(Indices);
		TestEqual(TEXT("UE survivor count"), Population->GetNumberOfInstances(), Ids.Num());
		// Check BEFORE allowing another frame/tree build: a sparse old render table is the regression.
		TestEqual(TEXT("No stale render slots after erase"), Foliage->InstanceCountToRender, Ids.Num());
		TestEqual(TEXT("Tree already reflects survivors"), Foliage->NumBuiltInstances, Ids.Num());
		TSet<int32> RenderSlots;
		for (int32 Index = 0; Index < Ids.Num(); ++Index)
		{
			TestTrue(TEXT("Survivor identity"), Population->GetInstanceRefId(Index) == Ids[Index]);
			TestEqual(TEXT("Survivor lookup"), Population->GetInstanceIndexFromRefId(Ids[Index]), Index);
			TestTrue(TEXT("Survivor transform"), Population->GetInstanceTransform(Index).Equals(Transforms[Index]));
			TestTrue(TEXT("Survivor custom data"), Population->GetInstanceColorVariation(Index).Equals(Colors[Index]));
			const int32 RenderIndex = Foliage->GetRenderIndex(Index);
			TestTrue(TEXT("Survivor has a compact render slot"), RenderIndex >= 0 && RenderIndex < Ids.Num());
			RenderSlots.Add(RenderIndex);
		}
		TestEqual(TEXT("Every survivor has a distinct render slot"), RenderSlots.Num(), Ids.Num());
	};
	Remove({ 2 }); // middle: swap the last placement into the erased slot
	Remove({ 6, 1 }); // tail and middle in a brush batch
	Remove({ 0 }, true); // single removal uses the same safe render path
	Remove({ 3, 2, 1, 0 }); // repeated erasing down to an empty population
	return true;
}

#endif
