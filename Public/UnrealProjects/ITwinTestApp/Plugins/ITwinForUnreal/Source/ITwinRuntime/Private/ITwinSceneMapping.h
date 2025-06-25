/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMapping.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Hashing/CesiumTileID.h>
#include <Hashing/UnrealGuid.h>
#include <Hashing/UnrealString.h>

#include <Cesium3DTilesSelection/Tile.h> // see CesiumTileID later on
#include <CesiumMaterialType.h>
#include <ITwinCoordSystem.h>
#include <ITwinElementID.h>
#include <ITwinFeatureID.h>
#include <ITwinDynamicShadingProperty.h>
#include <ITwinGltfMeshComponentWrapper.h>
#include <ITwinSceneMappingTypes.h>
#include <ITwinUtilityLibrary.h>

#include <DrawDebugHelpers.h>
#include <MaterialTypes.h>
#include <Math/Matrix.h>
#include <Misc/Guid.h>
#include <Templates/SharedPointer.h>
#include <Timeline/Timeline.h>
#include <UObject/WeakObjectPtr.h>

#include <functional> // std::function, but also std::reference_wrapper
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <boost/container/small_vector.hpp>
	#include <boost/multi_index_container.hpp>
	#include <boost/multi_index/hashed_index.hpp>
	#include <boost/multi_index/member.hpp>
	#include <boost/multi_index/random_access_index.hpp>
#include <Compil/AfterNonUnrealIncludes.h>


class AActor;
class UCesiumCustomVisibilitiesMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class FITwinSceneMappingBuilder;
class UTexture;

namespace AdvViz::SDK
{
	enum class EChannelType : uint8_t;
	struct ITwinUVTransform;
}

template <class T, std::size_t N> using FSmallVec = boost::container::small_vector<T, N>;

/* Cesium tiles streamed by the mesh export service contain a single gtlf mesh divided into gltf primitives.
 * Grouping into primitives was done by comparing the "display parameters" of the iModel meshes, which
 * include common 3D material parameters like texture, fill color, line color, opacity, etc. and also two
 * pieces of metadata: "category" and "subcategory". But not the ElementID, which are too fine-grained and
 * would prevent useful "batching" of the primitives and thus an inefficient number of draw calls.
 *
 * For material assignment per Element, the gltf meshes are retuned to isolate the specified Elements.
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

class FITwinPropertyTextureFlag
{
	/// Non-zero when SetupInMaterials has been called on the containing tile's property texture, for the
	/// material(s) used by the "item" this flag refers to: this can be either an FITwinElementFeaturesInTile,
	/// or an FITwinExtractedEntity. The value stored here is the size of the array of Materials when
	/// SetupInMaterials was called (for FITwinExtractedEntity it is always 1), so that when it grows,
	/// the method can be called again (at the moment we never clean up stale materials, but we could
	/// use another method of detecting new materials).
	/// TODO_GCO: each FITwinElementFeaturesInTile has a set of FITwinShadingTextureFlags (for each tex),
	/// which probably leads to a lot of redundant calls since batched materials are shared by many Elements,
	/// so we should probably move the batched Elements FITwinShadingTextureFlags at the tile level!
	/// The value will be 0 or 1 for extracted entities' flags.
	int32 TexturesSetup = 0;

public:
	void OnTextureSetupInMaterials(int32 const InTexturesSetup) { TexturesSetup = InTexturesSetup; }

	void Invalidate() { TexturesSetup = 0; }

	template<typename DataType, int NumChannels>
	[[nodiscard]] bool NeedSetupInMaterials(
		std::shared_ptr<FITwinDynamicShadingProperty<DataType, NumChannels>> const& PropertyTexture,
		int32 const TexturesToSetup) const
	{
		return PropertyTexture && (TexturesToSetup > TexturesSetup);
	}
};

struct FITwinShadingTextureFlags
{
	FITwinPropertyTextureFlag Synchro4DHighlightOpaTexFlag;
	FITwinPropertyTextureFlag Synchro4DCuttingPlaneTexFlag;
	FITwinPropertyTextureFlag SelectingAndHidingTexFlag;

	void InvalidateAll()
	{
		Synchro4DHighlightOpaTexFlag.Invalidate();
		Synchro4DCuttingPlaneTexFlag.Invalidate();
		SelectingAndHidingTexFlag.Invalidate();
	}
};

/// Scene data in a specific tile, related to a specific Element of an iModel: we have to browse through
/// all metadata when a Cesium tile is received and populate the FITwinSceneTile structure (unless it is
/// more efficient to do it on the fly only for the animated Elements).
struct FITwinElementFeaturesInTile
{
	/// I would have preferred it const, but see comment in implem of FITwinElementFeaturesInTile::Unload
	ITwinElementID ElementID;
	/// Direct index into the FITwinSceneMapping's AllElements container: all Elements for which we have
	/// received some geometry have this member set to a valid index.
	ITwinScene::ElemIdx SceneRank = ITwinScene::NOT_ELEM;
	/// Direct index into the FITwinSceneTile's ExtractedElements container: only Elements for which we need
	/// to (or have already) extract(ed) some geometry have this member set to a valid index.
	ITwinTile::ExtrIdx ExtractedRank = ITwinTile::NOT_EXTR;

	/// We'll need for each tile a mapping from an ElementID to all FeatureIDs which it is made of,
	/// to repeat the "animation" data at all corresponding slots in the HighlightsAndOpacities and
	/// CuttingPlane textures.
	FSmallVec<ITwinFeatureID, 2> Features;
	/// All material instances for this tile which are applied on Features of this ElementID.
	FSmallVec<TWeakObjectPtr<UMaterialInstanceDynamic>, 2> Materials;
	/// All mesh components of this tile which contain Features of this ElementID, known by their rank
	/// in the SceneTile's GltfMeshes vector.
	FSmallVec<int32_t, 2> Meshes;

	FITwinShadingTextureFlags TextureFlags = {};
	/// Invalidate the Selection flag for both this Element and all its Extracted entities
	void InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& SceneTile);

	/// Whether this Element was already extracted. If so, it must have been done to comply to *all* anim
	/// requirements, even if the StateToApply at the time had no partial transparency
	bool bIsElementExtracted : 1 = false;
	/// Whether we have already tested if some extraction was required to add some translucency (used to limit
	/// the number of calls to HasOpaqueOrMaskedMaterial in CheckAndExtractElements)
	bool bHasTestedForTranslucentFeaturesNeedingExtraction : 1 = false;
	bool bIsAlphaSetInTextureToHideExtractedElement : 1 = false;

	/// Returns whether at least one of the materials is using the opaque or masked blend mode.
	[[nodiscard]] bool HasOpaqueOrMaskedMaterial() const;
	[[nodiscard]] TWeakObjectPtr<UMaterialInstanceDynamic> GetFirstValidMaterial() const;
	/// Clear data related to this Element used after loading a given tile: we want to keep the ordering in
	/// the FITwinSceneTile even if the tile is unloaded/reloaded, which might not yield the same order of
	/// ElementID encountered in OnMeshConstructed (eg. following retuning)
	void Unload();
};

/// Same as FITwinElementFeaturesInTile, but with Material ID as key (and simplified a bit, as we only use
/// this mapping to perform per-material selection highlights.
struct FITwinMaterialFeaturesInTile
{
	/// same comment about constancy as for FITwinElementFeaturesInTile
	ITwinMaterialID MaterialID;
	FITwinPropertyTextureFlag SelectingAndHidingTexFlag = {};
	FSmallVec<ITwinFeatureID, 2> Features;

	void InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& SceneTile);
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
	ITwinElementID const ElementID;

	/// Needed to restore the original world transform when an Element stops being transformed or stops
	/// following a 3D path.
	FTransform OriginalTransform;
	/// To avoid resetting to OriginalTransform at each tick during a time segment where the entity is not to
	/// be transformed
	bool bIsCurrentlyTransformed = false;
	/// Something than can be used to transform the entity in the UE world.
	TWeakObjectPtr<UCesiumCustomVisibilitiesMeshComponent> MeshComponent;
	/// UV index where feature IDs were baked
	std::optional<uint32> FeatureIDsUVIndex;
	/// Material used to render the entity in the UE world
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;

	FITwinShadingTextureFlags TextureFlags = {};

	[[nodiscard]] bool IsValid() const {
		return MeshComponent.IsValid() && Material.IsValid();
	}
	/// Set the extracted mesh hidden or not.
	void SetHidden(bool bHidden);
	/// Change the mesh base material.
	bool SetBaseMaterial(UMaterialInterface* BaseMaterial);
	/// Returns whether the material is using the opaque or masked blend mode.
	[[nodiscard]] bool HasOpaqueOrMaskedMaterial() const;
	/// Set the ForcedAlpha (opacity) parameter of the Synchro4D material instance of this Extracted Element
	void SetForcedOpacity(float const Opacity);
};

struct FITwinExtractedElement
{
	ITwinElementID const ElementID;
	FSmallVec<FITwinExtractedEntity, 2> Entities;
	/// Clear data related to this Element used after loading a given tile: we want to keep the ordering (and
	/// the allocations (*) !!) in the FITwinSceneTile even if the tile is unloaded/reloaded.
	/// (*) because we never erase from TimelineOptim->Tiles so the calls to ExtractedElementSLOW are skipped
	/// in FITwinSceneMapping::OnElementsTimelineModified the 2nd time we load a given tile.
	void Unload();
};

using CesiumTileID = Cesium3DTilesSelection::TileID;

/// "Useless" since it is the first and thus default index into the multi_index_container's below.
/// On retrospect, it's probably better to use it rather than rely on the container declaration, to
/// make it easier to possibly refactor it in the future. Also less ambiguous when reading code.
struct IndexByRank {};
/// Use eg. ElementsFeatures.get<IndexByElemID>() to get the interface for this type of indexing
struct IndexByElemID {};
/// For containers indexing CesiumTileIDs
struct IndexByTileID {};
/// For containers indexing ITwinScene::TileIdx
struct IndexByTileRank {};
/// Use eg. MaterialsFeatures.get<IndexByMaterialID>() to get the interface for this type of indexing
struct IndexByMaterialID {};
/// For FederatedElementGUIDs
struct IndexByGUID {};
/// For SourceElementIDs
struct IndexBySourceID {};

class FITwinSceneTile
{
	friend class FITwinSceneMapping;
	friend class FITwinSceneMappingBuilder;

	/// Use random_access as the first, and thus the default, indexation mode, to "discourage" the use
	/// of the slower alternative (ElementID hashing), typically for performance-critical tasks like
	/// ApplyAnimation
	using FElementFeaturesCont = boost::multi_index_container<FITwinElementFeaturesInTile,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<boost::multi_index::tag<IndexByRank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByElemID>,
				boost::multi_index::member<FITwinElementFeaturesInTile, const ITwinElementID,
											&FITwinElementFeaturesInTile::ElementID>>>>;

	/// Use random_access as the first, and thus the default, indexation mode, to "discourage" the use
	/// of the slower alternative (ElementID hashing), typically for performance-critical tasks like
	/// ApplyAnimation
	using FExtractedElementCont = boost::multi_index_container<FITwinExtractedElement,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<boost::multi_index::tag<IndexByRank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByElemID>,
				boost::multi_index::member<FITwinExtractedElement, const ITwinElementID,
											&FITwinExtractedElement::ElementID>>>>;

	/// Same as for Elements, but for iTwin Material ID
	using FMaterialFeaturesCont = boost::multi_index_container<FITwinMaterialFeaturesInTile,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<boost::multi_index::tag<IndexByRank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByMaterialID>,
				boost::multi_index::member<FITwinMaterialFeaturesInTile, const ITwinMaterialID,
											&FITwinMaterialFeaturesInTile::MaterialID>>>>;

public:
	/// Without this the class is not EmplaceConstructible, maybe std::variant isn't?
	/// (CesiumTileID is a variant)
	FITwinSceneTile(const CesiumTileID& InTileID) : TileID(InTileID) {}

	/// I would have preferred it const, but see comment in implem of FITwinSceneTile::Unload
	CesiumTileID TileID;
	Cesium3DTilesSelection::Tile* pCesiumTile = nullptr;
	/// Maximum Feature ID in the tile. Only relevant when it does not equal ITwin::NOT_FEATURE.
	ITwinFeatureID MaxFeatureID = ITwin::NOT_FEATURE;
	/// RGBA texture where the 'RGB' part is the Synchro Highlight color (used by both opaque and translucent
	/// materials). The 'A' part will be used by the translucent materials for opacity animation. It can
	/// probably also be used by opaque materials (or, again, translucent materials) to mask out entirely
	/// the parts that had to be extracted as FITwinExtractedEntity.
	std::shared_ptr<FITwinDynamicShadingBGRA8Property> HighlightsAndOpacities;
	/// Note: despite the pixel format's name, alpha is still the fourth channel, otherwise
	/// EnsurePlaneEquation would have needed to swap the channels, but in practice it does not.
	std::shared_ptr<FITwinDynamicShadingABGR32fProperty> CuttingPlanes;
	/// Indices to be used in MainTimeline::GetContainer() - TODO_GCO: use strong type
	std::vector<int> TimelinesIndices;
	bool bIsSetupFor4DAnimation = false;
	/// Whether this tile is actually displayed in the viewport, as instructed by Cesium's tile selection
	/// algorithms. Value cached to detect changes in visibility and trigger 4D anim updates.
	/// NOTE: defaults to false so that unloaded tiles are rightfully considered invisible!
	bool bVisible = false;

private:
	/// List of all material instances in this tile, including those created for extracted Elements
	std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
	/// RGBA texture where the 'RGB' part is the selection Highlight color (used to highlight the selected
	/// iTwin Element during picking).
	std::shared_ptr<FITwinDynamicShadingBGRA8Property> SelectingAndHiding;

	bool bNeed4DHighlightsOpaTextureSetupInMaterials = false;
	bool bNeed4DCuttingPlanesTextureSetupInMaterials = false;
	bool bNeedSelectingAndHidingTextureSetupInMaterials = false;

	/// Lists the different mesh components created in this tile
	std::vector<FITwinGltfMeshComponentWrapper> GltfMeshes;

	/// Used to fill the textures (see (*) on FITwinElementFeaturesInTile::Features) for non-extracted Elements
	FElementFeaturesCont ElementsFeatures;
	/// Extracted Elements
	FExtractedElementCont ExtractedElements;
	// ExtractedElements being preallocated in FITwinSceneMapping::OnElementsTimelineModified and referenced
	// by their rank ("IndexByRank"), we shouldn't erase entries!
	//void EraseExtractedElement(FExtractedElementCont::const_iterator const Where);

	/// Last Element ID, if any, which the tile's SelectingAndHiding texture was actually used to highlight
	/// (ie the Element indeed has Features in this tile - see SelectedElemNotInTile below)
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
	/// Same principle, but for Material selection.
	ITwinMaterialID SelectedMaterial = ITwin::NOT_MATERIAL;
	
	std::unordered_set<ITwinElementID> CurrentSavedViewHiddenElements;
	std::unordered_set<ITwinElementID> CurrentConstructionHiddenElements;

	/// Store materials in which we have set our own custom textures, as the initial textures should be
	/// restored before #destroyGltfParameterValues is called (our textures are either static textures which
	/// should never be deleted (NoColorTexture etc.) or textures created from local files (their deletion
	/// should be managed as well in our code).
	struct FMatTextureRestoreInfo
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Mat = nullptr;
		std::array<TWeakObjectPtr<UTexture>, 2> OrigTextures = { nullptr, nullptr };
	};
	std::unordered_map<UMaterialInstanceDynamic*,
		std::unordered_map<AdvViz::SDK::EChannelType, FMatTextureRestoreInfo> > MatsWithTexturesToRestore;

	/// Maintain the correspondence between iTwin Material IDs and the features they are used in.
	/// Used for material selection.
	FMaterialFeaturesCont MaterialsFeatures;

	class ElementSelectionHelper;
	class MaterialSelectionHelper;

	struct FTextureNeeds
	{
		bool bWasCreated = false; ///< Created textures need to be set up in materials
		bool bWasChanged = false; ///< Changed textures need to be sent to the renderer for update on GPU
	};
	template <typename FeaturesInTile>
	void CreateAndSetSelectingAndHiding(FeaturesInTile& FeaturesToSelectOrHide,
		FTextureNeeds& TextureNeeds, const std::array<uint8, 4>& Color_BGRA, bool const bColorOrAlpha);

	/// \param bTestElementVisibility Pass true when this method is called to interactively select an Element,
	///		which is only allowed when it is visible (because the call is initiated by geometry intersection
	///		algorithms that have no knowledge of the possible masking of Elements by the 4D effects or custom
	///		hiding requests from the user (eg. construction data or applying Saved View properties)
	/// \return Whether the Element could indeed be selected in any of the currently known tiles. Failure to
	///		select can indeed happen when the Element is hidden through the material shader (but NO LONGER
	///		when it is already selected!)
	bool PickElement(ITwinElementID const& InElemID, bool const bOnlyVisibleTiles, FTextureNeeds& TextureNeeds,
		bool const bTestElementVisibility = false, bool const bSelectElement = true);
	/// Same as PickElement, but for a Material ID.
	bool PickMaterial(ITwinMaterialID const& InMaterialID, bool const bOnlyVisibleTiles, FTextureNeeds& TextureNeeds,
		bool const bTestElementVisibility = false, bool const bSelectMaterial = true);

	template<typename SelectableHelper, typename SelectableID>
	bool TPickSelectable(SelectableHelper const& PickHelper, SelectableID const& InElemID,
		bool const bOnlyVisibleTiles,
		FTextureNeeds& TextureNeeds,
		bool const bTestElemVisibility = false,
		bool const bSelectElem = true);

	template<typename SelectableHelper>
	void TResetSelection(SelectableHelper const& Helper, FTextureNeeds& TextureNeeds);
	void ResetSelection(FTextureNeeds& TextureNeeds);

	void HideElements(std::unordered_set<ITwinElementID> const& InElemIDs, bool const bOnlyVisibleTiles,
		FTextureNeeds& TextureNeeds, bool const IsConstruction);

	template<typename ElementsCont>
	void ForEachElementFeaturesSLOW(ElementsCont const& ForElementIDs,
									std::function<void(FITwinElementFeaturesInTile&)> const& Func);
	template<typename ElementsCont>
	void ForEachExtractedElementSLOW(ElementsCont const& ForElementIDs,
									 std::function<void(FITwinExtractedEntity&)> const& Func);
	void UseTunedMeshAsExtract(FITwinExtractedElement& DummyExtr, int32_t const GltfMeshWrapperIndex,
							   FTransform const& IModelTilesetTransform);

	/// Edit all material instances created from a primitive configured to match a given ITwin material ID.
	void ForEachMaterialInstanceMatchingID(uint64_t ITwinMaterialID,
										   std::function<void(UMaterialInstanceDynamic&)> const& Func);
	void SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID, AdvViz::SDK::EChannelType Channel,
										UTexture* pTexture);
	void ResetCustomTexturesInMaterials();

public:
	[[nodiscard]] bool IsLoaded() const;
	void Unload();
	FString ToString() const;
	void DrawTileBox(UWorld const* World) const; ///< no-op if !ENABLE_DRAW_DEBUG
	//[[nodiscard]] bool HasAnyVisibleMesh() const; <== no longer used, blame here to retrieve
	void AddMaterial(UMaterialInstanceDynamic* MaterialInUse);
	[[nodiscard]] std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> const& GetMaterials() const
		{ return Materials; }
	[[nodiscard]] std::vector<FITwinGltfMeshComponentWrapper> const& GetGltfMeshWrappers() const
		{ return GltfMeshes; }
	[[nodiscard]] std::vector<FITwinGltfMeshComponentWrapper>& GltfMeshWrappers()
		{ return GltfMeshes; }
	[[nodiscard]] bool Need4DAnimTexturesSetupInMaterials() const;
	[[nodiscard]] bool NeedSelectingAndHidingTexturesSetupInMaterials() const;
	[[nodiscard]] size_t NumElementsFeatures() const { return ElementsFeatures.size(); }
	[[nodiscard]] bool HasInitialTexturesForChannel(UMaterialInstanceDynamic*, AdvViz::SDK::EChannelType) const;
	void StoreInitialTexturesForChannel(UMaterialInstanceDynamic*, AdvViz::SDK::EChannelType,
		UTexture* pTexture_GlobalParam, UTexture* pTexture_LayerParam);

	/// Finds the FITwinElementFeaturesInTile for the passed Element ID or return nullptr
	/// \return A short-lived, const pointer on the existing FITwinElementFeaturesInTile, or nullptr
	[[nodiscard]] FITwinElementFeaturesInTile const* FindElementFeaturesConstSLOW(ITwinElementID const& ElemID,
		ITwinTile::ElemIdx* OutRank = nullptr) const;
	/// Finds the FITwinElementFeaturesInTile for the passed Element ID or return nullptr
	/// \return A short-lived, non-const pointer on the existing FITwinElementFeaturesInTile, or nullptr
	[[nodiscard]] FITwinElementFeaturesInTile* FindElementFeaturesSLOW(ITwinElementID const& ElemID,
		ITwinTile::ElemIdx* OutRank = nullptr);

	/// Finds or inserts a FITwinElementFeaturesInTile for the passed Element ID
	/// \return A short-lived, non-const reference on the existing or inserted FITwinElementFeaturesInTile
	[[nodiscard]] FITwinElementFeaturesInTile& ElementFeaturesSLOW(ITwinElementID const& ElemID);
	/// \return A short-lived, non-const reference on the existing FITwinElementFeaturesInTile
	[[nodiscard]] FITwinElementFeaturesInTile& ElementFeatures(ITwinTile::ElemIdx const Idx);

	void ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile&)> const& Func);
	void ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile const&)> const& Func) const;
	void ForEachExtractedElement(std::function<void(FITwinExtractedElement&)> const& Func);
	void ForEachExtractedElement(std::function<void(FITwinExtractedElement const&)> const& Func) const;
	void ForEachExtractedEntity(std::function<void(FITwinExtractedEntity&)> const& Func);
	void ForEachExtractedEntity(std::function<void(FITwinExtractedEntity const&)> const& Func) const;

	/// Finds the FITwinExtractedEntity for the passed Element ID or return nullptr
	/// \return A short-lived, const pointer on the existing FITwinExtractedElement, or nullptr
	[[nodiscard]] FITwinExtractedElement const* FindExtractedElementSLOW(ITwinElementID const& ElemID) const;
	/// \return A short-lived, non-const pointer on the existing FITwinExtractedElement, or nullptr
	[[nodiscard]] FITwinExtractedElement* FindExtractedElementSLOW(ITwinElementID const& ElemID);
	/// \return A short-lived, non-const reference on the existing FITwinExtractedElement
	[[nodiscard]] FITwinExtractedElement& ExtractedElement(ITwinTile::ExtrIdx const Idx);

	/// Finds or inserts an FITwinExtractedElement for the passed Element ID.
	/// \return A short-lived, non-const reference on the existing or inserted FITwinExtractedEntity
	[[nodiscard]] std::pair<std::reference_wrapper<FITwinExtractedElement>, bool> ExtractedElementSLOW(
		FITwinElementFeaturesInTile& ElementInTile);


	/// Finds the FITwinMaterialFeaturesInTile for the passed Material ID or return nullptr
	/// \return A short-lived, const pointer on the existing FITwinMaterialFeaturesInTile, or nullptr
	[[nodiscard]] FITwinMaterialFeaturesInTile const* FindMaterialFeaturesConstSLOW(
		ITwinMaterialID const& MatID) const;
	/// @copydoc FindMaterialFeaturesConstSLOW
	[[nodiscard]] FITwinMaterialFeaturesInTile* FindMaterialFeaturesSLOW(ITwinMaterialID const& MatID);

	/// Finds or inserts a FITwinMaterialFeaturesInTile for the passed Material ID
	/// \return A short-lived, non-const reference on the existing or inserted FITwinMaterialFeaturesInTile
	[[nodiscard]] FITwinMaterialFeaturesInTile& MaterialFeaturesSLOW(ITwinMaterialID const& MatID);

}; // class FITwinSceneTile

constexpr size_t NO_EXTRACTION = (size_t)-1;

/// Optimization structure to speed up application of a timeline by giving direct access to affected
/// data without involving hashing (by CesiumTileID or ElementID, typically)
struct FTimelineToSceneTile
{
	/// Tile rank in FITwinSceneMapping::KnownTiles
	const ITwinScene::TileIdx Rank;
	/// First index of ITwinTile::ElemIdx related to this tile in FTimelineToScene::TileElems, same for
	/// ITwinScene::ElemIdx in FTimelineToScene::SceneElems
	size_t FirstElement = 0;
	/// Total successive number of entries relevant to this tile in FTimelineToScene's members TileElems and
	/// SceneElems.
	uint32_t NbOfElements = 0;
	/// When not "(size_t)-1", first index of ITwinTile::ExtrIdx related to this tile in
	/// FTimelineToScene::Extracts. When left to the default value, it means there should be no extraction
	/// needed (or separately gltf-tuned mesh) for this Timeline (whatever the tile)
	size_t FirstExtract = NO_EXTRACTION;
	/// When extracting and not using glTF tuner, NbOfElements==NbOfExtracts, because we also need
	/// direct access to ITwinElementFeatures, which means looping on both in sync.
	/// Note that ExtractedElements will thus contain entries of all Elements of a timeline that has partial
	/// visibility, even if the Element already has a translucent material and does not actually requires
	/// extraction (not the most common case anyway).
	/// Also used when separating meshes using glTF tuner, because in that case we need a list of meshes and
	/// materials on which to apply translucencies and transformations. But in that case we will usually have
	/// (far) fewer meshes than Elements (whereas extraction produced on mesh per Element).
	uint32_t NbOfExtracts = 0;
};

/// Structure optimizing access to FITwinElementFeaturesInTile and FITwinExtractedElement for a given
/// timeline. We need to guarantee that a CesiumTileID will appear only once in the Tiles vector, because
/// the data persists when unloading then reloading a tile, so we shouldn't insert data multiple times.
struct FTimelineToScene
{
	/// multi_index_container only used to easily designate FTimelineToSceneTile::Rank as the hashing key
	boost::multi_index_container<FTimelineToSceneTile,
		boost::multi_index::indexed_by<
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByTileRank>,
				boost::multi_index::member<FTimelineToSceneTile, const ITwinScene::TileIdx,
											&FTimelineToSceneTile::Rank>>>>
		Tiles;
	/// Stores indices of FITwinElementFeaturesInTile in _different_ tiles, only meaningful in conjunction
	/// with the 'Tiles' member
	std::vector<ITwinTile::ElemIdx> TileElems;
	/// Stores indices of FITwinElement in the FITwinSceneMapping's container (to access anim requirements).
	/// Same size as TileElems.
	std::vector<ITwinScene::ElemIdx> SceneElems;
	/// Stores indices of FITwinExtractedElement in _different_ tiles, only meaningful in conjunction with
	/// the 'Tiles' member. Also used when separating meshes using glTF tuner!
	std::vector<ITwinTile::ExtrIdx> Extracts;
};

/// Global requirements to animate an Element, accounting for all timelines using it, but independently of
/// the tiles where it is found.
/// \see FITwinElement::Requirements
struct FElemAnimRequirements
{
	/// In practice, it means the Element is animated at least by one timeline, since almost any kind of 
	/// animation will require at least the alpha channel (see comment over tex creation in
	/// FITwinSceneMapping::OnElementsTimelineModified)
	bool bNeedHiliteAndOpaTex = false;
	/// Whether at least one timeline with growth simulation animates the Element
	bool bNeedCuttingPlaneTex = false;
	/// Whether at least one timeline with partial translucency animates the Element, ie. it will need
	/// extraction in tiles where not all its materials are translucent
	bool bNeedTranslucentMat = false;
	/// At least one timeline has a transformation for this Element, which will therefore need extraction
	/// in all tiles (not actually used, as it happens, but there for consistency).
	bool bNeedBeTransformable = false;
};

class FITwinElement
{
public:
	bool bHasMesh = false; ///< true when encountered in FITwinSceneMappingBuilder
	/// The only member strictly required for a valid structure, although usually you'll soon have either the
	/// Parent or the BBox known as well.
	const ITwinElementID ElementID = ITwin::NOT_ELEMENT;
	/// Index of Parent in storage array
	ITwinScene::ElemIdx ParentInVec = ITwinScene::NOT_ELEM;
	/// Index in FITwinSceneMapping::DuplicateElements
	ITwinScene::DuplIdx DuplicatesList = ITwinScene::NOT_DUPL;
	/// Timeline indexing key of (one of) the timeline(s) applying to this Element.
	using FAnimKeysVec = FSmallVec<FIModelElementsKey, 2>;
	FAnimKeysVec AnimationKeys;
	/// UE-World AABB for the Elements AS IF the iModel were "untransformed", ie default-placed (no custom
	/// offset nor rotation). We need the boxes for cutting plane animation, 3D path anchor, etc. which need
	/// to always appear as if applied on an untransformed iModel.
	FBox BBox;
	/// Stores the Element's "animation requirements" like the need for property textures or whether or not
	/// it is extracted at least in one tile. Useful to set up each tile in which they'll be found, even when
	/// we don't want to check its timelines one by one. This is filled even for Element nodes in the
	/// hierarchy tree for which bHasMesh is false, because we need the info when calling
	/// OnElementsTimelineModified on child Elements of animated tasks that may be only received later,
	/// typically in tiles of finer LOD.
	FElemAnimRequirements Requirements;
	using FSubElemsVec = FSmallVec<ITwinScene::ElemIdx, 4>;
	FSubElemsVec SubElemsInVec;
};

/// Helper used to postpone texture updates at the end of its scope.
struct [[nodiscard]] FITwinTextureUpdateDisabler
{
	FITwinTextureUpdateDisabler(FITwinSceneMapping& InOwner);
	~FITwinTextureUpdateDisabler();

	FITwinSceneMapping& Owner;
	const bool bPreviouslyDisabled;
};


class FITwinSceneMapping
{
public:
	using FDuplicateElementsVec = FSmallVec<ITwinScene::ElemIdx, 2>;
private:
	using FSceneElemsCont = boost::multi_index_container<FITwinElement,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<boost::multi_index::tag<IndexByRank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByElemID>,
				boost::multi_index::member<FITwinElement, const ITwinElementID, &FITwinElement::ElementID>>>>;
	/// Can still be filled by Element as Cesium tiles are loaded, but since we'll want the whole hierarchy
	/// tree, as soon as it's obtained from the web services, the whole list of iModel Elements will be
	/// present in the container, which does NOT mean we know anything more than their ParentId.
	/// To process only Elements that were visible in the UE scene at some point, you now need to check
	/// FITwinElement::bHasMesh.
	FSceneElemsCont AllElements;
	struct FElemGuid
	{
		ITwinScene::ElemIdx Rank; ///< first seen for this GUID, in case of duplicates
		FGuid FederatedGuid;
	};
	// Note: bidirectional map, without drawing in boost::bimap dependency...
	using FElemGuidsCont = boost::multi_index_container<FElemGuid,
		boost::multi_index::indexed_by<
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByRank>,
				boost::multi_index::member<FElemGuid, ITwinScene::ElemIdx, &FElemGuid::Rank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByGUID>,
				boost::multi_index::member<FElemGuid, FGuid, &FElemGuid::FederatedGuid>>>>;
	FElemGuidsCont FederatedElementGUIDs;
	struct FSourceElem
	{
		ITwinScene::ElemIdx Rank; ///< first seen for this GUID, in case of duplicates
		FString SourceId;
	};
	// Note: bidirectional map, without drawing in boost::bimap dependency...
	using FSourceElemenIDsCont = boost::multi_index_container<FSourceElem,
		boost::multi_index::indexed_by<
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByRank>,
				boost::multi_index::member<FSourceElem, ITwinScene::ElemIdx, &FSourceElem::Rank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexBySourceID>,
				boost::multi_index::member<FSourceElem, FString, &FSourceElem::SourceId>>>>;
	/// *All* Source Element IDs retrieved from the iModel metadata query. Cleared once DuplicateElements is
	/// finished calculating, ie. as soon as the iModel metadata query batch completes.
	FSourceElemenIDsCont SourceElementIDs;
	/// Only contains entries for Elements with either a common Source Element IDs, or a common Federated
	/// Element GUID: should be much fewer of those than SourceElementIDs.
	std::vector<FDuplicateElementsVec> DuplicateElements;

	std::function<FITwinScheduleTimeline const& ()> TimelineGetter;
	std::function<UMaterialInterface* (ECesiumMaterialType)> MaterialGetter;
	bool bTilesHaveNew4DAnimTextures = false;
	bool bNew4DAnimTexturesNeedSetupInMaterials = false;
	// Note: not using this in the end, since we can call UpdateSelectingAndHidingTextures right away after
	// a call to FITwinSceneMapping::SelectVisibleElement or HideElements, etc. (as opposed to 4D anim
	// textures, for which I initially chose to leave the Animator call the updates - maybe refactor that for
	// consistency?)
	//bool bTilesHaveNewSelectingAndHidingTextures = false;
	bool bNewSelectingAndHidingTexturesNeedSetupInMaterials = false;
	/// ID of the currently selected Element, if any
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
	/// ID of the currently selected Material, if any
	ITwinMaterialID SelectedMaterial = ITwin::NOT_MATERIAL;
	//enum class EHiddenByFlag : uint8_t
	//{
	//	ConstructionData, ///< Hidden because Element is construction data and user asked to hide them
	//	SavedView, ///< Hidden because a saved view is applied that is configured to hide this Element
	//};
	// TODO_GCO: refacto to use a mask and not one collection per hiding reason + use ITwinScene::ElemIdx
	// + store hiding mask directly in FITwinElement? (not sure it's a good idea, as it means rebuilding
	// the set for each call to HideElements, unless its param is changed too)
	// Currently hidden Elements, for whatever reason (as a mask of see EHiddenByFlag)
	//std::unordered_map<ITwinElementID, uint8_t/*Reasons(s)*/> HiddenElements;
	//std::unordered_set<ITwinElementID> HiddenConstructionData; == empty or GeometryIDToElementIDs[1]!!
	bool bHiddenConstructionData = false;
	std::unordered_set<ITwinElementID> HiddenElementsFromSavedView;
	/// Transform for conversion of this iModel's internal coordinates (saved views, synchro transforms...)
	/// into and from Unreal coordinates (with the current Georeference and iModel transform: needs to be
	/// updated if it changes)
	FITwinCoordConversions CoordConversions;
	/// Helper used to avoid stacking several texture updates for a same tile, in a limited sequence of
	/// operations (typically when we highlight a material, which implies to first deselect all).
	struct FTextureUpdateDisabler
	{
		bool bNeedUpdateSelectingAndHidingTextures = false;
	};
	std::optional<FTextureUpdateDisabler> TextureUpdateDisabler;

public:
	using FSceneTilesCont = boost::multi_index_container<FITwinSceneTile,
		boost::multi_index::indexed_by<
			boost::multi_index::random_access<boost::multi_index::tag<IndexByRank>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByTileID>,
				boost::multi_index::member<FITwinSceneTile, const CesiumTileID,
											&FITwinSceneTile::TileID>>>>;
	FSceneTilesCont KnownTiles;

	void ForEachKnownTile(std::function<void(FITwinSceneTile&)> const& Func);
	void ForEachKnownTile(std::function<void(FITwinSceneTile const&)> const& Func) const;
	[[nodiscard]] FITwinSceneTile& KnownTile(ITwinScene::TileIdx const Rank);
	FITwinSceneTile& KnownTileSLOW(Cesium3DTilesSelection::Tile& CesiumTile,
								   ITwinScene::TileIdx* Rank = nullptr);
	[[nodiscard]] FITwinSceneTile* FindKnownTileSLOW(CesiumTileID const& TileId);
	/// Do not actually erase the tile from the KnownTiles container, as it would shift all
	/// ITwinScene::TileIdx ;^^ Keep the (light-weight) structure forever, just clear its content.
	void UnloadKnownTile(FITwinSceneTile& SceneTile);
	[[nodiscard]] ITwinScene::TileIdx KnownTileRank(FITwinSceneTile const& SceneTile) const;

	// map to retrieve all elementsIDs for a given categoryID
	std::unordered_map<ITwinElementID, std::unordered_set<ITwinElementID>> CategoryIDToElementIDs;
	// map to retrieve all elementsIDs for a given modelID
	std::unordered_map<ITwinElementID, std::unordered_set<ITwinElementID>> ModelIDToElementIDs;
	// map to retrieve all elementsIDs for a given geometryID
	std::unordered_map<uint8_t, std::unordered_set<ITwinElementID>> GeometryIDToElementIDs;
	
	FITwinSceneMapping(bool const forCDO);

	/// Get all known Element structures in the storage array
	[[nodiscard]] FSceneElemsCont const& GetElements() const { return AllElements; }
	[[nodiscard]] size_t NumElements() const { return AllElements.size(); }
	/// Helper function to modify Elements (except their ElementID!)
	void MutateElements(std::function<void(FITwinElement&)>&& Functor);

	/// ElemDesignation can be by ElementID (ITwinElementID) or by index in AllElements (ITwinScene::ElemIdx)
	template<typename ElemDesignation>
	[[nodiscard]] FITwinElement const& GetElement(ElemDesignation const Designation) const {
		static_assert(always_false_v<ElemDesignation>,
					  "Unsupported Element designation type for FITwinSceneMapping::GetElement");
	}
	template<> [[nodiscard]] FITwinElement const& GetElement<ITwinScene::ElemIdx>(
		ITwinScene::ElemIdx const ElemRank) const { return AllElements[ElemRank.value()]; }
	template<> [[nodiscard]] FITwinElement const& GetElement<ITwinElementID>(ITwinElementID const ElementID) 
	const {
		static const FITwinElement Empty;
		auto const& ByID = AllElements.get<IndexByElemID>();
		auto const Found = ByID.find(ElementID);
		if (Found != ByID.cend())
			return *Found;
		else
			return Empty;
	}
	[[nodiscard]] FITwinElement const& ElementFor(ITwinScene::ElemIdx const ByElemIdx) const;
	[[nodiscard]] FITwinElement& ElementFor(ITwinScene::ElemIdx const ByElemIdx);
	[[nodiscard]] FITwinElement& ElementForSLOW(ITwinElementID const ById,
												ITwinScene::ElemIdx* Rank = nullptr);
	[[nodiscard]] bool FindElementIDForGUID(FGuid const& ElementGuid, ITwinElementID& Found) const;
	[[nodiscard]] bool FindGUIDForElement(ITwinScene::ElemIdx const Rank, FGuid& Found) const;
	[[nodiscard]] bool FindGUIDForElement(ITwinElementID const Elem, FGuid& Found) const;
	void ReserveIModelMetadata(int TotalElements);
	void FinishedParsingIModelMetadata();
	int ParseIModelMetadata(TArray<TSharedPtr<FJsonValue>> const& JsonRows);
	FDuplicateElementsVec const& GetDuplicateElements(ITwinElementID const ElemID) const;
	FString ToString() const;
	void CreateHighlightsAndOpacitiesTexture(FITwinSceneTile& SceneTile);
	void CreateCuttingPlanesTexture(FITwinSceneTile& SceneTile);
	/// Makes sure feature IDs are available in per-vertex UVs. They should have been baked
	/// already (through the instantiation of UCesiumFeaturesMetadataComponent upon tileset creation...)
	/// So here we should only have to configure the tile's materials to access the right UV layer.
	void SetupFeatureIDsInVertexUVs(FITwinSceneTile& SceneTile,
		FITwinGltfMeshComponentWrapper& GltfMeshData, bool bUpdatingTile = false);
	/// Same as the other SetupFeatureIDsInVertexUVs, but for all meshes of the tile (used for dev only)
	void SetupFeatureIDsInVertexUVs(FITwinSceneTile& SceneTile, bool bUpdatingTile = false);
	void OnNewTileBuilt(FITwinSceneTile& SceneTile);
	void OnVisibilityChanged(FITwinSceneTile& SceneTile, bool bVisible,
							 bool const bUseGltfTunerInsteadOfMeshExtraction);
	/// Prepares a tile's internal properties and structures to allow animation for a timeline
	void OnElementsTimelineModified(
		std::variant<ITwinScene::TileIdx, std::reference_wrapper<FITwinSceneTile>> const KnownTile,
		FITwinElementTimeline& ModifiedTimeline, std::vector<ITwinElementID> const* OnlyForElements,
		bool const bUseGltfTunerInsteadOfMeshExtraction, bool const bTileIsTunedFor4D,
		int const TimelineIndex);
	/// See long comment before call, in FITwinSynchro4DSchedulesInternals::HandleReceivedElements.
	/// Does not (need to) handle selection highlight (see UpdateSelectingAndHidingTextures).
	/// \return Whether new tiles received have textures set up for Synchro animation
	bool ReplicateAnimElemTextureSetupInTile(
		std::pair<ITwinScene::TileIdx, std::unordered_set<ITwinScene::ElemIdx>> const& TileElements);
	/// Update all dirty textures (which may post asynchronous tasks to be performed by the render thread)
	/// related to 4D animation (ie Highlights/Opacities textures + Cutting Planes textures).
	/// \return Number of textures for which we have to wait before being able to attach them to any
	///		materials (indeed, one cannot update a material with a texture which as never been fully updated).
	size_t Update4DAnimTextures();
	void Update4DAnimTileTextures(FITwinSceneTile& SceneTile, size_t& DirtyTexCount, size_t& TexToWait);
	/// Update all dirty textures (which may post asynchronous tasks to be performed by the render thread)
	/// related to selection highlights and (non-4D related) hiding of Elements.
	/// \return Number of textures for which we have to wait before being able to attach them to any
	///		materials (indeed, one cannot update a material with a texture which as never been fully updated).
	size_t UpdateSelectingAndHidingTextures();
	void UpdateSelectingAndHidingTileTextures(FITwinSceneTile& SceneTile, size_t& DirtyTexCount,
											  size_t& TexToWait);
	/// Disables (or re-enable) the update of dirty textures.
	void DisableUpdateSelectingAndHidingTextures(bool b);
	[[nodiscard]] bool AreSelectingAndHidingTexturesUpdatesDisabled() const;

	[[nodiscard]] bool TilesHaveNew4DAnimTextures(bool& bWaitingForTextures);
	// Note: we don't need a separate TilesHaveNewSelectingAndHidingTextures here, as opposed to 4D anim
	// textures (even for those we'll probably merge the two as well in the future)
	//[[nodiscard]] bool TilesHaveNewSelectingAndHidingTextures(bool& bWaitingForTextures);
	void HandleNew4DAnimTexturesNeedingSetupInMaterials();
	void HandleNewSelectingAndHidingTextures/*NeedingSetupInMaterials*/();

	void SetTimelineGetter(std::function<FITwinScheduleTimeline const& ()> const& InTimelineGetter)
		{ TimelineGetter = InTimelineGetter; }
	void SetMaterialGetter(std::function<UMaterialInterface* (ECesiumMaterialType)> const& InMaterialGetter)
		{ MaterialGetter = InMaterialGetter; }

	/// Get the Element's AABB in UE coordinates
	[[nodiscard]] FBox const& GetBoundingBox(ITwinElementID const Element) const;
	[[nodiscard]] FTransform const& GetIModel2UnrealTransfo() const { return CoordConversions.IModelToUnreal; }
	[[nodiscard]] FITwinCoordConversions const& GetIModel2UnrealCoordConv() const { return CoordConversions; }
	void SetIModel2UnrealTransfos(AITwinIModel const& IModel);

	static void SetForcedOpacity(TWeakObjectPtr<UMaterialInstanceDynamic> const& Mat, float const Opacity);
	static void SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);
	static void SetupHighlightsOpacities(FITwinSceneTile& SceneTile,
								FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupHighlightsOpacities(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile,
								   FITwinElementFeaturesInTile& ElementFeaturesInTile);
	static void SetupCuttingPlanes(FITwinSceneTile& SceneTile, FITwinExtractedEntity& ExtractedEntity);

	/// \return Material parameter info structure to use to modify the ForcedAlpha value (overriding the
	///		zeroed alpha masking the matching batched mesh) in an extracted Element's material
	[[nodiscard]] static FMaterialParameterInfo const& GetExtractedElementForcedAlphaMaterialParameterInfo();

	/// Extract Elements in all tiles if they were not yet
	uint32 CheckAndExtractElements(FTimelineToScene const& TimelineOptim, bool const bOnlyVisibleTiles,
		std::optional<ITwinScene::TileIdx> const& OnlySceneTile);
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

	/// Checks whether the Element can be picked, ie if it is visible. Optionally 
	/// \param InElemID Element to check. Pass NOT_ELEMENT to just discard any existing selection.
	/// \param bSelectElement If check succeeds, select the Element.
	/// \return Whether the Element could indeed be selected in any of the currently known tiles. Failure to
	///		select can indeed happen when the Element is hidden through the material shader, or simply when
	///		it is already selected.
	bool PickVisibleElement(ITwinElementID const& InElemID, bool const bSelectElement = true);

	void HideElements(std::unordered_set<ITwinElementID> const& InElemIDs, bool IsConstruction);
	/// Not const because empty set may be added to GeometryIDToElementIDs before being returned
	[[nodiscard]] std::unordered_set<ITwinElementID> const& ConstructionDataElements();
	[[nodiscard]] bool IsElementHiddenInSavedView(ITwinElementID const& InElemID) const;

	//! Returns the selected Element's ID, if an Element is selected, or ITwin::NOT_ELEMENT.
	[[nodiscard]] ITwinElementID GetSelectedElement() const { return SelectedElement; }

	using ITwinColor = std::array<double, 4>;

	/// Same as PickVisibleElement, but applying to a Material.
	bool PickVisibleMaterial(ITwinMaterialID const& InMaterialID, bool bIsMaterialPrediction,
		std::optional<ITwinColor> const& ColorToRestore = std::nullopt);
	//! Returns the selected Material's ID, if a Material is selected, or ITwin::NOT_MATERIAL.
	[[nodiscard]] ITwinMaterialID GetSelectedMaterial() const { return SelectedMaterial; }

	/// Given a component (typically obtained by a line trace hit), find its owner tile and glTF mesh wrapper
	std::pair<FITwinSceneTile const*, FITwinGltfMeshComponentWrapper const*> FindOwningTileSLOW(
		UPrimitiveComponent const* Component) const;

	/// Reset everything - should only be called before the tileset is reloaded.
	void Reset();

	/// Edit a scalar parameter in all Unreal materials created for the given ITwin material.
	void SetITwinMaterialChannelIntensity(uint64_t ITwinMaterialID,
		AdvViz::SDK::EChannelType Channel, double Intensity);

	/// Edit a color parameter in all Unreal materials created for the given ITwin material.
	void SetITwinMaterialChannelColor(uint64_t ITwinMaterialID,
		AdvViz::SDK::EChannelType Channel, ITwinColor const& Color);

	/// Edit a texture parameter in all Unreal materials created for the given ITwin material.
	void SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID,
		AdvViz::SDK::EChannelType Channel, UTexture* pTexture);

	/// Edit UV transformation parameters in all Unreal materials created for the given ITwin material.
	using ITwinUVTransform = AdvViz::SDK::ITwinUVTransform;
	void SetITwinMaterialUVTransform(uint64_t ITwinMaterialID, ITwinUVTransform const& UVTransform);

	/// Create a dummy mapping composed of just one tile using one material. Introduced to apply materials
	/// from the Material Library to arbitrary meshes.
	void BuildFromNonCesiumMesh(const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
		uint64_t ITwinMaterialID);

private:
	template<typename TSomeID, typename TMapByRank>
	bool ParseSomeElementIdentifier(TMapByRank& OutIDMap, ITwinElementID const ElemId,
		TSharedPtr<FJsonValue> const& Entry, int& GoodEntry, int& EmptyEntry);
	void ApplySelectingAndHiding(FITwinSceneTile& SceneTile);
	/// Extracts the given element in the given tile. New Unreal entities may be created.
	/// \return the number of created entities in Unreal.
	uint32 ExtractElementFromTile(ITwinElementID const Element, FITwinSceneTile& SceneTile,
		FITwinMeshExtractionOptions const& Options = {},
		FITwinElementFeaturesInTile* ElementFeaturesInTile = nullptr,
		FITwinExtractedElement* ExtractedElem = nullptr);

	template<typename Container>
	void GatherTimelineElemInfos(FITwinSceneTile& SceneTile, FITwinElementTimeline const& Timeline,
		Container const& TimelineElements, std::vector<ITwinScene::ElemIdx>& SceneElems,
		std::vector<ITwinTile::ElemIdx>& TileElems);
};

namespace ITwinMatParamInfo
{
	extern std::optional<FMaterialParameterInfo> SelectingAndHidingInfo;
	void SetupSelectingAndHidingInfo();
	void SetupFeatureIdInfo();
}

#if ENABLE_DRAW_DEBUG
	extern float ITwinDebugBoxNextLifetime;
#endif