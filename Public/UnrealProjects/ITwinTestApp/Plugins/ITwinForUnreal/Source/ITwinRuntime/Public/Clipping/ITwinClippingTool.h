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
#include <Templates/PimplPtr.h>

#include <glm/ext/matrix_double3x3.hpp>
#include <glm/ext/vector_double3.hpp>

#include <memory>
#include <optional>

#include "ITwinClippingTool.generated.h"


class AStaticMeshActor;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class ACesium3DTileset;
class FITwinTilesetAccess;
class UITwinClippingMPCHolder;

class AITwinPopulation;
class AITwinPopulationTool;
enum class EITwinInstantiatedObjectType : uint8;

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


	AITwinClippingTool();

	/// Register tileset in the clipping system (for tile excluder mechanism).
	void RegisterTileset(const FITwinTilesetAccess& TilesetAccess);

	/// Pre-load populations used for cutout effects.
	uint32 PreLoadClippingPrimitives(AITwinPopulationTool& PopulationTool);

	/// Initiate the interactive creation of a new effect.
	bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type);

	/// For first prototype, the clipping primitives are created/modified from the population tool, as it
	/// is already compatible with gizmo edition...
	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

	/// Update the clipping information in all tile excluders matching the modified instance, as well as in
	/// the material parameter collection.
	void OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

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
	void SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Zoom in on the effect of given type and index.
	void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Returns a pair identifying the selected effect, if any.
	using FEffectIdentifier = std::pair<EITwinClippingPrimitiveType, int32>;
	std::optional<FEffectIdentifier> GetSelectedEffect() const;
	/// Reset current selection to none.
	void DeSelectAll();

	/// Return the index of the selected polygon point, if any (if a cutout polygon point is selected) and if
	/// yes, fills its coordinates (latitude and longitude).
	/// If no polygon is selected, or if none of its points is selected, INDEX_NONE is returned.
	int32 GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const;
	/// Modify the location of the selected cutout polygon point, if any.
	void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const;

	/// Called when we activate/deactivate picking of clipping effects in the viewport.
	void OnActivatePicking(bool bActivate);
	/// Try to select a cut-out effect from a mouse click event.
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	//! Change the view camera so that the edited primitives can be edited from top.
	UFUNCTION(Category = "iTwinUX", BlueprintCallable)
	void OnOverviewCamera();

	/// Return whether the given effect is enabled.
	bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	/// Switches the given effect on or off.
	void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled);

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIndentifier) const;

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
	/// Retrieve the MPC for clipping.
	UMaterialParameterCollection* GetMPCClipping();
	UMaterialParameterCollectionInstance* GetMPCCLippingInstance();

	/// Update the given tileset by activating the different clipping effects to it, and deactivating any
	/// effect that should no longer affect it.
	void UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
		std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);
	/// Update clipping effects in all tilesets in the scene.
	void UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);

	struct FTilesetUpdateInfo;
	void UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess,
		FTilesetUpdateInfo& UpdateInfo);

	bool UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);

	FITwinClippingInfoBase& GetMutableClippingEffect(EITwinClippingPrimitiveType Type, int32 Index);

	const FITwinClippingInfoBase& GetClippingEffect(EITwinClippingPrimitiveType Type, int32 Index) const;

	template <typename Func>
	void VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun);

	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TPreLoadClippingPrimitive(TWeakObjectPtr<AITwinPopulation>& ClippingPopulation,
		AITwinPopulationTool& PopulationTool);

	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TStartInteractivePrimitiveInstanceCreation();

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	bool TAddClippingPrimitive(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	void TUpdateAllClippingPrimitives(TArray<PrimitiveInfo>& ClippingInfos);

	inline
	const TWeakObjectPtr<AITwinPopulation>& GetClippingEffectPopulation(EITwinClippingPrimitiveType Type) const;

	/// Update the plane equation in all tile excluders matching the modified actor, and update it in the
	/// material parameter collection.
	bool UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex);

	/// Retrieve the plane equation from the given instance.
	bool GetPlaneEquationFromUEInstance(FVector3f& OutPlaneOrientation, float& OutPlaneW, int32 InInstanceIndex) const;

	void UpdateAllClippingPlanes();

	/// Update the box 3D information in all tile excluders created for the clipping box, as well as in the
	/// material parameter collection.
	bool UpdateClippingBoxFromUEInstance(int32 InstanceIndex);

	/// Retrieve the box 3D information from the given instance.
	bool GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const;

	void UpdateAllClippingBoxes();

	bool EncodeFlippingInMPC(EITwinClippingPrimitiveType Type);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	bool TEncodeFlippingInMPC(TArray<PrimitiveInfo> const& ClippingInfos);


	void UpdatePolygonInfosFromScene();
	bool RegisterCutoutSpline(AITwinSplineHelper* SplineHelper);

	/// Change all effect helpers visibility in the viewport (without deactivating them).
	/// This affects translucent boxes/planes as well as spline meshes displayed for cutout polygons.
	void SetAllEffectHelpersVisibility(bool bVisibleInGame);
	void HideAllEffectHelpers() { SetAllEffectHelpersVisibility(false); }
	/// Show/Hide effect helpers for the given cutout type.
	void SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame);

	/// Update the instance properties to manage persistence.
	void UpdateAVizInstanceProperties(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const;
	/// Apply properties from the loaded instance.
	void UpdateClippingPropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);

private:
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	UITwinClippingMPCHolder* ClippingMPCHolder = nullptr;

	UPROPERTY()
	TArray<FITwinClippingPlaneInfo> ClippingPlaneInfos;
	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> ClippingPlanePopulation;

	UPROPERTY()
	TArray<FITwinClippingBoxInfo> ClippingBoxInfos;
	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> ClippingBoxPopulation;

	UPROPERTY()
	TArray<FITwinClippingCartographicPolygonInfo> ClippingPolygonInfos;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
