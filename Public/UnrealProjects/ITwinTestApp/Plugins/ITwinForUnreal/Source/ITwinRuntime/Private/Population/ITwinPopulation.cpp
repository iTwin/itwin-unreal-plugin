/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Population/ITwinPopulation.h"

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
	std::shared_ptr<SDK::Core::IInstancesManager> instancesManager;
	std::shared_ptr<SDK::Core::IInstancesGroup> instancesGroup; // the group to which this population belongs
};

AITwinPopulation::AITwinPopulation()
	: Impl(MakePimpl<FImpl>())
{
	PrimaryActorTick.bCanEverTick = false;

	mesh = nullptr;
	meshComp = nullptr;

	squareSideLength = 100000;
}

void AITwinPopulation::InitFoliageMeshComponent()
{
	if (!meshComp.IsValid())
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

	void UpdateSDKCoreInstanceTransform(
		SDK::Core::IInstance& dstInstance, const FTransform& srcInstanceTransform)
	{
		using namespace SDK::Core;
		FMatrix srcMat = srcInstanceTransform.ToMatrixWithScale();
		FVector srcPos = srcInstanceTransform.GetTranslation();

		dmat3x4 dstTransform;
		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				ColRow3x4(dstTransform,j,i) = srcMat.M[i][j];
			}
		}
		ColRow3x4(dstTransform,0,3) = srcPos.X;
		ColRow3x4(dstTransform,1,3) = srcPos.Y;
		ColRow3x4(dstTransform,2,3) = srcPos.Z;

		dstInstance.SetMatrix(dstTransform);
	}

	void UpdateSDKCoreInstanceColorShift(
		SDK::Core::IInstance& dstInstance, const FVector& srcColorShift)
	{
		FColor dstColorShift(
			static_cast<uint8>((srcColorShift.X + 0.5)*255.f),
			static_cast<uint8>((srcColorShift.Y + 0.5)*255.f),
			static_cast<uint8>((srcColorShift.Z + 0.5)*255.f));

		FString hexColor = dstColorShift.ToHex();
		dstInstance.SetColorShift(TCHAR_TO_UTF8(*hexColor));
	}

	void UpdateSDKCoreInstance(
		SDK::Core::IInstance& dstInstance, const UnrealInstanceInfo& srcInstance)
	{
		UpdateSDKCoreInstanceTransform(dstInstance, srcInstance.transform);
		UpdateSDKCoreInstanceColorShift(dstInstance, srcInstance.colorShift);
		dstInstance.SetName(TCHAR_TO_UTF8(*srcInstance.name));
	}

	bool checkVersion = true;
	bool isOldVersion = false;

	void UpdateUnrealInstance(
		UnrealInstanceInfo& dstInstance, /*const*/ SDK::Core::IInstance& srcInstance)
	{
		FMatrix dstMat(FMatrix::Identity);
		FVector dstPos;

		using namespace SDK::Core;
		// Temporary code for beta users:
		//   In earlier versions of the SDK and ITwinForUnreal, the transformation of instances
		//   used 4x3 matrices, which didn't follow the convention specified in the decoration
		//   service, using 3x4 matrices. This case is detected below by testing 2 values of the
		//   matrix: if they are greater than 100, it is very probably a translation value because
		//   the scale shouldn't vary much around 1. Then the matrix is fixed and the instance is
		//   marked to be resaved (when the user will close the scene). When removing this code
		//   later, the const attribute for srcInstance in the declaration should be restored.
		const dmat3x4& srcMat = srcInstance.GetMatrix();
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

			srcInstance.SetMatrix(newSrcMat);
			srcInstance.MarkForUpdate(SDK::Core::Database);
			}

		for (unsigned i = 0; i < 3; ++i)
		{
			for (unsigned j = 0; j < 3; ++j)
			{
				dstMat.M[j][i] = ColRow3x4(srcMat,i, j);
		}
		}
		dstPos.X = ColRow3x4(srcMat,0, 3);
		dstPos.Y = ColRow3x4(srcMat,1, 3);
		dstPos.Z = ColRow3x4(srcMat,2, 3);

		dstInstance.transform.SetFromMatrix(dstMat);
		dstInstance.transform.SetTranslation(dstPos);

		FString srcColorShift(srcInstance.GetColorShift().c_str());
		FColor rgbColor = FColor::FromHex(srcColorShift);
		dstInstance.colorShift.X = static_cast<double>(rgbColor.R)*convRGBToDouble - 0.5;
		dstInstance.colorShift.Y = static_cast<double>(rgbColor.G)*convRGBToDouble - 0.5;
		dstInstance.colorShift.Z = static_cast<double>(rgbColor.B)*convRGBToDouble - 0.5;

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

void AITwinPopulation::SetInstanceTransform(int32 instanceIndex, const FTransform& tm)
{
	if (instanceIndex >= 0 && instanceIndex < meshComp->GetInstanceCount())
	{
		meshComp->UpdateInstanceTransform(instanceIndex, tm, true);
		
		SDK::Core::SharedInstVect instances;
		Impl->instancesManager->GetInstancesByObjectRef(objectRef, instances);
		if (instanceIndex < instances.size())
		{
			SDK::Core::IInstance& inst = *instances[instanceIndex]; 
			UpdateSDKCoreInstanceTransform(inst, tm);
			inst.MarkForUpdate(SDK::Core::Database);
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

		SDK::Core::SharedInstVect instances;
		Impl->instancesManager->GetInstancesByObjectRef(objectRef, instances);
		if (instanceIndex < instances.size())
		{
			SDK::Core::IInstance& inst = *instances[instanceIndex]; 
			UpdateSDKCoreInstanceColorShift(inst, v);
			inst.MarkForUpdate(SDK::Core::Database);
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


	FVector GetRandomColorShift(const EITwinInstantiatedObjectType& type)
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
}

void AITwinPopulation::AddInstance(const FTransform& transform)
{
	// This function is used for the manual addition of a single instance.
	// The current position will be used later for the automatic filling of
	// a square with instances if the user changes their number. This is 
	// temporary for testing, before we have a better manner to do this...
	squareCenter = transform.GetLocation();

	// Create a local UnrealInstanceInfo
	UnrealInstanceInfo ueInstanceInfo;
	ueInstanceInfo.transform = baseTransform * transform;
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
	uint64_t instCount = Impl->instancesManager->GetInstanceCountByObjectRef(objectRef);
	Impl->instancesManager->SetInstanceCountByObjectRef(objectRef, instCount + 1);
	SDK::Core::SharedInstVect instances;
	Impl->instancesManager->GetInstancesByObjectRef(objectRef, instances);
	SDK::Core::IInstance& instance = *instances[instCount];
	UpdateSDKCoreInstance(instance, ueInstanceInfo);
	instance.SetName(std::string("inst"));
	instance.SetObjectRef(objectRef);
	instance.SetGroup(Impl->instancesGroup);
}

void AITwinPopulation::RemoveInstance(int32 instIndex)
{
	if (instIndex < 0 || instIndex > meshComp->GetInstanceCount())
		return;

	meshComp->RemoveInstance(instIndex);

	std::vector<int32_t> indices;
	indices.push_back(instIndex);
	Impl->instancesManager->RemoveInstancesByObjectRef(objectRef, indices);
}

void AITwinPopulation::RemoveInstances(const TArray<int32>& instanceIndices)
{
	meshComp->RemoveInstances(instanceIndices, true);

	std::vector<int32_t> indices;
	for(auto const& ind : instanceIndices)
	{
		indices.push_back(ind);
	}
	Impl->instancesManager->RemoveInstancesByObjectRef(objectRef, indices);
}

void AITwinPopulation::UpdateInstancesFromSDKCoreToUE()
{
	SDK::Core::SharedInstVect instances;
	Impl->instancesManager->GetInstancesByObjectRef(objectRef, instances);

	size_t numInst = instances.size(); 
	TArray<FTransform> instancesTM;
	instancesTM.SetNum(numInst);
	TArray<float> instancesColorVar;
	instancesColorVar.SetNum(numInst*3);

	checkVersion = true;

	for(size_t i = 0; i < numInst; ++i)
	{
		SDK::Core::IInstance& inst = *instances[i];

		UnrealInstanceInfo ueInstInfo;

		UpdateUnrealInstance(ueInstInfo, inst);
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
}

void AITwinPopulation::SetInstancesManager(std::shared_ptr<SDK::Core::IInstancesManager>& instManager)
{
	Impl->instancesManager = instManager;
}

void AITwinPopulation::SetInstancesGroup(std::shared_ptr<SDK::Core::IInstancesGroup>& instGroup)
{
	Impl->instancesGroup = instGroup;
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

void AITwinPopulation::SetNumberOfInstances(const int32& n)
{
	int32 diffInstances = n - meshComp->GetInstanceCount();
	if (diffInstances > 0)
	{
		AddInstances(diffInstances);
	}
	else if (diffInstances < 0)
	{
		RemoveInstances(-diffInstances);
	}
}

int32 AITwinPopulation::GetSquareSideLength() const
{
	return squareSideLength;
}

void AITwinPopulation::SetSquareSideLength(const int32& n)
{
	squareSideLength = n;
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

		pos.Z = squareCenter.Z;
		FVector instToCenter = squareCenter - pos;
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
	
	if (meshComp.IsValid() && mesh.IsValid())
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

		AddInstances(initialNumberOfInstances);
	}
}

void AITwinPopulation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Impl->instancesManager.reset();
	Impl->instancesGroup.reset();
}

void AITwinPopulation::AddInstances(int32 numInst)
{
	if (numInst == 0)
		return;

	double halfLength = static_cast<double>(squareSideLength)*0.5;
	FVector initialPos = squareCenter - FVector(halfLength, halfLength, 0.);

	TArray<FTransform> instancesTM;
	instancesTM.SetNum(numInst);
	TArray<float> instancesColorVar;
	instancesColorVar.SetNum(numInst*3);
	int32 oldNumInst = meshComp->GetInstanceCount();
	static bool intersectWorld = true;

	Impl->instancesManager->SetInstanceCountByObjectRef(objectRef, oldNumInst + numInst);
	SDK::Core::SharedInstVect instances;
	Impl->instancesManager->GetInstancesByObjectRef(objectRef, instances);

	// Place instances randomly in a square area
	for(int32 i = 0; i < numInst; ++i)
	{
		float ri = FMath::FRandRange(0.f, 1.f);
		float rj = FMath::FRandRange(0.f, 1.f);
		float rs = FMath::FRandRange(0.f, 1.f);

		FVector instPos = initialPos + FVector(ri*squareSideLength, rj*squareSideLength, 0.f);

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
		ueInstInfo.transform = baseTransform * ueInstInfo.transform;
		ueInstInfo.colorShift.X = FMath::FRandRange(-0.5f, 0.5f);
		ueInstInfo.colorShift.Y = FMath::FRandRange(-0.5f, 0.5f);
		ueInstInfo.colorShift.Z = FMath::FRandRange(-0.5f, 0.5f);

		instancesTM[i] = ueInstInfo.transform;
		instancesColorVar[i*3] = ueInstInfo.colorShift.X;
		instancesColorVar[i*3 + 1] = ueInstInfo.colorShift.Y;
		instancesColorVar[i*3 + 2] = ueInstInfo.colorShift.Z;

		// Update the instance in the SDK::Core manager
		SDK::Core::IInstance& inst = *instances[oldNumInst + i];
		UpdateSDKCoreInstance(inst, ueInstInfo);
		inst.MarkForUpdate(SDK::Core::Database);
		inst.SetName(std::string("inst"));
		inst.SetObjectRef(objectRef);
		inst.SetGroup(Impl->instancesGroup);
	}

	meshComp->AddInstances(instancesTM, false);

	// Set the custom data for color variations
	if (meshComp->NumCustomDataFloats != 3)
	{
		meshComp->SetNumCustomDataFloats(3);
	}
	for(int32 i = 0; i < numInst; ++i)
	{
		meshComp->SetCustomData(oldNumInst + i, MakeArrayView(&instancesColorVar[i*3], 3));
	}

	// Clear the selection to avoid an UE cash when removing instances
	meshComp->ClearInstanceSelection();
}

void AITwinPopulation::RemoveInstances(int32 numInst)
{
	int32 totalNumInstances = meshComp->GetInstanceCount();

	if (numInst == 1)
	{
		RemoveInstance(0);
	}
	else if (numInst > 0 && numInst <= totalNumInstances)
	{
		double step = static_cast<double>(totalNumInstances) / static_cast<double>(numInst);
		double dNumInst = static_cast<double>(numInst);
		TArray<int32> removedInst;
		for(double i = 0; i < totalNumInstances && removedInst.Num() < numInst; i += step)
		{
			removedInst.Add(static_cast<int32>(i));
		}
		removedInst.Sort([](const int32& a, const int32& b) {return a > b;});

		RemoveInstances(removedInst);
	}
}

