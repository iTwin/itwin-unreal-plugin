/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Population/ITwinPopulation.h"
#include "Population/ITwinPopulation.inl"
#include "Population/ITwinAnimPathManager.h"
#include "Population/ITwinPopulationWithPathExt.h"
#include <Clipping/ITwinClippingTool.h>
#include <Helpers/WorldSingleton.h>

#include "Math/UEMathConversion.h"

#include <DrawDebugHelpers.h>
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetSystemLibrary.h"
#include <FoliageInstancedStaticMeshComponent.h>

#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonWriter.h>
#include <Serialization/JsonSerializer.h>
#include <Decoration/ITwinContentLibrarySettings.h>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <SDK/Core/Tools/Assert.h>
#	include <SDK/Core/Visualization/InstancesGroup.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <numeric>
#include <array>


//---------------------------------------------------------------------------------------
// struct FITwinFoliageComponentHolder
//---------------------------------------------------------------------------------------

void FITwinFoliageComponentHolder::InitWithMasterMesh(AITwinPopulation& PopulationActor, UStaticMesh* Mesh)
{
	if (ensure(Mesh))
	{
		MasterMesh = Mesh;

		InitFoliageMeshComponent(PopulationActor);

		for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i)
		{
			FoliageInstMeshComp->SetMaterial(i, Mesh->GetMaterial(i));
		}
	}
}

void FITwinFoliageComponentHolder::InitFoliageMeshComponent(AITwinPopulation& PopulationActor)
{
	if (!FoliageInstMeshComp)
	{
		FoliageInstMeshComp = NewObject<UFoliageInstancedStaticMeshComponent>(&PopulationActor, UFoliageInstancedStaticMeshComponent::StaticClass());
		FoliageInstMeshComp->SetupAttachment(PopulationActor.K2_GetRootComponent());
		//PopulationActor.SetRootComponent(FoliageInstMeshComp.Get());
	}
}

void FITwinFoliageComponentHolder::BeginPlay(AITwinPopulation& PopulationActor)
{
	InitFoliageMeshComponent(PopulationActor);

	if (FoliageInstMeshComp && MasterMesh)
	{
		FoliageInstMeshComp->RegisterComponent();

		// Set the mesh (the movable mobility is needed to avoid a warning
		// when playing in the editor).
		FoliageInstMeshComp->SetMobility(EComponentMobility::Movable);
		FoliageInstMeshComp->SetStaticMesh(MasterMesh.Get());
		FoliageInstMeshComp->SetMobility(EComponentMobility::Static);

		FoliageInstMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FoliageInstMeshComp->SetEnableGravity(false);

		// Disable AO to get a better framerate (the unreal editor disables it
		// when instantiating a mesh with foliage)
		FoliageInstMeshComp->bAffectDistanceFieldLighting = false;
	}
}

int32 FITwinFoliageComponentHolder::GetInstanceCount() const
{
	return FoliageInstMeshComp ? FoliageInstMeshComp->GetInstanceCount() : 0;
}

FBox FITwinFoliageComponentHolder::GetMasterMeshBoundingBox() const
{
	if (FoliageInstMeshComp && FoliageInstMeshComp->GetStaticMesh())
	{
		return FoliageInstMeshComp->GetStaticMesh()->GetBoundingBox();
	}
	else
	{
		return FBox(); // return an invalid box
	}
}

FBoxSphereBounds FITwinFoliageComponentHolder::GetMasterMeshBounds() const
{
	if (MasterMesh)
	{
		return MasterMesh->GetBounds();
	}
	else
	{
		return {};
	}
}



//---------------------------------------------------------------------------------------
// class AITwinPopulation
//---------------------------------------------------------------------------------------

struct AITwinPopulation::FImpl
{
	std::shared_ptr<AdvViz::SDK::IInstancesManager> instancesManager_;
	std::shared_ptr<AdvViz::SDK::IInstancesGroup> instancesGroup_; // the group to which this population belongs
};

/* static */
AITwinPopulation* AITwinPopulation::CreatePopulation(const UObject* WorldContextObject, const FString& AssetPath,
	AVizInstancesManagerPtr const& AvizInstanceManager,
	AVizInstancesGroupPtr const& AvizInstanceGroup)
{
	// Spawn a new actor with a deferred call in order to be able
	// to set the static mesh before BeginPlay is called.
	FTransform spawnTransform;
	AActor* newActor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
		WorldContextObject, AITwinPopulation::StaticClass(), spawnTransform,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	AITwinPopulation* population = Cast<AITwinPopulation>(newActor);

	if (!population)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
	if (Mesh)
	{
		FITwinFoliageComponentHolder& FoliageHolder = population->FoliageComponents.AddDefaulted_GetRef();
		FoliageHolder.InitWithMasterMesh(*population, Mesh);
	}
	else
	{
		// We now support Blueprint format, to handle groups of meshes (introduced to fix Nanite limitations,
		// as translucent materials cannot be rendered with Nanite, so we separate the opaque mesh parts from
		// the translucent ones, and save them as a blueprint).
		FString BPLoadPath = FString::Printf(TEXT("Blueprint'%s.%s_C'"), *AssetPath, *FPaths::GetPathLeaf(AssetPath));

		// from https://dev.epicgames.com/community/snippets/d5R/load-spawn-blueprint-actor-asset-from-c-w-o-prev-ref?locale=pt-br
		TSoftClassPtr<AActor> ActorBpClass = TSoftClassPtr<AActor>(FSoftObjectPath(BPLoadPath));

		// The actual loading.
		UClass* LoadedBpAsset = ActorBpClass.LoadSynchronous();
		if (!LoadedBpAsset)
		{
			return nullptr;
		}

		// (Optional, depends on how you continue using it)
		// Make sure GC doesn't steal it away from us, again
		LoadedBpAsset->AddToRoot();

		// From here on, it's business as usual, common actor spawning, just using the BP asset we loaded
		// above.
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FActorSpawnParameters SpawnParams = FActorSpawnParameters();
		AActor* BP_Actor = WorldContextObject->GetWorld()->SpawnActor(LoadedBpAsset, &Loc, &Rot, SpawnParams);
		TArray<UActorComponent*> BP_Meshes = BP_Actor->K2_GetComponentsByClass(UStaticMeshComponent::StaticClass());

		// Create foliage components from loaded meshes:
		for (int32 i = 0; i < BP_Meshes.Num(); ++i)
		{
			UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(BP_Meshes[i]);
			FITwinFoliageComponentHolder& FoliageHolder = population->FoliageComponents.AddDefaulted_GetRef();
			FoliageHolder.InitWithMasterMesh(*population, MeshComp->GetStaticMesh().Get());
		}
	}

	UGameplayStatics::FinishSpawningActor(newActor, spawnTransform);

	population->SetInstancesManager(AvizInstanceManager);
	population->SetInstancesGroup(AvizInstanceGroup);
	population->SetObjectRef(TCHAR_TO_UTF8(*AssetPath));

	return population;
}

AITwinPopulation::AITwinPopulation()
	: Impl(MakePimpl<FImpl>())
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	SquareSideLength = 100000;
}

inline bool AITwinPopulation::CheckInstanceCount() const
{
	int32 InstCount_UE = -1;
	for (auto const& FoliageComp : FoliageComponents)
	{
		ensure(FoliageComp.FoliageInstMeshComp);
		if (!ensureMsgf(InstCount_UE == -1 ||
			InstCount_UE == FoliageComp.GetInstanceCount(),
			TEXT("All foliage components should have the same number of instances.")))
		{
			return false;
		}
		InstCount_UE = FoliageComp.GetInstanceCount();
	}

	if (Impl->instancesManager_)
	{
		uint64_t InstCountUE_Aviz = Impl->instancesManager_->GetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (!ensureMsgf((int32)InstCountUE_Aviz == InstCount_UE,
			TEXT("The UE and AdvViz::SDK population should have the same number of instances.")))
		{
			return false;
		}
	}

	return true;
}

FBox AITwinPopulation::GetMasterMeshBoundingBox() const
{
	FBox Box;
	for (auto const& FoliageComp : FoliageComponents)
	{
		Box += FoliageComp.GetMasterMeshBoundingBox();
	}
	return Box;
}

FBoxSphereBounds AITwinPopulation::GetMasterMeshBounds() const
{
	FBoxSphereBounds Bounds;
	for (auto const& FoliageComp : FoliageComponents)
	{
		Bounds = Bounds + FoliageComp.GetMasterMeshBounds();
	}
	return Bounds;
}

namespace
{
	struct UnrealInstanceInfo
	{
		FString name;
		FTransform transform;
		FVector colorShift;
	};

	const float convRGBToDouble = 0.003921568627450980; // 1/255

	void UpdateAVizInstanceTransform(
		AdvViz::SDK::IInstance& dstInstance, const FTransform& srcInstanceTransform)
	{
		const AdvViz::SDK::dmat3x4 m(FITwinMathConversion::UEtoSDK(srcInstanceTransform)); //LC: don't know exactly why but using a temporary variable removes warning C4686: 'FITwinMathConversion::UEtoSDK': possible change in behavior, change in UDT return calling convention 
		dstInstance.SetTransform(m);
	}

	void UpdateAVizInstanceColorShift(
		AdvViz::SDK::IInstance& dstInstance, const FVector& srcColorShift)
	{
		AdvViz::SDK::float3 color = { (srcColorShift.X + 0.5), (srcColorShift.Y + 0.5), (srcColorShift.Z + 0.5) };
		dstInstance.SetColorShift(color);
	}

	void UpdateAVizInstance(
		AdvViz::SDK::IInstance& dstInstance, const UnrealInstanceInfo& srcInstance)
	{
		UpdateAVizInstanceTransform(dstInstance, srcInstance.transform);
		UpdateAVizInstanceColorShift(dstInstance, srcInstance.colorShift);
		dstInstance.SetName(TCHAR_TO_UTF8(*srcInstance.name));
	}

	bool checkVersion = true;
	bool isOldVersion = false;

	void UpdateUnrealInstance(
		UnrealInstanceInfo& dstInstance, /*const*/ AdvViz::SDK::IInstance& srcInstance)
	{
		using namespace AdvViz::SDK;
		// Temporary code for beta users:
		//   In earlier versions of the SDK and ITwinForUnreal, the transformation of instances
		//   used 4x3 matrices, which didn't follow the convention specified in the decoration
		//   service, using 3x4 matrices. This case is detected below by testing 2 values of the
		//   matrix: if they are greater than 100, it is very probably a translation value because
		//   the scale shouldn't vary much around 1. Then the matrix is fixed and the instance is
		//   marked to be resaved (when the user will close the scene). When removing this code
		//   later, the const attribute for srcInstance in the declaration should be restored.
		const dmat3x4& srcMat = srcInstance.GetTransform();
		if (checkVersion)
		{
			isOldVersion = fabs(ColRow3x4(srcMat, 2, 1)) > 100. || fabs(ColRow3x4(srcMat, 2, 2)) > 100.;
			checkVersion = false;
		}

		if (isOldVersion)
		{
			dmat3x4 newSrcMat;
			ColRow3x4(newSrcMat,0, 0) = ColRow3x4(srcMat,0, 0);
			ColRow3x4(newSrcMat,1, 0) = ColRow3x4(srcMat,0, 1);
			ColRow3x4(newSrcMat,2, 0) = ColRow3x4(srcMat,0, 2);
			ColRow3x4(newSrcMat,0, 1) = ColRow3x4(srcMat,0, 3);
			ColRow3x4(newSrcMat,1, 1) = ColRow3x4(srcMat,1, 0);
			ColRow3x4(newSrcMat,2, 1) = ColRow3x4(srcMat,1, 1);
			ColRow3x4(newSrcMat,0, 2) = ColRow3x4(srcMat,1, 2);
			ColRow3x4(newSrcMat,1, 2) = ColRow3x4(srcMat,1, 3);
			ColRow3x4(newSrcMat,2, 2) = ColRow3x4(srcMat,2, 0);
			ColRow3x4(newSrcMat,0, 3) = ColRow3x4(srcMat,2, 1);
			ColRow3x4(newSrcMat,1, 3) = ColRow3x4(srcMat,2, 2);
			ColRow3x4(newSrcMat,2, 3) = ColRow3x4(srcMat,2, 3);

			srcInstance.SetTransform(newSrcMat);
			srcInstance.SetShouldSave(true);
		}

		dstInstance.transform =	FITwinMathConversion::SDKtoUE(srcMat);
		
		if (srcInstance.GetColorShift().has_value())
		{
			AdvViz::SDK::float3 color = srcInstance.GetColorShift().value();
			dstInstance.colorShift.X = color[0] - 0.5;
			dstInstance.colorShift.Y = color[1] - 0.5;
			dstInstance.colorShift.Z = color[2] - 0.5;
		}

		dstInstance.name = FString(srcInstance.GetName().c_str());
	}
}


std::shared_ptr<AdvViz::SDK::IInstance> AITwinPopulation::GetAVizInstance(int32 instanceIndex)
{
	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	if (instanceIndex >= 0 && instanceIndex < instances.size())
	{
		return instances[instanceIndex];
	}
	return std::shared_ptr<AdvViz::SDK::IInstance>();
}

bool AITwinPopulation::ToggleAutoRebuildTree(std::optional<bool> const& bSuspendAutoRebuildOpt /*= std::nullopt*/)
{
	bool bPreviousValue = false;
	bool bNewValue = false;
	if (!FoliageComponents.IsEmpty())
	{
		bPreviousValue = FoliageComponents[0].FoliageInstMeshComp->bAutoRebuildTreeOnInstanceChanges;
		bNewValue = bSuspendAutoRebuildOpt.value_or(!bPreviousValue);
	}
	for (auto& FoliageComp : FoliageComponents)
	{
		// All foliage components should have the same value...
		BE_ASSERT(bPreviousValue == FoliageComp.FoliageInstMeshComp->bAutoRebuildTreeOnInstanceChanges);

		FoliageComp.FoliageInstMeshComp->bAutoRebuildTreeOnInstanceChanges = bNewValue;

		if (bNewValue)
		{
			// When re-enabling automatic rebuild, we should also invalidate tree if needed.
			FoliageComp.FoliageInstMeshComp->BuildTreeIfOutdated(/*Async*/true, /*ForceUpdate*/false);
		}
	}
	return bPreviousValue;
}

AITwinPopulation::FAutoRebuildTreeDisabler::FAutoRebuildTreeDisabler(AITwinPopulation& InPopulation)
	: Population(&InPopulation)
{
	bAutoRebuildTreeOnInstanceChanges_Old = InPopulation.ToggleAutoRebuildTree(false);
}

AITwinPopulation::FAutoRebuildTreeDisabler::~FAutoRebuildTreeDisabler()
{
	if (Population.IsValid())
	{
		Population->ToggleAutoRebuildTree(bAutoRebuildTreeOnInstanceChanges_Old);
	}
}


FTransform AITwinPopulation::GetInstanceTransform(int32 instanceIndex) const
{
	FTransform instTM;

	if (instanceIndex >= 0 && instanceIndex < GetNumberOfInstances())
	{
		FoliageComponents[0].FoliageInstMeshComp->GetInstanceTransform(instanceIndex, instTM, true);
	}

	return instTM;
}

void AITwinPopulation::SetInstanceTransform(int32 instanceIndex, const FTransform& tm)
{
	if (SetInstanceTransformUEOnly(instanceIndex, tm))
	{
		if (IsClippingPrimitive())
		{
			// Notify the Clipping Tool.
			auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(GetWorld());
			if (ensure(ClippingActor))
			{
				ClippingActor->OnClippingInstanceModified(objectType, instanceIndex);
			}
		}
		
		const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (instanceIndex < instances.size())
		{
			AdvViz::SDK::IInstance& inst = *instances[instanceIndex]; 
			UpdateAVizInstanceTransform(inst, tm);
			inst.SetShouldSave(true);
		}
	}
}

void AITwinPopulation::MarkFoliageRenderStateDirty()
{
	for (auto& FoliageComp : FoliageComponents)
	{
		FoliageComp.FoliageInstMeshComp->MarkRenderStateDirty();
	}
}

FBox AITwinPopulation::GetInstanceBoundingBox(int32 instanceIndex) const
{
	FBox Box;
	if (ensure(instanceIndex >= 0 && instanceIndex < GetNumberOfInstances()))
	{
		const FTransform InstTransform = GetInstanceTransform(instanceIndex);
		for (auto const& FoliageComp : FoliageComponents)
		{
			if (FoliageComp.FoliageInstMeshComp)
			{
				Box += FoliageComp.GetMasterMeshBoundingBox().TransformBy(InstTransform);
			}
		}
	}
	return Box;
}

FVector AITwinPopulation::GetInstanceColorVariation(int32 instanceIndex) const
{
	FVector instColVar(0.5);

	if (instanceIndex >= 0 && instanceIndex < GetNumberOfInstances())
	{
		float const* colorVar = nullptr;
		auto const* meshComp(FoliageComponents[0].FoliageInstMeshComp.Get());
		if (meshComp->NumCustomDataFloats == 3)
		{
			colorVar = &meshComp->PerInstanceSMCustomData[instanceIndex*3];
		}
		if (colorVar)
		{
			instColVar.X = colorVar[0];
			instColVar.Y = colorVar[1];
			instColVar.Z = colorVar[2];
		}
	}

	return instColVar;
}

void AITwinPopulation::SetInstanceColorVariation(int32 instanceIndex, const FVector& v)
{
	if (SetInstanceColorVariationUEOnly(instanceIndex, v))
	{
		const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (instanceIndex < instances.size())
		{
			AdvViz::SDK::IInstance& inst = *instances[instanceIndex]; 
			UpdateAVizInstanceColorShift(inst, v);
			inst.SetShouldSave(true);
		}
	}
}

AdvViz::SDK::RefID AITwinPopulation::GetInstanceRefId(int32 instanceIndex) const
{
	if (instanceIndex >= 0 && instanceIndex < GetNumberOfInstances())
	{
		const AdvViz::SDK::SharedInstVect& instances =
			Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (ensure(instanceIndex < instances.size()))
		{
			AdvViz::SDK::IInstance const& inst = *instances[instanceIndex];
			return inst.GetRefId();
		}
	}
	return AdvViz::SDK::RefID::Invalid();
}

int32 AITwinPopulation::GetInstanceIndexFromRefId(const AdvViz::SDK::RefID& refId) const
{
	if (Impl->instancesManager_ && Impl->instancesGroup_)
	{
		const AdvViz::SDK::SharedInstVect& instances =
			Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		auto it = std::find_if(instances.begin(), instances.end(),
			[&refId](auto&& inst) { return inst->GetRefId() == refId; });
		if (it != instances.end())
		{
			return static_cast<int32>(std::distance(instances.begin(), it));
		}
	}
	return INDEX_NONE;
}

namespace
{
	struct RgbColor
	{
		int r,g,b;
		float metallic;
	};

	std::array<RgbColor, 125> carColors // from LumenRT
	{{
		{0, 0, 0,0.0}, {0, 0, 0,0.0}, {0, 0, 0,0.0}, {0, 0, 0,0.0}, {0, 0, 0,0.0}, {0, 0, 0,0.0}, {102, 86, 81,0.0},
		{12, 12, 12,1.0}, {12, 12, 12,1.0}, {12, 12, 12,1.0}, {35, 35, 35,1.0}, {35, 35, 35,1.0}, {255, 221, 188,0.0},
		{35, 35, 35,1.0}, {150, 150, 150,0.0}, {150, 150, 150,0.0}, {150, 150, 150,0.0}, {255, 216, 178,0.0},
		{150, 150, 150,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {239, 219, 229,0.0},
		{105, 105, 105,0.0}, {105, 105, 105,0.0}, {14, 10, 6,0.0}, {14, 10, 6,0.0}, {14, 10, 6,0.0}, {65, 24, 10,0.0},
		{229, 229, 229,0.0}, {229, 229, 229,0.0}, {229, 229, 229,0.0}, {229, 229, 229,0.0}, {132, 87, 71, 0.0},
		{58, 58, 58,0.0}, {58, 58, 58,0.0}, {58, 58, 58,0.0}, {58, 58, 58,0.0}, {47, 42, 28,0.0}, {4, 21, 5,0.0},
		{91, 102, 81,0.0},{150, 150, 150,0.0}, {150, 150, 150,0.0}, {150, 150, 150,0.0}, {20, 10, 0,0.0},
		{105, 105, 105,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {204, 153, 0,0.0},
		{105, 105, 105,0.0}, {14, 10, 6,0.0}, {14, 10, 6,0.0}, {91, 102, 81,0.0}, {7, 17, 13,0.0}, {204, 183, 163,0.0},
		{12, 12, 12,1.0}, {12, 12, 12,1.0}, {35, 35, 35,1.0}, {35, 35, 35,1.0}, {35, 35, 35,1.0}, {255, 152, 50,0.0},
		{150, 150, 150,0.0}, {150, 150, 150,0.0}, {150, 150, 150,0.0}, {150, 150, 150,0.0}, {123, 83, 49,0.0},
		{105, 105, 105,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {105, 105, 105,0.0}, {30, 11, 0,0.0},
		{105, 105, 105,0.0}, {14, 10, 6,0.0}, {14, 10, 6,0.0}, {12, 12, 12,1.0}, {12, 12, 12,1.0}, {204, 183, 122,0.0},
		{12, 12, 12,1.0}, {35, 35, 35,1.0}, {35, 35, 35,1.0}, {35, 35, 35,1.0}, {150, 150, 150,0.0}, {102, 81, 81,0.0},
		{150, 150, 150,0.0}, {150, 150, 150,0.0}, {105, 105, 105,0.0}, 	{105, 105, 105,0.0}, {228, 218, 194,0.0},
		{30, 10, 30, 0.0}, {14, 10, 6,0.0}, {14, 10, 6,0.0}, {14, 10, 6, 0.0}, {229, 229, 229, 0.0}, {209, 228, 194,0.0},
		{229, 229, 229,0.0}, {229, 229, 229,0.0}, {229, 229, 229,0.0}, {58, 58, 58,0.0}, {68, 7, 7,0.0}, {19, 2, 0,0.0},
		{58, 58, 58,0.0}, {58, 58, 58,0.0}, {58, 58, 58,0.0}, {47, 42, 28,0.0}, {91, 102, 81,0.0}, {51, 0, 0,0.0},
		{91, 102, 81,0.0}, {7, 17, 13,0.0}, {4, 22, 56,0.0}, {60, 94, 133,0.0}, {33, 49, 79,0.0}, {127, 0, 1,0.0},
		{6, 47, 105,0.0}, {7, 10, 33,0.0}, {9, 9, 15,0.0}, {40, 102, 102,0.0}, {126, 160, 177,0.0}, {127, 0, 1,0.0},
		{28, 20, 12,0.0}, {28, 20, 12,0.0}, {51, 106, 56,0.0}, {15, 50, 31,0.0}, {102, 0, 0,0.0}
	}};
}

/*static*/ FVector AITwinPopulation::GetRandomColorShift(const EITwinInstantiatedObjectType type)
{
	FVector colorShift(0.0, 0.0, 0.0);
	if (type == EITwinInstantiatedObjectType::Vehicle)
	{
		int32 index = FMath::RandRange((int32)0, (int32)(carColors.size() - 1));
		const RgbColor& color = carColors[index];
		colorShift.X = color.r * convRGBToDouble - 0.5;
		colorShift.Y = color.g * convRGBToDouble - 0.5;
		colorShift.Z = color.b * convRGBToDouble - 0.5;
	}
	else if (type == EITwinInstantiatedObjectType::Vegetation)
	{
	}
	else
	{
		colorShift.X = FMath::RandRange(-0.5, 0.5);
		colorShift.Y = FMath::RandRange(-0.5, 0.5);
		colorShift.Z = FMath::RandRange(-0.5, 0.5);
	}
	return colorShift;
}

void AITwinPopulation::FinalizeAddedInstance(int32 instIndex, const FTransform* FinalTransform /*= nullptr*/,
	const AdvViz::SDK::RefID* EnforcedRefID /*= nullptr*/)
{
	if (EnforcedRefID
		&& ensureMsgf(GetInstanceIndexFromRefId(*EnforcedRefID) == INDEX_NONE,
					TEXT("cannot have duplicated ref ID!")))
	{
		auto AVizInst = GetAVizInstance(instIndex);
		if (ensure(AVizInst))
			AVizInst->SetRefId(*EnforcedRefID);
	}
	if (IsClippingPrimitive())
	{
		// Notify the Clipping Tool.
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(GetWorld());
		if (ensure(ClippingActor))
		{
			ClippingActor->OnClippingInstanceAdded(this, objectType, instIndex);
		}
	}
	if (FinalTransform)
	{
		SetInstanceTransform(instIndex, *FinalTransform);
	}
}

int32 AITwinPopulation::AddInstance(const FTransform& transform, bool bInteractivePlacement /*= false*/)
{
	// This function is used for the manual addition of a single instance.
	// The current position will be used later for the automatic filling of
	// a square with instances if the user changes their number. This is 
	// temporary for testing, before we have a better manner to do this...
	SquareCenter = transform.GetLocation();

	// Create a local UnrealInstanceInfo
	UnrealInstanceInfo ueInstanceInfo;
	ueInstanceInfo.transform = BaseTransform * transform;
	ueInstanceInfo.colorShift = GetRandomColorShift(objectType);

	// Add an UE instance and apply the transform and color shift
	int32 instIndex = INDEX_NONE;
	for (auto& FoliageComp : FoliageComponents)
	{
		auto* meshComp(FoliageComp.FoliageInstMeshComp.Get());
		int32 instIndexInUE = meshComp->AddInstance(ueInstanceInfo.transform, false);
		ensure(instIndex == INDEX_NONE || instIndex == instIndexInUE);
		instIndex = instIndexInUE;

		if (meshComp->NumCustomDataFloats != 3)
		{
			meshComp->SetNumCustomDataFloats(3);
		}
		meshComp->SetCustomDataValue(instIndex, 0, ueInstanceInfo.colorShift.X);
		meshComp->SetCustomDataValue(instIndex, 1, ueInstanceInfo.colorShift.Y);
		meshComp->SetCustomDataValue(instIndex, 2, ueInstanceInfo.colorShift.Z);
	}
	if (instIndex == INDEX_NONE)
	{
		ensureMsgf(false, TEXT("no instance added"));
		return INDEX_NONE;
	}

	if (IsClippingPrimitive() && !bInteractivePlacement)
	{
		// Perform additional operations for the clipping tool.
		FinalizeAddedInstance(instIndex);
	}

	// Add the same instance in the manager of the SDK core
	uint64_t instCount = Impl->instancesManager_->GetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	Impl->instancesManager_->SetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId(), instCount + 1);
	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	AdvViz::SDK::IInstance& instance = *instances[instCount];
	UpdateAVizInstance(instance, ueInstanceInfo);
	instance.SetName("inst");
	instance.SetObjectRef(objectRef);
	instance.SetGroup(Impl->instancesGroup_);

	BE_ASSERT(CheckInstanceCount());

	return instIndex;
}

void AITwinPopulation::RemoveInstance(int32 instIndex)
{
	AITwinClippingTool* ClippingActor = IsClippingPrimitive()
		? TWorldSingleton<AITwinClippingTool>().Get(GetWorld()) : nullptr;
	if (ClippingActor)
	{
		// 2 notifications are needed in some cases: *before* and *after* the actual removal:
		// - before the event so that we can notify anyone with valid RefID
		// - after the event to let the cutout manager reconstruct its list of cutouts.
		ClippingActor->BeforeRemoveClippingInstances(objectType, { instIndex });
	}

	bool bValidIndex = false;
	if (instIndex >= 0)
	{
		for (auto& FoliageComp : FoliageComponents)
		{
			if (instIndex < FoliageComp.GetInstanceCount())
			{
				FoliageComp.FoliageInstMeshComp->RemoveInstance(instIndex);
				bValidIndex = true;
			}
		}
	}

	if (!bValidIndex)
		return;

	if (ClippingActor)
	{
		// Second notification for the Clipping Tool.
		ClippingActor->OnClippingInstancesRemoved(objectType, { instIndex });
	}

	std::vector<int32_t> indices;
	indices.push_back(instIndex);
	Impl->instancesManager_->RemoveInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId(), indices);

	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	for (size_t i = instIndex; i < instances.size(); ++i)
	{
		AdvViz::SDK::IInstance* inst = instances[i].get();
		if (FITwinInstance* ueInst = AdvViz::SDK::Tools::DynamicCast<FITwinInstance>(inst))
		{
			ueInst->population_ = this;
			ueInst->instanceIndex_ = i;
		}
		if (auto animPathExt = inst->GetExtension<InstanceWithSplinePathExt>())
		{
			animPathExt->Population_ = this;
			animPathExt->InstanceIdx_ = i;
		}
	}

	BE_ASSERT(CheckInstanceCount());
}

void AITwinPopulation::RemoveInstances(const TArray<int32>& instanceIndices)
{
	if (instanceIndices.IsEmpty())
		return;

	AITwinClippingTool* ClippingActor = IsClippingPrimitive()
		? TWorldSingleton<AITwinClippingTool>().Get(GetWorld()) : nullptr;
	if (ClippingActor)
	{
		// Same remark as for #RemoveInstance: two-step notification
		ClippingActor->BeforeRemoveClippingInstances(objectType, instanceIndices);
	}

	for (auto& FoliageComp : FoliageComponents)
	{
		FoliageComp.FoliageInstMeshComp->RemoveInstances(instanceIndices, true);
	}

	if (ClippingActor)
	{
		// Second notification for the Clipping Tool.
		ClippingActor->OnClippingInstancesRemoved(objectType, instanceIndices);
	}

	std::vector<int32_t> indices;
	indices.reserve((size_t)instanceIndices.Num());
	for (auto const& ind : instanceIndices)
	{
		indices.push_back(ind);
	}
	Impl->instancesManager_->RemoveInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId(), indices);

	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	for (size_t i = instanceIndices[0]; i < instances.size(); ++i)
	{
		AdvViz::SDK::IInstance *inst = instances[i].get();
		if (FITwinInstance* ueInst = AdvViz::SDK::Tools::DynamicCast<FITwinInstance>(inst))
		{
			ueInst->population_ = this;
			ueInst->instanceIndex_ = i;
		}
		if (auto animPathExt = inst->GetExtension<InstanceWithSplinePathExt>())
		{
			animPathExt->Population_ = this;
			animPathExt->InstanceIdx_ = i;
		}
	}

	BE_ASSERT(CheckInstanceCount());
}

void AITwinPopulation::OnInstanceRestored(const AdvViz::SDK::RefID& restoredID)
{
	if (Impl->instancesManager_ && Impl->instancesGroup_)
	{
		Impl->instancesManager_->OnInstancesRestored(objectRef, Impl->instancesGroup_->GetId(),
			{ restoredID });
	}
}

void AITwinPopulation::UpdateInstancesFromAVizToUE()
{
	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());

	size_t numInst = instances.size(); 
	TArray<FTransform> instancesTM;
	instancesTM.SetNum(numInst);
	TArray<float> instancesColorVar;
	instancesColorVar.SetNum(numInst*3);

	checkVersion = true;

	for (size_t i = 0; i < numInst; ++i)
	{
		AdvViz::SDK::IInstance* inst = instances[i].get();
		
		if (FITwinInstance* ueInst = AdvViz::SDK::Tools::DynamicCast<FITwinInstance>(inst))
		{
			ueInst->population_ = this;
			ueInst->instanceIndex_ = i;
		}

		UnrealInstanceInfo ueInstInfo;

		UpdateUnrealInstance(ueInstInfo, *inst);
		instancesTM[i] = ueInstInfo.transform;

		instancesColorVar[i*3] = ueInstInfo.colorShift.X;
		instancesColorVar[i*3 + 1] = ueInstInfo.colorShift.Y;
		instancesColorVar[i*3 + 2] = ueInstInfo.colorShift.Z;
	}

	for (auto& FoliageComp : FoliageComponents)
	{
		auto* meshComp(FoliageComp.FoliageInstMeshComp.Get());

		meshComp->AddInstances(instancesTM, false);

		// Set the custom data for color variations
		if (meshComp->NumCustomDataFloats != 3)
		{
			meshComp->SetNumCustomDataFloats(3);
		}
		for (size_t i = 0; i < numInst; ++i)
		{
			meshComp->SetCustomData(i, MakeArrayView(&instancesColorVar[i*3], 3), true);
		}

		// Clear the selection to avoid an UE cash when removing instances
		meshComp->ClearInstanceSelection();
	}

	if (GetExtension<FITwinPopulationWithPathExt>())
	{
		SetActorTickEnabled(true);
	}
	else
	{
		SetActorTickEnabled(false);
	}

	if (IsClippingPrimitive())
	{
		// Notify the Clipping Tool.
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(GetWorld());
		if (ensure(ClippingActor))
		{
			ClippingActor->OnClippingInstancesLoaded(this, objectType);
		}
	}

	BE_ASSERT(CheckInstanceCount());
}

std::shared_ptr<AdvViz::SDK::IInstancesManager>& AITwinPopulation::GetInstanceManager()
{
	return Impl->instancesManager_;
}

void AITwinPopulation::SetInstancesManager(AVizInstancesManagerPtr const& instManager)
{
	Impl->instancesManager_ = instManager;
}

std::shared_ptr<AdvViz::SDK::IInstancesGroup>& AITwinPopulation::GetInstancesGroup()
{
	return Impl->instancesGroup_;
}

void AITwinPopulation::SetInstancesGroup(AVizInstancesGroupPtr const& instGroup)
{
	Impl->instancesGroup_ = instGroup;
}

void AITwinPopulation::SetObjectRef(const std::string& objRef)
{
	objectRef = objRef;

	if (objRef.find("Character") != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::Character;
	}
	else if (objRef.find("Vehicle") != std::string::npos ||
			 objRef.find("Construction") != std::string::npos)
	{
		if (objRef.find("Crane") != std::string::npos)
		{
			objectType = EITwinInstantiatedObjectType::Crane;
		}
		else
		{
			objectType = EITwinInstantiatedObjectType::Vehicle;

			// For vehicles, color variations are fully applied, as they were
			// selected to be credible (we don't want intermediate values).
			SetColorVariationIntensity(1.f);
		}
	}
	else if (objRef.find("Vegetation") != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::Vegetation;
	}
	else if (objRef.find("ClippingPlane") != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::ClippingPlane;
	}
	else if (objRef.find("ClippingBox") != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::ClippingBox;
		// The cube imported for this tool has a side of one meter, which is quite small for an
		// infrastructure project. Increase its size to start seeing something.
		BaseTransform.MultiplyScale3D(FVector(10.0));
	}

#if WITH_EDITOR
	// Update name for Editor
	std::string shortName = objRef;
	auto const sepPos = objRef.find_last_of("/\\");
	if (sepPos != std::string::npos)
	{
		auto const nameStart = sepPos + 1;
		auto const dotPos = objRef.find('.', nameStart + 1);
		std::string::size_type shortNameSize = std::string::npos;
		if (dotPos != std::string::npos)
		{
			shortNameSize = dotPos - nameStart;
		}
		shortName = objRef.substr(nameStart, shortNameSize);
	}
	SetActorLabel(UTF8_TO_TCHAR(shortName.c_str()));
#endif
}

const std::string& AITwinPopulation::GetObjectRef() const
{
	return objectRef;
}

const AdvViz::SDK::RefID& AITwinPopulation::GetInstanceGroupId() const
{
	return Impl->instancesGroup_->GetId();
}

bool AITwinPopulation::IsRotationVariationEnabled() const
{
	return objectType == EITwinInstantiatedObjectType::Vegetation ||
		   objectType == EITwinInstantiatedObjectType::Character;
}

bool AITwinPopulation::IsScaleVariationEnabled() const
{
	return objectType == EITwinInstantiatedObjectType::Vegetation;
}

bool AITwinPopulation::IsPerpendicularToSurface() const
{
	return objectType == EITwinInstantiatedObjectType::Vehicle;
}

FString AITwinPopulation::GetObjectTypeName() const
{
	switch (objectType)
	{
	case EITwinInstantiatedObjectType::Vehicle:			return TEXT("vehicle");
	case EITwinInstantiatedObjectType::Vegetation:		return TEXT("vegetation");
	case EITwinInstantiatedObjectType::Character:		return TEXT("character");
	case EITwinInstantiatedObjectType::ClippingPlane:	return TEXT("plane");
	case EITwinInstantiatedObjectType::ClippingBox:		return TEXT("cube");
	case EITwinInstantiatedObjectType::Crane:			return TEXT("crane");
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH
	case EITwinInstantiatedObjectType::Other:			return TEXT("object");
	}
}

float AITwinPopulation::GetColorVariationIntensity() const
{
	UFoliageInstancedStaticMeshComponent const* meshComp = FoliageComponents.IsEmpty()
		? nullptr
		: FoliageComponents[0].FoliageInstMeshComp.Get();
	int32 const NumMats = meshComp ? meshComp->GetNumMaterials() : 0;
	for (int32 i = 0; i < NumMats; ++i)
	{
		UMaterialInterface const* mat = meshComp->GetMaterial(i);
		UMaterialInstance const* matInst = dynamic_cast<UMaterialInstance const*>(mat);
		if (matInst) // editable parameters are only in material instances
		{
			float colorVariationIntensity = 0.f;
			FHashedMaterialParameterInfo paramInfo(FName("ColorVariationIntensity"));
			if (matInst->GetScalarParameterValue(paramInfo, colorVariationIntensity))
			{
				return colorVariationIntensity;
			}
		}
	}

	return 0.f;
}

void AITwinPopulation::SetColorVariationIntensity(const float& f)
{
	for (auto const& FoliageComp : FoliageComponents)
	{
		auto* meshComp(FoliageComp.FoliageInstMeshComp.Get());
		int32 const NumMats = meshComp->GetNumMaterials();
		for (int32 i = 0; i < NumMats; ++i)
		{
			UMaterialInterface* mat = meshComp->GetMaterial(i);
			UMaterialInstance* matInst = dynamic_cast<UMaterialInstance*>(mat);

			if (matInst) // editable parameters are only in material instances
			{
				UMaterialInstanceDynamic* mtlInstDyn = dynamic_cast<UMaterialInstanceDynamic*>(mat);
				if (!mtlInstDyn)
				{
					mtlInstDyn = meshComp->CreateDynamicMaterialInstance(i);
				}
				if (mtlInstDyn)
				{
					mtlInstDyn->SetScalarParameterValue(FName("ColorVariationIntensity"), f);
				}
			}
		}
	}
}

int32 AITwinPopulation::GetNumberOfInstances() const
{
	if (FoliageComponents.IsEmpty())
	{
		return 0;
	}
	else
	{
		return FoliageComponents[0].GetInstanceCount();
	}
}

void AITwinPopulation::SetNumberOfInstances(const int32& NewInstanceCount)
{
	if (!ensure(FoliageComponents.Num() > 0))
		return;
	const int32 DiffInstances = NewInstanceCount - GetNumberOfInstances();
	if (DiffInstances > 0)
	{
		AddInstances(DiffInstances);
	}
	else if (DiffInstances < 0)
	{
		RemoveInstances(-DiffInstances);
	}
}

int32 AITwinPopulation::GetSquareSideLength() const
{
	return SquareSideLength;
}

void AITwinPopulation::SetSquareSideLength(const int32& n)
{
	SquareSideLength = n;
}

void AITwinPopulation::SetInstancesZCoordinate(const float& maxDistToSquareCenter, const float& z)
{
	if (!ensure(FoliageComponents.Num() > 0))
		return;
	auto const& FoliageComp0 = FoliageComponents[0];
	const int32 totalNumInstances = FoliageComp0.GetInstanceCount();

	for (int32 i = 0; i < totalNumInstances; ++i)
	{
		FTransform tm;
		FoliageComp0.FoliageInstMeshComp->GetInstanceTransform(i, tm);
		FMatrix mat = tm.ToMatrixWithScale();
		FVector pos = tm.GetTranslation();

		pos.Z = SquareCenter.Z;
		FVector instToCenter = SquareCenter - pos;
		if (instToCenter.Length() < maxDistToSquareCenter)
		{
			pos.Z = z;
			tm.SetTranslation(pos);
			SetInstanceTransform(i, tm);
		}
	}
}

void AITwinPopulation::BeginPlay()
{
	Super::BeginPlay();

	for (auto& FoliageComp : FoliageComponents)
	{
		FoliageComp.BeginPlay(*this);
	}

	if (FoliageComponents.Num() > 0 && InitialNumberOfInstances > 0)
	{
		AddInstances(InitialNumberOfInstances);
	}
}

void AITwinPopulation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Impl->instancesManager_.reset();
	Impl->instancesGroup_.reset();
}

void AITwinPopulation::AddInstances(int32 numInst)
{
	if (numInst <= 0)
		return;
	if (FoliageComponents.IsEmpty())
		return;

	double halfLength = static_cast<double>(SquareSideLength)*0.5;
	FVector initialPos = SquareCenter - FVector(halfLength, halfLength, 0.);

	TArray<FTransform> instancesTM;
	instancesTM.SetNum(numInst);
	TArray<float> instancesColorVar;
	instancesColorVar.SetNum(numInst*3);
	int32 oldNumInst = GetNumberOfInstances();
	static bool intersectWorld = true;

	const bool bSyncWithAdvVizSDK = (bool)Impl->instancesManager_;
	static const AdvViz::SDK::SharedInstVect NoSDKInstances;
	if (bSyncWithAdvVizSDK)
	{
		Impl->instancesManager_->SetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId(), oldNumInst + numInst);
	}
	const AdvViz::SDK::SharedInstVect& instances = bSyncWithAdvVizSDK
		? Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId())
		: NoSDKInstances;

	// Place instances randomly in a square area
	for (int32 i = 0; i < numInst; ++i)
	{
		float ri = FMath::FRandRange(0.f, 1.f);
		float rj = FMath::FRandRange(0.f, 1.f);
		float rs = FMath::FRandRange(0.f, 1.f);

		FVector instPos = initialPos + FVector(ri* SquareSideLength, rj* SquareSideLength, 0.f);

		if (intersectWorld)
		{
			FVector traceStart, traceEnd, traceDir(-FVector::ZAxisVector);
			traceStart = instPos - traceDir * 1e5f;
			traceEnd = traceStart + traceDir * 1e8f;
			const TArray<AActor*> actorsToIgnore;
			FHitResult hitResult;
			UKismetSystemLibrary::LineTraceSingle(
				this, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, false, actorsToIgnore,
				EDrawDebugTrace::None, hitResult, true);

			AActor* hitActor = hitResult.GetActor();
			if (hitActor)
			{
				instPos = hitResult.Location;
			}
		}

		UnrealInstanceInfo ueInstInfo;
		ueInstInfo.transform.SetTranslation(instPos);
		ueInstInfo.transform.SetScale3D(FVector(0.5f+rs));
		ueInstInfo.transform = BaseTransform * ueInstInfo.transform;
		ueInstInfo.colorShift.X = FMath::FRandRange(-0.5f, 0.5f);
		ueInstInfo.colorShift.Y = FMath::FRandRange(-0.5f, 0.5f);
		ueInstInfo.colorShift.Z = FMath::FRandRange(-0.5f, 0.5f);

		instancesTM[i] = ueInstInfo.transform;
		instancesColorVar[i*3] = ueInstInfo.colorShift.X;
		instancesColorVar[i*3 + 1] = ueInstInfo.colorShift.Y;
		instancesColorVar[i*3 + 2] = ueInstInfo.colorShift.Z;

		if (bSyncWithAdvVizSDK)
		{
			// Update the instance in the AdvViz::SDK manager
			AdvViz::SDK::IInstance& inst = *instances[oldNumInst + i];
			UpdateAVizInstance(inst, ueInstInfo);
			inst.SetShouldSave(true);
			inst.SetName("inst");
			inst.SetObjectRef(objectRef);
			inst.SetGroup(Impl->instancesGroup_);
		}
	}

	for (auto& FoliageComp : FoliageComponents)
	{
		auto* meshComp(FoliageComp.FoliageInstMeshComp.Get());
		meshComp->AddInstances(instancesTM, false);

		// Set the custom data for color variations
		if (meshComp->NumCustomDataFloats != 3)
		{
			meshComp->SetNumCustomDataFloats(3);
		}
		for (int32 i = 0; i < numInst; ++i)
		{
			meshComp->SetCustomData(oldNumInst + i, MakeArrayView(&instancesColorVar[i*3], 3));
		}

		// Clear the selection to avoid an UE cash when removing instances
		meshComp->ClearInstanceSelection();
	}

	BE_ASSERT(CheckInstanceCount());
}

void AITwinPopulation::RemoveInstances(int32 NumInst)
{
	const int32 TotalNumInstances = GetNumberOfInstances();

	if (NumInst == 1)
	{
		RemoveInstance(0);
	}
	else if (NumInst > 0 && NumInst <= TotalNumInstances)
	{
		TArray<int32> RemovedInst;
		RemovedInst.Reserve(NumInst);
		// Indices must be added in reverse order.
		double dIndex = static_cast<double>(TotalNumInstances - 1);
		const double dStep = static_cast<double>(TotalNumInstances) / static_cast<double>(NumInst);
		// By construction, this increment is >= 1, so we are sure not to add the same index twice in the
		// loop below...
		ensure(dStep >= 1.0);
		for ( ; dIndex >= 0.0 && RemovedInst.Num() < NumInst; dIndex -= dStep)
		{
			RemovedInst.Add(static_cast<int32>(dIndex));
		}
		RemoveInstances(RemovedInst);
	}
}

void AITwinPopulation::RemoveAllInstances()
{
	SetNumberOfInstances(0);
}

void AITwinPopulation::SetCollisionEnabled(ECollisionEnabled::Type NewType)
{
	for (auto& FoliageComp : FoliageComponents)
	{
		FoliageComp.FoliageInstMeshComp->SetCollisionEnabled(NewType);
	}
}

void AITwinPopulation::SetHiddenInGame(bool bHiddenInGame)
{
	for (auto& FoliageComp : FoliageComponents)
	{
		FoliageComp.FoliageInstMeshComp->SetHiddenInGame(bHiddenInGame, true);
	}
}

bool AITwinPopulation::IsHiddenInGame() const
{
	return !FoliageComponents.IsEmpty()
		&& FoliageComponents[0].FoliageInstMeshComp->bHiddenInGame != 0;
}

void AITwinPopulation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (auto ext = GetExtension<FITwinPopulationWithPathExt>())
		ext->UpdatePopulationInstances();

	static bool bDisplayAnimPathDebug = []()
	{
		// By default, we don't display the debug info for animation paths.
		// The user can enable it in the project settings.
		UITwinContentLibrarySettings const* ContentSettings = GetDefault<UITwinContentLibrarySettings>();
		return ContentSettings && ContentSettings->DisplayAnimPathDebug;
	}();

	if (bDisplayAnimPathDebug)
	{
		const int32 TotalNumInstances = GetNumberOfInstances();
		for (int32 i = 0; i < TotalNumInstances; ++i)
		{
			FTransform tm = GetInstanceTransform(i);
			FVector loc = tm.GetLocation();
			DrawDebugString(GetWorld(), loc, *FString::Printf(TEXT("%d: %0.1f, %0.1f, %0.1f"), i, loc.X, loc.Y, loc.Z), nullptr, FColor::White, 0.016f, false);
		}
	}
}

FITwinInstance::FITwinInstance()
{
}

AdvViz::expected<void, std::string> FITwinInstance::Update()
{
	using namespace AdvViz::SDK;
	if (population_.IsValid() && instanceIndex_ != NotSet)
	{
		if (auto animExt = population_->GetExtension<FITwinPopulationWithPathExt>())
		{
			//Only transformation for now
			dmat3x4 mat = GetTransform();
			FTransform transform = FITwinMathConversion::SDKtoUE(mat);
			animExt->InstanceToUpdateTransForm(instanceIndex_, transform);

			if (GetColorShift().has_value())
			{
				float3 color1 = GetColorShift().value();
				FVector color(color1[0] - 0.5f, color1[1] - 0.5f, color1[2] - 0.5f);
				if (!previousColor_.has_value() || previousColor_.value() != color)
				{
					animExt->InstanceToUpdateColor(instanceIndex_, color);
					previousColor_ = color;
				}
			}
		}
	}
	return AdvViz::expected<void, std::string>();
}


#if ENABLE_DRAW_DEBUG

// Console command to draw bounding boxes
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinDisplayFeaturesBBoxes(
	TEXT("cmd.ITwin_PopulationBoundingBox"),
	TEXT("Display populations as bounding boxes."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TActorIterator<AITwinPopulation> PopIter(World); PopIter; ++PopIter)
	{
		int32 NumInstances = PopIter->GetNumberOfInstances();
		for (int32 i(0); i < NumInstances; ++i)
		{
			FVector Center, Extent;
			PopIter->GetInstanceBoundingBox(i).GetCenterAndExtents(Center, Extent);
			DrawDebugBox(
				World,
				Center,
				Extent,
				FColor::Green,
				/*bool bPersistent =*/ false,
				/*float LifeTime =*/ 10.f);
		}
	}
})
);

#endif // ENABLE_DRAW_DEBUG
