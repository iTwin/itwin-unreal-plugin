/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulation.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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
	Vegetation,
	Character,
	ClippingPlane,
	ClippingBox,
	Crane,

	Other
};

namespace AdvViz::SDK
{
	class IInstancesManager;
	class IInstancesGroup;
	class RefID;
}
class AITwinPopulation;
class FITwinPopulationWithPathExt;
class UFoliageInstancedStaticMeshComponent;

namespace ECollisionEnabled { enum Type : int; }

USTRUCT()
struct FITwinFoliageComponentHolder
{
	GENERATED_BODY()

	UPROPERTY(Category = "iTwin", EditAnywhere)
	TObjectPtr<UStaticMesh> MasterMesh;

	UPROPERTY(Category = "iTwin", EditAnywhere)
	TObjectPtr<UFoliageInstancedStaticMeshComponent> FoliageInstMeshComp;

	void InitWithMasterMesh(AITwinPopulation& PopulationActor, UStaticMesh* Mesh);

	void InitFoliageMeshComponent(AITwinPopulation& PopulationActor);

	void BeginPlay(AITwinPopulation& PopulationActor);

	int32 GetInstanceCount() const;

	FBox GetMasterMeshBoundingBox() const;
	FBoxSphereBounds GetMasterMeshBounds() const;
};


UCLASS()
class ITWINRUNTIME_API AITwinPopulation : public AActor, public AdvViz::SDK::Tools::ExtensionSupport
{
	GENERATED_BODY()
	
public:
	using AVizInstancesManagerPtr = std::shared_ptr<AdvViz::SDK::IInstancesManager>;
	using AVizInstancesGroupPtr = std::shared_ptr<AdvViz::SDK::IInstancesGroup>;


	static AITwinPopulation* CreatePopulation(const UObject* WorldContextObject, const FString& AssetPath,
		AVizInstancesManagerPtr const& AvizInstanceManager,
		AVizInstancesGroupPtr const& AvizInstanceGroup);

	AITwinPopulation();

	/// Returns the bounding box of the master mesh object.
	FBox GetMasterMeshBoundingBox() const;
	FBoxSphereBounds GetMasterMeshBounds() const;
		
	std::shared_ptr<AdvViz::SDK::IInstance> GetAVizInstance(int32 instanceIndex);

	/// Toggle the automatic rebuild of the internal tree (UE internal structure to optimize the
	/// hierarchy of instances.
	/// \param bSuspendAutoRebuildOpt If provided, enforce the new value - if not, the property is toggled
	/// \return Previous value of the property.
	bool ToggleAutoRebuildTree(std::optional<bool> const& bSuspendAutoRebuildOpt = std::nullopt);

	/// Utility object to disable the automatic rebuild of the internal UE tree during a scope.
	class [[nodiscard]] FAutoRebuildTreeDisabler
	{
	public:
		FAutoRebuildTreeDisabler(AITwinPopulation& InPopulation);
		~FAutoRebuildTreeDisabler();
	private:
		TWeakObjectPtr<AITwinPopulation> Population;
		bool bAutoRebuildTreeOnInstanceChanges_Old = false;
	};

	FTransform GetInstanceTransform(int32 instanceIndex) const;
	/// Set the transformation of the given instance.
	/// \param bMarkRenderStateDirty If true, the change should be visible immediately. If you are updating
	///                              many instances you should only set this to true for the last instance.
	/// (Same meaning as in #UInstancedStaticMeshComponent::UpdateInstanceTransform).
	inline bool SetInstanceTransformUEOnly(int32 instanceIndex, const FTransform& tm, bool bMarkRenderStateDirty = true);
	/// Mark render state dirty for all foliage components. Can be used when SetInstanceTransformUEOnly is
	/// called on a batch of instances with bMarkRenderStateDirty as false, to invalidate the render state
	/// only once for the ISM.
	void MarkFoliageRenderStateDirty();

	void SetInstanceTransform(int32 instanceIndex, const FTransform& tm);

	const FTransform& GetBaseTransform() { return BaseTransform; }

	/// Returns the bounding box of the given instance.
	FBox GetInstanceBoundingBox(int32 instanceIndex) const;

	FVector GetInstanceColorVariation(int32 instanceIndex) const;
	void SetInstanceColorVariation(int32 instanceIndex, const FVector& v);
	inline bool SetInstanceColorVariationUEOnly(int32 instanceIndex, const FVector& v, bool bMarkRenderStateDirty = true);

	AdvViz::SDK::RefID GetInstanceRefId(int32 instanceIndex) const;
	int32 GetInstanceIndexFromRefId(const AdvViz::SDK::RefID& refId) const;

	float GetColorVariationIntensity() const;
	void SetColorVariationIntensity(const float& f);

	int32 GetNumberOfInstances() const;
	void SetNumberOfInstances(const int32& n);

	int32 GetSquareSideLength() const;
	void SetSquareSideLength(const int32& n);

	void SetInstancesZCoordinate(const float& maxDistToSquareCenter, const float& z);

	//! Add a new instance with given transformation, and return its index.
	int32 AddInstance(const FTransform& transform, bool bInteractivePlacement = false);
	//! Called when the instance is added (in case of non interactive placement) or once its position is
	//! validated (in case of interactive placement mode).
	void FinalizeAddedInstance(int32 instIndex, const FTransform* FinalTransform = nullptr,
		const AdvViz::SDK::RefID* EnforcedRefID = nullptr);

	void RemoveInstance(int32 instanceIndex);
	void RemoveInstances(const TArray<int32>& instanceIndices);

	//! Remove all instances belonging to this population, and invalidates the decoration for saving.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void RemoveAllInstances();

	void OnInstanceRestored(const AdvViz::SDK::RefID& restoredID);

	//! Enable/disable collisions.
	void SetCollisionEnabled(ECollisionEnabled::Type NewType);

	//! Hide/show all instances.
	void SetHiddenInGame(bool bHiddenInGame);
	//! Returns whether instances are currently hidden in game.
	bool IsHiddenInGame() const;

	void UpdateInstancesFromAVizToUE();

	AVizInstancesManagerPtr& GetInstanceManager();
	void SetInstancesManager(AVizInstancesManagerPtr const& instManager);

	AVizInstancesGroupPtr& GetInstancesGroup();
	void SetInstancesGroup(AVizInstancesGroupPtr const& instGroup);

	void SetObjectRef(const std::string& objRef);
	const std::string& GetObjectRef() const;
	const AdvViz::SDK::RefID& GetInstanceGroupId() const;
	
	bool IsRotationVariationEnabled() const;
	bool IsScaleVariationEnabled() const;
	bool IsPerpendicularToSurface() const;

	//! Returns whether this population corresponds to a clipping primitive (thus synchronized with the
	//! clipping tool).
	bool IsClippingPrimitive() const {
		return (objectType == EITwinInstantiatedObjectType::ClippingPlane
			|| objectType == EITwinInstantiatedObjectType::ClippingBox);
	}
	FString GetObjectTypeName() const;

	static FVector GetRandomColorShift(const EITwinInstantiatedObjectType type);


protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void Tick(float DeltaTime) override;


private:
	void AddInstances(int32 numInst);
	void RemoveInstances(int32 numInst);

	inline bool CheckInstanceCount() const;


private:
	UPROPERTY(Category = "iTwin", EditAnywhere)
	TArray<FITwinFoliageComponentHolder> FoliageComponents;

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


	struct FImpl;
	TPimplPtr<FImpl> Impl;

	std::string objectRef; // reference (url, id...) of the instantiated object

	EITwinInstantiatedObjectType objectType = EITwinInstantiatedObjectType::Other;

	friend class FITwinPopulationWithPathExt;
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
