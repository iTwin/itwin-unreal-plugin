/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMapping.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Cesium3DTilesSelection/Tile.h> // see TCesiumTileID later on
#include <CesiumTileIDHash.h>
#include <CesiumMaterialType.h>
#include <ITwinElementID.h>
#include <ITwinFeatureID.h>
#include <ITwinDynamicShadingProperty.h>
#include <ITwinGltfMeshComponentWrapper.h>
#include <Math/Matrix.h>
#include <Templates/SharedPointer.h>
#include <Timeline/TimelineFwd.h>
#include <UObject/WeakObjectPtr.h>

#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class FITwinSceneMappingBuilder;

/* Cesium tiles streamed by the mesh export service contain a single gtlf mesh divided into gltf primitives.
 * Grouping into primitives was done by comparing the "display parameters" of the iModel meshes, which
 * include common 3D material parameters like texture, fill color, line color, opacity, etc. and also two
 * pieces of metadata: "category" and "subcategory". But not the ElementID, which are too fine-grained and
 * would prevent useful "batching" of the primitives and thus an inefficient number of draw calls.
 *
 * For material editing, we will first assume we can limit our capability to assigning a material per
 * category and/or subcategory, and per original iModel material, once we get some kind of materialID
 * metadata added to the generated Cesium tiles.
 *
 * For Synchro "animations", we need to assign material effects per ElementID, so we /cannot/ assume to
 * have one (or more) UE material instance(s) used only for a specific ElementID. The effects will thus be
 * applied inside our modified Cesium materials by selecting the vertex using the metadata.
 * The largest mappable type in a UE texture is 32bits, so the TITwinElementID cannot be mapped because it
 * can be arbitrarily large, the high-order bits being used as Briefcase ID.
 * We can use Cesium's readily available per-vertex FeatureIDs, which is a tile-specific zero-based index.
 * The FeatureID can be used inside the material graph to look up "animation" data (highlight, opacity,
 * cutting plane equation...), the same way Cesium metadata are intended to be used to alter shading (there
 * are a few lines of HLSL code to copy from Cesium to get X/Y texture indices from FeatureID.)
 */

struct FITwinPropertyTextureFlags
{
	/// Whether UpdateInMaterials has been called on the containing tile's property texture
	bool bTextureIsSet : 1 = false;
	/// Whether the property texture has to be setup upon next tick
	bool bNeedSetupTexture : 1 = false;

	void OnTextureSetInMaterials() {
		bTextureIsSet = true;
		bNeedSetupTexture = false;
	}

	void Invalidate() {
		bNeedSetupTexture = true;
	}
	void InvalidateOnCondition(bool bCondition) {
		if (bCondition && !bTextureIsSet)
		{
			// Texture will have to be setup upon next tick
			Invalidate();
		}
	}

	[[nodiscard]] bool ShouldUpdateMaterials(
		std::unique_ptr<FITwinDynamicShadingBGRA8Property> const& PropertyTexture) const
	{
		return PropertyTexture && bNeedSetupTexture;
	}
};

struct FITwinSynchro4DTextureFlags
{
	FITwinPropertyTextureFlags HighlightsAndOpacitiesFlags;
	FITwinPropertyTextureFlags CuttingPlaneFlags;
	FITwinPropertyTextureFlags SelectionFlags;
};

/// Scene data in a specific tile, related to a specific Element of an iModel: we have to browse through
/// all metadata when a Cesium tile is received and populate the FITwinSceneTile structure (unless it is
/// more efficient to do it on the fly only for the animated Elements).
struct FITwinElementFeaturesInTile
{
	ITwinElementID ElementID;

	/// We'll need for each tile a mapping from an ElementID to all FeatureIDs which it is made of,
	/// (*) to repeat the "animation" data at all corresponding slots in the HighlightsAndOpacities and
	/// CuttingPlane textures.
	std::vector<ITwinFeatureID> Features;

	/// All material instances for this tile which are applied on Features of this ElementID.
	/// May not actually be needed, since any material parameter changes should mean updating the tile's
	/// textures only, and those will be shared by all of the tile's materials.
	/// But if some parameter has to be changed per ElementID and per-material, then it would be faster to
	/// have that kind of mapping.
	std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;

	FITwinSynchro4DTextureFlags TextureFlags = {};

	/// Whether we have already tested if some extraction was required to add some translucency.
	bool bHasTestedForTranslucentFeaturesNeedingExtraction : 1 = false;
	/// Whether this Element was extracted (!)
	bool bIsElementExtracted : 1 = false;
	bool bIsAlphaSetInTextureToHideExtractedElement : 1 = false;

	/// Returns whether at least one of the materials is using the opaque or masked blend mode.
	bool HasOpaqueOrMaskedMaterial() const;
	TWeakObjectPtr<UMaterialInstanceDynamic> GetFirstValidMaterial() const;
};

/// Mesh extracted from the Cesium tile as an independent UE entity, for one of the following reasons:
///	1. Either it should be transparent whereas it was part of an opaque entity (or the other way round).
///    In that case, it may even be possible that in a single tile, only a part of an iModel Element could
///    be extracted, not the whole of it! Typically only the opaque parts, when applying opacity, which
///    doesn't mean translucent parts cannot stay inside their original Cesium primitives.
///	2. Or it represents an animated object, so we need to be able to transform it independently. In that case,
///    obviously, the whole Element is probably extracted, in all tiles.
struct FITwinExtractedEntity
{
	ITwinElementID ElementID;

	/// Need to store Actor.GetTransform().ToMatrixWithScale() there for entities that will have a
	/// Transform animation.
	FMatrix OriginalMatrix;
	/// Something than can be used to transform the entity in the UE world.
	TWeakObjectPtr<UStaticMeshComponent> MeshComponent;
	/// UV index where feature IDs were baked
	std::optional<uint32> FeatureIDsUVIndex;
	/// Material used to render the entity in the UE world
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	/// The original GLTF mesh component this entity was extracted from.
	TWeakObjectPtr<UStaticMeshComponent> SourceMeshComponent;

	FITwinSynchro4DTextureFlags TextureFlags = {};

	bool IsValid() const {
		return MeshComponent.IsValid() && Material.IsValid();
	}
	/// Set the extracted mesh hidden or not.
	void SetHidden(bool bHidden);
	/// Change the mesh base material.
	bool SetBaseMaterial(UMaterialInterface* BaseMaterial);
	/// Returns whether the material is using the opaque or masked blend mode.
	bool HasOpaqueOrMaskedMaterial() const;
	/// Set the ForcedAlpha (opacity) parameter of the Synchro4D material instance of this Extracted Element
	void SetForcedOpacity(float const Opacity);
};

class FITwinSceneTile
{
	/// Used to fill the textures (see (*) on TITwinFeatureID) for non-extracted Elements.
	std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile> ElementsFeatures;

	/// Extracted Elements
	std::unordered_map<ITwinElementID, FITwinExtractedEntity> ExtractedElements;

	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;

public:
	/// Maximum Feature ID in the tile. Only relevant when it does not equal ITwin::NOT_FEATURE.
	ITwinFeatureID MaxFeatureID = ITwin::NOT_FEATURE;

	/// May or may not be useful to keep the list of all material instances for each tile
	//std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
	/// RGBA texture where the 'RGB' part is the Synchro Highlight color (used by both opaque and translucent
	/// materials). The 'A' part will be used by the translucent materials for opacity animation. It can
	/// probably also be used by opaque materials (or, again, translucent materials) to mask out entirely
	/// the parts that had to be extracted as FITwinExtractedEntity.
	std::unique_ptr<FITwinDynamicShadingBGRA8Property> HighlightsAndOpacities;
	/// Note: despite the pixel format's name, alpha is still the fourth channel, otherwise
	/// EnsurePlaneEquation would have needed to swap the channels, but in practice it does not.
	std::unique_ptr<FITwinDynamicShadingABGR32fProperty> CuttingPlanes;
	/// RGBA texture where the 'RGB' part is the selection Highlight color (used to highlight the selected
	/// iTwin Element during picking).
	std::unique_ptr<FITwinDynamicShadingBGRA8Property> SelectionHighlights;

	/// Lists the different mesh components created in this tile
	std::vector<FITwinGltfMeshComponentWrapper> GltfMeshes;
	/// FeatureID UV indices are computed from FITwinSceneMapping::OnBatchedElementTimelineModified, but
	/// applied only in SetupHighlightsAndOpacities/SetupCuttingPlanes, like other material parameters
	/// (ie. BGRA and CuttingPlane textures)
	std::unordered_map<UMaterialInterface*, uint32> FeatureIDsUVIndex;

	/// Finds the FITwinElementFeaturesInTile for the passed Element ID or return nullptr
	FITwinElementFeaturesInTile const* FindElementFeatures(ITwinElementID const& ElemID) const;
	FITwinElementFeaturesInTile* FindElementFeatures(ITwinElementID const& ElemID);

	/// Finds or inserts a FITwinElementFeaturesInTile for the passed Element ID
	std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile>::iterator
		ElementFeatures(ITwinElementID const& ElemID);

	void ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile&)> const& Func);

	/// Finds the FITwinExtractedEntity for the passed Element ID or return nullptr
	FITwinExtractedEntity const* FindExtractedElement(ITwinElementID const& ElemID) const;
	FITwinExtractedEntity* FindExtractedElement(ITwinElementID const& ElemID);

	/// Finds or inserts a FITwinExtractedEntity for the passed Element ID
	std::unordered_map<ITwinElementID, FITwinExtractedEntity>::iterator
		ExtractedElement(ITwinElementID const& ElemID);

	void EraseExtractedElement(
		std::unordered_map<ITwinElementID, FITwinExtractedEntity>::iterator const Where);

	void ForEachExtractedElement(std::function<void(FITwinExtractedEntity&)> const& Func);

	void SelectElement(ITwinElementID const& InElemID, bool& bHasUpdatedTextures);
	void UpdateSelectionTextureInMaterials();
};

using CesiumTileID = Cesium3DTilesSelection::TileID;

class FITwinSceneMapping
{
public:
	using FNewTileMeshObserver =
		std::function<void(CesiumTileID const&, std::set<ITwinElementID> const& /*MeshElementIDs*/)>;
private:
	/// World AABB for some Elements: we need them for cutting plane animation. We need some kind of caching
	/// if only because there are two keyframes each time, and it's not obvious how to associate the two so
	/// currently the two missing W coordinates in the plane equations stored are computed independently :/
	std::unordered_map<ITwinElementID, FBox> KnownBBoxes;
	std::function<FITwinScheduleTimeline const& ()> TimelineGetter;
	std::function<UMaterialInterface* (ECesiumMaterialType)> MaterialGetter;
	bool bNeedUpdateSelectionHighlights = false;
	FNewTileMeshObserver NewTileMeshObserver;
	/// Global bounding box for the whole model - very coarse, as it it filled from all known tiles,
	/// taking the lowest LODs into account. This could be improved by evaluating it from the tiles currently
	/// displayed...
	FBox IModelBBox;
	/// ModelCenter is defined as the translation of the iModel
	FVector ModelCenter = FVector(0, 0, 0);

public:
	std::unordered_map<CesiumTileID, FITwinSceneTile> KnownTiles;

	void OnBatchedElementTimelineModified(CesiumTileID const&, FITwinSceneTile& SceneTile,
		FITwinElementFeaturesInTile& ElementFeaturesInTile, FITwinElementTimeline const& ModifiedTimeline);
	void OnExtractedElementTimelineModified(FITwinExtractedEntity& ExtractedEntity,
											FITwinElementTimeline const& ModifiedTimeline);
	void OnNewTileMeshFromBuilder(CesiumTileID const& TileId, FITwinSceneTile& SceneTile,
							  std::set<ITwinElementID> const& ElementIDsWithNewMats);
	void SetTimelineGetter(std::function<FITwinScheduleTimeline const& ()> const& InTimelineGetter) {
		TimelineGetter = InTimelineGetter;
	}
	void SetMaterialGetter(std::function<UMaterialInterface* (ECesiumMaterialType)> const& InMaterialGetter) {
		MaterialGetter = InMaterialGetter;
	}
	void SetNewTileMeshObserver(FNewTileMeshObserver const& InNewTileMeshObserver) {
		NewTileMeshObserver = InNewTileMeshObserver;
	}

	FBox GetBoundingBox(ITwinElementID const Element) const;
	const std::unordered_map<ITwinElementID, FBox>& GetKnownBBoxes() const {
		return KnownBBoxes;
	}
	FBox const& GetIModelBoundingBox() const {
		return IModelBBox;
	}
	FVector const& GetModelCenter() const {
		return ModelCenter;
	}

	static void SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile,
									  FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);
	static void SetupHighlights(FITwinSceneTile& SceneTile,
								FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupHighlights(FITwinSceneTile& SceneTile,
								FITwinExtractedEntity& ExtractedEntity);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile,
								   FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);

	/// \return Material parameter info structure to use to modify the ForcedAlpha value (overriding the
	///		zeroed alpha masking the matching batched mesh) in an extracted Element's material
	static FMaterialParameterInfo const& GetExtractedElementForcedAlphaMaterialParameterInfo();

	/// Extracts the given element, in all known tiles.
	 /// New Unreal entities may be created.
	 /// \return the number of created entities in Unreal.
	uint32 ExtractElement(
		ITwinElementID const Element,
		FITwinMeshExtractionOptions const& Options = {});


	/// Extract some elements in a subset of the known tiles.
	/// \param percentageOfTiles Percentage (in range [0;1] of known tiles from which to
	/// extract.
	/// \param percentageOfEltsInTile Percentage of elements which will be extracted from
	/// each tile processed.
	/// \return Number of elements newly extracted.
	/// (for debugging).
	uint32 ExtractElementsOfSomeTiles(
		float PercentageOfTiles,
		float PercentageOfEltsInTile = 0.1f,
		FITwinMeshExtractionOptions const& Options = {});

	/// Hide or Show all extracted entities.
	void HideExtractedEntities(bool bHide = true);

	/// Hide or Show primitives from which we have previously extracted some entities.
	/// This was added for debugging, and needs some optimizations in case this is meant
	/// to be used in production for some reason (typically if we want the user to be
	/// able to see only the animated parts of a Synchro4D animation...)
	void HidePrimitivesWithExtractedEntities(bool bHide = true);

	/// Bake features in per-vertex UVs, for all known meshes
	/// This was added for debugging purpose only.
	void BakeFeaturesInUVs_AllMeshes();

	/// Set the current selected ElementID - can be NOT_ELEMENT to discard any selection.
	void SelectElement(ITwinElementID const& InElemID);

	bool NeedUpdateSelectionHighlights() const {
		return bNeedUpdateSelectionHighlights;
	}
	void UpdateSelectionHighlights();

	/// Given a hit component, display the world bounding box of its owner tile.
	std::optional<CesiumTileID> DrawOwningTileBox(
		UPrimitiveComponent const* Component,
		UWorld const* World) const;

private:
	/// Extracts the given element in the given tile.
	/// New Unreal entities may be created.
	/// \return the number of created entities in Unreal.
	uint32 ExtractElementFromTile(
		CesiumTileID const TileID,
		ITwinElementID const Element,
		FITwinSceneTile& SceneTile,
		FITwinMeshExtractionOptions const& Options = {});

	friend class FITwinSceneMappingBuilder;
};
