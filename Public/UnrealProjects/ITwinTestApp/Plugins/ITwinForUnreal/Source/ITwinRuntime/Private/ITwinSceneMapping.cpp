/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMapping.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"
#include <ITwinDynamicShadingProperty.h>
#include <ITwinDynamicShadingProperty.inl>
#include <ITwinIModel.h>
#include <ITwinUtilityLibrary.h>
#include <Math/UEMathExts.h>
#include <Timeline/Timeline.h>
#include <Timeline/SchedulesConstants.h>

#include <ITwinExtractedMeshComponent.h>
#include <Material/ITwinMaterialParameters.h>
#include <Material/ITwinMaterialParameters.inl>

#include <Engine/Texture.h>
#include <GenericPlatform/GenericPlatformTime.h>
#include <Logging/LogMacros.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>


//#include "../../CesiumRuntime/Private/CesiumMaterialUserData.h" // see comment in #GetSynchro4DLayerIndexInMaterial

DECLARE_LOG_CATEGORY_EXTERN(ITwinSceneMap, Log, All);
DEFINE_LOG_CATEGORY(ITwinSceneMap);

// TODO_GCO: ignored in FITwinSceneMapping::ReplicateAnimElemTextureSetupInTile
#define DEBUG_SYNCHRO4D_BGRA 0
bool FITwinSceneTile::HasInitialTexturesForChannel(UMaterialInstanceDynamic* Mat, AdvViz::SDK::EChannelType Chan) const
{
	auto const itMatInfo = MatsWithTexturesToRestore.find(Mat);
	if (itMatInfo == MatsWithTexturesToRestore.end())
		return false;
	auto const itRestoreInfo = itMatInfo->second.find(Chan);
	if (itRestoreInfo == itMatInfo->second.end())
		return false;
	return itRestoreInfo->second.Mat.IsValid();
}

void FITwinSceneTile::StoreInitialTexturesForChannel(UMaterialInstanceDynamic* Mat, AdvViz::SDK::EChannelType Chan,
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

FITwinSceneMapping::FITwinSceneMapping(bool const forCDO)
{
	//if (!forCDO)
	//	AllElements.reserve(16384); <== no longer needed, see ReserveIModelMetadata
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

void FITwinSceneMapping::UnloadKnownTile(FITwinSceneTile& SceneTile)
{
	SceneTile.Unload();
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

FITwinElement const& FITwinSceneMapping::ElementFor(ITwinScene::ElemIdx const Rank) const
{
	return AllElements[Rank.value()];
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

bool FITwinSceneMapping::FindElementIDForGUID(FGuid const& ElementGuid, ITwinElementID& Found) const
{
	auto& ByGUID = FederatedElementGUIDs.get<IndexByGUID>();
	auto It = ByGUID.find(ElementGuid);
	if (It == ByGUID.end())
		return false;
	Found = ElementFor(It->Rank).ElementID;
	return true;
}

bool FITwinSceneMapping::FindGUIDForElement(ITwinScene::ElemIdx const Rank, FGuid& Found) const
{
	auto& ByRank = FederatedElementGUIDs.get<IndexByRank>();
	auto It = ByRank.find(Rank);
	if (It == ByRank.end())
		return false;
	Found = It->FederatedGuid;
	return true;
}

bool FITwinSceneMapping::FindGUIDForElement(ITwinElementID const Elem, FGuid& Found) const
{
	auto& ByID = AllElements.get<IndexByElemID>();
	auto It = ByID.find(Elem);
	if (It == ByID.end())
		return false;
	auto& ByRank = AllElements.get<IndexByRank>();
	auto const Known = ByRank.iterator_to(*It);
	auto const Rank = Known - ByRank.begin();
	//if (Known != ByRank.end()) - being an element of the container is a requirement, hence the hard check:
	check(0 <= Rank && Rank < (decltype(Rank))ByRank.size());
	return FindGUIDForElement(ITwinScene::ElemIdx(static_cast<uint32_t>(Rank)), Found);
}

void FITwinSceneMapping::MutateElements(std::function<void(FITwinElement&)>&& Functor)
{
	auto& ByRank = AllElements.get<IndexByRank>();
	for (auto It = ByRank.begin(), ItE = ByRank.end(); It != ItE; ++It)
	{
		Functor(const_cast<FITwinElement&>(*It));
	}
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

bool FITwinSceneMapping::IsElementHiddenInSavedView(ITwinElementID const& InElemID) const
{
	return std::find(HiddenElementsFromSavedView.begin(), HiddenElementsFromSavedView.end(), InElemID)
		!= HiddenElementsFromSavedView.end();
}

void FITwinSceneMapping::ApplySelectingAndHiding(FITwinSceneTile& SceneTile)
{
	if (SceneTile.IsLoaded())
	{
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		constexpr bool bOnlyVisibleTiles = false;// because bVisible not yet set!
		//if (ITwin::NOT_ELEMENT != SelectedElement) <== No, may need to deselect!
		SceneTile.PickElement(SelectedElement, bOnlyVisibleTiles, TextureNeeds);
		SceneTile.PickMaterial(SelectedMaterial, bOnlyVisibleTiles, TextureNeeds);
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

void FITwinSceneMapping::OnVisibilityChanged(FITwinSceneTile& SceneTile, bool bVisible,
											 bool const bUseGltfTunerInsteadOfMeshExtraction)
{
	if (bVisible)
	{
		if (!bUseGltfTunerInsteadOfMeshExtraction)
		{
			SceneTile.ForEachExtractedEntity([](FITwinExtractedEntity& Extracted)
				{
					if (!Extracted.MeshComponent.IsValid() || !Extracted.MeshComponent->GetOuter())
						return;
					if (auto* SceneComp = Cast<USceneComponent>(Extracted.MeshComponent->GetOuter()))
					{
						Extracted.OriginalTransform = SceneComp->GetComponentTransform();
						Extracted.MeshComponent->SetWorldTransform(
							Extracted.OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
					}
				});
		}
		ensure(!SceneTile.bVisible);//should have been tested earlier
		ApplySelectingAndHiding(SceneTile);
	}
	//SceneTile->bVisible = bVisible; <== NO, done by FITwinIModelInternals::OnVisibilityChanged
}

void FITwinSceneMapping::ReserveIModelMetadata(int TotalElements)
{
	AllElements.reserve(TotalElements);
	FederatedElementGUIDs.reserve(TotalElements);
	SourceElementIDs.reserve(TotalElements);
}

void FITwinSceneMapping::FinishedParsingIModelMetadata()
{
// Need to keep at least as long as (APIM NextGen) schedule is loading!
//#if !WITH_EDITOR
//	decltype(FederatedElementGUIDs) EmptyFed;
//	FederatedElementGUIDs.swap(EmptyFed);
//#endif
	decltype(SourceElementIDs) Empty;
	SourceElementIDs.swap(Empty);
}

int FITwinSceneMapping::ParseIModelMetadata(TArray<TSharedPtr<FJsonValue>> const& JsonRows)
{
	int GoodSrcIDs = 0, GoodFedGUIDs = 0, EmptyFedGUIDs = 0, EmptySrcIDs = 0;
	auto& GuidMap = FederatedElementGUIDs.get<IndexByGUID>();
	auto& SourceIdMap = SourceElementIDs.get<IndexBySourceID>();
	for (auto const& Row : JsonRows)
	{
		auto const& Entries = Row->AsArray();
		if (!ensure(!Entries.IsEmpty()))
			continue;
		ITwinElementID const ElemId = ITwin::ParseElementID(Entries[0]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ElemId))
			continue;
		ITwinScene::ElemIdx InVec = ITwinScene::NOT_ELEM;
		FITwinElement& Elem = ElementForSLOW(ElemId, &InVec);
		if (ITwinScene::NOT_ELEM != Elem.ParentInVec)
			continue; // already known - our SQL query indeed generates duplicates in some iModel, why...?
		ITwinElementID const ParentId = (Entries.Num() < 2 || Entries[1]->IsNull())
			? ITwin::NOT_ELEMENT : ITwin::ParseElementID(Entries[1]->AsString());
		if (ITwin::NOT_ELEMENT != ParentId)
		{
			FITwinElement& ParentElem = ElementForSLOW(ParentId, &Elem.ParentInVec);
			// TODO_GCO: optimize with a first loop that creates all ParentElem and counts their children,
			// exploiting the fact that children of the same parent "seem" to be contiguous (but let's not
			// assume it's always the case...), then a second loop that reserves the SubElems vectors and
			// fills them
			ParentElem.SubElemsInVec.push_back(InVec);
		}
		if (Entries.Num() >= 3)
			ParseSomeElementIdentifier<FGuid>(GuidMap, ElemId, Entries[2], GoodFedGUIDs, EmptyFedGUIDs);
		else
			++EmptyFedGUIDs;
		if (Entries.Num() >= 4)
			ParseSomeElementIdentifier<FString>(SourceIdMap, ElemId, Entries[3], GoodSrcIDs, EmptySrcIDs);
		else
			++EmptySrcIDs;
	}
	// check there is no loop in the parent-child graph, it would be fatal
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
	if (GoodFedGUIDs != JsonRows.Num() || GoodSrcIDs != JsonRows.Num())
	{
		int const OtherErr = (2 * JsonRows.Num() - EmptyFedGUIDs - EmptySrcIDs) - GoodFedGUIDs - GoodSrcIDs;
		UE_LOG(ITwinSceneMap, Display, TEXT("When parsing Element metadata: out of %d entries received, %d had no Federation GUID, %d had no Source Element ID%s"),
			JsonRows.Num(), EmptyFedGUIDs, EmptySrcIDs, OtherErr
			? (*FString::Printf(
				TEXT(", %d Federation GUIDs or Source Element IDs were incomplete or could not be parsed"),
				OtherErr))
			: TEXT(""));
	}
	return GoodFedGUIDs;//informative only, but FedGUIDs are more important than SrcID
}

template<typename TSomeID, typename TMapByRank>
bool FITwinSceneMapping::ParseSomeElementIdentifier(TMapByRank& OutIDMap, ITwinElementID const ElemId,
	TSharedPtr<FJsonValue> const& Entry, int& GoodEntry, int& EmptyEntry)
{
	FString SomeIdStr;
	if (!Entry->TryGetString(SomeIdStr))
		return false;
	if (SomeIdStr.IsEmpty())
	{
		++EmptyEntry;
		return true;
	}
	TSomeID SomeID;
	if constexpr (std::is_same<TSomeID, FGuid>())
	{
		if (SomeIdStr.Len() < 36
			|| !FGuid::ParseExact(SomeIdStr, EGuidFormats::DigitsWithHyphensLower, SomeID))
		{
			return false;
		}
	}
	else
	{
		std::swap(SomeID, SomeIdStr);
	}
	ITwinScene::ElemIdx ThisElemIdx = ITwinScene::NOT_ELEM;
	auto& ThisElem = ElementForSLOW(ElemId, &ThisElemIdx);
	auto SourceEntry = OutIDMap.emplace(typename TMapByRank::value_type{ ThisElemIdx, SomeID });
	if (SourceEntry.second)
	{
		// was inserted => first time the Source Element ID is encountered, but don't create a duplicates
		// list just yet!
	}
	else
	{
		// already in set => we have a duplicate
		auto& FirstSourceElem = ElementFor(SourceEntry.first->Rank);
		// first duplicate: create the list
		if (ITwinScene::NOT_DUPL == FirstSourceElem.DuplicatesList)
		{
			FirstSourceElem.DuplicatesList = ITwinScene::DuplIdx(DuplicateElements.size());
			DuplicateElements.emplace_back(
				FDuplicateElementsVec{ SourceEntry.first->Rank, ThisElemIdx });
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
	if (TextureUpdateDisabler)
	{
		// Just record the need for update.
		TextureUpdateDisabler->bNeedUpdateSelectingAndHidingTextures = true;
		return 0;
	}
	auto const StartTime = FPlatformTime::Seconds();
	size_t DirtyTexCount = 0, TexToWait = 0;
	ForEachKnownTile(std::bind(&FITwinSceneMapping::UpdateSelectingAndHidingTileTextures, this,
							   std::placeholders::_1, std::ref(DirtyTexCount), std::ref(TexToWait)));
	UE_LOG(ITwinSceneMap, Verbose, TEXT(
		"Spent %dms in UpdateSelectingAndHidingTextures, found %llu of them 'dirty', %llu of which we have to wait for."),
		(int)(1000 * (FPlatformTime::Seconds() - StartTime)), DirtyTexCount, TexToWait);
	return TexToWait;
}


FITwinTextureUpdateDisabler::FITwinTextureUpdateDisabler(FITwinSceneMapping& InOwner)
	: Owner(InOwner)
	, bPreviouslyDisabled(InOwner.AreSelectingAndHidingTexturesUpdatesDisabled())
{
	Owner.DisableUpdateSelectingAndHidingTextures(true);
}

FITwinTextureUpdateDisabler::~FITwinTextureUpdateDisabler()
{
	Owner.DisableUpdateSelectingAndHidingTextures(bPreviouslyDisabled);
}

bool FITwinSceneMapping::AreSelectingAndHidingTexturesUpdatesDisabled() const
{
	return TextureUpdateDisabler.has_value();
}

void FITwinSceneMapping::DisableUpdateSelectingAndHidingTextures(bool b)
{
	if (b == AreSelectingAndHidingTexturesUpdatesDisabled())
	{
		// Nothing to do.
		return;
	}
	// Make sure we do not lose any update.
	const bool bNeedUpdate = TextureUpdateDisabler && TextureUpdateDisabler->bNeedUpdateSelectingAndHidingTextures;
	TextureUpdateDisabler.reset();
	if (b)
	{
		// Temporarily disable updates.
		TextureUpdateDisabler.emplace();
		if (bNeedUpdate)
			TextureUpdateDisabler->bNeedUpdateSelectingAndHidingTextures = true;
	}
	else if (bNeedUpdate)
	{
		// Actually perform the update.
		UpdateSelectingAndHidingTextures();
	}
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
	auto const UVIdx = GltfMeshData.GetFeatureIDsInVertexUVs();
	if (!UVIdx)
		return;

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
	FITwinElementTimeline& ModifiedTimeline, std::vector<ITwinElementID> const* OnlyForElements,
	bool const bUseGltfTunerInsteadOfMeshExtraction, bool const bTileIsTunedFor4D, int const TimelineIndex)
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
			ElemFeats.TextureFlags.Synchro4DHighlightOpaTexFlag.Invalidate();
		}
	}
	// I removed the big condition below because non-empty/bogus timelines almost(*) always use this texture,
	// either for coloring, or for masking fully clipped objects or, when using mesh extraction, for masking
	// parts that were extracted because of partial transparency and/or transformations. When using gtlf
	// tuning instead, there are rare cases where it would not be needed, like transform-only or
	// partial-viz-only tasks...
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
			SceneTile.ElementFeatures(ElemRank).TextureFlags.Synchro4DCuttingPlaneTexFlag.Invalidate();
		}
	}
	if (!SceneTile.CuttingPlanes && !ModifiedTimeline.ClippingPlane.Values.empty())
	{
		CreateCuttingPlanesTexture(SceneTile);
	}
	// Note: complementary to what is done in UITwinSynchro4DSchedules::FImpl::UpdateGltfTunerRules, where
	// requirements are only computed for the leaf Elements (bHasMesh==true)
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
	std::unordered_set<int32_t> TimelineMeshes;
	for (auto TileElIdx : TileElems)
	{
		FITwinElementFeaturesInTile& ElementInTile = SceneTile.ElementFeatures(TileElIdx);
		ElementInTile.TextureFlags.Synchro4DHighlightOpaTexFlag.Invalidate();
		if (SceneTile.CuttingPlanes)
			ElementInTile.TextureFlags.Synchro4DCuttingPlaneTexFlag.Invalidate();
		for (auto&& TileElemMesh : ElementInTile.Meshes)
			TimelineMeshes.insert(TileElemMesh);
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
			.NbOfElements = (uint32_t)TileElems.size(),
			.FirstExtract = bMayNeedExtraction ? TimelineOptim->Extracts.size() : NO_EXTRACTION,
			.NbOfExtracts = (uint32_t)(bMayNeedExtraction
				? (bUseGltfTunerInsteadOfMeshExtraction
					// Always assume a future retuning may yield non-empty TimelineMeshes here:
					? (/*TimelineMeshes.empty() ? 0 :*/ bTileIsTunedFor4D ? 1 : 0)
					: TileElems.size())
				: 0),
		});
	if (Entry.second) // was inserted
	{
		TimelineOptim->TileElems.reserve(TimelineOptim->TileElems.size() + Entry.first->NbOfElements);
		TimelineOptim->SceneElems.reserve(TimelineOptim->TileElems.capacity());
		if (bMayNeedExtraction)
		{
			TimelineOptim->Extracts.reserve(TimelineOptim->Extracts.size() + Entry.first->NbOfExtracts);
			if (bUseGltfTunerInsteadOfMeshExtraction && bTileIsTunedFor4D)
			{
				// With retuning, we use a single dummy Extract, with timeline index as ExtractedElement's ID
				FITwinElementFeaturesInTile DummyElemInTile{ ITwinElementID(TimelineIndex) };
				(void)SceneTile.ExtractedElementSLOW(DummyElemInTile);
				TimelineOptim->Extracts.push_back(DummyElemInTile.ExtractedRank);
			}
		}
		for (size_t i = 0; i < TileElems.size(); ++i)
		{
			TimelineOptim->TileElems.push_back(TileElems[i]);
			TimelineOptim->SceneElems.push_back(SceneElems[i]);
			if (bMayNeedExtraction && !bUseGltfTunerInsteadOfMeshExtraction)
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
	
	// Can't keep the list of meshes from one tile version to another, because gltf meshes may typically
	// have been split or merged: use a single dummy ExtractedElement and fill its vector with the list of
	// mesh components of current tile to which the current timeline applies:
	if (bMayNeedExtraction && bUseGltfTunerInsteadOfMeshExtraction && bTileIsTunedFor4D)
	{
		auto& SingleDummyExtract =
			SceneTile.ExtractedElement(TimelineOptim->Extracts[Entry.first->FirstExtract]);
		SingleDummyExtract.Entities.clear();
		SingleDummyExtract.Entities.reserve(TimelineMeshes.size());
		for (auto MeshComp : TimelineMeshes)
		{
			SceneTile.UseTunedMeshAsExtract(SingleDummyExtract, MeshComp,
											CoordConversions.IModelTilesetTransform);
		}
	}
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

bool FITwinSceneMapping::PickVisibleElement(ITwinElementID const& InElemID,
											bool const bSelectElement /*= true*/)
{
	// Bad: first, because returning false would make caller think the Element is not there or invisible,
	// and secondly because the visibility (thru shader) could have changed since the Element was selected!
	//if (bSelectElement && InElemID == SelectedElement)
	//	return false;
	if ((HiddenElementsFromSavedView.find(InElemID) != HiddenElementsFromSavedView.end())
		|| (bHiddenConstructionData
			&& (ConstructionDataElements().find(InElemID) != ConstructionDataElements().end())))
	{
		return false;
	}
	bool bPickedInATile = false;
	FITwinSceneTile::FTextureNeeds TextureNeeds;
	ForEachKnownTile([&InElemID, &bPickedInATile, &TextureNeeds](FITwinSceneTile& SceneTile)
	{
		bPickedInATile |= SceneTile.PickElement(InElemID, /*bOnlyVisibleTiles*/true, TextureNeeds,
												/*bTestElementVisibility*/true);
	});
	this->bNewSelectingAndHidingTexturesNeedSetupInMaterials |= TextureNeeds.bWasCreated;
	if (bSelectElement)
	{
		SelectedElement = InElemID;
		// Do it now for existing textures: the initial UpdateTexture call of new textures will also be attempted,
		// but most likely the TextureRHI is not ready yet, so it will be done again automatically when calling
		// SetupInMaterials (called from HandleNewSelectingAndHidingTextures!)
		if (TextureNeeds.bWasChanged)
			UpdateSelectingAndHidingTextures();
	}
	return bPickedInATile;
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

namespace ITwin
{
	std::array<uint8, 4> const& GetMaterialSelectionHighlightBGRA();
}

bool FITwinSceneMapping::PickVisibleMaterial(ITwinMaterialID const& InMaterialID, bool bIsMaterialPrediction,
	std::optional<ITwinColor> const& ColorToRestore /*= std::nullopt*/)
{
	bool bPickedMaterial = false;
	FITwinSceneTile::FTextureNeeds TextureNeeds;

	if (bIsMaterialPrediction)
	{
		// Special case of material prediction: temporarily override the material colors in all UE material
		// instances matching this iTwin material ID (we can do it because the whole tileset has been tuned
		// against the predicted materials).
		std::optional<ITwinColor> ColorToSet;
		uint64_t MatIdToEdit = InMaterialID.getValue();
		if (InMaterialID == ITwin::NOT_MATERIAL)
		{
			// Restore the original material's color, if any.
			if (SelectedMaterial != ITwin::NOT_MATERIAL)
			{
				ensure(ColorToRestore.has_value());
				ColorToSet = ColorToRestore;
				MatIdToEdit = SelectedMaterial.getValue();
			}
		}
		else
		{
			MatIdToEdit = InMaterialID.getValue();
			auto const& Highlight_BGRA = ITwin::GetMaterialSelectionHighlightBGRA();
			const double ColorConv = 1. / 255.;
			ColorToSet = ITwinColor{
				ColorConv * Highlight_BGRA[2],
				ColorConv * Highlight_BGRA[1],
				ColorConv * Highlight_BGRA[0],
				ColorConv * Highlight_BGRA[3]
			};
		}
		if (ColorToSet)
		{
			SetITwinMaterialChannelColor(MatIdToEdit,
				AdvViz::SDK::EChannelType::Color,
				*ColorToSet);
			bPickedMaterial = true;
		}
	}
	else
	{
		// General case, based on per-feature pixels in a texture, exactly as for ElementIDs.
		ForEachKnownTile([&InMaterialID, &bPickedMaterial, &TextureNeeds](FITwinSceneTile& SceneTile)
		{
			bPickedMaterial |= SceneTile.PickMaterial(InMaterialID, /*bOnlyVisibleTiles*/true, TextureNeeds,
				/*bTestElementVisibility*/true);
		});
	}

	SelectedMaterial = InMaterialID;
	this->bNewSelectingAndHidingTexturesNeedSetupInMaterials |= TextureNeeds.bWasCreated;
	// same comment as in #PickVisibleElement
	if (TextureNeeds.bWasChanged)
		UpdateSelectingAndHidingTextures();

	return bPickedMaterial;
}

std::pair<FITwinSceneTile const*, FITwinGltfMeshComponentWrapper const*>
FITwinSceneMapping::FindOwningTileSLOW(UPrimitiveComponent const* Component) const
{
	if (!Component)
	{
		return { nullptr, nullptr };
	}
	for (auto&& SceneTile : KnownTiles)
	{
		bool bFoundMesh = false;
		for (auto const& gltfMeshData : SceneTile.GltfMeshes)
		{
			if (gltfMeshData.GetMeshComponent() == Component)
			{
				return std::make_pair(&SceneTile, &gltfMeshData);
			}
		}
	}
	return { nullptr, nullptr };
}

void FITwinSceneMapping::Reset()
{
	*this = FITwinSceneMapping(false);
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


	/// This code depends on the parameters actually published in MF_CesiumGlTF.uasset
	static FName GetGlTFScalarParamName(AdvViz::SDK::EChannelType Channel)
	{
		switch (Channel)
		{
		case AdvViz::SDK::EChannelType::Color:
			return TEXT("baseColorTextureFactor");

		case AdvViz::SDK::EChannelType::Metallic:
			return TEXT("metallicFactor");

		case AdvViz::SDK::EChannelType::Roughness:
			return TEXT("roughnessFactor");

		case AdvViz::SDK::EChannelType::Transparency:
		case AdvViz::SDK::EChannelType::Alpha:
			return TEXT("baseColorFactor");

		case AdvViz::SDK::EChannelType::Normal:
			return TEXT("normalFlatness");

		case AdvViz::SDK::EChannelType::AmbientOcclusion:
			return TEXT("occlusionTextureStrength");

		case AdvViz::SDK::EChannelType::Specular:
			return TEXT("specularFactor");

		default:
			ensureMsgf(false, TEXT("channel %u not implemented for scalar values"), Channel);
			break;
		}
		return {};
	}

	// Cache the (constant by channel) parameter info, to avoid constructing a FName hundreds of time.

	static ITwin::FPerChannelParamInfos PerChannelScalarParamInfos;

	struct FITwinMaterialScalarParamHelper
	{
		AdvViz::SDK::EChannelType const Channel;
		float const Intensity;
		ITwin::FChannelParamInfosOpt& ParamInfosOpt;


		FITwinMaterialScalarParamHelper(AdvViz::SDK::EChannelType InChannel,
										float InIntensity)
			: Channel(InChannel)
			, Intensity(InIntensity)
			, ParamInfosOpt(PerChannelScalarParamInfos[(size_t)InChannel])
		{
			ensureMsgf(IsInGameThread(), TEXT("PerChannelScalarParamInfos handling is not thread-safe"));
			if (!ParamInfosOpt)
			{
				ParamInfosOpt.emplace(GetGlTFScalarParamName(Channel));
			}
		}

		inline void operator()(UMaterialInstanceDynamic& MatInstance) const
		{
			MatInstance.SetScalarParameterValueByInfo(ParamInfosOpt->GlobalParamInfo, Intensity);
			MatInstance.SetScalarParameterValueByInfo(ParamInfosOpt->LayerParamInfo, Intensity);
		}
	};

}

void FITwinSceneMapping::SetITwinMaterialChannelIntensity(uint64_t ITwinMaterialID,
	AdvViz::SDK::EChannelType Channel, double InIntensity)
{
	// Some parameters are "inverted" (typically, for normal mapping we set a normal flatness and not an
	// amplitude...)
	double NewScalarValue = InIntensity;
	if (Channel == AdvViz::SDK::EChannelType::Transparency
		|| Channel == AdvViz::SDK::EChannelType::Normal)
	{
		NewScalarValue = 1. - InIntensity;
	}
	FITwinMaterialScalarParamHelper const ScalarHelper(Channel, static_cast<float>(NewScalarValue));

	if (Channel == AdvViz::SDK::EChannelType::Alpha
		|| Channel == AdvViz::SDK::EChannelType::Transparency)
	{
		// Special handling for alpha: we edit the base color
		auto const UpdateAlphaInUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
		{
			SetBaseColorAlpha(MatInstance,
				ScalarHelper.ParamInfosOpt->GlobalParamInfo, ScalarHelper.Intensity);
			SetBaseColorAlpha(MatInstance,
				ScalarHelper.ParamInfosOpt->LayerParamInfo, ScalarHelper.Intensity);
		};
		ForEachKnownTile([&](FITwinSceneTile& SceneTile)
		{
			SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateAlphaInUnrealMatFunc);
		});
	}
	else
	{
		ForEachKnownTile([&](FITwinSceneTile& SceneTile)
		{
			SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, ScalarHelper);
		});
	}
}

void FITwinSceneMapping::SetITwinMaterialChannelTexture(uint64_t ITwinMaterialID,
	AdvViz::SDK::EChannelType Channel, UTexture* pTexture)
{
	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.SetITwinMaterialChannelTexture(ITwinMaterialID, Channel, pTexture);
	});
}

void FITwinSceneMapping::SetITwinMaterialUVTransform(uint64_t ITwinMaterialID, ITwinUVTransform const& UVTransform)
{
	ensureMsgf(IsInGameThread(), TEXT("UVParamInfo handling is not thread-safe"));
	static ITwin::FChannelParamInfosOpt ScaleOffsetParamInfo;
	static ITwin::FChannelParamInfosOpt RotationParamInfo;
	if (!ScaleOffsetParamInfo)
	{
		// Those constants depend on the parameters actually published in MF_CesiumGlTF.uasset
		ScaleOffsetParamInfo.emplace(	TEXT("uvScaleOffset"));
		RotationParamInfo.emplace(		TEXT("uvRotation"));
	}
	// Encode scale, offset and rotation values as expected by the glTF shader.
	const FLinearColor ScaleOffsetValues(
		UVTransform.scale[0],
		UVTransform.scale[1],
		UVTransform.offset[0],
		UVTransform.offset[1]);
	const FLinearColor RotationValues(
		float(FMath::Sin(UVTransform.rotation)),
		float(FMath::Cos(UVTransform.rotation)),
		0.0f,
		1.0f);
	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		MatInstance.SetVectorParameterValueByInfo(ScaleOffsetParamInfo->GlobalParamInfo, ScaleOffsetValues);
		MatInstance.SetVectorParameterValueByInfo(RotationParamInfo->GlobalParamInfo, RotationValues);

		MatInstance.SetVectorParameterValueByInfo(ScaleOffsetParamInfo->LayerParamInfo, ScaleOffsetValues);
		MatInstance.SetVectorParameterValueByInfo(RotationParamInfo->LayerParamInfo, RotationValues);
	};
	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	});
}

void FITwinSceneMapping::SetITwinMaterialChannelColor(uint64_t ITwinMaterialID,
	AdvViz::SDK::EChannelType Channel, AdvViz::SDK::ITwinColor const& InColor)
{
	ensureMsgf(Channel == AdvViz::SDK::EChannelType::Color, TEXT("channel %u not implemented for colors"), Channel);
	ensureMsgf(IsInGameThread(), TEXT("ColorParamInfo handling is not thread-safe"));
	static ITwin::FChannelParamInfosOpt ColorParamInfo;
	if (!ColorParamInfo)
	{
		// This constant depends on the parameters actually published in MF_CesiumGlTF.uasset
		ColorParamInfo.emplace(TEXT("baseColorFactor"));
	}

	FLinearColor const NewValue(
		InColor[0],
		InColor[1],
		InColor[2],
		InColor[3]);
	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		MatInstance.SetVectorParameterValueByInfo(ColorParamInfo->GlobalParamInfo, NewValue);
		MatInstance.SetVectorParameterValueByInfo(ColorParamInfo->LayerParamInfo, NewValue);
	};

	ForEachKnownTile([&](FITwinSceneTile& SceneTile)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	});
}
