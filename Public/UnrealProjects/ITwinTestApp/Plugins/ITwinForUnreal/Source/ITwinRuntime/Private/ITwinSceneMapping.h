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
#include <Compil/StdHash.h>
#include <ITwinCoordSystem.h>
#include <ITwinElementID.h>
#include <ITwinFeatureID.h>
#include <ITwinDynamicShadingProperty.h>
#include <ITwinGltfMeshComponentWrapper.h>
#include <Math/Matrix.h>
#include <Templates/SharedPointer.h>
#include <Timeline/Timeline.h>
#include <UObject/WeakObjectPtr.h>

#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/AlwaysFalse.h>
#	include <boost/container/small_vector.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class FITwinSceneMappingBuilder;

template <class T, std::size_t N> using FSmallVec = boost::container::small_vector<T, N>;

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
	/// Non-zero when UpdateInMaterials has been called on the containing tile's property texture. The value
	/// stored here is the size of the array of Materials when UpdateInMaterials was called, so that when it
	/// grows, the method can be called again (at the moment we never clean up stale materials, but we could
	/// use another method of detecting new materials).
	/// TODO_GCO: each FITwinElementFeaturesInTile has a set of FITwinSynchro4DTextureFlags (for each tex),
	/// which probably leads to a lot of redundant calls since batched materials are shared by many Elements,
	/// so we should probably move the batched Elements FITwinSynchro4DTextureFlags at the tile level!
	/// The value will be 0 or 1 for extracted entities' flags.
	int32 TexturesSet = 0;
	/// Whether the property texture has to be setup upon next tick
	bool bNeedSetupTexture = false;

	void OnTextureSetInMaterials(int32 const InTexturesSet) {
		TexturesSet = InTexturesSet;
		bNeedSetupTexture = false;
	}

	void Invalidate() {
		bNeedSetupTexture = true;
	}
	void InvalidateOnCondition(bool bCondition) {
		if (bCondition && TexturesSet == 0)
		{
			// Texture will have to be setup upon next tick
			Invalidate();
		}
	}

	[[nodiscard]] bool ShouldUpdateMaterials(
		std::unique_ptr<FITwinDynamicShadingBGRA8Property> const& PropertyTexture,
		int32 const InTexturesSet) const
	{
		if (PropertyTexture)
		{
			if (bNeedSetupTexture)
				return true;
			else
				return (InTexturesSet > TexturesSet);
		}
		return false;
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

	/// Needed to restore the original world transform when an Element stops being transformed or stops
	/// following a 3D path.
	FTransform OriginalTransform;
	/// To avoid resetting to OriginalTransform at each tick during a time segment where the entity is not to
	/// be transformed
	bool bIsCurrentlyTransformed = false;
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
public:
	using ExtractedEntityVec = FSmallVec<FITwinExtractedEntity, 4>;
	using ExtractedEntityMap = std::unordered_map<ITwinElementID, ExtractedEntityVec>;


	/// Maximum Feature ID in the tile. Only relevant when it does not equal ITwin::NOT_FEATURE.
	ITwinFeatureID MaxFeatureID = ITwin::NOT_FEATURE;

	/// May or may not be useful to keep the list of all material instances for each tile
	std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
	/// RGBA texture where the 'RGB' part is the Synchro Highlight color (used by both opaque and translucent
	/// materials). The 'A' part will be used by the translucent materials for opacity animation. It can
	/// probably also be used by opaque materials (or, again, translucent materials) to mask out entirely
	/// the parts that had to be extracted as FITwinExtractedEntity.
	std::unique_ptr<FITwinDynamicShadingBGRA8Property> HighlightsAndOpacities;
	bool bNeedUpdateHighlightsAndOpacitiesInMaterials = true;
	/// Note: despite the pixel format's name, alpha is still the fourth channel, otherwise
	/// EnsurePlaneEquation would have needed to swap the channels, but in practice it does not.
	std::unique_ptr<FITwinDynamicShadingABGR32fProperty> CuttingPlanes;
	bool bNeedUpdateCuttingPlanesInMaterials = true;
	/// RGBA texture where the 'RGB' part is the selection Highlight color (used to highlight the selected
	/// iTwin Element during picking).
	std::unique_ptr<FITwinDynamicShadingBGRA8Property> SelectionHighlights;

	/// Lists the different mesh components created in this tile
	std::vector<FITwinGltfMeshComponentWrapper> GltfMeshes;
	/// FeatureID UV indices are computed from FITwinSceneMapping::OnBatchedElementTimelineModified, but
	/// applied only in SetupHighlightsAndOpacities/SetupCuttingPlanes, like other material parameters
	/// (ie. BGRA and CuttingPlane textures)
	std::unordered_map<UMaterialInterface*, uint32> FeatureIDsUVIndex;

	/// Bake feature IDs in per-vertex UVs if needed
	void BakeFeatureIDsInVertexUVs(bool bUpdatingTile = false);
	/// Whether at least once of the tile meshes is (valid and) visible
	bool HasVisibleMesh() const;

	/// Finds the FITwinElementFeaturesInTile for the passed Element ID or return nullptr
	FITwinElementFeaturesInTile const* FindElementFeatures(ITwinElementID const& ElemID) const;
	FITwinElementFeaturesInTile* FindElementFeatures(ITwinElementID const& ElemID);

	/// Finds or inserts a FITwinElementFeaturesInTile for the passed Element ID
	std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile>::iterator
		ElementFeatures(ITwinElementID const& ElemID);

	void ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile&)> const& Func);

	/// Finds the FITwinExtractedEntity for the passed Element ID or return nullptr
	ExtractedEntityVec const* FindExtractedElement(ITwinElementID const& ElemID) const;
	ExtractedEntityVec* FindExtractedElement(ITwinElementID const& ElemID);

	/// Finds or inserts an ExtractedEntityVec for the passed Element ID
	ExtractedEntityMap::iterator ExtractedElement(ITwinElementID const& ElemID);

	void EraseExtractedElement(ExtractedEntityMap::iterator const Where);

	void ForEachExtractedElement(std::function<void(FITwinExtractedEntity&)> const& Func);

	bool SelectElement(ITwinElementID const& InElemID, bool& bHasUpdatedTextures, UWorld const* World);
	void UpdateSelectionTextureInMaterials();
	void DrawTileBox(UWorld const* World) const;

	template<typename ElementsCont>
	void ForEachElementFeatures(ElementsCont const& ForElementIDs,
								std::function<void(FITwinElementFeaturesInTile&)> const& Func);
	template<typename ElementsCont>
	void ForEachExtractedElement(ElementsCont const& ForElementIDs,
								 std::function<void(FITwinExtractedEntity&)> const& Func);


private:
	/// Used to fill the textures (see (*) on TITwinFeatureID) for non-extracted Elements.
	std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile> ElementsFeatures;

	/// Extracted Elements
	ExtractedEntityMap ExtractedElements;

	/// Last Element ID, if any, for which the tile's SelectionHighlight texture was set up
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
};

using CesiumTileID = Cesium3DTilesSelection::TileID;

struct FTileRequirements
{
	bool bNeedHiliteAndOpaTex = false;
	bool bNeedCuttingPlaneTex = false;
	bool bNeedTranslucentMaterial = false; ///< ie need extraction (unless material already translucent?)
	bool bNeedBeTransformable = false; ///< unused unless SYNCHRO4D_ENABLE_TRANSFORMATIONS() is set to 1
};

namespace ITwinScene
{
	DEFINE_STRONG_UINT64(ElemIdx);
	constexpr ElemIdx NOT_ELEM{ (size_t)-1 };
	DEFINE_STRONG_UINT64(DuplIdx);
	constexpr DuplIdx NOT_DUPL{ (size_t)-1 };
}

class FITwinElement
{
public:
	bool bHasMesh = false; ///< true when encountered in FITwinSceneMappingBuilder
	/// The only member strictly required for a valid structure, although usually you'll soon have either the
	/// Parent or the BBox known as well.
	ITwinElementID Id = ITwin::NOT_ELEMENT;
	/// Index of Parent in storage array
	ITwinScene::ElemIdx ParentInVec = ITwinScene::NOT_ELEM;
	/// Index in FITwinSceneMapping::DuplicateElements
	ITwinScene::DuplIdx DuplicatesList = ITwinScene::NOT_DUPL;
	/// Timeline indexing key of (one of) the timeline(s) applying to this Element.
	using FAnimKeysVec = FSmallVec<FIModelElementsKey, 2>;
	FAnimKeysVec AnimationKeys;
	/// UE-World AABB for the Elements: we need them for cutting plane animation, 3D path anchor, etc.
	FBox BBox;
	/// Stores the Element's "animation requirements" like the need for property textures or whether or not
	/// it is extracted at least in one tile. Useful to set up each tile in which they'll be found, even when
	/// we don't want to check its timelines one by one. This is filled even for Element nodes in the
	/// hierarchy tree for which bhasMesh is false, because we need the info when calling
	/// OnElementsTimelineModified on child Elements of animated tasks that may be only received later,
	/// typically in tiles of finer LOD.
	FTileRequirements TileRequirements;
	using FSubElemsVec = FSmallVec<ITwinScene::ElemIdx, 4>;
	FSubElemsVec SubElemsInVec;
};

class FITwinSceneMapping
{
public:
	using FDuplicateElementsVec = FSmallVec<ITwinScene::ElemIdx, 2>;
private:
	/// Can still be filled by Element as Cesium tiles are loaded, but since we'll want the whole hierarchy
	/// tree, as soon as it's obtained from the web services, the whole list of iModel Elements will be
	/// present in the vector, which does NOT mean we know anything more than their ParentId.
	/// To process only Elements that were visible in the UE scene at some point, you now need to check
	/// FITwinElement::bHasMesh.
	std::vector<FITwinElement> AllElements;
	std::unordered_map<ITwinElementID, ITwinScene::ElemIdx> ElementsInVec;
	/// *All* Source Element IDs retrieved from the iModel metadata query.
	std::unordered_map<FString, ITwinScene::ElemIdx/*first seen for this Source*/> SourceElementIDs;
	/// Only contains entries for Elements with a common Source Element IDs: should be much fewer of those
	/// than SourceElementIDs.
	std::vector<FDuplicateElementsVec> DuplicateElements;
	std::function<FITwinScheduleTimeline const& ()> TimelineGetter;
	std::function<UMaterialInterface* (ECesiumMaterialType)> MaterialGetter;
	bool bNewTilesReceivedHaveTextures = false;
	bool bNewTileTexturesNeedUpateInMaterials = false;
	bool bNeedUpdateSelectionHighlights = false;
	/// ID of the currently selected Element, if any
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
	/// Global bounding box for the whole model - very coarse, as it it filled from all known tiles,
	/// taking the lowest LODs into account. This could be improved by evaluating it from the tiles currently
	/// displayed...
	FBox IModelBBox_ITwin;
	FBox IModelBBox_UE; // same, but in Unreal Engine coordinate system
	/// ModelCenter, in iTwin world, is defined as the translation of the iModel ; this data is directly
	/// retrieved from the iModel.
	std::optional<FVector> ModelCenter_ITwin;
	std::optional<FVector> ModelCenter_UE; // same, but in Unreal Engine coordinate system
	/// Constant? See UITwinCesiumGltfPrimitiveComponent::UpdateTransformFromCesium
	std::optional<FTransform> CesiumToUnrealTransform;

public:
	std::unordered_map<CesiumTileID, FITwinSceneTile> KnownTiles;
	std::function<void(CesiumTileID const&, std::set<ITwinElementID>&&)> OnNewTileMeshBuilt;

	FITwinSceneMapping();

	/// Get all known Element structures in the storage array
	const std::vector<FITwinElement>& GetElements() const { return AllElements; }
	/// Get all known Elements IDs and indices in the storage array
	const std::unordered_map<ITwinElementID, ITwinScene::ElemIdx>& GetElementIDs() const {
		return ElementsInVec;
	}

	/// ElemDesignation can be by ElementID (ITwinElementID) or by index in AllElements (ITwinScene::ElemIdx)
	template<typename ElemDesignation>
	FITwinElement const& GetElement(ElemDesignation const Designation) const {
		static_assert(always_false_v<ElemDesignation>,
					  "Unsupported Element designation type for FITwinSceneMapping::GetElement");
	}
	template<> FITwinElement const& GetElement<ITwinScene::ElemIdx>(ITwinScene::ElemIdx const ByIndex) const {
		return AllElements[ByIndex.value()];
	}
	template<> FITwinElement const& GetElement<ITwinElementID>(ITwinElementID const ById) const {
		static const FITwinElement Empty;
		auto Found = ElementsInVec.find(ById);
		if (Found != ElementsInVec.end())
			return AllElements[Found->second.value()];
		else
			return Empty;
	}
	FITwinElement& ElementFor(ITwinScene::ElemIdx const ByElemIdx);
	FITwinElement& ElementFor(ITwinElementID const ById, ITwinScene::ElemIdx* IndexInVec = nullptr);
	int ParseHierarchyTree(FString const& JsonStr);
	int ParseSourceElementIDs(FString const& JsonStr);
	FDuplicateElementsVec const& GetDuplicateElements(ITwinElementID const ElemID) const;

	void OnElementsTimelineModified(CesiumTileID const& TileID, FITwinSceneTile& SceneTile,
									FITwinElementTimeline& ModifiedTimeline,
									std::vector<ITwinElementID> const* OnlyForElements = nullptr);
	/// See long comment before call, in FITwinSynchro4DSchedulesInternals::HandleReceivedElements.
	/// Does not (need to) handle selection highlight (see UpdateSelectionAndHighlightTextures).
	/// \return Whether new tiles received have textures set up for Synchro animation
	bool ReplicateAnimatedElementsSetupInTile(
		std::pair<CesiumTileID, std::set<ITwinElementID>> const& TileElements);
	size_t UpdateAllTextures();
	bool NewTilesReceivedHaveTextures(bool& bHasUpdatedTextures);
	void HandleNewTileTexturesNeedUpateInMaterials();

	void SetTimelineGetter(std::function<FITwinScheduleTimeline const& ()> const& InTimelineGetter) {
		TimelineGetter = InTimelineGetter;
	}
	void SetMaterialGetter(std::function<UMaterialInterface* (ECesiumMaterialType)> const& InMaterialGetter) {
		MaterialGetter = InMaterialGetter;
	}

	/// Get the Element's AABB in UE coordinates
	FBox const& GetBoundingBox(ITwinElementID const Element) const;
	FBox const& GetIModelBoundingBox(EITwinCoordSystem CoordSystem) const {
		return (CoordSystem == EITwinCoordSystem::UE) ? IModelBBox_UE: IModelBBox_ITwin;
	}
	FVector const& GetModelCenter(EITwinCoordSystem CoordSystem) const {
		auto&& Center = (CoordSystem == EITwinCoordSystem::UE) ? ModelCenter_UE : ModelCenter_ITwin;
		return Center ? (*Center) : FVector::ZeroVector;
	}
	std::optional<FTransform> const& GetCesiumToUnrealTransform() const {
		return CesiumToUnrealTransform;
	}

	static void SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile,
									  FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);
	static void SetupHighlightsOpacities(FITwinSceneTile& SceneTile,
								FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupHighlightsOpacities(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile,
								   FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);

	/// \return Material parameter info structure to use to modify the ForcedAlpha value (overriding the
	///		zeroed alpha masking the matching batched mesh) in an extracted Element's material
	static FMaterialParameterInfo const& GetExtractedElementForcedAlphaMaterialParameterInfo();

	/// Extract Elements in all tiles if they were not yet
	uint32 CheckAndExtractElements(std::set<ITwinElementID> const& Elements,
								   bool const bTranslucencyNeeded);
	/// Extracts the given element, in all known tiles. New Unreal entities may be created.
	/// \return the number of created entities in Unreal.
	uint32 ExtractElement(ITwinElementID const Element, FITwinMeshExtractionOptions const& Options = {});
	/// For debugging purposes - Extract some elements in a subset of the known tiles.
	/// \param percentageOfTiles Percentage (in range [0;1] of known tiles from which to extract
	/// \param percentageOfEltsInTile Percentage of elements which will be extracted from each tile processed
	/// \return Number of elements newly extracted
	uint32 ExtractElementsOfSomeTiles(float PercentageOfTiles, float PercentageOfEltsInTile = 0.1f,
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
	bool SelectElement(ITwinElementID const& InElemID, UWorld const* World);

	//! Returns the selected Element's ID, if an Element is selected, or ITwin::NOT_ELEMENT.
	ITwinElementID GetSelectedElement() const { return SelectedElement; }

	/// Called each frame: even if nothing changed in the selection, new tiles may have arrived and need it
	void UpdateSelectionAndHighlightTextures();

	/// Given a hit component, display the world bounding box of its owner tile.
	std::optional<CesiumTileID> DrawOwningTileBox(
		UPrimitiveComponent const* Component,
		UWorld const* World) const;

	/// Reset everything - should only be called before the tileset is reloaded.
	void Reset();

private:
	/// Extracts the given element in the given tile. New Unreal entities may be created.
	/// \return the number of created entities in Unreal.
	uint32 ExtractElementFromTile(ITwinElementID const Element, FITwinSceneTile& SceneTile,
		FITwinMeshExtractionOptions const& Options = {},
		std::optional<std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile>::iterator> 
			ElementFeaturesInTile = {});

	friend class FITwinSceneMappingBuilder;
};
