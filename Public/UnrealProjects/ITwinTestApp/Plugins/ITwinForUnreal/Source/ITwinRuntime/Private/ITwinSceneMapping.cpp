/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMapping.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"
#include <ITwinIModel.h>
#include <ITwinUtilityLibrary.h>
#include <Math/UEMathExts.h>
#include <Timeline/Timeline.h>
#include <Timeline/SchedulesConstants.h>

#include <ITwinExtractedMeshComponent.h>
#include <Material/ITwinMaterialParameters.inl>

#include <GenericPlatform/GenericPlatformTime.h>
#include <Logging/LogMacros.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <MaterialTypes.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>

//#include "../../CesiumRuntime/Private/CesiumMaterialUserData.h" // see comment in #GetSynchro4DLayerIndexInMaterial

DECLARE_LOG_CATEGORY_EXTERN(ITwinSceneMap, Log, All);
DEFINE_LOG_CATEGORY(ITwinSceneMap);

// TODO_GCO: ignored in FITwinSceneMapping::ReplicateAnimElemTextureSetupInTile
#define DEBUG_SYNCHRO4D_BGRA 0
bool FITwinSceneTile::HasInitialTexturesForChannel(UMaterialInstanceDynamic* Mat, SDK::Core::EChannelType Chan) const
{
	auto const itMatInfo = MatsWithTexturesToRestore.find(Mat);
	if (itMatInfo == MatsWithTexturesToRestore.end())
		return false;
	auto const itRestoreInfo = itMatInfo->second.find(Chan);
	if (itRestoreInfo == itMatInfo->second.end())
		return false;
	return itRestoreInfo->second.Mat.IsValid();
}

void FITwinSceneTile::StoreInitialTexturesForChannel(UMaterialInstanceDynamic* Mat, SDK::Core::EChannelType Chan,
	UTexture* pTexture_GlobalParam, UTexture* pTexture_LayerParam)
{
	auto& MatRestoreInfo = MatsWithTexturesToRestore[Mat];
	FMatTextureRestoreInfo& MatTexRestoreInfo = MatRestoreInfo[Chan];
	MatTexRestoreInfo.Mat = Mat;
	MatTexRestoreInfo.OrigTextures = { pTexture_GlobalParam, pTexture_LayerParam };
}


static const FName SelectingAndHidingMaterialParameterName = "PROP_Selection_RGBA";
static const FName HighlightsAndOpacitiesMaterialParameterName = "PROP_Synchro4D_RGBA";
static const FName CuttingPlanesMaterialParameterName = "PROP_Synchro4D_CutPlanes";
static const FName ExtractedElementForcedAlphaName = "PROP_Synchro4D_ForcedAlpha";
static const FName FeatureIdMaterialParameterName = "_FEATURE_ID_0";

constexpr int32 GetSynchro4DLayerIndexInMaterial(/*TWeakObjectPtr<UMaterialInstanceDynamic> pMaterial*/)
{
	// Counting layers from the top it seems...So this makes Cesium's Clipping (unrelated to Synchro
	// clipping!) the layer of index 0, Cesium's DitheringFade the index 1, and ours is 2.
	constexpr int32 s_DefaultSynchro4DLayerIndexInMaterial = 2;
	return s_DefaultSynchro4DLayerIndexInMaterial;

	//static int32 s_Synchro4DLayerIndexInMaterial = -1;
	//if (s_Synchro4DLayerIndexInMaterial != -1)
	//{
	//	return s_Synchro4DLayerIndexInMaterial;
	//}
	//if (!pMaterial.IsValid())
	//{
	//	check(false); return s_DefaultSynchro4DLayerIndexInMaterial;
	//}

	// Compiles using the above commented-out inclusion but, as expected, one gets an unresolved
	// UCesiumMaterialUserData::GetPrivateStaticClass symbol at link:
	//UCesiumMaterialUserData* pCesiumData = pMaterial->GetAssetUserData<UCesiumMaterialUserData>();
	//if (pCesiumData)
	//	s_Synchro4DLayerIndexInMaterial = pCesiumData->LayerNames.Find(TEXT("ITwin Overlay"));
	//if (INDEX_NONE == s_Synchro4DLayerIndexInMaterial)
	//{
	//	s_Synchro4DLayerIndexInMaterial = s_DefaultSynchro4DLayerIndexInMaterial;
	//}

	//s_Synchro4DLayerIndexInMaterial = s_DefaultSynchro4DLayerIndexInMaterial;
	//return s_Synchro4DLayerIndexInMaterial;
}

namespace
{
	static std::optional<FMaterialParameterInfo> HighlightsAndOpacitiesInfo;
	static std::optional<FMaterialParameterInfo> FeatureIdInfo;
	static std::optional<FMaterialParameterInfo> CuttingPlanesInfo;
	static std::optional<FMaterialParameterInfo> ExtractedElementForcedAlphaInfo;
}

namespace ITwinMatParamInfo
{
	std::optional<FMaterialParameterInfo> SelectingAndHidingInfo;

	void SetupFeatureIdInfo()
	{
		if (!FeatureIdInfo)
		{
			FeatureIdInfo.emplace(FeatureIdMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial(/*pMaterial*/));
		}
	}

	void SetupSelectingAndHidingInfo()
	{
		if (!SelectingAndHidingInfo)
		{
			SelectingAndHidingInfo.emplace(SelectingAndHidingMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial());
		}
	}
}

namespace
{
	inline void SetupHighlightsAndOpacitiesInfo()
	{
		if (!HighlightsAndOpacitiesInfo)
		{
			HighlightsAndOpacitiesInfo.emplace(HighlightsAndOpacitiesMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial(/*pMaterial*/));
		}
	}

	inline void SetupExtractedElementForcedAlphaInfo()
	{
		if (!ExtractedElementForcedAlphaInfo)
		{
			ExtractedElementForcedAlphaInfo.emplace(ExtractedElementForcedAlphaName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial(/*pMaterial*/));
		}
	}

	inline void SetupCuttingPlanesInfo()
	{
		if (!CuttingPlanesInfo)
		{
			CuttingPlanesInfo.emplace(CuttingPlanesMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial(/*ElementFeaturesInTile.GetFirstValidMaterial()*/));
		}
	}

#define ENABLE_CheckMaterialSetup 0

	void CheckMaterialSetup(UMaterialInstanceDynamic& Mat, bool const bIsExtracted,
							bool const bCheckBGRA, bool const bCheckCutPlanes, bool const bCheckSelection)
	{
	#if ENABLE_CheckMaterialSetup
		float FeatUDUVIdx = -1;
		Mat.GetScalarParameterValue(*FeatureIdInfo, FeatUDUVIdx, true);
		check(FeatUDUVIdx >= 0);
		if (bIsExtracted)
		{
			float OutVal = -42.f;
			Mat.GetScalarParameterValue(
				FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo(), OutVal, true);
			check(OutVal >= 0.f);
		}
		if (bCheckBGRA)
		{
			UTexture* OutTex = nullptr;
			Mat.GetTextureParameterValue(*HighlightsAndOpacitiesInfo, OutTex, true);
			check(OutTex);
		}
		if (bCheckCutPlanes)
		{
			UTexture* OutTex = nullptr;
			Mat.GetTextureParameterValue(*CuttingPlanesInfo, OutTex, true);
			check(OutTex);
		}
		if (bCheckSelection)
		{
			UTexture* OutTex = nullptr;
			Mat.GetTextureParameterValue(*ITwinMatParamInfo::SelectingAndHidingInfo, OutTex, true);
			check(OutTex);
		}
	#endif // ENABLE_CheckMaterialSetup
	}

} // anon ns.

#if ENABLE_DRAW_DEBUG
	float ITwinDebugBoxNextLifetime = 5.f;
#endif

//---------------------------------------------------------------------------------------
// class FITwinSceneMapping
//---------------------------------------------------------------------------------------

/*static*/
FMaterialParameterInfo const& FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo()
{
	SetupExtractedElementForcedAlphaInfo();
	return *ExtractedElementForcedAlphaInfo;
}

/*static*/
void FITwinSceneMapping::SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile,
											   FITwinExtractedEntity& ExtractedEntity)
{
	if (ensure(FeatureIdInfo && ExtractedEntity.FeatureIDsUVIndex)
		&& ExtractedEntity.Material.IsValid())
	{
		ExtractedEntity.Material->SetScalarParameterValueByInfo(*FeatureIdInfo,
																*ExtractedEntity.FeatureIDsUVIndex);
	}
}

/*static*/
void FITwinSceneMapping::SetupHighlightsOpacities(
	FITwinSceneTile& SceneTile,
	FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ElementFeaturesInTile.Materials.empty())
		return;

	auto const& HighlightOpaTexFlags = ElementFeaturesInTile.TextureFlags.Synchro4DHighlightOpaTexFlag;
	bool const bNeedHighlightOpaTexSetupInMaterials = HighlightOpaTexFlags.NeedSetupInMaterials(
		SceneTile.HighlightsAndOpacities, (int32)ElementFeaturesInTile.Materials.size());

	// NB: those FMaterialParameterInfo info no longer depend on the material, so we can setup them at once:
	if (bNeedHighlightOpaTexSetupInMaterials)
	{
		SetupHighlightsAndOpacitiesInfo();
	}
	ITwinMatParamInfo::SetupFeatureIdInfo();
	if (!bNeedHighlightOpaTexSetupInMaterials)
	{
		// Nothing to do - let's just make some sanity checks here
		for (auto&& Mat : ElementFeaturesInTile.Materials)
		{
			if (Mat.IsValid())
			{
				CheckMaterialSetup(*Mat.Get(), false,
					(bool)SceneTile.HighlightsAndOpacities, (bool)SceneTile.CuttingPlanes,
					(bool)SceneTile.SelectingAndHiding);
			}
		}
		return;
	}
	auto const SetupElement = [&SceneTile, bNeedHighlightOpaTexSetupInMaterials]
		(FITwinElementFeaturesInTile& Element)
		{
			if (bNeedHighlightOpaTexSetupInMaterials && !Element.Materials.empty())
			{
				if (SceneTile.HighlightsAndOpacities->SetupInMaterials(Element.Materials,
																	   *HighlightsAndOpacitiesInfo))
				{
					Element.TextureFlags.Synchro4DHighlightOpaTexFlag.OnTextureSetupInMaterials(
						(int32)Element.Materials.size());
				}
				else ensure(SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials);
			}
		};
#if DEBUG_SYNCHRO4D_BGRA
	// Handle all elements, otherwise only materials used by animated elements will have the debug
	// colors, so you won't probably see much of anything...(you still won't get all tiles colored, only
	// those containing at least one animated Element - unless using OnFadeOutNonAnimatedElements I guess)
	SceneTile.ForEachElementFeatures([&SetupElement](FITwinElementFeaturesInTile& ElementFeaturesInTile)
		{
			SetupElement(ElementFeaturesInTile);
		});
#else
	SetupElement(ElementFeaturesInTile);
#endif
}

/*static*/
void FITwinSceneMapping::SetupHighlightsOpacities(
	FITwinSceneTile& SceneTile,
	FITwinExtractedEntity& ExtractedEntity)
{
	auto const& HighlightOpaTexFlags = ExtractedEntity.TextureFlags.Synchro4DHighlightOpaTexFlag;
	bool const bNeedHighlightOpaTexSetupInMaterials =
		HighlightOpaTexFlags.NeedSetupInMaterials(SceneTile.HighlightsAndOpacities, 1);
	if (!bNeedHighlightOpaTexSetupInMaterials || !ExtractedEntity.Material.IsValid())
	{
		if (ensure(ExtractedEntity.Material.IsValid()))
		{
			CheckMaterialSetup(*ExtractedEntity.Material.Get(), true,
				(bool)SceneTile.HighlightsAndOpacities, (bool)SceneTile.CuttingPlanes,
				(bool)SceneTile.SelectingAndHiding);
		}
		return;
	}
	SetupHighlightsAndOpacitiesInfo();
	ITwinMatParamInfo::SetupFeatureIdInfo();
	SetupFeatureIdUVIndex(SceneTile, ExtractedEntity);
	if (bNeedHighlightOpaTexSetupInMaterials &&
		SceneTile.HighlightsAndOpacities->SetupInMaterial(ExtractedEntity.Material,
														   *HighlightsAndOpacitiesInfo))
	{
		ExtractedEntity.TextureFlags.Synchro4DHighlightOpaTexFlag.OnTextureSetupInMaterials(1);
	}
}

/*static*/
void FITwinSceneMapping::SetupCuttingPlanes(
	FITwinSceneTile& SceneTile,
	FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ElementFeaturesInTile.Materials.empty())
		return;
	auto const& CuttingPlaneTexFlags = ElementFeaturesInTile.TextureFlags.Synchro4DCuttingPlaneTexFlag;
	bool const bCuttingPlaneTexSetupInMaterials = CuttingPlaneTexFlags.NeedSetupInMaterials(
		SceneTile.CuttingPlanes, (int32)ElementFeaturesInTile.Materials.size());
	if (!bCuttingPlaneTexSetupInMaterials)
		return;
	SetupCuttingPlanesInfo();
	ITwinMatParamInfo::SetupFeatureIdInfo();
	if (SceneTile.CuttingPlanes->SetupInMaterials(ElementFeaturesInTile.Materials, *CuttingPlanesInfo))
	{
		ElementFeaturesInTile.TextureFlags.Synchro4DCuttingPlaneTexFlag.OnTextureSetupInMaterials(
			(int32)ElementFeaturesInTile.Materials.size());
	}
	else ensure(SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials);
}

/*static*/
void FITwinSceneMapping::SetupCuttingPlanes(
	FITwinSceneTile& SceneTile,
	FITwinExtractedEntity& ExtractedEntity)
{
	if (!ExtractedEntity.Material.IsValid())
		return;
	auto const& CuttingPlaneTexFlags = ExtractedEntity.TextureFlags.Synchro4DCuttingPlaneTexFlag;
	bool const bCuttingPlaneTexSetupInMaterials = CuttingPlaneTexFlags.NeedSetupInMaterials(
		SceneTile.CuttingPlanes, 1);
	if (!bCuttingPlaneTexSetupInMaterials)
		return;
	SetupCuttingPlanesInfo();
	ITwinMatParamInfo::SetupFeatureIdInfo();
	SetupFeatureIdUVIndex(SceneTile, ExtractedEntity);
	if (SceneTile.CuttingPlanes->SetupInMaterial(ExtractedEntity.Material, *CuttingPlanesInfo))
	{
		ExtractedEntity.TextureFlags.Synchro4DCuttingPlaneTexFlag.OnTextureSetupInMaterials(1);
	}
}

/*static*/
void FITwinSceneMapping::SetForcedOpacity(TWeakObjectPtr<UMaterialInstanceDynamic> const& Material,
										  float const Opacity)
{
	if (Material.IsValid())
	{
		Material->SetScalarParameterValueByInfo(
			FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo(),
			Opacity);
	}
}

FITwinSceneMapping::FITwinSceneMapping()
{
	AllElements.reserve(16384);
}

FITwinSceneTile& FITwinSceneMapping::KnownTile(ITwinScene::TileIdx const Rank)
{
	// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
	return const_cast<FITwinSceneTile&>(KnownTiles.get<IndexByRank>()[Rank.value()]);
}

FITwinSceneTile& FITwinSceneMapping::KnownTileSLOW(Cesium3DTilesSelection::Tile& CesiumTile,
												   ITwinScene::TileIdx* Rank/*= nullptr*/)
{
	auto& ByRank = KnownTiles.get<IndexByRank>();
	auto const It = ByRank.emplace_back(CesiumTile.getTileID()).first;
	if (Rank)
	{
		*Rank = ITwinScene::TileIdx(static_cast<uint32_t>(It - ByRank.begin()));
	}
	// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
	auto& SceneTile = const_cast<FITwinSceneTile&>(*It);
	ensure(!SceneTile.pCesiumTile || SceneTile.pCesiumTile == &CesiumTile);
	SceneTile.pCesiumTile = &CesiumTile;
	return SceneTile;
}

FITwinSceneTile* FITwinSceneMapping::FindKnownTileSLOW(CesiumTileID const& TileId)
{
	auto& ByID = KnownTiles.get<IndexByTileID>();
	auto const It = ByID.find(TileId);
	if (It != ByID.end())
	{
		// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
		return const_cast<FITwinSceneTile*>(&(*It));
	}
	else return nullptr;
}

void FITwinSceneMapping::ForEachKnownTile(std::function<void(FITwinSceneTile const&)> const& Func) const
{
	for (auto&& SceneTile : KnownTiles)
		Func(SceneTile);
}

void FITwinSceneMapping::ForEachKnownTile(std::function<void(FITwinSceneTile&)> const& Func)
{
	for (auto&& SceneTile : KnownTiles)
	{
		// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
		Func(const_cast<FITwinSceneTile&>(SceneTile));
	}
}

void FITwinSceneMapping::UnloadKnownTile(CesiumTileID const& TileID)
{
	auto* Known = FindKnownTileSLOW(TileID);
	if (Known)
		Known->Unload();
}

ITwinScene::TileIdx FITwinSceneMapping::KnownTileRank(FITwinSceneTile const& SceneTile) const
{
	auto& ByRank = KnownTiles.get<IndexByRank>();
	auto const Known = ByRank.iterator_to(SceneTile);
	auto const Rank = Known - ByRank.begin();
	//if (Known != ByRank.end()) - being an element of the container is a requirement, hence the hard check:
	check(0 <= Rank && Rank < (decltype(Rank))ByRank.size());
	return ITwinScene::TileIdx(static_cast<uint32_t>(Rank));
}

FITwinElement& FITwinSceneMapping::ElementFor(ITwinScene::ElemIdx const Rank)
{
	// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
	return const_cast<FITwinElement&>(AllElements[Rank.value()]);
}

FITwinElement& FITwinSceneMapping::ElementForSLOW(ITwinElementID const ElementID,
												  ITwinScene::ElemIdx* Rank/*= nullptr*/)
{
	auto& ByRank = AllElements.get<IndexByRank>();
	auto const It = ByRank.emplace_back(FITwinElement{ false, ElementID }).first;
	if (Rank)
	{
		*Rank = ITwinScene::ElemIdx(static_cast<uint32_t>(It - ByRank.begin()));
	}
	// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
	return const_cast<FITwinElement&>(*It);
}

/// Setup the tile's SelectingAndHiding texture while its render-readiness is still false, so that the tile
/// is only shown when the Elements that need to be hidden are indeed made so.
void FITwinSceneMapping::OnNewTileBuilt(FITwinSceneTile& SceneTile)
{
	ApplySelectingAndHiding(SceneTile);
}

std::unordered_set<ITwinElementID> const& FITwinSceneMapping::ConstructionDataElements()
{
	return GeometryIDToElementIDs[1];
}

void FITwinSceneMapping::ApplySelectingAndHiding(FITwinSceneTile& SceneTile)
{
	if (SceneTile.IsLoaded())
	{
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		constexpr bool bOnlyVisibleTiles = false;// because bVisible not yet set!
		//if (ITwin::NOT_ELEMENT != SelectedElement) <== No, may need to deselect!
		SceneTile.SelectElement(SelectedElement, bOnlyVisibleTiles, TextureNeeds);
		//if (bHiddenConstructionData) <== No, may need to un-hide!
		SceneTile.HideElements(
			bHiddenConstructionData ? ConstructionDataElements() : std::unordered_set<ITwinElementID>(),
			bOnlyVisibleTiles, TextureNeeds, true);
		//if (!HiddenElementsFromSavedView.empty()) <== same
		SceneTile.HideElements(HiddenElementsFromSavedView, bOnlyVisibleTiles, TextureNeeds, false);
		this->bNewSelectingAndHidingTexturesNeedSetupInMaterials |= TextureNeeds.bWasCreated;
		if (TextureNeeds.bWasChanged)
			UpdateSelectingAndHidingTextures();
	}
}

void FITwinSceneMapping::OnVisibilityChanged(FITwinSceneTile& SceneTile, bool bVisible)
{
	if (bVisible)
	{
		ensure(!SceneTile.bVisible);//should have been tested earlier
		ApplySelectingAndHiding(SceneTile);
	}
	//SceneTile->bVisible = bVisible; <== NO, done by FITwinIModelInternals::OnVisibilityChanged
}

int FITwinSceneMapping::ParseHierarchyTree(TArray<TSharedPtr<FJsonValue>> const& JsonRows)
{
	// See comment about reservation in FQueryElementMetadataPageByPage::DoRestart
	AllElements.reserve(AllElements.size() + (size_t)JsonRows.Num());
	int TotalGoodRows = 0;
	for (auto const& Row : JsonRows)
	{
		auto const& Entries = Row->AsArray();
		if (!ensure(Entries.Num() == 2))
			continue;
		ITwinElementID const ElemId = ITwin::ParseElementID(Entries[0]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ElemId))
			continue;
		ITwinScene::ElemIdx ChildInVec = ITwinScene::NOT_ELEM;
		FITwinElement& ChildElem = ElementForSLOW(ElemId, &ChildInVec);
		if (!ensure(ITwinScene::NOT_ELEM == ChildElem.ParentInVec))
			continue; // already known - how come?!
		ITwinElementID const ParentId = ITwin::ParseElementID(Entries[1]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ParentId))
			continue;
		FITwinElement& ParentElem = ElementForSLOW(ParentId, &ChildElem.ParentInVec);
		// TODO_GCO: optimize with a first loop that creates all ParentElem and counts their children,
		// exploiting the fact that children of the same parent "seem" to be contiguous (but let's not
		// assume it's always the case...), then a second loop that reserves the SubElems vectors and
		// fills them
		ParentElem.SubElemsInVec.push_back(ChildInVec);
		++TotalGoodRows;
	}
	// check there is no loop, it would be fatal
	size_t const Count = AllElements.size();
	std::vector<bool> Visited(Count, false);
	bool bError = false;
	for (size_t LoopIdxInVec = 0; LoopIdxInVec < Count; ++LoopIdxInVec)
	{
		if (Visited[LoopIdxInVec]) continue; // ok here
		ITwinScene::ElemIdx InVec(LoopIdxInVec);
		int Depth = 0;
		FITwinElement const* Elem = nullptr;
		do
		{
			//if (Visited[InVec]) break; <== not here, we'd never reach Count in case of a loop!!
			Visited[InVec.value()] = true;
			++Depth;
			Elem = &GetElement(InVec);
			InVec = Elem->ParentInVec;
		} while (ITwinScene::NOT_ELEM != InVec && Depth <= Count);

		if (Depth > Count) // we have obviously been looping "forever", let's stop
		{
			bError = true;
			break;
		}
	}
	if (bError)
	{
		for (auto& Elem : AllElements) // it's so unlikely, let's just trash all relationships
		{
			// Same comment about const_cast as on FITwinSceneTile::FindElementFeaturesSLOW
			const_cast<FITwinElement&>(Elem).ParentInVec = ITwinScene::NOT_ELEM;
		}
		UE_LOG(ITwinSceneMap, Error, TEXT("Loop found in iModel Elements hierarchy, it will be IGNORED!"));
		return 0;
	}
	return TotalGoodRows;
}

bool FITwinSceneMapping::ParseSourceElementID(ITwinElementID const ElemId, TSharedPtr<FJsonValue> const& Entry,
	int& GoodEntry, int& EmptyEntry)
{
	FString SourceId;
	if (!Entry->TryGetString(SourceId))
		return false;
	if (SourceId.IsEmpty())
	{
		++EmptyEntry;
		return true;
	}
	auto SourceEntry = SourceElementIDs.try_emplace(SourceId, ITwinScene::NOT_ELEM);
	ITwinScene::ElemIdx ThisElemIdx = ITwinScene::NOT_ELEM;
	auto& ThisElem = ElementForSLOW(ElemId, &ThisElemIdx);
	if (SourceEntry.second)
	{
		// was inserted => first time the Source Element ID is encountered, but don't create a duplicates
		// list just yet!
		SourceEntry.first->second = ThisElemIdx;
	}
	else
	{
		// already in set => we have a duplicate
		auto& FirstSourceElem = ElementFor(SourceEntry.first->second);
		// first duplicate: create the list
		if (ITwinScene::NOT_DUPL == FirstSourceElem.DuplicatesList)
		{
			FirstSourceElem.DuplicatesList = ITwinScene::DuplIdx(DuplicateElements.size());
			DuplicateElements.emplace_back(
				FDuplicateElementsVec{ SourceEntry.first->second, ThisElemIdx });
		}
		else
		{
			DuplicateElements[FirstSourceElem.DuplicatesList.value()].push_back(ThisElemIdx);
		}
		ThisElem.DuplicatesList = FirstSourceElem.DuplicatesList;
	}
	++GoodEntry;
	return true;
}

int FITwinSceneMapping::ParseSourceElementIDs(TArray<TSharedPtr<FJsonValue>> const& JsonRows)
{
	int TotalGoodRows = 0, EmptySourceId = 0, ExtraRows = 0;
	for (auto const& Row : JsonRows)
	{
		auto const& Entries = Row->AsArray();
		if (!ensure(Entries.Num() == 2))
			continue;
		ITwinElementID const ElemId = ITwin::ParseElementID(Entries[0]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ElemId))
			continue;
		// we can get array of strings here (multiple sources): we'll assume we need to hide non-animated
		// Elements that are a duplicate of any of their Source Elements
		TArray<TSharedPtr<FJsonValue>>* SourceArray;
		if (Entries[1]->TryGetArray(SourceArray))
		{
			ExtraRows += SourceArray->IsEmpty() ? 0 : (SourceArray->Num() - 1);
			for (auto&& Src : (*SourceArray))
				if (!ParseSourceElementID(ElemId, Src,TotalGoodRows, EmptySourceId))
					{ ensure(false); /*parse error, will be logged below*/ }
		}
		else
		{
			if (!ParseSourceElementID(ElemId, Entries[1],TotalGoodRows, EmptySourceId))
				{ ensure(false); /*parse error, will be logged below*/ }
		}
	}
	if (TotalGoodRows != JsonRows.Num() || 0 != EmptySourceId)
	{
		int const OtherErrors = (JsonRows.Num() + ExtraRows - EmptySourceId) - TotalGoodRows;
		UE_LOG(ITwinSceneMap, Warning,
			TEXT("When parsing 'Source Element ID' metadata: out of %d entries received, %d were empty%s"),
			JsonRows.Num(), EmptySourceId,
			OtherErrors ? (*FString::Printf(TEXT(", %d others were incomplete or could not be parsed"), 
											OtherErrors))
						: TEXT(""));
	}
	return TotalGoodRows;
}

FITwinSceneMapping::FDuplicateElementsVec const& FITwinSceneMapping::GetDuplicateElements(
	ITwinElementID const ElemID) const
{
	static FDuplicateElementsVec Empty;
	auto const& Elem = GetElement(ElemID);
	if (ITwinScene::NOT_DUPL == Elem.DuplicatesList)
		return Empty;
	else
		return DuplicateElements[Elem.DuplicatesList.value()];
}

void FITwinSceneMapping::Update4DAnimTileTextures(FITwinSceneTile& SceneTile, size_t& DirtyTexCount,
												  size_t& TexToWait)
{
	// Can't really do that, because we have a global "bTilesHaveNew4DAnimTextures" flag, which would thus
	// stay true forever if tiles are loaded but hidden before their textures have been sent to the GPU
	// (and yes I just witnessed this case!)
	// ALSO importantly, this is called from FITwinSynchro4DAnimator::ApplyAnimationOnTile, at which point
	// the SceneTile.bVisible flag hasn't been toggled on yet!
	// Note that methods like FITwinSynchro4DAnimator::Stop() also need to update textures for *all* tiles
	//if (!SceneTile.bVisible)
	//	return;
	if (SceneTile.HighlightsAndOpacities && SceneTile.HighlightsAndOpacities->UpdateTexture())
	{
		++DirtyTexCount;
		if (SceneTile.HighlightsAndOpacities->NeedToWaitForAsyncUpdate())
			++TexToWait;
	}
	if (SceneTile.CuttingPlanes && SceneTile.CuttingPlanes->UpdateTexture())
	{
		++DirtyTexCount;
		if (SceneTile.CuttingPlanes->NeedToWaitForAsyncUpdate())
			++TexToWait;
	}
}

size_t FITwinSceneMapping::Update4DAnimTextures()
{
	auto const StartTime = FPlatformTime::Seconds();
	size_t DirtyTexCount = 0, TexToWait = 0;
	ForEachKnownTile(std::bind(&FITwinSceneMapping::Update4DAnimTileTextures, this, std::placeholders::_1,
							   std::ref(DirtyTexCount), std::ref(TexToWait)));
	UE_LOG(ITwinSceneMap, Verbose, TEXT(
		"Spent %dms in Update4DAnimTextures, found %llu of them 'dirty', %llu of which we have to wait for."),
		(int)(1000 * (FPlatformTime::Seconds() - StartTime)), DirtyTexCount, TexToWait);
	return TexToWait;
}

bool FITwinSceneMapping::TilesHaveNew4DAnimTextures(bool& bWaitingForTextures)
{
	if (bTilesHaveNew4DAnimTextures)
	{
		bTilesHaveNew4DAnimTextures = false;
		bNew4DAnimTexturesNeedSetupInMaterials = true;
		if (Update4DAnimTextures())
		{
			bWaitingForTextures = true;
		}
		return true;
	}
	else
		return false;
}

void FITwinSceneMapping::HandleNew4DAnimTexturesNeedingSetupInMaterials()
{
	if (bNew4DAnimTexturesNeedSetupInMaterials)
	{
		bool bHasPendingTextureInitialUpdates = false;
		ForEachKnownTile([this, &bHasPendingTextureInitialUpdates](FITwinSceneTile& SceneTile)
		{
			if (SceneTile.HighlightsAndOpacities && SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials)
			{
				SetupHighlightsAndOpacitiesInfo();
				if (SceneTile.HighlightsAndOpacities->SetupInMaterials(SceneTile.Materials,
																	   *HighlightsAndOpacitiesInfo))
				{
					SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials = false;
				}
				else
				{
					bHasPendingTextureInitialUpdates = true;
				}
			}
			if (SceneTile.CuttingPlanes && SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials)
			{
				SetupCuttingPlanesInfo();
				if (SceneTile.CuttingPlanes->SetupInMaterials(SceneTile.Materials, *CuttingPlanesInfo))
				{
					SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials = false;
				}
				else
				{
					bHasPendingTextureInitialUpdates = true;
				}
			}
		});
		bNew4DAnimTexturesNeedSetupInMaterials = bHasPendingTextureInitialUpdates;
	}
}

void FITwinSceneMapping::UpdateSelectingAndHidingTileTextures(FITwinSceneTile& SceneTile,
	size_t& DirtyTexCount, size_t& TexToWait)
{
	//if (!SceneTile.bVisible) <== see Update4DAnimTileTextures
	//	return;
	if (SceneTile.SelectingAndHiding && SceneTile.SelectingAndHiding->UpdateTexture())
	{
		++DirtyTexCount;
		if (SceneTile.SelectingAndHiding->NeedToWaitForAsyncUpdate())
			++TexToWait;
	}
}

size_t FITwinSceneMapping::UpdateSelectingAndHidingTextures()
{
	auto const StartTime = FPlatformTime::Seconds();
	size_t DirtyTexCount = 0, TexToWait = 0;
	ForEachKnownTile(std::bind(&FITwinSceneMapping::UpdateSelectingAndHidingTileTextures, this,
							   std::placeholders::_1, std::ref(DirtyTexCount), std::ref(TexToWait)));
	UE_LOG(ITwinSceneMap, Verbose, TEXT(
		"Spent %dms in UpdateSelectingAndHidingTextures, found %llu of them 'dirty', %llu of which we have to wait for."),
		(int)(1000 * (FPlatformTime::Seconds() - StartTime)), DirtyTexCount, TexToWait);
	return TexToWait;
}

void FITwinSceneMapping::HandleNewSelectingAndHidingTextures()
{
	if (bNewSelectingAndHidingTexturesNeedSetupInMaterials)
	{
		bool bHasPendingTextureInitialUpdates = false;
		ForEachKnownTile([this, &bHasPendingTextureInitialUpdates](FITwinSceneTile& SceneTile)
		{
			if (SceneTile.SelectingAndHiding && SceneTile.bNeedSelectingAndHidingTextureSetupInMaterials)
			{
				ITwinMatParamInfo::SetupSelectingAndHidingInfo();
				if (SceneTile.SelectingAndHiding->SetupInMaterials(SceneTile.Materials,
																   *ITwinMatParamInfo::SelectingAndHidingInfo))
				{
					SceneTile.bNeedSelectingAndHidingTextureSetupInMaterials = false;
				}
				else
				{
					bHasPendingTextureInitialUpdates = true;
				}
			}
		});
		bNewSelectingAndHidingTexturesNeedSetupInMaterials = bHasPendingTextureInitialUpdates;
	}
}

void FITwinSceneMapping::SetupFeatureIDsInVertexUVs(FITwinSceneTile& SceneTile, bool bUpdatingTile/*= false*/)
{
	checkSlow(IsInGameThread());
	for (FITwinGltfMeshComponentWrapper& GltfMeshData : SceneTile.GltfMeshes)
	{
		SetupFeatureIDsInVertexUVs(SceneTile, GltfMeshData, bUpdatingTile);
	}
}

void FITwinSceneMapping::SetupFeatureIDsInVertexUVs(FITwinSceneTile& SceneTile,
	FITwinGltfMeshComponentWrapper& GltfMeshData, bool bUpdatingTile/*= false*/)
{
	auto const UVIdx = GltfMeshData.GetOrBakeFeatureIDsInVertexUVs();
	if (!UVIdx) return;

	ITwinMatParamInfo::SetupFeatureIdInfo();
	auto const MeshComp = GltfMeshData.GetMeshComponent();
	int32 const NumMats = MeshComp ? MeshComp->GetNumMaterials() : 0;
	for (int32 m = 0; m < NumMats; ++m)
	{
		auto Mat = MeshComp->GetMaterial(m);
		auto* AsDynMat = Cast<UMaterialInstanceDynamic>(Mat);
		if (ensure(AsDynMat))
			AsDynMat->SetScalarParameterValueByInfo(*FeatureIdInfo, *UVIdx);
	}
}

template<typename Container>
void FITwinSceneMapping::GatherTimelineElemInfos(FITwinSceneTile& SceneTile,
	FITwinElementTimeline const& Timeline, Container const& TimelineElements,
	std::vector<ITwinScene::ElemIdx>& SceneElems, std::vector<ITwinTile::ElemIdx>& TileElems)
{
	SceneElems.reserve(SceneElems.size() + TimelineElements.size());
	TileElems.reserve(TileElems.size() + TimelineElements.size());
	for (auto&& ElementID : TimelineElements)
	{
		ITwinTile::ElemIdx TileElem;
		FITwinElementFeaturesInTile* Found = SceneTile.FindElementFeaturesSLOW(ElementID, &TileElem);
		if (Found && ITwinScene::NOT_ELEM != Found->SceneRank)
		{
			SceneElems.push_back(Found->SceneRank);
			TileElems.push_back(TileElem);
		}
	}
}

bool FITwinSceneMapping::ReplicateAnimElemTextureSetupInTile(
	std::pair<ITwinScene::TileIdx, std::unordered_set<ITwinScene::ElemIdx>> const& TileElements)
{
	auto& SceneTile = KnownTile(TileElements.first);
	FElemAnimRequirements TileReq = {
		.bNeedHiliteAndOpaTex = (bool)SceneTile.HighlightsAndOpacities,
		.bNeedCuttingPlaneTex = (bool)SceneTile.CuttingPlanes
	};
	for (auto const& ElemRank : TileElements.second)
	{
		if (TileReq.bNeedHiliteAndOpaTex && TileReq.bNeedCuttingPlaneTex)
			break;
		ITwinScene::ElemIdx IdxInVec = ElemRank;
		do
		{
			auto const& Elem = GetElement(IdxInVec);
			TileReq.bNeedHiliteAndOpaTex |= Elem.Requirements.bNeedHiliteAndOpaTex;
			TileReq.bNeedCuttingPlaneTex |= Elem.Requirements.bNeedCuttingPlaneTex;
			// If animated by a Parent Element node, no specific animation will be received, we thus need
			// to traverse ancesters here:
			IdxInVec = Elem.ParentInVec;
		}
		while (IdxInVec != ITwinScene::NOT_ELEM
			&& (!TileReq.bNeedHiliteAndOpaTex || !TileReq.bNeedCuttingPlaneTex));
	}
	// Note: doesn't account for DEBUG_SYNCHRO4D_BGRA
	if (!SceneTile.HighlightsAndOpacities && TileReq.bNeedHiliteAndOpaTex)
	{
		CreateHighlightsAndOpacitiesTexture(SceneTile);
	}
	// Even if textures were already present, we'll have to SetupInMaterials in all (new) materials
	SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials = TileReq.bNeedHiliteAndOpaTex;

	if (!SceneTile.CuttingPlanes && TileReq.bNeedCuttingPlaneTex)
	{
		CreateCuttingPlanesTexture(SceneTile);
	}
	// Even if textures were already present, we'll have to SetupInMaterials in all (new) materials
	SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials = TileReq.bNeedCuttingPlaneTex;

	// Even if textures were already present, we'll have to SetupInMaterials in all (new) materials
	bTilesHaveNew4DAnimTextures |= SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials
								  || SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials;
	return bTilesHaveNew4DAnimTextures;
}

void FITwinSceneMapping::CreateHighlightsAndOpacitiesTexture(FITwinSceneTile& SceneTile)
{
	FITwinDynamicShadingBGRA8Property::Create(SceneTile.HighlightsAndOpacities, SceneTile.MaxFeatureID,
											  std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255)));
	SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials = true;
	bTilesHaveNew4DAnimTextures = true;
}

void FITwinSceneMapping::CreateCuttingPlanesTexture(FITwinSceneTile& SceneTile)
{
	FITwinDynamicShadingABGR32fProperty::Create(SceneTile.CuttingPlanes, SceneTile.MaxFeatureID,
												std::array<float, 4>(S4D_CLIPPING_DISABLED));
	SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials = true;
	bTilesHaveNew4DAnimTextures = true;
}

void FITwinSceneMapping::OnElementsTimelineModified(
	std::variant<ITwinScene::TileIdx, std::reference_wrapper<FITwinSceneTile>> const Tile,
	FITwinElementTimeline& ModifiedTimeline, std::vector<ITwinElementID> const* OnlyForElements/*= nullptr*/)
{
	auto& SceneTile = (Tile.index() == 0) ? KnownTile(std::get<0>(Tile)) : (std::get<1>(Tile).get());
	if (0 == ModifiedTimeline.NumKeyframes() || ITwin::NOT_FEATURE == SceneTile.MaxFeatureID)
	{
		return;
	}
	std::vector<ITwinScene::ElemIdx> SceneElems;
	std::vector<ITwinTile::ElemIdx> TileElems;
	if (OnlyForElements)
		GatherTimelineElemInfos(SceneTile, ModifiedTimeline, *OnlyForElements, SceneElems, TileElems);
	else
		GatherTimelineElemInfos(SceneTile, ModifiedTimeline, ModifiedTimeline.GetIModelElements(),
								SceneElems, TileElems);
	// FITwinIModelInternals::OnElementsTimelineModified and UITwinSynchro4DSchedules::TickSchedules call us
	// for every SceneTile, even if it contains no Element affected by this timeline!
	if (TileElems.empty())
		return;

	// Check whether with this ModifiedTimeline we need to switch the Element's material from opaque to
	// translucent (not the other way round: even if Visibility can force opacity to 1, and not only
	// multiplies, the material can be translucent for other reasons).
	bool const bTimelineHasPartialVisibility = ModifiedTimeline.HasPartialVisibility();
	bool const bTimelineHasTransformations =
		#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
			!ModifiedTimeline.Transform.Values.empty();
		#else
			false;
		#endif

	if (SceneTile.HighlightsAndOpacities
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SceneTile.HighlightsAndOpacities->GetTotalUsedPixels() < (SceneTile.MaxFeatureID.value() + 1))
	{
		ensure(false); // see comment on FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt
		SceneTile.HighlightsAndOpacities.reset(); // let's hope it doesn't crash everything...
		for (auto&& ElemRank : TileElems)
		{
			auto& ElemFeats = SceneTile.ElementFeatures(ElemRank);
			ElemFeats.bIsAlphaSetInTextureToHideExtractedElement = false;
			ElemFeats.TextureFlags.Synchro4DHighlightOpaTexFlag.Invalidate();// no cond on bTextureIsSet
		}
	}
	// I removed the big condition below because a non-empty/bogus timeline necessarily needs this texture,
	// either for coloring, or for masking fully clipped objects, or for masking parts that were extracted
	// because of partial transparency and/or transformations
	if (!SceneTile.HighlightsAndOpacities)
	{
		CreateHighlightsAndOpacitiesTexture(SceneTile);
	#if DEBUG_SYNCHRO4D_BGRA
		SceneTile.ForEachElementFeatures([&SceneTile](FITwinElementFeaturesInTile& ElementFeaturesInTile)
			{
				auto const RandClr =
					FITwinMathExts::RandomBGRA8ColorFromIndex(ElementFeaturesInTile.ElementID.value(), true);
				for (ITwinFeatureID const& p : ElementFeaturesInTile.Features)
				{
					SceneTile.HighlightsAndOpacities->SetPixel(p.value(), RandClr);
				}
			});
	#endif
	}
	if (SceneTile.CuttingPlanes
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SceneTile.CuttingPlanes->GetTotalUsedPixels() < (SceneTile.MaxFeatureID.value() + 1))
	{
		ensure(false); // see comment on FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt
		SceneTile.CuttingPlanes.reset(); // let's hope it doesn't crash everything...
		for (auto&& ElemRank : TileElems)
		{
			// no cond on bTextureIsSet here
			SceneTile.ElementFeatures(ElemRank).TextureFlags.Synchro4DCuttingPlaneTexFlag.Invalidate();
		}
	}
	if (!SceneTile.CuttingPlanes && !ModifiedTimeline.ClippingPlane.Values.empty())
	{
		CreateCuttingPlanesTexture(SceneTile);
	}
	for (size_t i = 0; i < SceneElems.size(); ++i)
	{
		// Propagate upwards as long as the nodes are marked as animated by the same timeline, ie up to
		// the originally animated parent node (because non-mesh nodes are not part of [Scene|Tile]Elems)
		FITwinElement* ITwinElem = &ElementFor(SceneElems[i]);
		while (true)
		{
			ITwinElem->Requirements.bNeedHiliteAndOpaTex |= (bool)SceneTile.HighlightsAndOpacities;
			ITwinElem->Requirements.bNeedCuttingPlaneTex |= (bool)SceneTile.CuttingPlanes;
			ITwinElem->Requirements.bNeedTranslucentMat  |= bTimelineHasPartialVisibility;
			ITwinElem->Requirements.bNeedBeTransformable |= bTimelineHasTransformations;
			if (ITwinElem->ParentInVec == ITwinScene::NOT_ELEM)
			{
				break;
			}
			ITwinElem = &ElementFor(ITwinElem->ParentInVec);
			if (ITwinElem->AnimationKeys.end()
				== std::find(ITwinElem->AnimationKeys.begin(), ITwinElem->AnimationKeys.end(), 
								ModifiedTimeline.GetIModelElementsKey()))
			{
				break;
			}
		}
	}
	for (auto TileElIdx : TileElems)
	{
		FITwinElementFeaturesInTile& ElementInTile = SceneTile.ElementFeatures(TileElIdx);
		ElementInTile.TextureFlags.Synchro4DHighlightOpaTexFlag.Invalidate();
		if (SceneTile.CuttingPlanes)
			ElementInTile.TextureFlags.Synchro4DCuttingPlaneTexFlag.Invalidate();
	}

	FTimelineToScene* TimelineOptim = ModifiedTimeline.ExtraData
		? static_cast<FTimelineToScene*>(ModifiedTimeline.ExtraData)
		// It's FITwinScheduleTimelineBuilder's responsibility to delete those, because they have the same
		// scope as the schedule and timeline, not as the iModel's!
		// Note that even when applying gltf tuning, the ElementsFeatures only vary (as these TimelineOptim's
		// are concerned) in the number of UE materials they reference, ie. whatever the underlying mesh
		// structure, I think it is safe to keep these optim structures.
		: (new FTimelineToScene);
	if (!ModifiedTimeline.ExtraData)
		ModifiedTimeline.ExtraData = TimelineOptim;
	bool const bMayNeedExtraction = (bTimelineHasTransformations || bTimelineHasPartialVisibility);
	auto Entry = TimelineOptim->Tiles.insert(
		FTimelineToSceneTile
		{
			.Rank = (Tile.index() == 0) ? std::get<0>(Tile) : KnownTileRank(std::get<1>(Tile).get()),
			.FirstElement = TimelineOptim->TileElems.size(),
			.FirstExtract = bMayNeedExtraction ? TimelineOptim->Extracts.size() : NO_EXTRACTION,
			.NbOfElements = (uint32_t)TileElems.size()
		});
	if (Entry.second) // was inserted
	{
		TimelineOptim->TileElems.reserve(TimelineOptim->TileElems.size() + TileElems.size());
		TimelineOptim->SceneElems.reserve(TimelineOptim->TileElems.capacity());
		if (bMayNeedExtraction)
			TimelineOptim->Extracts.reserve(TimelineOptim->Extracts.size() + TileElems.size());
		for (size_t i = 0; i < TileElems.size(); ++i)
		{
			TimelineOptim->TileElems.push_back(TileElems[i]);
			TimelineOptim->SceneElems.push_back(SceneElems[i]);
			if (bMayNeedExtraction)
			{
				auto& ElementInTile = SceneTile.ElementFeatures(TileElems[i]);
				// Allocate the Extraction entry even if the Element will not actually need extraction in
				// the end (case of already translucent mat - see doc on FTimelineToSceneTile::NbOfElements)
				(void)SceneTile.ExtractedElementSLOW(ElementInTile);
				TimelineOptim->Extracts.push_back(ElementInTile.ExtractedRank);
			}
		}
	}
	//else: not the first time we load this tile, but we never erase from TimelineOptim->Tiles
}

void FITwinSceneMapping::HideExtractedEntities(bool bHide /*= true*/)
{
	ForEachKnownTile([bHide](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachExtractedEntity([bHide](FITwinExtractedEntity& ExtractedEntity)
			{
				ExtractedEntity.SetHidden(bHide);
			});
	});
}

FBox const& FITwinSceneMapping::GetBoundingBox(ITwinElementID const Element) const
{
	auto const& Elem = GetElement(Element);
	if (ITwin::NOT_ELEMENT != Elem.ElementID)
	{
		return Elem.BBox;
	}
	// The Element bounding boxes are created and expanded as mesh components are notified by Cesium
	// (see FITwinSceneMappingBuilder::OnMeshConstructed), we have no other way of knowing them.
	// Note that FITwinIModelInternals::HasElementWithID uses this assumption too for the moment.
	// We never know when the full and most accurate BBox is obtained, since new tiles and new LODs can
	// always come later, containing the Element, so improving this with a cache a tricky, unless we cache
	// the box and all the tile IDs that contributed to it, so that we can skip them in OnMeshConstructed.
	static FBox EmptyBox(ForceInit);
	return EmptyBox;
}

void FITwinSceneMapping::SetIModel2UnrealTransfos(AITwinIModel const& IModel)
{
	UITwinUtilityLibrary::GetIModelCoordinateConversions(IModel, CoordConversions);
}

namespace
{
	struct FExtractionOperationInfo
	{
		uint32 nbUEEntities = 0;
		FITwinMeshExtractionOptions OptsOpaque, OptsTranslucent;

		FExtractionOperationInfo(
			std::function<UMaterialInterface* (ECesiumMaterialType)> const& MaterialGetter)
		{
			SetupHighlightsAndOpacitiesInfo();
			ITwinMatParamInfo::SetupSelectingAndHidingInfo();
			SetupCuttingPlanesInfo();
			ITwinMatParamInfo::SetupFeatureIdInfo();
			// Even merely transforming requires a new material instance, because of ForcedOpacity!
			OptsOpaque.bCreateNewMaterialInstance = true;
			OptsTranslucent = OptsOpaque;
			OptsOpaque.BaseMaterialForNewInstance = MaterialGetter(ECesiumMaterialType::Opaque);
			OptsTranslucent.BaseMaterialForNewInstance = MaterialGetter(ECesiumMaterialType::Translucent);
		}
	};
}

uint32 FITwinSceneMapping::CheckAndExtractElements(FTimelineToScene const& TimelineOptim,
	bool const bOnlyVisibleTiles, std::optional<ITwinScene::TileIdx> const& OnlySceneTile)
{
	if (!ensure(MaterialGetter))
		return 0;
	std::optional<FExtractionOperationInfo> ExtractOp;
	for (auto&& TileOptim : TimelineOptim.Tiles)
	{
		auto& SceneTile = KnownTile(TileOptim.Rank);
		if (!SceneTile.IsLoaded()
			|| (bOnlyVisibleTiles && !SceneTile.bVisible)
			|| (OnlySceneTile && (*OnlySceneTile) != TileOptim.Rank)
			|| !ensure(TileOptim.FirstExtract != NO_EXTRACTION))
		{
			continue;
		}
		auto const ElemStart = TimelineOptim.TileElems.begin() + TileOptim.FirstElement;
		auto const ScElemStart = TimelineOptim.SceneElems.begin() + TileOptim.FirstElement;
		auto const ElemEnd = ElemStart + TileOptim.NbOfElements;
		auto const ExtrStart = TimelineOptim.Extracts.begin() + TileOptim.FirstExtract;
		auto ExtrIt = ExtrStart;
		auto ScElemIt = ScElemStart;
		for (auto It = ElemStart; It != ElemEnd; ++It, ++ScElemIt, ++ExtrIt)
		{
			FITwinElementFeaturesInTile& ElementInTile = SceneTile.ElementFeatures(*It);
			if (ElementInTile.bIsElementExtracted)
				continue;
			FITwinElement& SceneElem = ElementFor(*ScElemIt);
			ensure(SceneElem.ElementID == ElementInTile.ElementID);
			// If extracting only for translucency, and Element only has translucent materials in this tile
			// already, we won't actually need to extract.
			if (!SceneElem.Requirements.bNeedBeTransformable
				// && SceneElem.Requirements.bNeedTranslucentMat <== obvious
				&& ElementInTile.bHasTestedForTranslucentFeaturesNeedingExtraction)
			{
				continue;
			}
			if (!ExtractOp)
				ExtractOp.emplace(MaterialGetter);
			FITwinExtractedElement& ExtractedElem = SceneTile.ExtractedElement(*ExtrIt);
			ensure(ExtractedElem.ElementID == ElementInTile.ElementID);
			bool const bOriginalMatOpaque = ElementInTile.HasOpaqueOrMaskedMaterial();
			bool const bExtractedNeedTranslucentMat =
				SceneElem.Requirements.bNeedTranslucentMat || !bOriginalMatOpaque;
			ElementInTile.bHasTestedForTranslucentFeaturesNeedingExtraction = true;
			ExtractOp->nbUEEntities += ExtractElementFromTile(ElementInTile.ElementID, SceneTile,
				bExtractedNeedTranslucentMat ? ExtractOp->OptsTranslucent : ExtractOp->OptsOpaque,
				&ElementInTile, &ExtractedElem);
			if (ElementInTile.bIsElementExtracted
				&& !ElementInTile.bIsAlphaSetInTextureToHideExtractedElement
				// I think this one can fail now in the non-prefetched case :/ see calls to
				// IsPrefetchedAvailableAndApplied added because FITwinSynchro4DAnimator::OnChangedScheduleTime
				// was called from AMainPanel::OnModelLoaded in Carrot, and itself called TickAnimation
				// directly _before_ ApplySchedule was set to InitialPassDone, which led this code here to be
				// reached without a valid SceneTile.HighlightsAndOpacities!
				&& ensure(SceneTile.HighlightsAndOpacities))
			{
				// Ensure the parts that were extracted are made invisible in the original mesh
				SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElementInTile.Features, 0);
				ElementInTile.bIsAlphaSetInTextureToHideExtractedElement = true;
				ElementInTile.TextureFlags.Synchro4DHighlightOpaTexFlag.Invalidate();
			}
			ElementInTile.bIsElementExtracted = true;// even if it failed, do not try over again
		}
	}
	return ExtractOp ? ExtractOp->nbUEEntities : 0;
}

uint32 FITwinSceneMapping::ExtractElement(
	ITwinElementID const Element,
	FITwinMeshExtractionOptions const& Options /*= {}*/)
{
	uint32 nbUEEntities = 0;
	ForEachKnownTile([&, this](FITwinSceneTile& SceneTile)
	{
		nbUEEntities += ExtractElementFromTile(Element, SceneTile, Options);
	});
	return nbUEEntities;
}

uint32 FITwinSceneMapping::ExtractElementFromTile(ITwinElementID const Element, FITwinSceneTile& SceneTile,
	FITwinMeshExtractionOptions const& InOptions /*= {}*/,
	FITwinElementFeaturesInTile* ElementFeaturesInTile/*= nullptr*/,
	FITwinExtractedElement* ExtractedElemInTile/*= nullptr*/)
{
	uint32 nbUEEntities = 0;
	// Beware several primitives in the tile can contain the element to extract. That's why we store a vector
	// in ExtractedEntityCont.
	decltype(FITwinExtractedElement::Entities)* EntitiesVec = nullptr;
	FITwinMeshExtractionOptions Options = InOptions;
	Options.SceneTile = &SceneTile;
	if (!ElementFeaturesInTile) // slow path for mostly dev/debug code
		ElementFeaturesInTile = &SceneTile.ElementFeaturesSLOW(Element);
	for (FITwinGltfMeshComponentWrapper& gltfMeshData : SceneTile.GltfMeshes)
	{
		if (gltfMeshData.CanExtractElement(Element))
		{
			if (!EntitiesVec)
			{
				if (ExtractedElemInTile) // fast path used for code inside ApplyAnimation
				{
					EntitiesVec = &ExtractedElemInTile->Entities;
				}
				else // slow path for mostly dev/debug code
				{
					auto Entry = SceneTile.ExtractedElementSLOW(*ElementFeaturesInTile);
					EntitiesVec = &((Entry.first).get().Entities);
				}
				EntitiesVec->clear(); // just in case we had extracted an obsolete version
			}
			auto& ExtractedEntity = EntitiesVec->emplace_back(FITwinExtractedEntity{ Element });
			if (gltfMeshData.ExtractElement(Element, ExtractedEntity, Options))
			{
				nbUEEntities++;
			}
			else
			{
				// Don't keep half constructed extracted entity
				EntitiesVec->pop_back();
			}
		}
	}
	if (nbUEEntities > 0)
	{
		// Set a flag to mark this Element as extracted.
		ElementFeaturesInTile->bIsElementExtracted = true;
		ITwinMatParamInfo::SetupFeatureIdInfo();
		for (auto&& Extracted : *EntitiesVec)
		{
			SetupFeatureIdUVIndex(SceneTile, Extracted);
			if (SceneTile.HighlightsAndOpacities)
			{
				if (SceneTile.HighlightsAndOpacities->SetupInMaterial(Extracted.Material,
																	  *HighlightsAndOpacitiesInfo))
				{
					Extracted.TextureFlags.Synchro4DHighlightOpaTexFlag.OnTextureSetupInMaterials(1);
				}
				else
				{
					SceneTile.bNeed4DHighlightsOpaTextureSetupInMaterials = true;
					bNew4DAnimTexturesNeedSetupInMaterials = true;//tex is not new, but material is!
				}
			}
			if (SceneTile.CuttingPlanes)
			{
				if (SceneTile.CuttingPlanes->SetupInMaterial(Extracted.Material, *CuttingPlanesInfo))
					Extracted.TextureFlags.Synchro4DCuttingPlaneTexFlag.OnTextureSetupInMaterials(1);
				else
				{
					SceneTile.bNeed4DCuttingPlanesTextureSetupInMaterials = true;
					bNew4DAnimTexturesNeedSetupInMaterials = true;//tex is not new, but material is!
				}
			}
			if (SceneTile.SelectingAndHiding)
			{
				if (SceneTile.SelectingAndHiding->SetupInMaterial(
					Extracted.Material, *ITwinMatParamInfo::SelectingAndHidingInfo))
				{
					Extracted.TextureFlags.SelectingAndHidingTexFlag.OnTextureSetupInMaterials(1);
				}
				else
				{
					SceneTile.bNeedSelectingAndHidingTextureSetupInMaterials = true;
					//tex is not new, but material is!
					bNewSelectingAndHidingTexturesNeedSetupInMaterials = true;
				}
			}
		}
	}
	return nbUEEntities;
}

uint32 FITwinSceneMapping::ExtractElementsOfSomeTiles(
	float percentageOfTiles,
	float percentageOfEltsInTile /*= 0.1f*/,
	FITwinMeshExtractionOptions const& opts /*= {}*/)
{
	uint32 nbExtractedElts = 0;

#if ENABLE_DRAW_DEBUG
	uint32 const nbTilesToExtract = static_cast<uint32>(std::ceil(KnownTiles.size() * percentageOfTiles));
	uint32 nbProcessedTiles = 0;

	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		if (nbProcessedTiles >= nbTilesToExtract)
		{
			return;
		}
		for (FITwinGltfMeshComponentWrapper& gltfMeshData : SceneTile.GltfMeshes)
		{
			uint32 const nbExtracted = gltfMeshData.ExtractSomeElements(
				SceneTile,
				percentageOfEltsInTile,
				opts);
			if (nbExtracted > 0)
			{
				nbExtractedElts += nbExtracted;
				nbProcessedTiles++;
				if (nbProcessedTiles >= nbTilesToExtract)
				{
					break;
				}
			}
		}
	});
#endif // ENABLE_DRAW_DEBUG

	return nbExtractedElts;
}

void FITwinSceneMapping::HidePrimitivesWithExtractedEntities(bool bHide /*= true*/)
{
	ForEachKnownTile([bHide](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachExtractedEntity([&SceneTile, bHide](FITwinExtractedEntity& Extracted)
			{
				// Note that there is room for optimization: with this implementation, we may
				// hide a same mesh again and again (as many times as the number of extracted
				// elements...)
				for (FITwinGltfMeshComponentWrapper& gltfMeshData : SceneTile.GltfMeshes)
				{
					if (gltfMeshData.HasDetectedElementID(Extracted.ElementID))
					{
						gltfMeshData.HideOriginalMeshComponent(bHide);
					}
				}
			});
	});
}

void FITwinSceneMapping::BakeFeaturesInUVs_AllMeshes()
{
	ForEachKnownTile([](FITwinSceneTile& SceneTile)
	{
		for (auto& gltfMeshData : SceneTile.GltfMeshes)
		{
			gltfMeshData.GetOrBakeFeatureIDsInVertexUVs();
		}
	});
}

bool FITwinSceneMapping::SelectVisibleElement(ITwinElementID const& InElemID)
{
	if (InElemID == SelectedElement)
	{
		return false;
	}
	bool bSelectedInATile = false;
	FITwinSceneTile::FTextureNeeds TextureNeeds;
	ForEachKnownTile([&InElemID, &bSelectedInATile, &TextureNeeds](FITwinSceneTile& SceneTile)
	{
		bSelectedInATile |= SceneTile.SelectElement(InElemID, /*bOnlyVisibleTiles*/true, TextureNeeds,
													/*bTestElementVisibility*/true);
	});
	SelectedElement = InElemID;
	this->bNewSelectingAndHidingTexturesNeedSetupInMaterials |= TextureNeeds.bWasCreated;
	// Do it now for existing textures: the initial UpdateTexture call of new textures will also be attempted,
	// but most likely the TextureRHI is not ready yet, so it will be done again automatically when calling
	// SetupInMaterials (called from HandleNewSelectingAndHidingTextures!)
	if (TextureNeeds.bWasChanged)
		UpdateSelectingAndHidingTextures();
	return bSelectedInATile;
}

void FITwinSceneMapping::HideElements(std::unordered_set<ITwinElementID> const& InElemIDs, bool IsConstruction)
{
	// Note: SelectionAndHiding texture affects both "batched" and Extracted Element meshes
	FITwinSceneTile::FTextureNeeds TextureNeeds;
	ForEachKnownTile([&InElemIDs, &TextureNeeds, IsConstruction](FITwinSceneTile& SceneTile)
	{
		SceneTile.HideElements(InElemIDs, /*bOnlyVisibleTiles*/true, TextureNeeds, IsConstruction);
	});
	if (IsConstruction)
		bHiddenConstructionData = !InElemIDs.empty();
	else
		HiddenElementsFromSavedView = InElemIDs;
	this->bNewSelectingAndHidingTexturesNeedSetupInMaterials |= TextureNeeds.bWasCreated;
	if (TextureNeeds.bWasChanged)
		UpdateSelectingAndHidingTextures();
}

FITwinSceneTile const* FITwinSceneMapping::FindOwningTile(UPrimitiveComponent const* Component) const
{
	if (!Component)
	{
		return nullptr;
	}
	for (auto&& SceneTile : KnownTiles)
	{
		bool bFoundMesh = false;
		for (auto const& gltfMeshData : SceneTile.GltfMeshes)
		{
			if (gltfMeshData.GetMeshComponent() == Component)
			{
				// We have found the owning tile
				bFoundMesh = true;
				break;
			}
		}
		if (bFoundMesh)
		{
			return &SceneTile;
		}
	}
	return nullptr;
}

FITwinGltfMeshComponentWrapper const* FITwinSceneMapping::FindGlTFMeshWrapper(UPrimitiveComponent const* Component) const
{
	if (!Component)
	{
		return nullptr;
	}
	for (auto&& SceneTile : KnownTiles)
	{
		for (auto const& gltfMeshData : SceneTile.GltfMeshes)
		{
			if (gltfMeshData.GetMeshComponent() == Component)
			{
				return &gltfMeshData;
			}
		}
	}
	return nullptr;
}

void FITwinSceneMapping::Reset()
{
	*this = FITwinSceneMapping();
}

FString FITwinSceneMapping::ToString() const
{
	return FString::Printf(TEXT("SceneMapping: Elems:%llu (geom rec. for %llu), SourceElementIDs:%llu DuplicateElements:%llu unique (total %llu)\n\tNew4DTex:%d(NeedSetup:%d) SelHidTexNeedSetup:%d SelectedElement:%s\n\tKnownTiles:%llu (%llu loaded, %llu visible)"),
		AllElements.size(),
		[this]() { size_t N = 0; for (auto&& Elem : AllElements) if (Elem.bHasMesh) ++N; return N; }(),
		SourceElementIDs.size(), DuplicateElements.size(),
		[this]() { size_t N = 0; for (auto&& DuplVec : DuplicateElements) N += DuplVec.size(); return N; }(),
		bTilesHaveNew4DAnimTextures, bNew4DAnimTexturesNeedSetupInMaterials,
		bNewSelectingAndHidingTexturesNeedSetupInMaterials, *ITwin::ToString(SelectedElement),
		KnownTiles.size(),
		[this]() { size_t N = 0; for (auto&& Tile : KnownTiles) if (Tile.IsLoaded()) ++N; return N; }(),
		[this]() { size_t N = 0; for (auto&& Tile : KnownTiles) if (Tile.bVisible) ++N; return N; }()
	);
}

namespace
{
	bool SetBaseColorAlpha(UMaterialInstanceDynamic& MatInstance, FMaterialParameterInfo const& ParameterInfo, float Alpha)
	{
		FLinearColor Color4;
		if (!MatInstance.GetVectorParameterValue(ParameterInfo, Color4))
		{
			return false;
		}
		Color4.A = Alpha;
		MatInstance.SetVectorParameterValueByInfo(ParameterInfo, Color4);
		return true;
	}
	void SetBaseColorAlpha(UMaterialInstanceDynamic& MatInstance, float Alpha)
	{
		SetBaseColorAlpha(MatInstance,
			FMaterialParameterInfo("baseColorFactor", EMaterialParameterAssociation::GlobalParameter, INDEX_NONE),
			Alpha);
		SetBaseColorAlpha(MatInstance,
			FMaterialParameterInfo("baseColorFactor", EMaterialParameterAssociation::LayerParameter, 0),
			Alpha);
	}
}

void FITwinSceneMapping::SetITwinMaterialChannelIntensity(uint64_t ITwinMaterialID,
	SDK::Core::EChannelType Channel, double InIntensity)
{
	const float Intensity = static_cast<float>(InIntensity);


#define DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE(_paramName, _intensity)	\
	MatInstance.SetScalarParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE), \
		_intensity);									\
	MatInstance.SetScalarParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::LayerParameter, 0), \
		_intensity);


	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		// This code depends on the parameters actually published in MF_ITwinCesiumGlTF.uasset
		switch (Channel)
		{
		case SDK::Core::EChannelType::Metallic:
			DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE("metallicFactor", Intensity);
			break;

		case SDK::Core::EChannelType::Roughness:
			DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE("roughnessFactor", Intensity);
			break;

		case SDK::Core::EChannelType::Transparency:
			SetBaseColorAlpha(MatInstance, 1.f - Intensity);
			break;

		case SDK::Core::EChannelType::Alpha:
			SetBaseColorAlpha(MatInstance, Intensity);
			break;

		case SDK::Core::EChannelType::Normal:
			DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE("normalFlatness", 1.f - Intensity);
			break;

		case SDK::Core::EChannelType::AmbientOcclusion:
			DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE("occlusionTextureStrength", Intensity);
			break;

		case SDK::Core::EChannelType::Specular:
			DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE("specularFactor", Intensity);
			break;

		default:
			ensureMsgf(false, TEXT("channel %u not implemented for scalar values"), Channel);
			break;
		}
	};
#undef DO_SET_CESIUMMAT_SCALAR_PARAM_VALUE

	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	});
}

void FITwinSceneMapping::SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID,
	SDK::Core::EChannelType Channel, UTexture* pTexture)
{
	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.SetITwinMaterialChannelTexture(ITwinMaterialID, Channel, pTexture);
	});
}

void FITwinSceneMapping::SetITwinMaterialUVTransform(uint64_t ITwinMaterialID, ITwinUVTransform const& UVTransform)
{
	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		ITwin::SetUVTransformInMaterialInstance(UVTransform, MatInstance,
			EMaterialParameterAssociation::GlobalParameter, INDEX_NONE);
		ITwin::SetUVTransformInMaterialInstance(UVTransform, MatInstance,
			EMaterialParameterAssociation::LayerParameter, 0);
	};
	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	});
}

void FITwinSceneMapping::SetITwinMaterialChannelColor(uint64_t ITwinMaterialID,
	SDK::Core::EChannelType Channel, SDK::Core::ITwinColor const& InColor)
{
	FLinearColor const NewValue(
		InColor[0],
		InColor[1],
		InColor[2],
		InColor[3]);

#define DO_SET_CESIUMMAT_COLOR_PARAM_VALUE(_paramName)	\
	MatInstance.SetVectorParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE), \
		NewValue);									\
	MatInstance.SetVectorParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::LayerParameter, 0), \
		NewValue);

	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		// This code depends on the parameters actually published in MF_ITwinCesiumGlTF.uasset
		switch (Channel)
		{
		case SDK::Core::EChannelType::Color:
			DO_SET_CESIUMMAT_COLOR_PARAM_VALUE("baseColorFactor");
			break;

		default:
			ensureMsgf(false, TEXT("channel %u not implemented for colors"), Channel);
			break;
		}
	};
#undef DO_SET_CESIUMMAT_COLOR_PARAM_VALUE

	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	});
}
