/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingEffectManager.h>

#include <optional>

#include "ITwinClippingToolImpl.generated.h"

class AITwinClippingEventHub;
class UITwinClippingPersistence;
class UITwinClippingRenderer;
class AITwinInteractiveTool;

struct FITwinRayTraceInput;
class IITwinPopulationInstanceTransformProxy;
using IITwinPopulationInstanceTransformProxyPtr = TSharedPtr<IITwinPopulationInstanceTransformProxy>;

enum class EITwinInstantiatedObjectType : uint8;
enum class ETransformationMode : uint8;
enum class EChangeType : uint8;

namespace ITwinClippingDetails
{
	/// Miscellaneous info about the camera view, that we can use to adapt the clipping plane proxy scale, so
	/// that the gizmo is always visible and usable, even when the plane is very far from the camera.
	struct FCameraViewInfo
	{
		FVector CameraLocation = FVector::ZeroVector;
		FVector ViewDirection = FVector::ZeroVector;
		double FovScaling = 1.0;
		double DPIScaling = 1.0;

		FCameraViewInfo() = default;
		FCameraViewInfo(FVector const& InCameraLocation, FVector const& InViewDirection, double InFovScaling, double InDPIScaling)
			: CameraLocation(InCameraLocation)
			, ViewDirection(InViewDirection)
			, FovScaling(InFovScaling)
			, DPIScaling(InDPIScaling)
		{
		}

		bool Equals(FCameraViewInfo const& Other, double Tolerance = 1e-5) const
		{
			return CameraLocation.Equals(Other.CameraLocation, Tolerance)
				&& ViewDirection.Equals(Other.ViewDirection, Tolerance)
				&& FMath::Abs(FovScaling - Other.FovScaling) <= Tolerance
				&& FMath::Abs(DPIScaling - Other.DPIScaling) <= Tolerance;
		}
	};
}

/// Implementation class for AITwinClippingTool, which handles the actual logic for managing cutout effects,
/// their influence on models, and interactions with the Population and Spline tools.
UCLASS()
class UITwinClippingToolImpl : public UITwinClippingEffectManager
{
	GENERATED_BODY()
public:
	UITwinClippingToolImpl();
	void SetEventHub(AITwinClippingEventHub* InEventHub);


	/// Return whether the given effect is enabled.
	bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	/// Switches the given effect on or off.
	void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled);

	void EnableAllEffectsOfType(EITwinClippingPrimitiveType Type, bool bInEnabled);

	bool IsUsingPerLayerTypeInfluence() const;
	void ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers);

	/// Return whether the given effect should influence the given model type globally.
	bool ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType) const;
	void SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType, bool bAll);
	void SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	/// Return whether the given effect does influence the model specified by the given identifier.
	/// Beware that it does not take the enabled state of the effect into account: if the effect is disabled,
	/// this will still return true if the effect would influence the model.
	bool DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;
	TSet<FString> GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
		int32 EffectIndex,
		EITwinModelType LayerType) const;

	void ClippingModified(EITwinClippingPrimitiveType Type, EChangeType change,
		const FString& eventSource = FString(), const FString& cutoutSetting = FString());

	bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const;
	void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert);

	void FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);
	void FlipAllEffectsOfType(EITwinClippingPrimitiveType Type);

	using FEffectIdentifier = std::pair<EITwinClippingPrimitiveType, int32>;

	std::optional<FEffectIdentifier> GetSelectedEffect() const;
	bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bEnterIsolationMode = true);

	struct [[nodiscard]] FSelectionChangeDetector
	{
		UITwinClippingToolImpl& Impl;
		std::optional<FEffectIdentifier> const PreviousSelection;
		bool const bManageIsolationMode = false;

		FSelectionChangeDetector(UITwinClippingToolImpl& InImpl, bool bInManageIsolationMode = false);
		~FSelectionChangeDetector();
	};

	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	/// Zoom in on the effect of given type and index.
	void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Make the Population Tool the active tool, with usage restricted to cutout primitives.
	virtual TWeakObjectPtr<AITwinPopulationTool> ActivatePopulationTool(bool bUpdateTransformationMode = true) override;

	AITwinPopulation const* GetSelectedPopulation(int32& OutSelectedInstanceIndex) const;

	/// Select a cutout primitive in Population Tool, or reset selection if Population is null.
	void SelectPopulationInstance(AITwinPopulation* Population, int32 InstanceIndex,
		EITwinClippingPrimitiveType Type);

	bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS);

	/// Select a spline, or reset selection if InSplineHelper is null.
	void SelectSpline(AITwinSplineHelper* InSplineHelper, UWorld* World);

	void SetTransformationMode(ETransformationMode Mode);
	bool IsRotationMode() const;

	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);
	void OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap);

	UFUNCTION()
	void OnSplineHelperAdded(AITwinSplineHelper* NewSpline);

	UFUNCTION()
	void OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);

	/// Change all effect proxies visibility in the viewport (without deactivating them).
	/// This affects translucent boxes/planes as well as spline meshes displayed for cutout polygons.
	void SetAllEffectProxiesVisibility(bool bVisibleInGame);
	void HideAllEffectProxies() { SetAllEffectProxiesVisibility(false); }

	/// Show/Hide effect proxies for the given cutout type.
	void SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame, bool bIsolationMode = false);
	void ShowOnlyProxiesOfType(EITwinClippingPrimitiveType SelectedType, bool bIsolationMode);

	/// Returns whether the proxies of the given type are visible.
	bool IsEffectProxyVisible(EITwinClippingPrimitiveType Type) const;

	/// Modify the location of the selected cutout polygon point, if any.
	void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const;

	bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;

	void SetEffectLocation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		double InLatitude, double InLongitude, double InElevation,
		bool bTriggeredFromITS) const;

	void SetEffectRotation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		double InRotX, double InRotY, double InRotZ,
		bool bTriggeredFromITS);

	std::optional<FVector> GetPointOnPlaneAtRayIntersection(int32 PlaneIndex,
		FITwinRayTraceInput const& TraceInput,
		bool bClampToInfluenceBounds);

	std::optional<FVector> ProjectPredefinedScreenPositionOnPlane(int32 PlaneIndex,
		bool bClampToInfluenceBounds);

	bool UpdateClippingPlaneTransformFromBoundingBox(int32 PlaneIndex, bool bForceResetGizmoProxy = false);
	bool FindVisiblePointOnPlane(int32 PlaneIndex, std::optional<FVector> const& CurrentGizmoPositionOpt,
		FVector& OutPosition, bool& bOutIsCenter);
	void ConfigurePlaneProxyForGizmo(int32 PlaneIndex);
	void OnPlaneSelectionChanged(int32 PlaneIndex);

	void OnBoxSelectionChanged(int32 BoxIndex);
	void OnPrimitiveSelectionChanged(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	void OnItemCreatedInTool(const AITwinInteractiveTool& Tool, bool bTriggeredFromITS);

	//-----------------------------------------------------------------------------------
	// Persistence management functions (Scene API)
	//-----------------------------------------------------------------------------------

	/// Register effects just loaded in the Scene, if they were loaded from the Decoration Service
	/// (for old scene conversion...)
	void RegisterLoadedEffectsInScene(EITwinClippingPrimitiveType EffectType);

	void OnSceneLoaded(bool bSuccess);

	virtual void OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos) override;

	int32 FindSelectedPolygonIndex() const;

	UFUNCTION()
	void OnSplineMoveStart();

	UFUNCTION()
	void OnSplinePointMoveStart();
	UFUNCTION()
	void OnSplinePointMoved(bool bMovedInITS);
	UFUNCTION()
	void OnSplinePointAdded();
	UFUNCTION()
	void OnSplinePointRemoved();

	void OnSplinePointArrayModified();

	void InvalidateBoundingBoxOfClippingPlanes(ITwin::ModelLink const& ModelLink);

	void Tick(float DeltaTime);


private:
	template <typename FTransfoBuilderFunc>
	void TModifyEffectTransformation(FTransfoBuilderFunc const& Func,
		EITwinClippingPrimitiveType Type,
		int32 PrimitiveIndex,
		bool bTriggeredFromITS,
		bool bOnlyModifyProxy = false) const;

	void AdjustTransformationModeForPopulation(const AITwinPopulation* Population);

private:
	UPROPERTY()
	TWeakObjectPtr<AITwinClippingEventHub> EventHub;

	UPROPERTY()
	TObjectPtr<UITwinClippingEffectFactory> Factory;

	UPROPERTY()
	TObjectPtr<UITwinClippingPersistence> Persistence;

	UPROPERTY()
	TObjectPtr<UITwinClippingRenderer> Renderer;


	IITwinPopulationInstanceTransformProxyPtr PlaneTransformProxy;


	/// We store the chosen transformation mode for cutout primitives here, because the user can modify the
	/// transformation mode of the population tool for another purpose (3D Object edition), and we want to
	/// be able to restore the right mode when enabling the cutout tool again.
	std::optional<ETransformationMode> TransformationModeOpt;

	/// Additional information about the camera view and selection when entering interactive transformation.
	using FCameraViewInfo = ITwinClippingDetails::FCameraViewInfo;
	std::optional<FCameraViewInfo> LastViewInfo;
	std::optional<int32> LastSelectedPlaneIndex;


	friend class AITwinClippingTool;
};
