/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMapping.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMapping.h"
#include <Math/UEMathExts.h>
#include <Timeline/Timeline.h>
#include <Timeline/SchedulesConstants.h>

#include <Components/StaticMeshComponent.h>
#include <DrawDebugHelpers.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <MaterialTypes.h>

//#include "../../CesiumRuntime/Private/CesiumMaterialUserData.h" // see comment in #GetSynchro4DLayerIndexInMaterial


namespace ITwin
{
	/// Highlight color for selected element
	static const std::array<uint8, 4> COLOR_SELECTED_ELEMENT_BGRA = { 96, 230, 0, 94 };
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


//---------------------------------------------------------------------------------------
// struct FITwinExtractedEntity
//---------------------------------------------------------------------------------------

void FITwinExtractedEntity::SetHidden(bool bHidden)
{
	if (SourceMeshComponent.IsValid() && !SourceMeshComponent->IsVisible())
	{
		// if the original mesh is globally hidden by the 3D tile system, we should not show the
		// extracted entity
		bHidden = true;
	}
	if (MeshComponent.IsValid())
	{
		MeshComponent->SetVisibility(!bHidden, true);
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
		checkf(false, TEXT("orphan mesh component"));
		return false;
	}

	TArray<FStaticMaterial> StaticMaterials =
		StaticMesh->GetStaticMaterials();
	check(StaticMaterials.Num() == 1);

	FStaticMaterial& StaticMaterial = StaticMaterials[0];
	checkf(StaticMaterial.MaterialInterface == this->Material,
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
	if (Material.IsValid())
	{
		Material->SetScalarParameterValueByInfo(
			FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo(),
			Opacity);
	}
}

//---------------------------------------------------------------------------------------
// class FITwinSceneTile
//---------------------------------------------------------------------------------------

FITwinElementFeaturesInTile const* FITwinSceneTile::FindElementFeatures(ITwinElementID const& ElemID) const
{
	auto const Found = ElementsFeatures.find(ElemID);
	if (Found != ElementsFeatures.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

FITwinElementFeaturesInTile* FITwinSceneTile::FindElementFeatures(ITwinElementID const& ElemID)
{
	auto const Found = ElementsFeatures.find(ElemID);
	if (Found != ElementsFeatures.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile>::iterator
	FITwinSceneTile::ElementFeatures(ITwinElementID const& ElemID)
{
	auto const Found = ElementsFeatures.find(ElemID);
	if (Found != ElementsFeatures.end())
	{
		return Found;
	}
	else
	{
		return ElementsFeatures.insert(
			std::make_pair(ElemID, FITwinElementFeaturesInTile{ ElemID })).first;
	}
}

void FITwinSceneTile::ForEachElementFeatures(std::function<void(FITwinElementFeaturesInTile&)> const& Func)
{
	for (auto&& [ElemID, FeaturesInTile] : ElementsFeatures)
	{
		Func(FeaturesInTile);
	}
}

FITwinExtractedEntity const* FITwinSceneTile::FindExtractedElement(ITwinElementID const& ElemID) const
{
	auto const Found = ExtractedElements.find(ElemID);
	if (Found != ExtractedElements.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

FITwinExtractedEntity* FITwinSceneTile::FindExtractedElement(ITwinElementID const& ElemID)
{
	auto const Found = ExtractedElements.find(ElemID);
	if (Found != ExtractedElements.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

std::unordered_map<ITwinElementID, FITwinExtractedEntity>::iterator
	FITwinSceneTile::ExtractedElement(ITwinElementID const& ElemID)
{
	auto const Found = ExtractedElements.find(ElemID);
	if (Found != ExtractedElements.end())
	{
		return Found;
	}
	else
	{
		return ExtractedElements.insert(
			std::make_pair(ElemID, FITwinExtractedEntity{ ElemID })).first;
	}
}

void FITwinSceneTile::EraseExtractedElement(
	std::unordered_map<ITwinElementID, FITwinExtractedEntity>::iterator const Where)
{
	ExtractedElements.erase(Where);
}

void FITwinSceneTile::ForEachExtractedElement(std::function<void(FITwinExtractedEntity&)> const& Func)
{
	for (auto&& [ElemID, Extracted] : ExtractedElements)
	{
		Func(Extracted);
	}
}

//---------------------------------------------------------------------------------------
// class FITwinSceneMapping
//---------------------------------------------------------------------------------------

#define DEBUG_SYNCHRO4D_BGRA 0

static const FName SelectionHighlightsMaterialParameterName = "PROP_Selection_RGBA";
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
	// Bake feature IDs in per-vertex UVs if needed
	void BakeFeatureIDsInVertexUVs(FITwinSceneTile& SceneTile)
	{
		check(IsInGameThread());
		for (FITwinGltfMeshComponentWrapper& GltfMeshData : SceneTile.GltfMeshes)
		{
			auto const UVIdx = GltfMeshData.BakeFeatureIDsInVertexUVs();
			if (!UVIdx) continue;

			auto const MeshComp = GltfMeshData.GetMeshComponent();
			int32 const NumMats = MeshComp ? MeshComp->GetNumMaterials() : 0;
			for (int32 m = 0; m < NumMats; ++m)
			{
				auto Mat = MeshComp->GetMaterial(m);
				checkSlow(SceneTile.FeatureIDsUVIndex.end() == SceneTile.FeatureIDsUVIndex.find(Mat));
				SceneTile.FeatureIDsUVIndex[Mat] = *UVIdx;
			}
		}
	}

	static std::optional<FMaterialParameterInfo> HighlightsAndOpacitiesInfo;
	static std::optional<FMaterialParameterInfo> FeatureIdInfo;
	static std::optional<FMaterialParameterInfo> CuttingPlanesInfo;
	static std::optional<FMaterialParameterInfo> ExtractedElementForcedAlphaInfo;
	static std::optional<FMaterialParameterInfo> SelectionHighlightsInfo;

	inline void SetupFeatureIdInfo()
	{
		if (!FeatureIdInfo)
		{
			FeatureIdInfo.emplace(FeatureIdMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial(/*pMaterial*/));
		}
	}

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

	inline void SetupSelectionHighlightsInfo()
	{
		if (!SelectionHighlightsInfo)
		{
			SelectionHighlightsInfo.emplace(SelectionHighlightsMaterialParameterName,
				EMaterialParameterAssociation::BlendParameter,
				GetSynchro4DLayerIndexInMaterial());
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
			Mat.GetTextureParameterValue(*SelectionHighlightsInfo, OutTex, true);
			check(OutTex);
		}
	#endif // ENABLE_CheckMaterialSetup
	}
} // anon ns.


void FITwinSceneTile::SelectElement(ITwinElementID const& InElemID, bool& bHasUpdatedTextures)
{
	bHasUpdatedTextures = false;
	if (InElemID == SelectedElement)
	{
		// Nothing to do.
		return;
	}
	if (MaxFeatureID == ITwin::NOT_FEATURE)
	{
		// No Feature at all.
		check(SelectedElement == ITwin::NOT_ELEMENT);
		return;
	}
	// 1. Reset current selection, if any.
	if (SelectedElement != ITwin::NOT_ELEMENT)
	{
		check(SelectionHighlights);
		FITwinElementFeaturesInTile* FeaturesToDeSelect = FindElementFeatures(SelectedElement);
		if (ensure(FeaturesToDeSelect != nullptr))
		{
			SelectionHighlights->SetPixels(FeaturesToDeSelect->Features, { 0, 0, 0, 255 });

			FeaturesToDeSelect->TextureFlags.SelectionFlags.Invalidate();
			bHasUpdatedTextures = true;
		}
		SelectedElement = ITwin::NOT_ELEMENT;
	}

	// 2. Select new Element, only if it exists in the tile.
	FITwinElementFeaturesInTile* FeaturesToSelect = nullptr;
	if (InElemID != ITwin::NOT_ELEMENT)
	{
		FeaturesToSelect = FindElementFeatures(InElemID);
	}
	if (FeaturesToSelect != nullptr)
	{
		SetupSelectionHighlightsInfo();

		// Create selection texture if needed.
		if (!SelectionHighlights)
		{
			SelectionHighlights = std::make_unique<FITwinDynamicShadingBGRA8Property>(
				MaxFeatureID, std::array<uint8, 4>({ 0, 0, 0, 255 }));

			// Bake feature IDs in per-vertex UVs if needed
			BakeFeatureIDsInVertexUVs(*this);
		}
		// Apply constant highlight color to pixels matching the Element's features
		SelectionHighlights->SetPixels(FeaturesToSelect->Features, ITwin::COLOR_SELECTED_ELEMENT_BGRA);

		FeaturesToSelect->TextureFlags.SelectionFlags.Invalidate();
		bHasUpdatedTextures = true;

		SelectedElement = InElemID;
	}
}

void FITwinSceneTile::UpdateSelectionTextureInMaterials()
{
	if (!SelectionHighlights)
	{
		return;
	}
	bool hasUpdatedTex = false;
	for (auto&& [ElemID, FeaturesInTile] : ElementsFeatures)
	{
		if (FeaturesInTile.TextureFlags.SelectionFlags.bNeedSetupTexture)
		{
			if (!hasUpdatedTex)
			{
				// important: we must call UpdateTexture once, *before* updating materials
				SelectionHighlights->UpdateTexture();
				hasUpdatedTex = true;
			}
			SelectionHighlights->UpdateInMaterials(FeaturesInTile.Materials,
				*SelectionHighlightsInfo);
			FeaturesInTile.TextureFlags.SelectionFlags.OnTextureSetInMaterials();
		}
	}
}


/*static*/
FMaterialParameterInfo const& FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo()
{
	SetupExtractedElementForcedAlphaInfo();
	return *ExtractedElementForcedAlphaInfo;
}

/*static*/
void FITwinSceneMapping::SetupFeatureIdUVIndex(FITwinSceneTile& SceneTile,
											   FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ensure(FeatureIdInfo))
	{
		for (auto& MatPtr : ElementFeaturesInTile.Materials)
		{
			if (MatPtr.IsValid())
			{
				auto const UVIndex = SceneTile.FeatureIDsUVIndex.find(MatPtr.Get());
				if (UVIndex != SceneTile.FeatureIDsUVIndex.end())
				{
					MatPtr->SetScalarParameterValueByInfo(*FeatureIdInfo, UVIndex->second);
				}
			}
		}
	}
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
void FITwinSceneMapping::SetupHighlights(
	FITwinSceneTile& SceneTile,
	FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ElementFeaturesInTile.Materials.empty())
		return;

	auto const& HighlightOpaFlags = ElementFeaturesInTile.TextureFlags.HighlightsAndOpacitiesFlags;
	auto const& SelectionHiLFlags = ElementFeaturesInTile.TextureFlags.SelectionFlags;

	bool const bNeedUpdateHighlightOpa = HighlightOpaFlags.ShouldUpdateMaterials(SceneTile.HighlightsAndOpacities);
	bool const bNeedUpdateSelectionHiL = SelectionHiLFlags.ShouldUpdateMaterials(SceneTile.SelectionHighlights);

	// NB: those FMaterialParameterInfo info no longer depend on the material, so we can setup them at once:
	if (bNeedUpdateHighlightOpa)
	{
		SetupHighlightsAndOpacitiesInfo();
	}
	SetupFeatureIdInfo();
	check(!bNeedUpdateSelectionHiL || SelectionHighlightsInfo);

	if (!bNeedUpdateHighlightOpa && !bNeedUpdateSelectionHiL)
	{
		// Nothing to do
		for (auto&& Mat : ElementFeaturesInTile.Materials)
		{
			if (Mat.IsValid())
			{
				CheckMaterialSetup(*Mat.Get(), false,
					(bool)SceneTile.HighlightsAndOpacities, (bool)SceneTile.CuttingPlanes,
					(bool)SceneTile.SelectionHighlights);
			}
		}
		return;
	}
	auto const SetupElement = [&SceneTile, bNeedUpdateHighlightOpa, bNeedUpdateSelectionHiL]
	(FITwinElementFeaturesInTile& Element)
	{
		if (!Element.Materials.empty())
		{
			SetupFeatureIdUVIndex(SceneTile, Element);
			if (bNeedUpdateHighlightOpa)
			{
				SceneTile.HighlightsAndOpacities->UpdateInMaterials(Element.Materials,
					*HighlightsAndOpacitiesInfo);
				Element.TextureFlags.HighlightsAndOpacitiesFlags.OnTextureSetInMaterials();
			}
			if (bNeedUpdateSelectionHiL)
			{
				SceneTile.SelectionHighlights->UpdateInMaterials(Element.Materials,
					*SelectionHighlightsInfo);
				Element.TextureFlags.SelectionFlags.OnTextureSetInMaterials();
			}
		}
	};
#if DEBUG_SYNCHRO4D_BGRA
	// Handle all elements, otherwise only materials used by animated elements will have the debug
	// colors, so you won't probably see much of anything...(you still won't get all tiles colored, only
	// those containing at least one animated Element)
	for (auto&& [ElemID, FeaturesInTile] : SceneTile.ElementsFeatures)
	{
		SetupElement(FeaturesInTile);
	}
#else
	SetupElement(ElementFeaturesInTile);
#endif
}

/*static*/
void FITwinSceneMapping::SetupHighlights(
	FITwinSceneTile& SceneTile,
	FITwinExtractedEntity& ExtractedEntity)
{
	auto const& HighlightOpaFlags = ExtractedEntity.TextureFlags.HighlightsAndOpacitiesFlags;
	auto const& SelectionHiLFlags = ExtractedEntity.TextureFlags.SelectionFlags;

	bool const bNeedUpdateHighlightOpa = HighlightOpaFlags.ShouldUpdateMaterials(SceneTile.HighlightsAndOpacities);
	bool const bNeedUpdateSelectionHiL = SelectionHiLFlags.ShouldUpdateMaterials(SceneTile.SelectionHighlights);

	if ((!bNeedUpdateHighlightOpa && !bNeedUpdateSelectionHiL)
		|| !ExtractedEntity.Material.IsValid())
	{
		if (ExtractedEntity.Material.IsValid())
		{
			CheckMaterialSetup(*ExtractedEntity.Material.Get(), true,
				(bool)SceneTile.HighlightsAndOpacities, (bool)SceneTile.CuttingPlanes,
				(bool)SceneTile.SelectionHighlights);
		}
		else check(false);// need to reset Material??
		return;
	}
	SetupHighlightsAndOpacitiesInfo();
	SetupFeatureIdInfo();

	SetupFeatureIdUVIndex(SceneTile, ExtractedEntity);
	if (bNeedUpdateHighlightOpa)
	{
		SceneTile.HighlightsAndOpacities->UpdateInMaterial(ExtractedEntity.Material,
			*HighlightsAndOpacitiesInfo);
		ExtractedEntity.TextureFlags.HighlightsAndOpacitiesFlags.OnTextureSetInMaterials();
	}
	if (bNeedUpdateSelectionHiL)
	{
		SceneTile.SelectionHighlights->UpdateInMaterial(ExtractedEntity.Material,
			*SelectionHighlightsInfo);
		ExtractedEntity.TextureFlags.SelectionFlags.OnTextureSetInMaterials();
	}
}

/*static*/
void FITwinSceneMapping::SetupCuttingPlanes(
	FITwinSceneTile& SceneTile,
	FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (!SceneTile.CuttingPlanes
		|| !ElementFeaturesInTile.TextureFlags.CuttingPlaneFlags.bNeedSetupTexture
		|| ElementFeaturesInTile.Materials.empty())
	{
		return;
	}
	SetupCuttingPlanesInfo();
	SetupFeatureIdInfo();
	SceneTile.CuttingPlanes->UpdateInMaterials(ElementFeaturesInTile.Materials, *CuttingPlanesInfo);
	SetupFeatureIdUVIndex(SceneTile, ElementFeaturesInTile);
	ElementFeaturesInTile.TextureFlags.CuttingPlaneFlags.OnTextureSetInMaterials();
}

/*static*/
void FITwinSceneMapping::SetupCuttingPlanes(
	FITwinSceneTile& SceneTile,
	FITwinExtractedEntity& ExtractedEntity)
{
	if (!SceneTile.CuttingPlanes
		|| !ExtractedEntity.TextureFlags.CuttingPlaneFlags.bNeedSetupTexture
		|| !ExtractedEntity.Material.IsValid())
	{
		return;
	}
	SetupCuttingPlanesInfo();
	SetupFeatureIdInfo();
	SceneTile.CuttingPlanes->UpdateInMaterial(ExtractedEntity.Material, *CuttingPlanesInfo);
	SetupFeatureIdUVIndex(SceneTile, ExtractedEntity);
	ExtractedEntity.TextureFlags.CuttingPlaneFlags.OnTextureSetInMaterials();
}

void FITwinSceneMapping::OnBatchedElementTimelineModified(CesiumTileID const& TileID,
	FITwinSceneTile& SceneTile, FITwinElementFeaturesInTile& ElementFeaturesInTile,
	FITwinElementTimeline const& ModifiedTimeline)
{
	if (ModifiedTimeline.IsEmpty())
	{
		return;
	}
	// Check whether with ModifiedTimeline we need to switch the Element's material from
	// opaque to translucent (not the other way round: even if visibility_ can force
	// opacity to 1, and not only multiplies, the material can be translucent for other
	// reasons).
	bool bTranslucencyNeeded = false;
	if (!ElementFeaturesInTile.bHasTestedForTranslucentFeaturesNeedingExtraction
		&& ModifiedTimeline.NeedsPartialVisibility())
	{
		bTranslucencyNeeded = ElementFeaturesInTile.HasOpaqueOrMaskedMaterial();
		ElementFeaturesInTile.bHasTestedForTranslucentFeaturesNeedingExtraction = true;
	}
	// Animated transformation?
	bool const bAnimatedTSF = !ModifiedTimeline.transform_.list_.empty();
	if ((bTranslucencyNeeded || bAnimatedTSF)
		&& !ElementFeaturesInTile.bIsElementExtracted)
	{
		// Extract the Element in this tile, and assign it a translucent material if needed.
		FITwinMeshExtractionOptions ExtractOpts;
		if (bTranslucencyNeeded && ensure(MaterialGetter))
		{
			ExtractOpts.bCreateNewMaterialInstance = true;
			ExtractOpts.BaseMaterialForNewInstance =
				MaterialGetter(ECesiumMaterialType::Translucent);
			ExtractOpts.ScalarParameterToSet.emplace(
				GetExtractedElementForcedAlphaMaterialParameterInfo(), 1.f);
		}
		if (bAnimatedTSF)
		{
			// Extract the Element in all tiles
			ExtractElement(ModifiedTimeline.IModelElementID, ExtractOpts);
		}
		else
		{
			// Extract the Element just in this tile (will probably be needed in other
			// tiles afterwards, so this distinction is probably useless...)
			ExtractElementFromTile(TileID, ModifiedTimeline.IModelElementID, SceneTile, ExtractOpts);
		}
	}
	if (!SceneTile.HighlightsAndOpacities
		&& (ITwin::NOT_FEATURE != SceneTile.MaxFeatureID)
		&& (!ModifiedTimeline.color_.list_.empty()
			|| !ModifiedTimeline.visibility_.list_.empty()
			// For each Feature, masking it or not will depend on where exactly we are in the timeline right
			// now, but here we just want to know whether we create the texture or not, which is
			// time-independent:
			|| ElementFeaturesInTile.bIsElementExtracted
			// when the cutting plane fully hides an object (after a 'Remove' or 'Temporary' task), the flag
			// can be used to mask out the object using the 'Mask' shader output, which is set when the
			// alpha/visibility is set to zero in HighlightsAndOpacities
			|| ModifiedTimeline.HasFullyHidingCuttingPlaneKeyframes()))
	{
		SceneTile.HighlightsAndOpacities = std::make_unique<FITwinDynamicShadingBGRA8Property>(
			SceneTile.MaxFeatureID, std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255)));
	#if DEBUG_SYNCHRO4D_BGRA
		for (uint32 p = 0; p <= SceneTile.MaxFeatureID.value(); ++p)
		{
			SceneTile.HighlightsAndOpacities->SetPixel(p,
				FITwinMathExts::RandomBGRA8ColorFromIndex(ModifiedTimeline.IModelElementID), true);
		}
		SceneTile.HighlightsAndOpacities->UpdateTexture();
	#endif

		// Bake feature IDs in per-vertex UVs if needed
		BakeFeatureIDsInVertexUVs(SceneTile);
	}
	if (SceneTile.HighlightsAndOpacities
		&& ElementFeaturesInTile.bIsElementExtracted
		&& !ElementFeaturesInTile.bIsAlphaSetInTextureToHideExtractedElement)
	{
		// Ensure the parts that were extracted are made invisible in the original mesh
		SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElementFeaturesInTile.Features, 0);
		ElementFeaturesInTile.bIsAlphaSetInTextureToHideExtractedElement = true;
	}
	ElementFeaturesInTile.TextureFlags.HighlightsAndOpacitiesFlags.InvalidateOnCondition(
		(bool)SceneTile.HighlightsAndOpacities);
	if (!SceneTile.CuttingPlanes && (ITwin::NOT_FEATURE != SceneTile.MaxFeatureID)
		&& !ModifiedTimeline.clippingPlane_.list_.empty())
	{
		SceneTile.CuttingPlanes = std::make_unique<FITwinDynamicShadingABGR32fProperty>(
			SceneTile.MaxFeatureID, std::array<float, 4>(S4D_CLIPPING_DISABLED));

		// Bake feature IDs in per-vertex UVs if needed
		BakeFeatureIDsInVertexUVs(SceneTile);
	}
	ElementFeaturesInTile.TextureFlags.CuttingPlaneFlags.InvalidateOnCondition(
		(bool)SceneTile.CuttingPlanes);
}

void FITwinSceneMapping::OnExtractedElementTimelineModified(FITwinExtractedEntity& ExtractedEntity,
	FITwinElementTimeline const& ModifiedTimeline)
{
	// Check the need for opaque/translucent materials didn't just arise here too, ie in case the Element
	// was already extracted but for another reason, and without a Translucent material (unlikely...).
	bool bTranslucencyNeeded = false;
	if (ModifiedTimeline.NeedsPartialVisibility() && ExtractedEntity.MeshComponent.IsValid())
	{
		bTranslucencyNeeded = ExtractedEntity.HasOpaqueOrMaskedMaterial();
	}
	if (bTranslucencyNeeded && ensure(MaterialGetter))
	{
		check(false); // TODO_GCO: I used this codepath for testing and it crashes...(TODO_JDE?)
		ExtractedEntity.SetBaseMaterial(MaterialGetter(ECesiumMaterialType::Translucent));
		checkf(!ExtractedEntity.HasOpaqueOrMaskedMaterial(),
			   TEXT("material should be translucent now"));
	}

	ExtractedEntity.TextureFlags.CuttingPlaneFlags.InvalidateOnCondition(
		!ModifiedTimeline.clippingPlane_.list_.empty());
}

void FITwinSceneMapping::OnNewTileMeshFromBuilder(CesiumTileID const& TileId, FITwinSceneTile& SceneTile,
												  std::set<ITwinElementID> const& MeshElementIDs)
{
	if (NewTileMeshObserver)
		NewTileMeshObserver(TileId, MeshElementIDs);
	if (!TimelineGetter)
		return;
	FITwinScheduleTimeline const& MainTimeline = TimelineGetter();
	for (ITwinElementID const Elem : MeshElementIDs)
	{
		int const Found = MainTimeline.GetElementTimelineIndex(Elem);
		if (-1 != Found)
		{
			OnBatchedElementTimelineModified(TileId, SceneTile, SceneTile.ElementFeatures(Elem)->second,
											 MainTimeline.GetElementTimelineByIndex(Found));
		}
	}
}

void FITwinSceneMapping::HideExtractedEntities(bool bHide /*= true*/)
{
	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		SceneTile.ForEachExtractedElement([bHide](FITwinExtractedEntity& ExtractedEntity)
			{
				ExtractedEntity.SetHidden(bHide);
			});
	}
}

FBox FITwinSceneMapping::GetBoundingBox(ITwinElementID const Element) const
{
	auto it = KnownBBoxes.find(Element);
	if (it != KnownBBoxes.cend())
	{
		return it->second;
	}
	// For a first (naive) implementation, the map of bounding boxes is
	// filled as soon as a mesh component is created, so if we don't have
	// it in cache, there is no chance we could compute it now...
	// Note that FITwinIModelInternals::HasElementWithID uses this assumption too for the moment.
	// We may change that in the future if it's too slow or consumes too
	// much memory, using a cache
	// TODO_GCO/TODO_JDE
	return FBox();
}

uint32 FITwinSceneMapping::ExtractElement(
    ITwinElementID const Element,
    FITwinMeshExtractionOptions const& Options /*= {}*/)
{
    uint32 nbUEEntities = 0;
    for (auto& [TileID, SceneTile] : KnownTiles)
    {
        nbUEEntities += ExtractElementFromTile(TileID, Element, SceneTile, Options);
    }
    return nbUEEntities;
}

uint32 FITwinSceneMapping::ExtractElementFromTile(
    CesiumTileID const TileID,
    ITwinElementID const Element,
    FITwinSceneTile& SceneTile,
    FITwinMeshExtractionOptions const& Options /*= {}*/)
{
	uint32 nbUEEntities = 0;
	for (FITwinGltfMeshComponentWrapper& gltfMeshData : SceneTile.GltfMeshes)
	{
		if (gltfMeshData.CanExtractElement(Element))
		{
			auto ExtractedEntity = SceneTile.ExtractedElement(Element);
			if (gltfMeshData.ExtractElement(Element, ExtractedEntity->second, Options))
			{
				nbUEEntities++;
			}
			else
			{
				// Don't keep half constructed extracted entity
				SceneTile.EraseExtractedElement(ExtractedEntity);
			}
		}
    }
	if (nbUEEntities > 0)
	{
		// Set a flag to mark this Element as extracted.
		SceneTile.ElementFeatures(Element)->second.bIsElementExtracted = true;
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

	for (auto& [TileID, SceneTile] : KnownTiles)
	{
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
		if (nbProcessedTiles >= nbTilesToExtract)
		{
			break;
		}
	}
#endif // ENABLE_DRAW_DEBUG

    return nbExtractedElts;
}


void FITwinSceneMapping::HidePrimitivesWithExtractedEntities(bool bHide /*= true*/)
{
    for (auto& it : KnownTiles)
    {
		auto & TileID = it.first;
		auto & SceneTile= it.second;
		SceneTile.ForEachExtractedElement([&SceneTile, bHide](FITwinExtractedEntity& Extracted)
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
    }
}

void FITwinSceneMapping::BakeFeaturesInUVs_AllMeshes()
{
    for (auto& it: KnownTiles)
    {
		auto & TileID = it.first;
		auto & SceneTile= it.second;
        for (auto& gltfMeshData : SceneTile.GltfMeshes)
        {
            gltfMeshData.BakeFeatureIDsInVertexUVs();
        }
    }
}

void FITwinSceneMapping::SelectElement(ITwinElementID const& InElemID)
{
	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		bool bHasUpdatedTex(false);
		SceneTile.SelectElement(InElemID, bHasUpdatedTex);
		if (bHasUpdatedTex)
			bNeedUpdateSelectionHighlights = true;
	}
}

void FITwinSceneMapping::UpdateSelectionHighlights()
{
	if (!bNeedUpdateSelectionHighlights)
		return;
	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		SceneTile.UpdateSelectionTextureInMaterials();
	}
	bNeedUpdateSelectionHighlights = false;
}

std::optional<CesiumTileID> FITwinSceneMapping::DrawOwningTileBox(
	UPrimitiveComponent const* Component,
	UWorld const* World) const
{
#if ENABLE_DRAW_DEBUG
	if (!Component)
	{
		return std::nullopt;
	}
	for (auto const& [TileID, SceneTile] : KnownTiles)
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
			// Display the bounding box of the tile
			FBox Box;
			for (auto const& gltfMeshData : SceneTile.GltfMeshes)
			{
				if (gltfMeshData.GetMeshComponent())
				{
					Box += gltfMeshData.GetMeshComponent()->Bounds.GetBox();
				}
			}
			FVector Center, Extent;
			Box.GetCenterAndExtents(Center, Extent);
			DrawDebugBox(
				World,
				Center,
				Extent,
				FColor::Red,
				/*bool bPersistent =*/ false,
				/*float LifeTime =*/ 5.f);
			return TileID;
		}
	}
#endif //ENABLE_DRAW_DEBUG
	return std::nullopt;
}
