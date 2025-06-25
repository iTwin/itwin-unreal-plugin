/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include <UObject/StrongObjectPtr.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Tools.h>
#	include <SDK/Core/Visualization/Instance.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <string>

#include "ITwinPopulation.generated.h"

enum class EITwinInstantiatedObjectType : uint8
{
	Vehicle = 0,
	Vegetation = 1,
	Character = 2,
	Crane = 3,
	Other = 4
};

namespace AdvViz::SDK
{
	class IInstancesManager;
	class IInstancesGroup;
	class RefID;
}

UCLASS()
class ITWINRUNTIME_API AITwinPopulation : public AActor, public AdvViz::SDK::Tools::ExtensionSupport
{
	GENERATED_BODY()
	
public:	
	AITwinPopulation();

	void InitFoliageMeshComponent();

	FTransform GetInstanceTransform(int32 instanceIndex) const;
	void SetInstanceTransformUEOnly(int32 instanceIndex, const FTransform& tm);

	void SetInstanceTransform(int32 instanceIndex, const FTransform& tm);

	FVector GetInstanceColorVariation(int32 instanceIndex) const;
	void SetInstanceColorVariation(int32 instanceIndex, const FVector& v);

	float GetColorVariationIntensity() const;
	void SetColorVariationIntensity(const float& f);

	int32 GetNumberOfInstances() const;
	void SetNumberOfInstances(const int32& n);

	int32 GetSquareSideLength() const;
	void SetSquareSideLength(const int32& n);

	void SetInstancesZCoordinate(const float& maxDistToSquareCenter, const float& z);

	void AddInstance(const FTransform& transform);
	void RemoveInstance(int32 instanceIndex);
	void RemoveInstances(const TArray<int32>& instanceIndices);

	//! Remove all instances belonging to this population, and invalidates the decoration for saving.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void RemoveAllInstances();

	void UpdateInstancesFromAVizToUE();

	std::shared_ptr<AdvViz::SDK::IInstancesManager>& GetInstanceManager();
	void SetInstancesManager(std::shared_ptr<AdvViz::SDK::IInstancesManager>& instManager);
	std::shared_ptr<AdvViz::SDK::IInstancesGroup>& GetInstancesGroup();
	void SetInstancesGroup(std::shared_ptr<AdvViz::SDK::IInstancesGroup>& instGroup);
	void SetObjectRef(const std::string& objRef);
	const std::string& GetObjectRef() const;
	const AdvViz::SDK::RefID& GetInstanceGroupId() const;
	
	bool IsRotationVariationEnabled() const;
	bool IsScaleVariationEnabled() const;
	bool IsPerpendicularToSurface() const;

	static FVector GetRandomColorShift(const EITwinInstantiatedObjectType& type);

	UPROPERTY(Category = "iTwin", EditAnywhere)
	TObjectPtr<UStaticMesh> mesh;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> meshComp;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	int32 InitialNumberOfInstances = 0;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	int32 SquareSideLength = 1;

	UPROPERTY(EditAnywhere, Transient, Category = "Debug")
	bool bDisplayInfo = false;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	FVector SquareCenter = FVector::Zero();

	UPROPERTY(Category = "iTwin", EditAnywhere)
	FTransform BaseTransform = FTransform::Identity;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void Tick(float DeltaTime) override;
	void AddInstances(int32 numInst);
	void RemoveInstances(int32 numInst);

protected:
	struct FImpl;
	TPimplPtr<FImpl> Impl;

	std::string objectRef; // reference (url, id...) of the instantiated object

	EITwinInstantiatedObjectType objectType = EITwinInstantiatedObjectType::Other;
};

class FITwinInstance : public AdvViz::SDK::Instance, AdvViz::SDK::Tools::TypeId<FITwinInstance>
{
public:
	FITwinInstance();

	AdvViz::expected<void, std::string> Update() override;

	using AdvViz::SDK::Tools::TypeId<FITwinInstance>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::Instance::IsTypeOf(i); }

	TWeakObjectPtr<AITwinPopulation> population_;
	static const std::uint32_t NotSet = std::numeric_limits<std::uint32_t>::max();
	std::uint32_t instanceIndex_ = NotSet;
	std::optional<FVector> previousColor_;
};
