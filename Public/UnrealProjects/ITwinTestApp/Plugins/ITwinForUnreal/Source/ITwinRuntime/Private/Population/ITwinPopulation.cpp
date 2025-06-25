/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Population/ITwinPopulation.h"
#include "Population/ITwinPopulationWithPathExt.h"
#include "Math/UEMathConversion.h"

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

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/InstancesGroup.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <numeric>
#include <array>


struct AITwinPopulation::FImpl
{
	std::shared_ptr<AdvViz::SDK::IInstancesManager> instancesManager_;
	std::shared_ptr<AdvViz::SDK::IInstancesGroup> instancesGroup_; // the group to which this population belongs
};

AITwinPopulation::AITwinPopulation()
	: Impl(MakePimpl<FImpl>())
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	SquareSideLength = 100000;
}

void AITwinPopulation::InitFoliageMeshComponent()
{
	if (!meshComp)
	{
		meshComp = NewObject<UFoliageInstancedStaticMeshComponent>(this, UFoliageInstancedStaticMeshComponent::StaticClass());
		meshComp->SetupAttachment(RootComponent);
		SetRootComponent(meshComp.Get());
	}
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
		dstInstance.SetTransform(FITwinMathConversion::UEtoSDK(srcInstanceTransform));
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

FTransform AITwinPopulation::GetInstanceTransform(int32 instanceIndex) const
{
	FTransform instTM;

	if (instanceIndex >= 0 && instanceIndex < meshComp->GetInstanceCount())
	{
		meshComp->GetInstanceTransform(instanceIndex, instTM, true);
	}

	return instTM;
}

void AITwinPopulation::SetInstanceTransformUEOnly(int32 instanceIndex, const FTransform& tm)
{
	meshComp->UpdateInstanceTransform(instanceIndex, tm, true);
}

void AITwinPopulation::SetInstanceTransform(int32 instanceIndex, const FTransform& tm)
{
	if (instanceIndex >= 0 && instanceIndex < meshComp->GetInstanceCount())
	{
		SetInstanceTransformUEOnly(instanceIndex, tm);
		
		const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (instanceIndex < instances.size())
		{
			AdvViz::SDK::IInstance& inst = *instances[instanceIndex]; 
			UpdateAVizInstanceTransform(inst, tm);
			inst.SetShouldSave(true);
		}
	}
}

FVector AITwinPopulation::GetInstanceColorVariation(int32 instanceIndex) const
{
	FVector instColVar(0.5);

	if (instanceIndex >= 0 && instanceIndex < meshComp->GetInstanceCount())
	{
		float* colorVar = nullptr;
		if (meshComp->NumCustomDataFloats == 3)
		{
			colorVar = &meshComp->PerInstanceSMCustomData[instanceIndex*3];
		}
		instColVar.X = colorVar[0];
		instColVar.Y = colorVar[1];
		instColVar.Z = colorVar[2];
	}

	return instColVar;
}

void AITwinPopulation::SetInstanceColorVariation(int32 instanceIndex, const FVector& v)
{
	if (instanceIndex >= 0 && instanceIndex < meshComp->GetInstanceCount())
	{
		if (meshComp->NumCustomDataFloats != 3)
		{
			meshComp->SetNumCustomDataFloats(3);
		}

		meshComp->SetCustomDataValue(instanceIndex, 0, v.X, true);
		meshComp->SetCustomDataValue(instanceIndex, 1, v.Y, true);
		meshComp->SetCustomDataValue(instanceIndex, 2, v.Z, true);

		const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
		if (instanceIndex < instances.size())
		{
			AdvViz::SDK::IInstance& inst = *instances[instanceIndex]; 
			UpdateAVizInstanceColorShift(inst, v);
			inst.SetShouldSave(true);
		}
	}
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

/*static*/ FVector AITwinPopulation::GetRandomColorShift(const EITwinInstantiatedObjectType& type)
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

void AITwinPopulation::AddInstance(const FTransform& transform)
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
	int32 instIndex = meshComp->AddInstance(ueInstanceInfo.transform, false);

	if (meshComp->NumCustomDataFloats != 3)
	{
		meshComp->SetNumCustomDataFloats(3);
	}
	meshComp->SetCustomDataValue(instIndex, 0, ueInstanceInfo.colorShift.X);
	meshComp->SetCustomDataValue(instIndex, 1, ueInstanceInfo.colorShift.Y);
	meshComp->SetCustomDataValue(instIndex, 2, ueInstanceInfo.colorShift.Z);

	// Add the same instance in the manager of the SDK core
	uint64_t instCount = Impl->instancesManager_->GetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	Impl->instancesManager_->SetInstanceCountByObjectRef(objectRef, Impl->instancesGroup_->GetId(), instCount + 1);
	const AdvViz::SDK::SharedInstVect& instances = Impl->instancesManager_->GetInstancesByObjectRef(objectRef, Impl->instancesGroup_->GetId());
	AdvViz::SDK::IInstance& instance = *instances[instCount];
	UpdateAVizInstance(instance, ueInstanceInfo);
	instance.SetName(std::string("inst"));
	instance.SetObjectRef(objectRef);
	instance.SetGroup(Impl->instancesGroup_);
}

void AITwinPopulation::RemoveInstance(int32 instIndex)
{
	if (instIndex < 0 || instIndex > meshComp->GetInstanceCount())
		return;

	meshComp->RemoveInstance(instIndex);

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
	}
}

void AITwinPopulation::RemoveInstances(const TArray<int32>& instanceIndices)
{
	if (instanceIndices.IsEmpty())
		return;

	meshComp->RemoveInstances(instanceIndices, true);

	std::vector<int32_t> indices;
	indices.reserve((size_t)instanceIndices.Num());
	for(auto const& ind : instanceIndices)
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

	for(size_t i = 0; i < numInst; ++i)
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

	meshComp->AddInstances(instancesTM, false);

	// Set the custom data for color variations
	if (meshComp->NumCustomDataFloats != 3)
	{
		meshComp->SetNumCustomDataFloats(3);
	}
	for(size_t i = 0; i < numInst; ++i)
	{
		meshComp->SetCustomData(i, MakeArrayView(&instancesColorVar[i*3], 3), true);
	}

	// Clear the selection to avoid an UE cash when removing instances
	meshComp->ClearInstanceSelection();

	if (GetExtension<FITwinPopulationWithPathExt>())
	{
		SetActorTickEnabled(true);
	}
	else
	{
		SetActorTickEnabled(false);
	}

}

std::shared_ptr<AdvViz::SDK::IInstancesManager>& AITwinPopulation::GetInstanceManager()
{
	return Impl->instancesManager_;
}

void AITwinPopulation::SetInstancesManager(std::shared_ptr<AdvViz::SDK::IInstancesManager>& instManager)
{
	Impl->instancesManager_ = instManager;
}

std::shared_ptr<AdvViz::SDK::IInstancesGroup>& AITwinPopulation::GetInstancesGroup()
{
	return Impl->instancesGroup_;
}

void AITwinPopulation::SetInstancesGroup(std::shared_ptr<AdvViz::SDK::IInstancesGroup>& instGroup)
{
	Impl->instancesGroup_ = instGroup;
}

void AITwinPopulation::SetObjectRef(const std::string& objRef)
{
	objectRef = objRef;

	if (objRef.find(std::string("Character")) != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::Character;
	}
	else if (objRef.find(std::string("Vehicle")) != std::string::npos ||
			 objRef.find(std::string("Construction")) != std::string::npos)
	{
		if (objRef.find(std::string("Crane")) != std::string::npos)
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
	else if (objRef.find(std::string("Vegetation")) != std::string::npos)
	{
		objectType = EITwinInstantiatedObjectType::Vegetation;
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

float AITwinPopulation::GetColorVariationIntensity() const
{
	for (int32 i = 0; i < meshComp->GetNumMaterials(); ++i)
	{
		UMaterialInterface* mat = meshComp->GetMaterial(i);
		UMaterialInstance* matInst = dynamic_cast<UMaterialInstance*>(mat);
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
	for (int32 i = 0; i < meshComp->GetNumMaterials(); ++i)
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

int32 AITwinPopulation::GetNumberOfInstances() const
{
	return meshComp->GetInstanceCount();
}

void AITwinPopulation::SetNumberOfInstances(const int32& NewInstanceCount)
{
	if (!ensure(meshComp))
		return;
	const int32 DiffInstances = NewInstanceCount - meshComp->GetInstanceCount();
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
	int32 totalNumInstances = meshComp->GetInstanceCount();

	for (int32 i = 0; i < totalNumInstances; ++i)
	{
		FTransform tm;
		meshComp->GetInstanceTransform(i, tm);
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
	
	InitFoliageMeshComponent();

	if (meshComp && mesh)
	{
		meshComp->RegisterComponent();

		// Set the mesh (the movable mobility is needed to avoid a warning
		// when playing in the editor).
		meshComp->SetMobility(EComponentMobility::Movable);
		meshComp->SetStaticMesh(mesh.Get());
		meshComp->SetMobility(EComponentMobility::Static);

		meshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		meshComp->SetEnableGravity(false);

		// Disable AO to get a better framerate (the unreal editor disables it
		// when instantiating a mesh with foliage)
		meshComp->bAffectDistanceFieldLighting = false;

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
	if (!ensure(meshComp))
		return;

	double halfLength = static_cast<double>(SquareSideLength)*0.5;
	FVector initialPos = SquareCenter - FVector(halfLength, halfLength, 0.);

	TArray<FTransform> instancesTM;
	instancesTM.SetNum(numInst);
	TArray<float> instancesColorVar;
	instancesColorVar.SetNum(numInst*3);
	int32 oldNumInst = meshComp->GetInstanceCount();
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
			inst.SetName(std::string("inst"));
			inst.SetObjectRef(objectRef);
			inst.SetGroup(Impl->instancesGroup_);
		}
	}

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

void AITwinPopulation::RemoveInstances(int32 NumInst)
{
	if (!ensure(meshComp))
		return;
	const int32 TotalNumInstances = meshComp->GetInstanceCount();

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

void AITwinPopulation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (auto ext = GetExtension<FITwinPopulationWithPathExt>())
		ext->UpdatePopulationInstances();

	if (bDisplayInfo)
	{
		const int32 TotalNumInstances = meshComp->GetInstanceCount();
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

