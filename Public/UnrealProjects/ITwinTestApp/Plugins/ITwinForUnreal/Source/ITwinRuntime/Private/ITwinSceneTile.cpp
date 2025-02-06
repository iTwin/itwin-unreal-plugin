/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneTile.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"

#include <Cesium3DTilesSelection/Tile.h>
#include <ITwinExtractedMeshComponent.h>
#include <Engine/StaticMesh.h> // FStaticMaterial
#include <Materials/MaterialInstanceDynamic.h>
#include <MaterialTypes.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{
	/// Highlight color for selected element (CANNOT use the alpha component, which would conflict with
	/// HideElements!)
	static const std::array<uint8, 4> COLOR_SELECTED_ELEMENT_BGRA = { 96, 230, 0, 255 };
	/// Highlight color for hidden element
	static const std::array<uint8, 4> COLOR_HIDDEN_ELEMENT_BGRA = { 0, 0, 0, 0 };
	/// Highlight color to revert to when deselecting AND un-hiding an element
	static const std::array<uint8, 4> COLOR_UNSELECT_ELEMENT_BGRA = { 0, 0, 0, 255 };
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
void FITwinExtractedElement::Unload()
{
	Entities.clear();
}

//---------------------------------------------------------------------------------------
// struct FITwinExtractedEntity
//---------------------------------------------------------------------------------------

void FITwinExtractedEntity::SetHidden(bool bHidden)
{
	if (MeshComponent.IsValid())
	{
		MeshComponent->SetFullyHidden(bHidden);
	}
}

bool FITwinExtractedEntity::SetBaseMaterial(UMaterialInterface* BaseMaterial)
{
	if (!MeshComponent.IsValid())
	{
		// Was the tile from which this mesh was extracted invalidated?
		return false;
	}
	TObjectPtr<UStaticMesh> StaticMesh = MeshComponent->GetStaticMesh();
	if (!StaticMesh)
	{
		ensureMsgf(false, TEXT("orphan mesh component"));
		return false;
	}

	TArray<FStaticMaterial> StaticMaterials =
		StaticMesh->GetStaticMaterials();
	ensure(StaticMaterials.Num() == 1);

	FStaticMaterial& StaticMaterial = StaticMaterials[0];
	ensureMsgf(StaticMaterial.MaterialInterface == this->Material,
		TEXT("material mismatch"));

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
	return ITwin::NOT_FEATURE != MaxFeatureID;
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

void FITwinSceneTile::ResetSelection(ITwinElementID& SelectedElementID, FTextureNeeds& TextureNeeds)
{
	check(SelectingAndHiding);
	FITwinElementFeaturesInTile* FeaturesToDeSelect = FindElementFeaturesSLOW(SelectedElementID);
	if (ensure(FeaturesToDeSelect != nullptr))
	{
		SelectingAndHiding->SetPixelsExceptAlpha(FeaturesToDeSelect->Features, 
												 ITwin::COLOR_UNSELECT_ELEMENT_BGRA);
		TextureNeeds.bWasChanged = true;
	}
	SelectedElementID = ITwin::NOT_ELEMENT;
}

void FITwinSceneTile::CreateAndSetSelectingAndHiding(FITwinElementFeaturesInTile& FeaturesToSelectOrHide,
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
	// Apply constant highlight color to pixels matching the Element's features
	if (bColorOrAlpha)
		SelectingAndHiding->SetPixelsExceptAlpha(FeaturesToSelectOrHide.Features, Color_BGRA);
	else
		SelectingAndHiding->SetPixelsAlpha(FeaturesToSelectOrHide.Features, Color_BGRA[3]);
	TextureNeeds.bWasChanged = true;
}

bool FITwinSceneTile::SelectElement(ITwinElementID const& InElemID, bool const bOnlyVisibleTiles,
									FTextureNeeds& TextureNeeds, bool const bTestElementVisibility/*=false*/)
{
	if (InElemID == SelectedElement // Element exists in this tile and is already highlighted
		// Element is known to NOT exist in this tile:
		|| (SelectedElementNotInTile != ITwin::NOT_ELEMENT && SelectedElementNotInTile == SelectedElement)
		|| (bOnlyVisibleTiles && !bVisible) // filter out hidden tiles too (other LODs, culled out...)
		|| MaxFeatureID == ITwin::NOT_FEATURE // empty tile
	) {
		return false;
	}

	// 0. SAFETY measure
	if (SelectingAndHiding
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SelectingAndHiding->GetTotalUsedPixels() < (MaxFeatureID.value() + 1))
	{
		ensure(false); // should not happen
		SelectingAndHiding.reset(); // let's hope it doesn't crash everything...
		SelectedElement = ITwin::NOT_ELEMENT;
		ForEachElementFeatures([](FITwinElementFeaturesInTile& ElementInTile)
			{ ElementInTile.TextureFlags.SelectingAndHidingTexFlag.Invalidate(); }); // no cond on bTextureIsSet here
	}

	// 1. Reset current selection, if any.
	if (SelectedElement != ITwin::NOT_ELEMENT)
	{
		ResetSelection(SelectedElement, TextureNeeds);
	}

	// 2. Select new Element, only if it exists in the tile.
	FITwinElementFeaturesInTile* FeaturesToSelect = nullptr;
	if (InElemID != ITwin::NOT_ELEMENT)
	{
		FeaturesToSelect = FindElementFeaturesSLOW(InElemID);
	}
	if (FeaturesToSelect != nullptr && !FeaturesToSelect->Features.empty())
	{
		if (bTestElementVisibility)
		{
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
					bool bHasVisibleExtractedElem = false;
					if (ITwinTile::NOT_EXTR != FeaturesToSelect->ExtractedRank)
					{
						FITwinExtractedElement& Extracted = ExtractedElement(FeaturesToSelect->ExtractedRank);
						for (auto&& Entry : Extracted.Entities)
						{
							if (Entry.MeshComponent.IsValid() && Entry.MeshComponent->IsVisible())
							{
								bHasVisibleExtractedElem = true;
								break;
							}
						}
					}
					if (!bHasVisibleExtractedElem)
						return false;
				}
			}
		}
		CreateAndSetSelectingAndHiding(*FeaturesToSelect, TextureNeeds, ITwin::COLOR_SELECTED_ELEMENT_BGRA,
									   /*bColorOrAlpha: color only*/true);
		SelectedElement = InElemID;
		SelectedElementNotInTile = ITwin::NOT_ELEMENT;
		return true;
	}
	else
	{
		SelectedElementNotInTile = InElemID; // may be ITwin::NOT_ELEMENT in case of deselection
		return false;
	}
}

void FITwinSceneTile::HideElements(std::unordered_set<ITwinElementID> const& InElemIDs,
	bool const bOnlyVisibleTiles, FTextureNeeds& TextureNeeds, bool const IsConstruction)
{
	if (MaxFeatureID == ITwin::NOT_FEATURE
		|| (bOnlyVisibleTiles && !bVisible)) // filter out hidden tiles too (other LODs, culled out...)
	{
		// No Feature at all.
		return;
	}
	auto& CurrentHiddenElements =
		IsConstruction ? CurrentConstructionHiddenElements : CurrentSavedViewHiddenElements;
	// Update hidden elements in current saved view
	for (auto it = CurrentHiddenElements.begin(), it2 = CurrentHiddenElements.end();
		 it != it2; ++it)
	{
		if (*it != ITwin::NOT_ELEMENT && std::find(InElemIDs.begin(), InElemIDs.end(), *it) == InElemIDs.end())
		{
			FITwinElementFeaturesInTile* FeaturesToUnHide = FindElementFeaturesSLOW(*it);
			if (FeaturesToUnHide != nullptr)
			{
				//UE_LOG(LogTemp, Display, TEXT("ElementID to be SHOWN is 0x % I64x"), (*it).value());
				SelectingAndHiding->SetPixelsAlpha(FeaturesToUnHide->Features, 255);
				TextureNeeds.bWasChanged = true;
			}
			*it = ITwin::NOT_ELEMENT;
		}
	}
	for (const auto& InElemID : InElemIDs)
	{
		if (std::find(CurrentHiddenElements.begin(), CurrentHiddenElements.end(), InElemID) 
			!= CurrentHiddenElements.end())
		{
			// Element already hidden in previous saved view
			// Nothing to do.
			continue;
		}
		else
		{
			CurrentHiddenElements.push_back(InElemID);
		}
		// 1. Deselect element to be hidden.
		if (SelectedElement == InElemID)
		{
			ResetSelection(SelectedElement, TextureNeeds);
		}
		// 2. Hide new Element, only if it exists in the tile.
		FITwinElementFeaturesInTile* FeaturesToHide = nullptr;
		if (InElemID != ITwin::NOT_ELEMENT)
		{
			FeaturesToHide = FindElementFeaturesSLOW(InElemID);
		}
		if (FeaturesToHide != nullptr && !FeaturesToHide->Features.empty())
		{
			//UE_LOG(LogTemp, Display, TEXT("ElementID to be hidden is 0x % I64x"), InElemID.value());
			CreateAndSetSelectingAndHiding(*FeaturesToHide, TextureNeeds, ITwin::COLOR_HIDDEN_ELEMENT_BGRA,
										  /*bColorOrAlpha: alpha only*/false);
		}
	}
}

bool FITwinSceneTile::IsElementHiddenInSavedView(ITwinElementID const& InElemID) const
{
	return std::find(CurrentSavedViewHiddenElements.begin(), CurrentSavedViewHiddenElements.end(), InElemID)
		!= CurrentSavedViewHiddenElements.end();
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

FString FITwinSceneTile::ToString() const
{
	return FString::Printf(TEXT(
"Tile %s Viz:%d #Elems:%llu #Extr:%llu(%llu) #Feat:%u #Gltf:%llu #Mats:%llu\n\t4D:%d #Tml:%llu Tex[HiO/CUT/SEL]:%d/%d/%d NeedSetup[HiO/CUT/SEL]:%d/%d/%d\n\tSelec:%s%s CurSVHidn:%llu CurCSTHidn:%llu"),
		*FString(Cesium3DTilesSelection::TileIdUtilities::createTileIdString(TileID).c_str()),
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
		(ITwin::NOT_ELEMENT == SelectedElement && ITwin::NOT_ELEMENT == SelectedElementNotInTile)
			? TEXT("no") : (*ITwin::ToString(ITwin::NOT_ELEMENT == SelectedElement ? SelectedElementNotInTile
																				   : SelectedElement)),
		(ITwin::NOT_ELEMENT == SelectedElement && ITwin::NOT_ELEMENT == SelectedElementNotInTile)
			? TEXT("") : (ITwin::NOT_ELEMENT != SelectedElement ? TEXT("(in T.)") : TEXT("(NOT in T.)")),
		CurrentSavedViewHiddenElements.size(), CurrentConstructionHiddenElements.size()
	);
}

namespace
{
	struct FITwinMaterialTextureHelper
	{
		FITwinSceneTile& SceneTile;
		bool bIsRestoringInitialTextures = false;
		SDK::Core::EChannelType Channel = {};
		UTexture* pTexture_GlobalParam = nullptr;
		UTexture* pTexture_LayerParam = nullptr;


#define DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE(_paramName)	\
	BeforeSetTextures(&MatInstance, _paramName);	\
	MatInstance.SetTextureParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE), \
		pTexture_GlobalParam);									\
	MatInstance.SetTextureParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::LayerParameter, 0), \
		pTexture_LayerParam);


		void BeforeSetTextures(UMaterialInstanceDynamic* Mat, FName const& ParamName) const
		{
			// Store the initial textures being replaced for this material, in order to restore them during
			// the deletion process of the tile.
			if (!bIsRestoringInitialTextures
				&& !SceneTile.HasInitialTexturesForChannel(Mat, Channel))
			{
				UTexture* Tex_GlobalParam = nullptr;
				Mat->GetTextureParameterValue(
					FMaterialParameterInfo(ParamName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE),
					Tex_GlobalParam, true);
				UTexture* Tex_LayerParam = nullptr;
				Mat->GetTextureParameterValue(
					FMaterialParameterInfo(ParamName, EMaterialParameterAssociation::LayerParameter, 0),
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
			// This code depends on the parameters actually published in MF_ITwinCesiumGlTF.uasset
			switch (Channel)
			{
			case SDK::Core::EChannelType::Color:
			case SDK::Core::EChannelType::Alpha: /* alpha should be merged with colors */
				DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE("baseColorTexture");
				break;

			case SDK::Core::EChannelType::Transparency:
				BE_ISSUE("transparency texture not implemented - please use opacity (=Alpha)");
				break;

			case SDK::Core::EChannelType::Normal:
				DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE("normalTexture");
				break;

			case SDK::Core::EChannelType::Metallic:
			case SDK::Core::EChannelType::Roughness:
				DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE("metallicRoughnessTexture");
				break;

			case SDK::Core::EChannelType::AmbientOcclusion:
				DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE("occlusionTexture");
				break;

			default:
				ensureMsgf(false, TEXT("channel %u not implemented for texture maps"), Channel);
				break;
			}
		}

#undef DO_SET_CESIUMMAT_TEXTURE_PARAM_VALUE

	}; // FITwinMaterialTextureHelper

} // anon. ns.

void FITwinSceneTile::SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID,
	SDK::Core::EChannelType Channel, UTexture* pTexture)
{
	FITwinMaterialTextureHelper const TexHelper = {
		.SceneTile = *this,
		.Channel = Channel,
		.pTexture_GlobalParam = pTexture,
		.pTexture_LayerParam = pTexture
	};
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
			FITwinMaterialTextureHelper const TexHelper = {
				.SceneTile = *this,
				.bIsRestoringInitialTextures = true,
				.Channel = Chan,
				.pTexture_GlobalParam = RestoreInfo.OrigTextures[0],
				.pTexture_LayerParam = RestoreInfo.OrigTextures[1]
			};
			TexHelper(*RestoreInfo.Mat);
			RestoreInfo.Mat.Reset();
		}
	}
}
