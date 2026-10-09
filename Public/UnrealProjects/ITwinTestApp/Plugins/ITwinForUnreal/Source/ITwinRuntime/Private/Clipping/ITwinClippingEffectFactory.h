/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEffectFactory.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingEffectManager.h>

#include <optional>

#include "ITwinClippingEffectFactory.generated.h"


class AITwinClippingEventHub;
class UITwinClippingRenderer;
class UITwinClippingPersistence;
enum class EITwinInstantiatedObjectType : uint8;


/// Responsible for creating/deleting the different types of cutout effects, and updating them with the
/// appropriate parameters, deduced from the corresponding Population Tool or Spline Tool instances.
UCLASS()
class UITwinClippingEffectFactory : public UObject
{
	GENERATED_BODY()
public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEffectPropertiesModifiedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex);
	UPROPERTY()
	FEffectPropertiesModifiedEvent EffectPropertiesModifiedEvent;


	// Store whether the removal event was initiated by Unreal (delete key in 3D viewport) or iTwin Studio
	// (trash icon in Cutout Property Page).
	enum class ERemovalInitiator : uint8_t
	{
		Unreal,
		ITS
	};
	struct FRemovalContext
	{
		ERemovalInitiator Initiator = ERemovalInitiator::ITS;
		EITwinClippingPrimitiveType PrimitiveType = EITwinClippingPrimitiveType::Count;
	};

	struct [[nodiscard]] FScopedRemovalContext
	{
		UITwinClippingEffectFactory& Factory;

		FScopedRemovalContext(UITwinClippingEffectFactory& InFactory, ERemovalInitiator RemovalInitiator, EITwinClippingPrimitiveType Type);

		~FScopedRemovalContext()
		{
			Factory.RemovalContextOpt.reset();
		}
	};

	UITwinClippingEffectFactory();

	void Connect(AITwinClippingEventHub* InEventHub,
		UITwinClippingEffectManager* InEffectManager,
		UITwinClippingPersistence* InPersistence);

	void SetEventHub(AITwinClippingEventHub* InEventHub);

	bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type);

	static EITwinClippingPrimitiveType GetEffectType(EITwinInstantiatedObjectType ObjectType);

	EITwinClippingPrimitiveType OnClippingInstanceAdded(AITwinPopulation* Population,
		EITwinInstantiatedObjectType ObjectType,
		int32 InstanceIndex);

	EITwinClippingPrimitiveType OnClippingInstancesLoaded(AITwinPopulation* Population,
		bool bUpdateEffectInfos);

	void BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType,
		const TArray<int32>& InstanceIndices);

	EITwinClippingPrimitiveType OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType,
		const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap);

	EITwinClippingPrimitiveType OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType,
		int32 InstanceIndex);

	/// Create a new cutout polygon effect from the given spline, if possible, and return new effect index.
	/// (or INDEX_NONE if creation failed).
	int32 RegisterCutoutSpline(AITwinSplineHelper* SplineHelper);

	bool DeRegisterCutoutSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);

	/// Remove the effect of given type and index, and return whether the removal was successful.
	bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS);

	//! Update the displayed edges from the instance tranformation.
	void UpdateEdgesFromInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);

private:
	inline bool IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const;

	void DeleteSelectedPopulationInstance();

	enum class EUpdateContext : uint8_t
	{
		Load,
		Add,
		Remove
	};

	bool UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex, EUpdateContext Context);

	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TStartInteractivePrimitiveInstanceCreation();


	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	bool TAddEffectFromInstance(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex);

	bool AddEffectFromInstance(EITwinClippingPrimitiveType PrimitiveType, int32 InstanceIndex);


	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	void TUpdateAllClippingPrimitives(TArray<PrimitiveInfo>& ClippingInfos, EUpdateContext Context);

	void UpdateAllClippingPrimitives(EITwinClippingPrimitiveType PrimitiveType, EUpdateContext Context);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	void TOnClippingInstancesRemoved(TArray<PrimitiveInfo>& ClippingInfos,
		const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap);


	/// Update the plane equation in all tile excluders matching the modified actor, and update it in the
	/// material parameter collection.
	bool UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex, bool bInvalidateDB);

	/// Retrieve the plane equation from the given instance.
	template <typename T>
	bool GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW, int32 InInstanceIndex) const;

	/// Update the box 3D information in all tile excluders created for the clipping box, as well as in the
	/// material parameter collection.
	bool UpdateClippingBoxFromUEInstance(int32 InstanceIndex, bool bInvalidateDB);

	/// Retrieve the box 3D information from the given instance.
	bool GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const;

	/// Retrieve the constant scale factor applied to the primitive's master mesh, that we need to take into
	/// account when computing the primitive effect parameters from the UE instance transform.
	inline double GetPrimitiveMasterMeshScale(EITwinClippingPrimitiveType Type) const;

	/// Apply properties from the loaded instance (for legacy support: cutout used to be saved on the
	/// decoration service). Also used during interactive creation of a new effect.
	void UpdatePropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);

	/// Store the effect properties in the AdvViz instance persist it in case of undo.
	void StorePropertiesInAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const;


private:
	UPROPERTY()
	TWeakObjectPtr<AITwinClippingEventHub> EventHub;

	UPROPERTY()
	TWeakObjectPtr<UITwinClippingEffectManager> EffectManager;

	UPROPERTY()
	TWeakObjectPtr<UITwinClippingPersistence> Persistence;

	std::optional<FRemovalContext> RemovalContextOpt;

	mutable TMap<EITwinClippingPrimitiveType, double> MasterMeshScaleCacheByType;
};
