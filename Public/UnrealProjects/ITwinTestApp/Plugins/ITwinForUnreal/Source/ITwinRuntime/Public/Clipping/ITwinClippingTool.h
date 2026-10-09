/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Clipping/ITwinClippingEventHub.h>
#include <Clipping/ITwinClippingToolApi.h>
#include <Containers/Array.h>
#include <Containers/Map.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinClippingTool.generated.h"


struct FITwinClippingInfoBase;
class FITwinTilesetAccess;
class UITwinClippingToolImpl;
class UITwinClippingPersistence;
class UITwinClippingRenderer;
class UCesiumPolygonRasterOverlay;

class AITwinInteractiveTool;
class AITwinPopulation;
class AITwinPopulationTool;
enum class EITwinInstantiatedObjectType : uint8;


/// Class managing Clipping Tools.
/// For this prototype, those tools are linked to the population tool, with dedicated objects.
UCLASS()
class ITWINRUNTIME_API AITwinClippingTool final : public AITwinClippingEventHub, public IITwinClippingToolApi
{
	GENERATED_BODY()
public:
	AITwinClippingTool();

	virtual void Tick(float DeltaTime) override;

	/// Connect the Population Tool (mandatory to manage box/plane effects).
	void ConnectPopulationTool(AITwinPopulationTool* PopulationTool);

	/// Connect the Spline Tool (mandatory to manage cutout polygon effects).
	void ConnectSplineTool(class AITwinSplineTool* SplineTool);

	/// Connect the scene persistence manager.
	void ConnectPersistenceManager(class AITwinDecorationHelper* DecorationHelper);

	/// Register tileset in the clipping system (for tile excluder mechanism).
	void RegisterTileset(const FITwinTilesetAccess& TilesetAccess);

	void OnModelRemoved(const ITwin::ModelLink& ModelIdentifier);

	virtual bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type) override;

	/// Abort current cutout effect creation, if any.
	void AbortInteractiveCreation(bool bTriggeredFromITS);

	/// Deactivate the cutout tool. This also aborts any cutout creation, if any.
	void Deactivate();

	/// The clipping cube and plane primitives are created/modified from the population tool, as it
	/// is already compatible with gizmo edition...
	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

	/// Update the clipping information in all tile excluders matching the modified instance, as well as in
	/// the material parameter collection.
	void OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex, bool bTriggeredFromITS);

	/// Called before some clipping instances are actually removed.
	void BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices);

	/// Update the clipping information upon the removal of clipping primitives.
	void OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap);

	/// Returns true if we are allowed to load legacy cutout instances, those retrieved from the Decoration
	/// Service.
	bool AllowLoadingLegacyInstances() const;

	/// Update the clipping information and/or rendering data upon the loading of clipping primitives.
	void OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos);

	/// Perform some automatic conversions when the loading is complete (such as migration to Scene API).
	void OnLoadComplete();

	virtual int32 NumEffects(EITwinClippingPrimitiveType Type) const override;

	/// Return a mutable reference to the clipping effect of given type and index, to modify it.
	FITwinClippingInfoBase& GetMutableEffect(EITwinClippingPrimitiveType Type, int32 Index);

	/// Return a const reference to the clipping effect of given type and index, to read its info.
	const FITwinClippingInfoBase& GetEffect(EITwinClippingPrimitiveType Type, int32 Index) const;

	virtual bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS) override;

	/// Flip the effect of given type and index.
	void FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Flip all effects of the given type.
	void FlipAllEffectsOfType(EITwinClippingPrimitiveType Type);

	virtual bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const override;
	virtual void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert) override;

	virtual bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		bool bEnterIsolationMode = true) override;

	virtual std::optional<FEffectIdentifier> GetSelectedEffect() const override;
	/// \note The default value is provided here only for the numerous existing call sites
	/// inside ITwinRuntime; IITwinClippingToolApi deliberately declares none.
	virtual void DeSelectAll(bool bExitIsolationMode = true) override;

	virtual void BroadcastSelection() override;


	virtual int32 GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const override;
	virtual void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) override;

	virtual bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const override;
	virtual void SetEffectLocation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InLatitude, double InLongitude, double InElevation,
		bool bTriggeredFromITS) override;
	virtual void SetEffectRotation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InRotX, double InRotY, double InRotZ,
		bool bTriggeredFromITS) override;

	virtual void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) override;

	/// Called when we activate/deactivate picking of clipping effects in the viewport.
	void OnActivatePicking(bool bActivate);
	/// Try to select a cut-out effect from a mouse click event.
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	//! Change the view camera so that the cutout polygons can be edited from top.
	//! If SpecificSpline is provided, only the corresponding polygon will be framed.
	UFUNCTION(Category = "iTwinUX", BlueprintCallable)
	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	virtual void SetOverviewCamera() override;

	virtual void SetTransformationMode(ETransformationMode Mode) override;

	virtual bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const override;
	virtual void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled) override;
	virtual void EnableAllEffects(bool bInEnabled) override;

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;

	/// Whether the influence of effects is defined by layer type (iModel, Reality data, etc.).
	bool IsUsingPerLayerTypeInfluence() const;
	/// Converts the influence of effects to be defined by layer instead of layer type (iModel, Reality data,
	/// etc.).
	/// \param InCurrentLayers The list of currently loaded layers, per model type, to initialize the new
	/// per-layer influence settings. This is required to avoid losing the current influence settings during
	/// the conversion.
	void ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers);

	/// Return whether the given effect should influence the given model type globally.
	bool ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType) const;
	/// Set whether the given effect should influence the given model type globally.
	void SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType, bool bAll);

	virtual void SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence) override;

	virtual bool DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const override;

	virtual TSet<FString> GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
		int32 EffectIndex,
		EITwinModelType LayerType) const override;

	virtual AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const override;

	virtual int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const override;

	UFUNCTION()
	void OnSceneLoaded(bool bSuccess);

	UFUNCTION()
	void OnItemCreationAbortedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS);

	UFUNCTION()
	void OnItemCreatedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS);

	UFUNCTION()
	void OnCutoutPolygonSelected();

	/// Returns the renderer used to manage cutout effects in the scene.
	const UITwinClippingRenderer* GetRenderer() const;

	virtual bool HasEffectListListener() const override;

#if WITH_EDITOR

	/// Globally activate/deactivate all effects of given type, at given level. This is for debugging, only
	/// possible in Editor.
	/// \param Type The cutout type which should be affected
	/// \param Level The effect level which should be affected (shader or tileset excluder)
	/// \param bActivate Whether the effect should be activated or not
	void ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate);

	/// Globally activate/deactivate all effects of given type, at all levels.
	void ActivateEffectsAllLevels(EITwinClippingPrimitiveType Type, bool bActivate);

#endif // WITH_EDITOR


private:
	UPROPERTY()
	TObjectPtr<UITwinClippingToolImpl> Impl;
};
