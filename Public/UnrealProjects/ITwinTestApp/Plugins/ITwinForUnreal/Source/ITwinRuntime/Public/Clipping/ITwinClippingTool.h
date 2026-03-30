/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Clipping/ITwinClippingBoxInfo.h>
#include <Clipping/ITwinClippingCartographicPolygonInfo.h>
#include <Clipping/ITwinClippingPlaneInfo.h>
#include <Containers/Array.h>
#include <Containers/Map.h>
#include <GameFramework/Actor.h>
#include <Misc/EnumRange.h>
#include <Templates/PimplPtr.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
	#include <glm/ext/matrix_double3x3.hpp>
	#include <glm/ext/vector_double3.hpp>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinClippingTool.generated.h"


class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class ACesium3DTileset;
class FITwinTilesetAccess;
class UITwinClippingMPCHolder;
class UCesiumPolygonRasterOverlay;

class AITwinPopulation;
class AITwinPopulationTool;
enum class EITwinInstantiatedObjectType : uint8;
enum class ETransformationMode : uint8;

namespace AdvViz::SDK
{
	class RefID;
}

/// Supported primitive types for clipping.
UENUM(BlueprintType)
enum class EITwinClippingPrimitiveType : uint8
{
	Box,
	Plane,
	Polygon, /* stands for Cesium Cartographic Polygon (2.5D) */

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinClippingPrimitiveType, EITwinClippingPrimitiveType::Count);


/// Clipping effects usually work both at the tileset level (tile exclusion) and the
/// shader level (to clip more precisely inside a given tile).
UENUM(BlueprintType)
enum class EITwinClippingEffectLevel : uint8
{
	Shader,
	Tileset,
};

namespace ITwin
{
	// The number of planes & boxes is currently limited, due to the way it is coded in the material graph:
	// see Shaders/ITwin/GetPlanesClipping.ush for details, and the way it is connected to the material
	// parameter collection (MPC_Clipping) in the material function (MF_GlobalClipping).
	// (I have quickly looked for a way to manipulate these parameters with an index instead, but found
	// nothing, hence the ridiculous number of connections in the graph...)
	static constexpr int MAX_CLIPPING_PLANES = 32;

	static constexpr int MAX_CLIPPING_BOXES = 32;
}


/// Class managing Clipping Tools.
/// For this prototype, those tools are linked to the population tool, with dedicated objects.
UCLASS()
class ITWINRUNTIME_API AITwinClippingTool : public AActor
{
	GENERATED_BODY()

public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEffectListModifiedEvent);
	UPROPERTY()
	FEffectListModifiedEvent EffectListModifiedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEffectAddedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex);
	UPROPERTY()
	FEffectAddedEvent EffectAddedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveEffectStartedEvent);
	UPROPERTY()
	FRemoveEffectStartedEvent RemoveEffectStartedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveEffectCompletedEvent);
	UPROPERTY()
	FRemoveEffectCompletedEvent RemoveEffectCompletedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEffectRemovedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex, bool, bTriggeredFromITS);
	UPROPERTY()
	FEffectRemovedEvent EffectRemovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEffectSelectedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex);
	UPROPERTY()
	FEffectSelectedEvent EffectSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplinePointSelectedEvent);
	UPROPERTY()
	FSplinePointSelectedEvent SplinePointSelectedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSplinePointMovedEvent, bool, bMovedInITS);
	UPROPERTY()
	FSplinePointMovedEvent SplinePointMovedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationEvent, bool, bActivated);
	UPROPERTY()
	FActivationEvent ActivationEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteractiveCreationAbortedEvent, bool, bTriggeredFromITS);
	UPROPERTY()
	FInteractiveCreationAbortedEvent InteractiveCreationAbortedEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEffectModifiedEvent, EITwinClippingPrimitiveType, EffectType, int32, EffectIndex, bool, bTriggeredFromITS);
	UPROPERTY()
	FEffectModifiedEvent EffectModifiedEvent;


	AITwinClippingTool();

	virtual void Tick(float DeltaTime) override;

	/// Connect the Population Tool (mandatory to manage box/plane effects).
	void ConnectPopulationTool(AITwinPopulationTool* PopulationTool);

	/// Connect the Spline Tool (mandatory to manage cutout polygon effects).
	void ConnectSplineTool(class AITwinSplineTool* SplineTool);

	/// Register tileset in the clipping system (for tile excluder mechanism).
	void RegisterTileset(const FITwinTilesetAccess& TilesetAccess);

	void OnModelRemoved(const ITwin::ModelLink& ModelIdentifier);

	/// Initiate the interactive creation of a new effect.
	bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type);

	/// Abort current cutout effect creation, if any.
	void AbortInteractiveCreation(bool bTriggeredFromITS);

	/// Deactivate the cutout tool. This also aborts any cutout creation, if any.
	void Deactivate();

	/// For first prototype, the clipping primitives are created/modified from the population tool, as it
	/// is already compatible with gizmo edition...
	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

	/// Update the clipping information in all tile excluders matching the modified instance, as well as in
	/// the material parameter collection.
	void OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex, bool bTriggeredFromITS);

	/// Called before some clipping instances are actually removed.
	void BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices);

	/// Update the clipping information upon the removal of clipping primitives.
	void OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices);

	/// Update the clipping information upon the loading of clipping primitives.
	void OnClippingInstancesLoaded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType);

	/// Return the number of clipping effects for the given primitive type.
	int32 NumEffects(EITwinClippingPrimitiveType Type) const;

	/// Remove an individual clipping primitive. Returns true if the effect was actually removed.
	bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS);

	/// Flip the effect of given type and index.
	void FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const;
	void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert);

	/// Select the effect of given type and index.
	/// \param bEnterIsolationMode When true, we enter isolation mode, by hiding all the other proxies.
	/// \return True if the effect could be selected.
	bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		bool bEnterIsolationMode = true);

	/// Zoom in on the effect of given type and index.
	void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Returns a pair identifying the selected effect, if any.
	using FEffectIdentifier = std::pair<EITwinClippingPrimitiveType, int32>;
	std::optional<FEffectIdentifier> GetSelectedEffect() const;
	/// Reset current selection to none.
	/// \param bExitIsolationMode When true, and if there was currently an isolation mode, we exit it by
	/// restoring the normal visibility of effect proxies.
	void DeSelectAll(bool bExitIsolationMode = true);

	/// Return the index of the selected polygon point, if any (if a cutout polygon point is selected) and if
	/// yes, fills its coordinates (latitude and longitude).
	/// If no polygon is selected, or if none of its points is selected, INDEX_NONE is returned.
	int32 GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const;
	/// Modify the location of the selected cutout polygon point, if any.
	void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const;

	bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;
	bool GetSelectedEffectTransform(FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;
	void SetEffectLocation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InLatitude, double InLongitude, double InElevation,
		bool bTriggeredFromITS) const;
	void SetEffectRotation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InRotX, double InRotY, double InRotZ,
		bool bTriggeredFromITS) const;

	bool RecenterPlaneProxy(int32 PlaneIndex);

	/// Called when we activate/deactivate picking of clipping effects in the viewport.
	void OnActivatePicking(bool bActivate);
	/// Try to select a cut-out effect from a mouse click event.
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	//! Change the view camera so that the cutout polygons can be edited from top.
	//! If SpecificSpline is provided, only the corresponding polygon will be framed.
	UFUNCTION(Category = "iTwinUX", BlueprintCallable)
	void OnOverviewCamera(AITwinSplineHelper const* SpecificSpline = nullptr);

	//! Set the transformation mode (for selection gizmo).
	void SetTransformationMode(ETransformationMode Mode);

	/// Return whether the given effect is enabled.
	bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	/// Switches the given effect on or off.
	void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled);
	/// Enable or disable all effects.
	void EnableAllEffects(bool bInEnabled);

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;

	/// Return whether the given effect should influence the given model type globally.
	bool ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType) const;
	void SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType, bool bAll);

	void SetEffectInfluenceSpecificModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	/// Returns the unique identifier of an effect from its index.
	AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const;

	/// Returns the index of a given effect from its unique identifier.
	int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const;

	UFUNCTION()
	void OnSplineHelperAdded(AITwinSplineHelper* NewSpline);

	UFUNCTION()
	void OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved);

	UFUNCTION()
	void OnItemCreationAbortedInTool(bool bTriggeredFromITS);

	/// Returns whether a cutout effect is excluding (cutting) the given position, for the given type of layer.
	bool ShouldCutOut(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier,
		UCesiumPolygonRasterOverlay const* RasterOverlay) const;


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
	void BroadcastSelection();


private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	UITwinClippingMPCHolder* ClippingMPCHolder = nullptr;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> ClippingPlanePopulation;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> ClippingBoxPopulation;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
