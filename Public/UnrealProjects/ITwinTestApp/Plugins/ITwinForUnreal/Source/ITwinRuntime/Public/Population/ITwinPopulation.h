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

namespace SDK::Core
{
	class IInstancesManager;
	class IInstancesGroup;
}

UCLASS()
class ITWINRUNTIME_API AITwinPopulation : public AActor
{
	GENERATED_BODY()
	
public:	
	AITwinPopulation();

	void InitFoliageMeshComponent();

	FTransform GetInstanceTransform(int32 instanceIndex) const; 
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

	void UpdateInstancesFromSDKCoreToUE();

	void SetInstancesManager(std::shared_ptr<SDK::Core::IInstancesManager>& instManager);
	void SetInstancesGroup(std::shared_ptr<SDK::Core::IInstancesGroup>& instGroup);
	void SetObjectRef(const std::string& objRef);
	const std::string& GetObjectRef() const;
	
	bool IsRotationVariationEnabled() const;
	bool IsScaleVariationEnabled() const;
	bool IsPerpendicularToSurface() const;

	TWeakObjectPtr<UStaticMesh> mesh;
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> meshComp;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	int32 initialNumberOfInstances = 0;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	int32 squareSideLength = 1;

	FVector squareCenter = FVector::Zero();

	FTransform baseTransform = FTransform::Identity;




protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void AddInstances(int32 numInst);
	void RemoveInstances(int32 numInst);




private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;

	std::string objectRef; // reference (url, id...) of the instantiated object

	EITwinInstantiatedObjectType objectType = EITwinInstantiatedObjectType::Other;
};
