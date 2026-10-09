/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfoBase.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Containers/Array.h>
#include <ITwinModelType.h>

#include <Containers/Set.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <ITwinClippingInfoBase.generated.h>

class UITwinTileExcluderBase;
class AITwinSplineHelper;
class AITwinSplineTool;
class UWorld;


USTRUCT()
struct FITwinClippingInfluenceInfo
{
	GENERATED_USTRUCT_BODY()

	void SetInfluenceNone();

	/// Whether the effect applies to all layers of the given type.
	UPROPERTY()
	bool bInfluenceAll = false;

	/// Set of influenced layers. Only relevant when bInfluenceAll is false.
	UPROPERTY()
	TSet<FString> SpecificIDs;
};

USTRUCT()
struct FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual ~FITwinClippingInfoBase();

	virtual void BeforeDestroy();

	//! Copies the generic info (enabled state, influence info) from another clipping effect.
	void CopyGenericInfoFrom(FITwinClippingInfoBase const& Other);

	bool IsEnabled() const { return bIsEnabled; }
	void SetEnabled(bool bInEnabled);

	void ActivateEffectAtTilesetLevel(bool bActivate) const;

	void SetInvertEffect(bool bInvert);
	virtual bool GetInvertEffect() const { return false; }

	virtual void DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const;

	bool NeedsCreateEdgeSplines() const;
	void CreateEdgeSplines(TWeakObjectPtr<AITwinSplineTool> const& SplineTool);
	void UpdateEdgeSplinesTransform(FTransform const& InstanceTransform);
	void SetEdgeSplinesSelected(bool bSelected);
	void SetEdgeVisibility(bool bVisible);

	/// Returns whether the given model should be influenced by this clipping effect.
	/// Note that if the effect is disabled, this will always return false.
	/// (Google 3D tilesets use EITwinModelType::GlobalMapLayer as model type).
	bool ShouldInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const;

	/// Returns whether the given model should be influenced by this clipping effect, independently of the
	/// enabled state of the effect.
	inline bool DoesInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const;

	/// Whether the influence of this effect is defined by layer type (iModel, Reality data, etc.).
	bool IsUsingPerLayerTypeInfluence() const;
	/// Converts the influence of this effect to be defined by layer instead of layer type.
	/// \param InCurrentLayers The list of currently loaded layers, per layer type, to initialize the new
	/// per-layer influence settings. This is required to avoid losing the current influence settings during
	/// the conversion.
	void ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers);

	bool ShouldInfluenceFullModelType(EITwinModelType ModelType) const;
	void SetInfluenceFullModelType(EITwinModelType ModelType, bool bAll);

	void SetInfluenceSpecificModel(const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	/// Make the effect apply to none.
	void SetInfluenceNone();

	/// Get the influence info for the given model type.
	inline FITwinClippingInfluenceInfo const& GetInfluenceInfo(EITwinModelType ModelType) const;

	/// Get the bounds of the area influenced by this clipping primitive, in world coordinates.
	/// An invalid box will be returned if the cutout influences the Google tileset (which is infinite), or
	/// if it influences nothing.
	FBox const& GetInfluenceBoundingBox() const;

	void InvalidateInfluenceBoundingBox();
	void UpdateInfluenceBoundingBox(UWorld const* World);
	bool NeedsUpdateInfluenceBoundingBox() const { return bNeedsUpdateBoundingBox; }

	FBox const& GetUpToDateInfluenceBoundingBox(UWorld const* World);

	//! Persistence support: returns true if there is an existing link for this effect in the scene.
	bool HasSceneLink() const { return SceneLinkId.IsValid(); }
	AdvViz::SDK::RefID const& GetSceneLinkId() const { return SceneLinkId; }
	void SetSceneLinkId(AdvViz::SDK::RefID const& InSceneLinkId);

	//! Append a tile excluder to the list of excluders that are used by this clipping primitive, so that
	//! the latter can be deactivated in the excluder when the primitive is removed or disabled.
	void RecordTileExcluder(UITwinTileExcluderBase* Excluder);

protected:
	virtual void DoSetInvertEffect(bool bInvert);
	virtual void DoSetEnabled(bool bInEnabled);

	virtual int32 CountRequiredEdgeSplines() const { return 0; }
	virtual void DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& OutEdgeSplines, AITwinSplineTool& SplineTool);

private:
	inline FITwinClippingInfluenceInfo& MutableInfluenceInfo(EITwinModelType ModelType);


protected:

	/// Cesium tile exclusion helpers created for this primitive.
	UPROPERTY()
	TArray<TWeakObjectPtr<UITwinTileExcluderBase>> TileExcluders;

private:
	UPROPERTY()
	bool bIsEnabled = true;

	UPROPERTY()
	FITwinClippingInfluenceInfo IModelInfluenceInfo;

	UPROPERTY()
	FITwinClippingInfluenceInfo RealityDataInfluenceInfo;

	// Global Map Layers is the generic term for tilesets such as the Google tileset.
	UPROPERTY()
	FITwinClippingInfluenceInfo GlobalMapLayersInfluenceInfo;

	/// Bounding box of the zone influenced by this clipping primitive, in world coordinates.
	/// Beware it will need to be updated if the corresponding layer is moved/transformed.
	FBox InfluenceBoundingBox;
	bool bNeedsUpdateBoundingBox = true;

	// Helper splines to visualize edges behind other objects (used for box and plane).
	TArray<TObjectPtr<AITwinSplineHelper>> EdgeSplines;

	/// Reference to the corresponding SceneLink, if any. This is used to update the cutout's state in the
	/// SceneLink, so that it is properly saved and restored with the scene.
	AdvViz::SDK::RefID SceneLinkId = AdvViz::SDK::RefID::Invalid();
};
