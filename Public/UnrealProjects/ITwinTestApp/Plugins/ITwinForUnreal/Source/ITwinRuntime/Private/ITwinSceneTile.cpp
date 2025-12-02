/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneTile.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"

#include <Cesium3DTilesSelection/Tile.h>
#include <ITwinDynamicShadingProperty.h>
#include <ITwinDynamicShadingProperty.inl>
#include <ITwinExtractedMeshComponent.h>
#include <Engine/StaticMesh.h> // FStaticMaterial
#include <Materials/MaterialInstanceDynamic.h>

#include <Material/ITwinMaterialParameters.h>


namespace ITwin
{
	/// Highlight color for selected element (CANNOT use the alpha component, which would conflict with
	/// HideElements!)
	static const std::array<uint8, 4> COLOR_SELECTED_ELEMENT_BGRA = { 96, 230, 0, 255 };
	/// Highlight color for hidden element
	static const std::array<uint8, 4> COLOR_HIDDEN_ELEMENT_BGRA = { 0, 0, 0, 0 };
	/// Highlight color to revert to when deselecting AND un-hiding an element
	static const std::array<uint8, 4> COLOR_UNSELECT_ELEMENT_BGRA = { 0, 0, 0, 255 };

	static const std::array<uint8, 4> COLOR_SELECTED_MATERIAL_BGRA = { 0, 230, 96, 255 };

	std::array<uint8, 4> const& GetMaterialSelectionHighlightBGRA()
	{
		return COLOR_SELECTED_MATERIAL_BGRA;
	}
}

//---------------------------------------------------------------------------------------
// struct FITwinElementFeaturesInTile
//---------------------------------------------------------------------------------------
bool FITwinElementFeaturesInTile::HasOpaqueOrMaskedMaterial() const
{
	for (auto const& MatPtr : Materials)
	{
		if (MatPtr.IsValid()
			&& (MatPtr->GetBlendMode() == BLEND_Opaque || MatPtr->GetBlendMode() == BLEND_Masked))
		{
			return true;
		}
	}
	return false;
}

TWeakObjectPtr<UMaterialInstanceDynamic> FITwinElementFeaturesInTile::GetFirstValidMaterial() const
{
	for (auto const& MatPtr : Materials)
	{
		if (MatPtr.IsValid())
		{
			return MatPtr;
		}
	}
	return {};
}

void FITwinElementFeaturesInTile::Unload()
{
	// Avoid this pattern which apparently (and mysteriously to me) makes boost multi_index insertion crash
	// later (see FITwinSceneTile::Unload)

	// Was: "ElementID is const, hence this way to reset without copying all the default inits:"
	//this->~FITwinElementFeaturesInTile();
	//new (this)FITwinElementFeaturesInTile{ ElementID };

	*this = FITwinElementFeaturesInTile{ ElementID };
}

void FITwinElementFeaturesInTile::InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& SceneTile)
{
	TextureFlags.SelectingAndHidingTexFlag.Invalidate();
	if (ITwinTile::NOT_EXTR != ExtractedRank)
	{
		auto& Extracted = SceneTile.ExtractedElement(ExtractedRank);
		for (auto& Entity : Extracted.Entities)
			Entity.TextureFlags.SelectingAndHidingTexFlag.Invalidate();
	}
}


//---------------------------------------------------------------------------------------
// struct FITwinExtractedElement
//---------------------------------------------------------------------------------------

void FITwinMaterialFeaturesInTile::InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& /*SceneTile*/)
{
	SelectingAndHidingTexFlag.Invalidate();
}

void FITwinModelFeaturesInTile::InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& /*SceneTile*/)
{
	SelectingAndHidingTexFlag.Invalidate();
}

void FITwinCategoryFeaturesInTile::InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& /*SceneTile*/)
{
	SelectingAndHidingTexFlag.Invalidate();
}

void FITwinCategoryPerModelFeaturesInTile::InvalidateSelectingAndHidingTexFlags(FITwinSceneTile& /*SceneTile*/)
{
	SelectingAndHidingTexFlag.Invalidate();
}

//---------------------------------------------------------------------------------------
// struct FITwinExtractedElement
//---------------------------------------------------------------------------------------
void FITwinExtractedElement::Unload()
{
	Entities.clear();
}

//---------------------------------------------------------------------------------------
// struct FITwinExtractedEntity
//---------------------------------------------------------------------------------------

void FITwinExtractedEntity::SetHidden(bool bHidden)
{
	if (ExtractedMeshComponent.IsValid())
	{
		ExtractedMeshComponent->SetFullyHidden(bHidden);
	}
}

namespace ITwin
{
	UMaterialInstanceDynamic* ChangeBaseMaterialInUEMesh(UStaticMeshComponent& MeshComponent,
		UMaterialInterface* BaseMaterial,
		TWeakObjectPtr<UMaterialInstanceDynamic> const* SupposedPreviousMaterial /*= nullptr*/)
	{
		TObjectPtr<UStaticMesh> StaticMesh = MeshComponent.GetStaticMesh();
		if (!StaticMesh)
		{
			ensureMsgf(false, TEXT("orphan mesh component"));
			return nullptr;
		}

		TArray<FStaticMaterial> StaticMaterials =
			StaticMesh->GetStaticMaterials();
		ensure(StaticMaterials.Num() == 1);

		FStaticMaterial& StaticMaterial = StaticMaterials[0];
		if (SupposedPreviousMaterial)
		{
			ensureMsgf(StaticMaterial.MaterialInterface == *SupposedPreviousMaterial,
				TEXT("material mismatch"));
		}
		UMaterialInstanceDynamic* SrcMaterialInstance = nullptr;
		if (StaticMaterial.MaterialInterface)
		{
			SrcMaterialInstance = Cast<UMaterialInstanceDynamic>(
				StaticMaterial.MaterialInterface.Get());
		}

		UMaterialInstanceDynamic* NewMaterialInstance =
			UMaterialInstanceDynamic::Create(
				BaseMaterial,
				nullptr,
				StaticMaterial.MaterialSlotName);
		if (SrcMaterialInstance)
		{
			NewMaterialInstance->CopyParameterOverrides(SrcMaterialInstance);
		}
		NewMaterialInstance->TwoSided = true;//probably ineffective, see azdev#1414081
		StaticMaterial.MaterialInterface = NewMaterialInstance;

		StaticMesh->SetStaticMaterials(StaticMaterials);
		return NewMaterialInstance;
	}
}

bool FITwinExtractedEntity::SetBaseMaterial(UMaterialInterface* BaseMaterial)
{
	if (!TransformableMeshComponent.IsValid())
	{
		// Was the tile from which this mesh was extracted invalidated?
		return false;
	}

	UMaterialInstanceDynamic* NewMaterialInstance =
		ITwin::ChangeBaseMaterialInUEMesh(*TransformableMeshComponent, BaseMaterial, &this->Material);
	if (!NewMaterialInstance)
	{
		return false;
	}

	this->Material = NewMaterialInstance;

	return true;
}

bool FITwinExtractedEntity::HasOpaqueOrMaskedMaterial() const
{
	if (Material.IsValid()
		&& (Material->GetBlendMode() == BLEND_Opaque || Material->GetBlendMode() == BLEND_Masked))
	{
		return true;
	}
	return false;
}

void FITwinExtractedEntity::SetForcedOpacity(float const Opacity)
{
	FITwinSceneMapping::SetForcedOpacity(Material, Opacity);
}

//---------------------------------------------------------------------------------------
// class FITwinSceneTile
//---------------------------------------------------------------------------------------
bool FITwinSceneTile::IsLoaded() const
{
	// Old implem: wrong because some tiles have meshes with no FeatureID metadata (background meshes added
	// by user for context, in GSW Stadium or AP Edmonton Demo, for example)
	//return ITwin::NOT_FEATURE != MaxFeatureID;
	return !GltfMeshes.empty();
}

void FITwinSceneTile::Unload()
{
	// If some textures were overridden during material tuning, we must reset them before they are processed
	// by #destroyGltfParameterValues
	ResetCustomTexturesInMaterials();

	ForEachElementFeatures([](FITwinElementFeaturesInTile& ElemInTile) { ElemInTile.Unload(); });
	ForEachExtractedElement([](FITwinExtractedElement& Extracted) { Extracted.Unload(); });

	// Preserve containers to keep ordering
	FElementFeaturesCont Elems;
	Elems.swap(ElementsFeatures);
	FExtractedElementCont Extracts;
	Extracts.swap(ExtractedElements);
	FMaterialFeaturesCont MatFeatures;
	MatFeatures.swap(MaterialsFeatures);
	// Preserve this one for performance (cleared in FITwinScheduleTimelineBuilder's dtor)
	std::vector<int> Timelines;
	Timelines.swap(TimelinesIndices);

	// When FITwinSceneTile::TileID was constant, we couldn't do *this = FITwinSceneTile(TileID);
	// So I tried this trick, which seemed perfectly safe to me since we don't really touch the TileID member
	// (and there is no concurrent access to the KnownTiles container):
	//		this->~FITwinSceneTile();
	//		new (this)FITwinSceneTile(TileID);
	// BUT it did lead to crashes when calling emplace_back later in FITwinSceneMapping::KnownTileSLOW!
	*this = FITwinSceneTile(TileID);
	// swap back
	Elems.swap(ElementsFeatures);
	Extracts.swap(ExtractedElements);
	MatFeatures.swap(MaterialsFeatures);
	Timelines.swap(TimelinesIndices);

	// So I made TileID no longer constant, instead of resetting everything (except 'preserved' containers)
	// by hand:
	//MaxFeatureID = ITwin::NOT_FEATURE;
	//HighlightsAndOpacities.reset();
	//CuttingPlanes.reset();
	//	(etc.)
}

FITwinElementFeaturesInTile const* FITwinSceneTile::FindElementFeaturesConstSLOW(ITwinElementID const& ElemID,
	ITwinTile::ElemIdx* OutRank /*= nullptr*/) const
{
	auto& ByID = ElementsFeatures.get<IndexByElemID>();
	auto const Found = ByID.find(ElemID);
	if (Found != ByID.end())
	{
		if (OutRank)
		{
			auto& ByElemRank = ElementsFeatures.get<IndexByRank>();
			*OutRank = ITwinTile::ElemIdx(
				static_cast<uint32_t>(ElementsFeatures.project<0>(Found) - ByElemRank.begin()));
		}
		return &(*Found);
	}
	else return nullptr;
}

FITwinElementFeaturesInTile* FITwinSceneTile::FindElementFeaturesSLOW(ITwinElementID const& ElemID,
	ITwinTile::ElemIdx* OutRank /*= nullptr*/)
{
	auto const* Found = FindElementFeaturesConstSLOW(ElemID, OutRank);
	if (Found)
	{
		// multi_index containers are const for safety, because modifying the values could break the
		// container's consistency without the container being aware: in our case, the only hash key
		// is ElementID, which is sadly no longer declared as const in FITwinElementFeaturesInTile...
		// But using multi_index_container::modify everywhere is a real pain because we typically insert
		// elements with their should-be-const ID, then modify them in many places while building the
		// scene mapping. So we'll just assume it is clear enough that the structures (FITwinSceneTile and
		// FITwinElementFeaturesInTile) shouldn't change their ID during their lifetime, which is really
		// quite obvious IMO!
		return const_cast<FITwinElementFeaturesInTile*>(Found);
	}
	else return nullptr;
}

FITwinElementFeaturesInTile& FITwinSceneTile::ElementFeaturesSLOW(ITwinElementID const& ElemID)
{
	// See comment about const_cast above
	return const_cast<FITwinElementFeaturesInTile&>(
		*ElementsFeatures.get<IndexByRank>().emplace_back(FITwinElementFeaturesInTile{ ElemID }).first);
}

FITwinElementFeaturesInTile& FITwinSceneTile::ElementFeatures(ITwinTile::ElemIdx const Rank)
{
	// See comment about const_cast above
	return const_cast<FITwinElementFeaturesInTile&>(ElementsFeatures.get<IndexByRank>()[Rank.value()]);
}

void FITwinSceneTile::ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile&)> const& Func)
{
	for (auto&& ElementFeatures : ElementsFeatures)
	{
		// See comment about const_cast above
		Func(const_cast<FITwinElementFeaturesInTile&>(ElementFeatures));
	}
}

void FITwinSceneTile::ForEachElementFeatures(
	std::function<void(FITwinElementFeaturesInTile const&)> const& Func) const
{
	for (auto&& ElementFeatures : ElementsFeatures)
		Func(ElementFeatures);
}

FITwinMaterialFeaturesInTile const* FITwinSceneTile::FindMaterialFeaturesConstSLOW(
	ITwinMaterialID const& MaterialID) const
{
	auto& ByID = MaterialsFeatures.get<IndexByMaterialID>();
	auto const Found = ByID.find(MaterialID);
	if (Found != ByID.end())
	{
		return &(*Found);
	}
	else return nullptr;
}

FITwinMaterialFeaturesInTile* FITwinSceneTile::FindMaterialFeaturesSLOW(ITwinMaterialID const& MatID)
{
	auto const* Found = FindMaterialFeaturesConstSLOW(MatID);
	if (Found)
	{
		// same remark as in FindElementFeaturesSLOW (yes, that's the same code, but I did not want to
		// templatize everything...
		return const_cast<FITwinMaterialFeaturesInTile*>(Found);
	}
	else
		return nullptr;
}

FITwinMaterialFeaturesInTile& FITwinSceneTile::MaterialFeaturesSLOW(ITwinMaterialID const& MatID)
{
	// See comment about const_cast above
	return const_cast<FITwinMaterialFeaturesInTile&>(
		*MaterialsFeatures.get<IndexByRank>().emplace_back(FITwinMaterialFeaturesInTile{ MatID }).first);
}

FITwinModelFeaturesInTile const* FITwinSceneTile::FindModelFeaturesConstSLOW(
	ITwinElementID const& ModelID) const
{
	auto& ByID = ModelsFeatures.get<IndexByModelID>();
	auto const Found = ByID.find(ModelID);
	if (Found != ByID.end())
	{
		return &(*Found);
	}
	else return nullptr;
}

FITwinModelFeaturesInTile* FITwinSceneTile::FindModelFeaturesSLOW(ITwinElementID const& ModelID)
{
	auto const* Found = FindModelFeaturesConstSLOW(ModelID);
	if (Found)
	{
		// same remark as in FindElementFeaturesSLOW (yes, that's the same code, but I did not want to
		// templatize everything...
		return const_cast<FITwinModelFeaturesInTile*>(Found);
	}
	else
		return nullptr;
}

FITwinModelFeaturesInTile& FITwinSceneTile::ModelFeaturesSLOW(ITwinElementID const& ModelID)
{
	// See comment about const_cast above
	return const_cast<FITwinModelFeaturesInTile&>(
		*ModelsFeatures.get<IndexByRank>().emplace_back(FITwinModelFeaturesInTile{ ModelID }).first);
}

FITwinCategoryFeaturesInTile const* FITwinSceneTile::FindCategoryFeaturesConstSLOW(
	ITwinElementID const& CategoryID) const
{
	auto& ByID = CategoriesFeatures.get<IndexByCategoryID>();
	auto const Found = ByID.find(CategoryID);
	if (Found != ByID.end())
	{
		return &(*Found);
	}
	else return nullptr;
}

FITwinCategoryFeaturesInTile* FITwinSceneTile::FindCategoryFeaturesSLOW(ITwinElementID const& CategoryID)
{
	auto const* Found = FindCategoryFeaturesConstSLOW(CategoryID);
	if (Found)
	{
		// same remark as in FindElementFeaturesSLOW (yes, that's the same code, but I did not want to
		// templatize everything...
		return const_cast<FITwinCategoryFeaturesInTile*>(Found);
	}
	else
		return nullptr;
}

FITwinCategoryFeaturesInTile& FITwinSceneTile::CategoryFeaturesSLOW(ITwinElementID const& CategoryID)
{
	// See comment about const_cast above
	return const_cast<FITwinCategoryFeaturesInTile&>(
		*CategoriesFeatures.get<IndexByRank>().emplace_back(FITwinCategoryFeaturesInTile{ CategoryID }).first);
}

FITwinCategoryPerModelFeaturesInTile const* FITwinSceneTile::FindCategoryPerModelFeaturesConstSLOW(
	ITwinElementID const& CategoryID, ITwinElementID const& ModelID) const
{
	auto& ByID = CategoriesPerModelsFeatures.get<IndexByCategoryAndModelID>();
	auto const Found = ByID.find(boost::make_tuple(CategoryID, ModelID));
	if (Found != ByID.end())
	{
		return &(*Found);
	}
	else return nullptr;
}

FITwinCategoryPerModelFeaturesInTile* FITwinSceneTile::FindCategoryPerModelFeaturesSLOW(std::pair<ITwinElementID const&, ITwinElementID const&> CategoryPerModelID)
{
	auto const* Found = FindCategoryPerModelFeaturesConstSLOW(CategoryPerModelID.first, CategoryPerModelID.second);
	if (Found)
	{
		// same remark as in FindElementFeaturesSLOW (yes, that's the same code, but I did not want to
		// templatize everything...
		return const_cast<FITwinCategoryPerModelFeaturesInTile*>(Found);
	}
	else
		return nullptr;
}

FITwinCategoryPerModelFeaturesInTile& FITwinSceneTile::CategoryPerModelFeaturesSLOW(ITwinElementID const& CategoryID, ITwinElementID const& ModelID)
{
	// See comment about const_cast above
	return const_cast<FITwinCategoryPerModelFeaturesInTile&>(
		*CategoriesPerModelsFeatures.get<IndexByRank>().emplace_back(FITwinCategoryPerModelFeaturesInTile{ CategoryID, ModelID }).first);
}

FITwinExtractedElement const* FITwinSceneTile::FindExtractedElementSLOW(ITwinElementID const& ElemID) const
{
	auto& ByID = ExtractedElements.get<IndexByElemID>();
	auto Found = ByID.find(ElemID);
	if (Found != ByID.end())
	{
		return &(*Found);
	}
	else return nullptr;
}

FITwinExtractedElement* FITwinSceneTile::FindExtractedElementSLOW(ITwinElementID const& ElemID)
{
	auto& ByID = ExtractedElements.get<IndexByElemID>();
	auto Found = ByID.find(ElemID);
	if (Found != ByID.end())
	{
		// See comment about const_cast above
		return const_cast<FITwinExtractedElement*>(&(*Found));
	}
	else return nullptr;
}

FITwinExtractedElement& FITwinSceneTile::ExtractedElement(ITwinTile::ExtrIdx const Rank)
{
	// See comment about const_cast above
	return const_cast<FITwinExtractedElement&>(ExtractedElements.get<IndexByRank>()[Rank.value()]);
}

std::pair<std::reference_wrapper<FITwinExtractedElement>, bool>
	FITwinSceneTile::ExtractedElementSLOW(FITwinElementFeaturesInTile& ElementInTile)
{
	auto& ByRank = ExtractedElements.get<IndexByRank>();
	auto Known = ByRank.emplace_back(FITwinExtractedElement{ ElementInTile.ElementID });
	ElementInTile.ExtractedRank = ITwinTile::ExtrIdx(static_cast<uint32_t>(Known.first - ByRank.begin()));
	// See comment about const_cast above
	return std::make_pair(std::ref(const_cast<FITwinExtractedElement&>(*Known.first)), Known.second);
}

void FITwinSceneTile::UseTunedMeshAsExtract(FITwinExtractedElement& DummyExtr,
	int32_t const GltfMeshWrapperIndex, FTransform const& IModelTilesetTransform)
{
	if (!ensure(GltfMeshWrapperIndex < GltfMeshes.size()))
		return;
	auto& MeshWrapper = GltfMeshes[GltfMeshWrapperIndex];
	auto* MeshComp = MeshWrapper.MeshComponent();
	if (!ensure(IsValid(MeshComp)))
		return;
	auto const UVIndex = MeshWrapper.GetFeatureIDsInVertexUVs();
	if (!ensure(UVIndex))
		return;
	// About index 0: same assumption as in FITwinGltfMeshComponentWrapper::FinalizeExtractedEntity
	auto* Material = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(0));
	if (!ensure(Material != nullptr))
		return;
	DummyExtr.Entities.emplace_back(FITwinExtractedEntity{
		.ElementID = DummyExtr.ElementID,
		.OriginalTransform = MeshComp->GetComponentTransform(),
		.TransformableMeshComponent = MeshComp,
		.FeatureIDsUVIndex = UVIndex,
		.Material = Material,
		//.TextureFlags <== will be ignored, see UpdateExtractedElement in ITwinSynchro4DAnimator.cpp
		});

	// MeshComp->GetComponentTransform() is not yet the actual transform to World coordinates (Tileset
	// and iModel transforms are not accounted), because the Component is not attached yet: when it
	// happens (see "if (pGltf->GetAttachParent() == nullptr)" in ACesium3DTileset::showTilesToRender),
	// the transform of the pGltf and all its children primitive components are updated with the right value.
	// => Apply the tileset transform manually:
	if (!MeshComp->GetAttachParent()/*gltf component*/->GetAttachParent()/*tileset actor*/)
	{
		auto& Transfo = DummyExtr.Entities.back().OriginalTransform;
		Transfo = Transfo * IModelTilesetTransform;
	}
}

void FITwinSceneTile::ForEachExtractedElement(std::function<void(FITwinExtractedElement&)> const& Func)
{
	for (auto&& Extracted : ExtractedElements)
	{
		// See comment about const_cast above
		Func(const_cast<FITwinExtractedElement&>(Extracted));
	}
}

void FITwinSceneTile::ForEachExtractedElement(std::function<void(FITwinExtractedElement const&)> const& Func)
	const
{
	for (auto&& Extracted : ExtractedElements)
		Func(Extracted);
}

void FITwinSceneTile::ForEachExtractedEntity(std::function<void(FITwinExtractedEntity&)> const& Func)
{
	for (auto&& ExtractedVec : ExtractedElements)
	{
		for (auto&& Extracted : ExtractedVec.Entities)
		{
			// See comment about const_cast above
			Func(const_cast<FITwinExtractedEntity&>(Extracted));
		}
	}
}

void FITwinSceneTile::ForEachExtractedEntity(std::function<void(FITwinExtractedEntity const&)> const& Func)
	const
{
	for (auto&& ExtractedVec : ExtractedElements)
		for (auto&& Extracted : ExtractedVec.Entities)
			Func(Extracted);
}

template<typename ElementsCont>
void FITwinSceneTile::ForEachElementFeaturesSLOW(ElementsCont const& ForElementIDs,
												 std::function<void(FITwinElementFeaturesInTile&)> const& Func)
{
	for (auto const& ElemID : ForElementIDs)
	{
		auto* Found = FindElementFeaturesSLOW(ElemID);
		if (Found) Func(*Found);
	}
}

template<typename ElementsCont>
void FITwinSceneTile::ForEachExtractedElementSLOW(ElementsCont const& ForElementIDs,
												  std::function<void(FITwinExtractedEntity&)> const& Func)
{
	for (auto const& ElemID : ForElementIDs)
	{
		auto* Found = FindExtractedElementSLOW(ElemID);
		if (Found)
		{
			for (auto&& ExtractedElt : Found->Entities)
			{
				Func(ExtractedElt);
			}
		}
	}
}

void FITwinSceneTile::ForEachMaterialInstanceMatchingID(uint64_t ITwinMaterialID,
														std::function<void(UMaterialInstanceDynamic&)> const& Func)
{
	for (auto& gltfMeshData : GltfMeshes)
	{
		if (gltfMeshData.HasITwinMaterialID(ITwinMaterialID))
		{
			gltfMeshData.ForEachMaterialInstance(Func);
		}
	}
}

void FITwinSceneTile::AddMaterial(UMaterialInstanceDynamic* MaterialInUse)
{
	Materials.push_back(MaterialInUse);
}

void FITwinSceneTile::DrawTileBox(UWorld const* World) const
{
#if ENABLE_DRAW_DEBUG
	// Display the bounding box of the tile
	FBox Box;
	for (auto const& gltfMeshData : GltfMeshes)
	{
		if (gltfMeshData.GetMeshComponent())
		{
			Box += gltfMeshData.GetMeshComponent()->Bounds.GetBox();
		}
	}
	FVector Center, Extent;
	Box.GetCenterAndExtents(Center, Extent);

	FColor const boxColor = (ITwinDebugBoxNextLifetime > 5)
		? FColor::MakeRandomColor()
		: FColor::Red;
	DrawDebugBox(
		World,
		Center,
		Extent,
		boxColor,
		/*bool bPersistent =*/ false,
		/*float LifeTime =*/ ITwinDebugBoxNextLifetime);
	ITwinDebugBoxNextLifetime += 5.f;
#endif //ENABLE_DRAW_DEBUG
}


class FITwinSceneTile::ElementSelectionHelper
{
public:
	using SelectableID = ITwinElementID;
	using SelectableFeaturesInTile = FITwinElementFeaturesInTile;

	ElementSelectionHelper(FITwinSceneTile& InSceneTile)
		: SceneTile(InSceneTile)
	{}

	inline static constexpr ITwinElementID NoneSelected() { return ITwin::NOT_ELEMENT; }

	inline static const std::array<uint8, 4>& GetSelectedItemColor()
	{
		return ITwin::COLOR_SELECTED_ELEMENT_BGRA;
	}

	inline ITwinElementID const& GetSelectedID() const
	{
		return SceneTile.SelectedElement;
	}
	void SetSelectedID(ITwinElementID const& NewID) const
	{
		SceneTile.SelectedElement = NewID;
	}

	inline FITwinElementFeaturesInTile* FindSelectableFeaturesSLOW(ITwinElementID const& EltID) const
	{
		return SceneTile.FindElementFeaturesSLOW(EltID);
	}

	template<typename Func>
	void ForEachFeaturesSelectionTexFlag(Func const& InFunc) const
	{
		SceneTile.ForEachElementFeatures([&InFunc](FITwinElementFeaturesInTile& ElementInTile)
		{ InFunc(ElementInTile.TextureFlags.SelectingAndHidingTexFlag); });
	}

	void ResetSelection(FTextureNeeds& TextureNeeds) const
	{
		SceneTile.TResetSelection(*this, TextureNeeds);
	}

	bool HasVisibleExtractedItem(FITwinElementFeaturesInTile const& FeaturesToSelect) const
	{
		if (ITwinTile::NOT_EXTR != FeaturesToSelect.ExtractedRank)
		{
			FITwinExtractedElement& Extracted = SceneTile.ExtractedElement(FeaturesToSelect.ExtractedRank);
			for (auto&& Entry : Extracted.Entities)
			{
				if (Entry.ExtractedMeshComponent.IsValid() && Entry.ExtractedMeshComponent->IsVisible())
				{
					return true;
				}
			}
		}
		return false;
	}

private:
	FITwinSceneTile& SceneTile;
};

class FITwinSceneTile::MaterialSelectionHelper
{
public:
	using SelectableID = ITwinMaterialID;
	using SelectableFeaturesInTile = FITwinMaterialFeaturesInTile;

	MaterialSelectionHelper(FITwinSceneTile& InSceneTile)
		: SceneTile(InSceneTile)
	{}

	inline static constexpr ITwinMaterialID NoneSelected() { return ITwin::NOT_MATERIAL; }

	inline static const std::array<uint8, 4>& GetSelectedItemColor()
	{
		return ITwin::COLOR_SELECTED_MATERIAL_BGRA;
	}

	inline ITwinMaterialID const& GetSelectedID() const
	{
		return SceneTile.SelectedMaterial;
	}
	void SetSelectedID(ITwinMaterialID const& NewID) const
	{
		SceneTile.SelectedMaterial = NewID;
	}

	inline FITwinMaterialFeaturesInTile* FindSelectableFeaturesSLOW(ITwinMaterialID const& MatID) const
	{
		return SceneTile.FindMaterialFeaturesSLOW(MatID);
	}

	template<typename Func>
	void ForEachFeaturesSelectionTexFlag(Func const& InFunc) const
	{
		for (auto&& MaterialFeatures : SceneTile.MaterialsFeatures)
		{
			// See comment about const_cast above in ForEachElementFeatures
			InFunc(const_cast<FITwinMaterialFeaturesInTile&>(MaterialFeatures).SelectingAndHidingTexFlag);
		}
	}

	void ResetSelection(FTextureNeeds& TextureNeeds) const
	{
		SceneTile.TResetSelection(*this, TextureNeeds);
	}

	bool HasVisibleExtractedItem(FITwinMaterialFeaturesInTile const& /*FeaturesToSelect*/) const
	{
		// TODO_JDE material selection vs extraction...
		return false;
	}

private:
	FITwinSceneTile& SceneTile;
};


template<typename SelectableHelper>
void FITwinSceneTile::TResetSelection(SelectableHelper const& Helper, FTextureNeeds& TextureNeeds)
{
	if (Helper.GetSelectedID() == SelectableHelper::NoneSelected())
		return;
	check(SelectingAndHiding);
	auto* FeaturesToDeSelect = Helper.FindSelectableFeaturesSLOW(Helper.GetSelectedID());
	if (ensure(FeaturesToDeSelect != nullptr))
	{
		SelectingAndHiding->SetPixelsExceptAlpha(FeaturesToDeSelect->Features,
			ITwin::COLOR_UNSELECT_ELEMENT_BGRA);
		TextureNeeds.bWasChanged = true;
	}
	Helper.SetSelectedID(SelectableHelper::NoneSelected());
}

void FITwinSceneTile::ResetSelection(FTextureNeeds& TextureNeeds)
{
	if (SelectedElement != ITwin::NOT_ELEMENT)
	{
		ElementSelectionHelper EltSelectionHelper(*this);
		TResetSelection(EltSelectionHelper, TextureNeeds);
	}
	if (SelectedMaterial != ITwin::NOT_MATERIAL)
	{
		MaterialSelectionHelper MatSelectionHelper(*this);
		TResetSelection(MatSelectionHelper, TextureNeeds);
	}
}


template <typename FeaturesInTile>
void FITwinSceneTile::CreateAndSetSelectingAndHiding(FeaturesInTile& FeaturesToSelectOrHide,
	FTextureNeeds& TextureNeeds, const std::array<uint8, 4>& Color_BGRA, bool const bColorOrAlpha)
{
	ITwinMatParamInfo::SetupSelectingAndHidingInfo();
	// Create selection texture if needed.
	if (!SelectingAndHiding)
	{
		FITwinDynamicShadingBGRA8Property::Create(SelectingAndHiding, MaxFeatureID,
			ITwin::COLOR_UNSELECT_ELEMENT_BGRA);
		FeaturesToSelectOrHide.InvalidateSelectingAndHidingTexFlags(*this);
		bNeedSelectingAndHidingTextureSetupInMaterials = true;
		TextureNeeds.bWasCreated = true;
	}
	// Apply constant highlight color to pixels matching the given features
	if (bColorOrAlpha)
		SelectingAndHiding->SetPixelsExceptAlpha(FeaturesToSelectOrHide.Features, Color_BGRA);
	else
		SelectingAndHiding->SetPixelsAlpha(FeaturesToSelectOrHide.Features, Color_BGRA[3]);
	TextureNeeds.bWasChanged = true;
}

template<typename SelectableHelper, typename SelectableID>
bool FITwinSceneTile::TPickSelectable(SelectableHelper const& PickHelper, SelectableID const& InElemID,
									  FTextureNeeds& TextureNeeds, FPickingOptions const Opts)
{
	// filter out hidden and empty tiles
	if ((Opts.OnlyVisibleTiles() && !bVisible) || MaxFeatureID == ITwin::NOT_FEATURE)
		return false;
	// Bad! See similar comment at the beginning of FITwinSceneMapping::PickVisibleElement
	//if (Opts.MakeSelected() && InElemID == SelectedElement)
	//	return false;
	ensure(InElemID != SelectableHelper::NoneSelected() || Opts.MakeSelected());//de-selecting requires bSelecElem==true...

	// 0. SAFETY measure
	if (Opts.MakeSelected() && SelectingAndHiding
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SelectingAndHiding->GetTotalUsedPixels() < (MaxFeatureID.value() + 1))
	{
		ensure(false); // should not happen
		SelectingAndHiding.reset(); // let's hope it doesn't crash everything...
		PickHelper.SetSelectedID(SelectableHelper::NoneSelected());
		PickHelper.ForEachFeaturesSelectionTexFlag([](FITwinPropertyTextureFlag& SelectingAndHidingTexFlag)
			{ SelectingAndHidingTexFlag.Invalidate(); });
	}

	// 1. Reset current selection, if any, when we (try to) select an Element (even if the same),
	//    or simply when we deselect
	if (Opts.MakeSelected() && !Opts.SkipResetSelection())
	{
		ResetSelection(TextureNeeds);
	}

	// 2. Select new Element, only if it exists in the tile.
	using SelectableFeaturesInTile = typename SelectableHelper::SelectableFeaturesInTile;
	SelectableFeaturesInTile* FeaturesToSelect = nullptr;
	if (InElemID != SelectableHelper::NoneSelected())
	{
		FeaturesToSelect = PickHelper.FindSelectableFeaturesSLOW(InElemID);
	}
	if (FeaturesToSelect != nullptr && !FeaturesToSelect->Features.empty())
	{
		if (Opts.TestElementVisibility())
		{
			// This used to be commented out as redundant with explicit (and much faster) tests made at the
			// beginning of FITwinSceneMapping::PickVisibleElement on HiddenElementsFromSavedView,
			// bHiddenConstructionData and ConstructionDataElements, BUT with all the other hiding reasons
			// now (per-category or per-model from saved views, etc. see FITwinSceneMapping's method
			// ApplySelectingAndHiding) skipping this was probably very buggy!
			// That and FITwinSceneMapping::PickVisibleMaterial also reaches this code and didn't have the
			// early tests like in PickVisibleElement!
			if (SelectingAndHiding)
			{
				auto&& SelHide_BGRA = SelectingAndHiding->GetPixel(FeaturesToSelect->Features[0].value());
				if (SelHide_BGRA[3] == 0)
					return false;
			}
			if (HighlightsAndOpacities)
			{
				auto&& S4D_BGRA = HighlightsAndOpacities->GetPixel(FeaturesToSelect->Features[0].value());
				// Ignore masked Elements unless they are masked because they were extracted, and at least one
				// of the extracted mesh parts is itself visible
				if (S4D_BGRA[3] == 0)
				{
					bool bHasVisibleExtractedElem = PickHelper.HasVisibleExtractedItem(*FeaturesToSelect);
					if (!bHasVisibleExtractedElem)
						return false;
				}
			}
		}
		if (Opts.MakeSelected())
		{
			CreateAndSetSelectingAndHiding(*FeaturesToSelect, TextureNeeds,
				SelectableHelper::GetSelectedItemColor(), /*bColorOrAlpha: color only*/true);
			PickHelper.SetSelectedID(InElemID);
		}
		return true;
	}
	else
	{
		return false;
	}
}

bool FITwinSceneTile::PickElement(ITwinElementID const& InElemID, FTextureNeeds& TextureNeeds,
								  FPickingOptions const Opts)
{
	ElementSelectionHelper EltSelectionHelper(*this);
	return TPickSelectable(EltSelectionHelper, InElemID, TextureNeeds, Opts);
}

bool FITwinSceneTile::PickMaterial(ITwinMaterialID const& InMaterialID, FTextureNeeds& TextureNeeds,
								   FPickingOptions const Opts)
{
	MaterialSelectionHelper MatSelectionHelper(*this);
	return TPickSelectable(MatSelectionHelper, InMaterialID, TextureNeeds, Opts);
}

template<typename IDType, typename FeatureType>
void FITwinSceneTile::THideIDs(std::unordered_set<IDType>& CurrentHiddenItems,
		std::unordered_set<IDType> const& NewIDs,
		std::function<FeatureType* (IDType)> FindFeatures,
		std::function<void(FeatureType*)> UnhideFeatures,
		std::function<void(FeatureType*)> HideFeatures,
		FITwinSceneTile::FTextureNeeds& TextureNeeds,
		FShowHideOptions Opts,
		std::optional<IDType> SelectedID /*= std::nullopt*/)
{
	// Update hidden elements in current saved view
	for (auto it = CurrentHiddenItems.begin(); it != CurrentHiddenItems.end();)
	{
		if (NewIDs.find(*it) == NewIDs.end())
		{
			FeatureType* FeaturesToUnHide = FindFeatures(*it);
			if (FeaturesToUnHide != nullptr)
			{
				UnhideFeatures(FeaturesToUnHide);
				TextureNeeds.bWasChanged = true;
			}
			it = CurrentHiddenItems.erase(it);
		}
		else
			++it;
	}

	for (const auto& InID : NewIDs)
	{
		// Element already hidden in previous saved view
		// Nothing to do.
		if (CurrentHiddenItems.find(InID) != CurrentHiddenItems.end() && !Opts.Force())
			continue;

		CurrentHiddenItems.insert(InID);

		// 1. Deselect element to be hidden if any.
		if (SelectedID && *SelectedID == InID && !Opts.SkipResetSelection())
		{
			ResetSelection(TextureNeeds);
		}
		// 2. Hide new Element, only if it exists in the tile.
		FeatureType* FeaturesToHide = nullptr;
		if (InID != ITwin::NOT_ELEMENT)
		{
			FeaturesToHide = FindFeatures(InID);
		}
		if (FeaturesToHide && !FeaturesToHide->Features.empty())
		{
			HideFeatures(FeaturesToHide);
		}
	}
}

void FITwinSceneTile::HideElements(std::unordered_set<ITwinElementID> const& InElemIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		 || (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentHiddenElements =
		Opts.ConstructionData() ? CurrentConstructionHiddenElements : CurrentSavedViewHiddenElements;

	THideIDs<ITwinElementID, FITwinElementFeaturesInTile>(
		CurrentHiddenElements,
		InElemIDs,
		[this](ITwinElementID id) { return FindElementFeaturesSLOW(id); },
		[this](FITwinElementFeaturesInTile* f) { SelectingAndHiding->SetPixelsAlpha(f->Features, 255); },
		[this, &TextureNeeds](FITwinElementFeaturesInTile* f) {
			CreateAndSetSelectingAndHiding(*f, TextureNeeds, ITwin::COLOR_HIDDEN_ELEMENT_BGRA, false);
		},
		TextureNeeds, Opts, SelectedElement);
}

void FITwinSceneTile::ShowElements(std::unordered_set<ITwinElementID> const& InElemIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentAlwaysDrawnElements = CurrentSavedViewAlwaysDrawnElements;

	// Update always drawn elements in current saved view
	for (auto it = CurrentAlwaysDrawnElements.begin(); it != CurrentAlwaysDrawnElements.end();)
	{
		if (InElemIDs.find(*it) == InElemIDs.end())
			it = CurrentAlwaysDrawnElements.erase(it);
		else
			++it;
	}

	for (const auto& InID : InElemIDs)
	{
		// Element already shown in previous saved view
		// Nothing to do.
		if (CurrentAlwaysDrawnElements.find(InID) != CurrentAlwaysDrawnElements.end() && !Opts.Force())
			continue;

		CurrentAlwaysDrawnElements.insert(InID);

		// 2. Show new Element, only if it exists in the tile.
		FITwinElementFeaturesInTile* FeaturesToUnHide = nullptr;
		if (InID != ITwin::NOT_ELEMENT)
		{
			FeaturesToUnHide = FindElementFeaturesSLOW(InID);
		}
		if (FeaturesToUnHide && !FeaturesToUnHide->Features.empty())
		{
			SelectingAndHiding->SetPixelsAlpha(FeaturesToUnHide->Features, 255);
			TextureNeeds.bWasChanged = true;
		}
	}
}

void FITwinSceneTile::HideModels(std::unordered_set<ITwinElementID> const& InModelIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentHiddenModels = CurrentSavedViewHiddenModels;
	
	THideIDs<ITwinElementID, FITwinModelFeaturesInTile>(
		CurrentHiddenModels,
		InModelIDs,
		[this](ITwinElementID id) { return FindModelFeaturesSLOW(id); },
		[this](FITwinModelFeaturesInTile* f) { SelectingAndHiding->SetPixelsAlpha(f->Features, 255); },
		[this, &TextureNeeds](FITwinModelFeaturesInTile* f) {
			CreateAndSetSelectingAndHiding(*f, TextureNeeds, ITwin::COLOR_HIDDEN_ELEMENT_BGRA, false);
		},
		TextureNeeds, Opts);
}

void FITwinSceneTile::HideCategories(std::unordered_set<ITwinElementID> const& InCategoryIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentHiddenCategories = CurrentSavedViewHiddenCategories;

	THideIDs<ITwinElementID, FITwinCategoryFeaturesInTile>(
		CurrentHiddenCategories,
		InCategoryIDs,
		[this](ITwinElementID id) { return FindCategoryFeaturesSLOW(id); },
		[this](FITwinCategoryFeaturesInTile* f) { SelectingAndHiding->SetPixelsAlpha(f->Features, 255); },
		[this, &TextureNeeds](FITwinCategoryFeaturesInTile* f) {
			CreateAndSetSelectingAndHiding(*f, TextureNeeds, ITwin::COLOR_HIDDEN_ELEMENT_BGRA, false);
		},
		TextureNeeds, Opts);
}

void FITwinSceneTile::HideCategoriesPerModel(
	std::unordered_set<std::pair<ITwinElementID,ITwinElementID>, FITwinSceneTile::pair_hash> const& InCategoryPerModelIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}

	auto& CurrentHiddenItems = CurrentSavedViewHiddenCategoriesPerModel;

	for (auto it = CurrentHiddenItems.begin(); it != CurrentHiddenItems.end();)
	{
		if (InCategoryPerModelIDs.find(*it) == InCategoryPerModelIDs.end())
		{
			FITwinCategoryPerModelFeaturesInTile* FeaturesToUnHide = FindCategoryPerModelFeaturesSLOW(*it);
			if (FeaturesToUnHide != nullptr)
			{
				SelectingAndHiding->SetPixelsAlpha(FeaturesToUnHide->Features, 255);
				TextureNeeds.bWasChanged = true;
			}
			it = CurrentHiddenItems.erase(it);
		}
		else
			++it;
	}

	for (const auto& InID : InCategoryPerModelIDs)
	{
		// Element already hidden in previous saved view
		// Nothing to do.
		if (CurrentHiddenItems.find(InID) != CurrentHiddenItems.end() && !Opts.Force())
			continue;

		CurrentHiddenItems.insert(InID);

		// 2. Hide new Element, only if it exists in the tile.
		FITwinCategoryPerModelFeaturesInTile* FeaturesToHide = nullptr;
		if (InID.first != ITwin::NOT_ELEMENT && InID.second != ITwin::NOT_ELEMENT)
		{
			FeaturesToHide = FindCategoryPerModelFeaturesSLOW(InID);
		}
		if (FeaturesToHide && !FeaturesToHide->Features.empty())
		{
			CreateAndSetSelectingAndHiding(*FeaturesToHide, TextureNeeds, ITwin::COLOR_HIDDEN_ELEMENT_BGRA, false);
		}
	}
}

void FITwinSceneTile::ShowCategoriesPerModel(
	std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> const& InCategoryPerModelIDs,
	FTextureNeeds& TextureNeeds, FShowHideOptions const Opts)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (Opts.OnlyVisibleTiles() && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentAlwaysDrawnCategories = CurrentSavedViewAlwaysDrawnCategoriesPerModel;

	// Update always drawn categories in current saved view
	for (auto it = CurrentAlwaysDrawnCategories.begin(); it != CurrentAlwaysDrawnCategories.end();)
	{
		if (InCategoryPerModelIDs.find(*it) == InCategoryPerModelIDs.end())
			it = CurrentAlwaysDrawnCategories.erase(it);
		else
			++it;
	}

	for (const auto& InID : InCategoryPerModelIDs)
	{
		// Category already shown in previous saved view
		// Nothing to do.
		if (CurrentAlwaysDrawnCategories.find(InID) != CurrentAlwaysDrawnCategories.end() && !Opts.Force())
			continue;

		CurrentAlwaysDrawnCategories.insert(InID);

		// 2. Show new category, only if it exists in the tile.
		FITwinCategoryPerModelFeaturesInTile* FeaturesToUnHide = nullptr;
		if (InID.first != ITwin::NOT_ELEMENT && InID.second != ITwin::NOT_ELEMENT)
		{
			FeaturesToUnHide = FindCategoryPerModelFeaturesSLOW(InID);
		}
		if (FeaturesToUnHide && !FeaturesToUnHide->Features.empty())
		{
			SelectingAndHiding->SetPixelsAlpha(FeaturesToUnHide->Features, 255);
			TextureNeeds.bWasChanged = true;
		}
	}
}

bool FITwinSceneTile::Need4DAnimTexturesSetupInMaterials() const
{
	return (HighlightsAndOpacities && bNeed4DHighlightsOpaTextureSetupInMaterials)
		|| (CuttingPlanes && bNeed4DCuttingPlanesTextureSetupInMaterials);
}

bool FITwinSceneTile::NeedSelectingAndHidingTexturesSetupInMaterials() const
{
	return (SelectingAndHiding && bNeedSelectingAndHidingTextureSetupInMaterials);
}

FString FITwinSceneTile::GetIDString() const
{
	FString IdStr(Cesium3DTilesSelection::TileIdUtilities::createTileIdString(TileID.first).c_str());
	if (!TileID.second.empty())
	{
		IdStr += TEXT(" (") + FString(TileID.second.c_str()) + TEXT(")");
	}
	return IdStr;
}

FString FITwinSceneTile::ToString() const
{
	return FString::Printf(TEXT(
		"Tile %s tuneVer#%d Viz:%d #Elems:%llu #Extr:%llu(%llu) #Feat:%u #Gltf:%llu #Mats:%llu\n\t" \
		"4D:%d #Tml:%llu Tex[HiO/CUT/SEL]:%d/%d/%d NeedSetup[HiO/CUT/SEL]:%d/%d/%d\n\t" \
		"Selec:%s CurSVHidn:%llu CurCSTHidn:%llu"),
		*GetIDString(),
		(pCesiumTile->GetGltfModel() && pCesiumTile->GetGltfModel()->version)
			? (*pCesiumTile->GetGltfModel()->version) : -1,
		bVisible ? 1 : 0, ElementsFeatures.size(), ExtractedElements.size(),
		[this]() { size_t Total = 0;
			for (auto const& Extr : ExtractedElements) Total += Extr.Entities.size(); return Total; } (),
		(ITwin::NOT_FEATURE == MaxFeatureID) ? 0 : (MaxFeatureID.value() + 1),
		GltfMeshes.size(), Materials.size(),
		bIsSetupFor4DAnimation ? 1 : 0, TimelinesIndices.size(),
		HighlightsAndOpacities ? 1 : 0, CuttingPlanes ? 1 : 0, SelectingAndHiding ? 1 : 0,
		(HighlightsAndOpacities && bNeed4DHighlightsOpaTextureSetupInMaterials) ? 1 : 0,
		(CuttingPlanes && bNeed4DCuttingPlanesTextureSetupInMaterials) ? 1 : 0,
		(SelectingAndHiding && bNeedSelectingAndHidingTextureSetupInMaterials) ? 1 : 0,
		(ITwin::NOT_ELEMENT == SelectedElement) ? TEXT("no") : (*ITwin::ToString(ITwin::NOT_ELEMENT)),
		CurrentSavedViewHiddenElements.size(), CurrentConstructionHiddenElements.size()
	);
}

namespace
{

	/// This code depends on the parameters actually published in MF_CesiumGlTF.uasset
	static FName GetTextureMapParamName(AdvViz::SDK::EChannelType Channel)
	{
		switch (Channel)
		{
		case AdvViz::SDK::EChannelType::Color:
		case AdvViz::SDK::EChannelType::Alpha: /* alpha should be merged with colors */
			return TEXT("baseColorTexture");

		case AdvViz::SDK::EChannelType::Transparency:
			BE_ISSUE("transparency texture not implemented - please use opacity (=Alpha)");
			break;

		case AdvViz::SDK::EChannelType::Normal:
			return TEXT("normalTexture");

		case AdvViz::SDK::EChannelType::Metallic:
		case AdvViz::SDK::EChannelType::Roughness:
			return TEXT("metallicRoughnessTexture");

		case AdvViz::SDK::EChannelType::AmbientOcclusion:
			return TEXT("occlusionTexture");

		default:
			ensureMsgf(false, TEXT("channel %u not implemented for texture maps"), Channel);
			break;
		}
		return {};
	}

	// Cache the (constant by channel) parameter info, to avoid constructing a FName hundreds of time.

	static ITwin::FPerChannelParamInfos PerChannelTexParamInfos;


	struct FITwinMaterialTextureHelper
	{
		FITwinSceneTile& SceneTile;
		AdvViz::SDK::EChannelType const Channel = {};
		UTexture* pTexture_GlobalParam = nullptr;
		UTexture* pTexture_LayerParam = nullptr;
		ITwin::FChannelParamInfosOpt& ParamInfosOpt;
		bool bIsRestoringInitialTextures = false;

		FITwinMaterialTextureHelper(FITwinSceneTile& InSceneTile,
			AdvViz::SDK::EChannelType InChannel,
			UTexture* InTexture_GlobalParam,
			UTexture* InTexture_LayerParam)
			: SceneTile(InSceneTile)
			, Channel(InChannel)
			, pTexture_GlobalParam(InTexture_GlobalParam)
			, pTexture_LayerParam(InTexture_LayerParam)
			, ParamInfosOpt(PerChannelTexParamInfos[(size_t)InChannel])
		{
			ensureMsgf(IsInGameThread(), TEXT("PerChannelTexParamInfos handling is not thread-safe"));
			if (!ParamInfosOpt)
			{
				ParamInfosOpt.emplace(GetTextureMapParamName(Channel));
			}
		}

		void BeforeSetTextures(UMaterialInstanceDynamic* Mat) const
		{
			// Store the initial textures being replaced for this material, in order to restore them during
			// the deletion process of the tile.
			if (!bIsRestoringInitialTextures
				&& !SceneTile.HasInitialTexturesForChannel(Mat, Channel))
			{
				UTexture* Tex_GlobalParam = nullptr;
				Mat->GetTextureParameterValue(
					ParamInfosOpt->GlobalParamInfo,
					Tex_GlobalParam, true);
				UTexture* Tex_LayerParam = nullptr;
				Mat->GetTextureParameterValue(
					ParamInfosOpt->LayerParamInfo,
					Tex_LayerParam, true);
				// Remark: both textures are usually the same, due to the way Cesium creates them
				if (Tex_GlobalParam || Tex_LayerParam)
				{
					SceneTile.StoreInitialTexturesForChannel(Mat, Channel, Tex_GlobalParam, Tex_LayerParam);
				}
			}
		}

		void operator()(UMaterialInstanceDynamic& MatInstance) const
		{
			BeforeSetTextures(&MatInstance);

			MatInstance.SetTextureParameterValueByInfo(
				ParamInfosOpt->GlobalParamInfo,
				pTexture_GlobalParam);
			MatInstance.SetTextureParameterValueByInfo(
				ParamInfosOpt->LayerParamInfo,
				pTexture_LayerParam);
		}

	}; // FITwinMaterialTextureHelper

} // anon. ns.

void FITwinSceneTile::SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID,
	AdvViz::SDK::EChannelType Channel, UTexture* pTexture)
{
	FITwinMaterialTextureHelper const TexHelper(*this, Channel, pTexture, pTexture);
	ForEachMaterialInstanceMatchingID(ITwinMaterialID, TexHelper);
}

void FITwinSceneTile::ResetCustomTexturesInMaterials()
{
	for (auto& [MatInstance, Channels] : MatsWithTexturesToRestore)
	{
		for (auto& [Chan, RestoreInfo] : Channels)
		{
			if (!RestoreInfo.Mat.IsValid())
				continue;

			UTexture* TexToRestore_Global = RestoreInfo.OrigTextures[0].IsValid()
				? RestoreInfo.OrigTextures[0].Get() : nullptr;
			UTexture* TexToRestore_Layer = RestoreInfo.OrigTextures[1].IsValid()
				? RestoreInfo.OrigTextures[1].Get() : nullptr;
			// Important note: #SetTextureParameterValueByInfo with a null texture does nothing internally
			// (ie. the texture currently present in the material will remain), and we cannot let our static
			// textures (such as NoNormalTexture...) be destroyed by Cesium's function #destroyTexture, so
			// as a quick workaround, I protected them from deletion in #UITwinMaterialDefaultTexturesHolder
			// static constructor...
			FITwinMaterialTextureHelper TexHelper(*this, Chan,
				TexToRestore_Global, TexToRestore_Layer);
			TexHelper.bIsRestoringInitialTextures = true;
			TexHelper(*RestoreInfo.Mat);
			RestoreInfo.Mat.Reset();
		}
	}
}
