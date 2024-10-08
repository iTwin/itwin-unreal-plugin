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

#include <ITwinExtractedMeshComponent.h>
#include <DrawDebugHelpers.h>
#include <Engine/StaticMesh.h>
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

void FITwinSceneTile::BakeFeatureIDsInVertexUVs(bool bUpdatingTile /*= false*/)
{
	checkSlow(IsInGameThread());
	for (FITwinGltfMeshComponentWrapper& GltfMeshData : GltfMeshes)
	{
		auto const UVIdx = GltfMeshData.BakeFeatureIDsInVertexUVs();
		if (!UVIdx) continue;

		auto const MeshComp = GltfMeshData.GetMeshComponent();
		int32 const NumMats = MeshComp ? MeshComp->GetNumMaterials() : 0;
		for (int32 m = 0; m < NumMats; ++m)
		{
			auto Mat = MeshComp->GetMaterial(m);
			checkSlow(FeatureIDsUVIndex.end() == FeatureIDsUVIndex.find(Mat)
					|| (bUpdatingTile && FeatureIDsUVIndex[Mat] == *UVIdx));
			FeatureIDsUVIndex[Mat] = *UVIdx;
		}
	}
}

bool FITwinSceneTile::HasAnyVisibleMesh() const
{
	for (auto&& Wrapper : GltfMeshes)
	{
		auto&& MeshComp = Wrapper.GetMeshComponent();
		if (MeshComp && MeshComp->IsVisible())
			return true;
	}
	return false;
}

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

FITwinSceneTile::ExtractedEntityVec const* FITwinSceneTile::FindExtractedElement(ITwinElementID const& ElemID) const
{
	auto const Found = ExtractedElements.find(ElemID);
	if (Found != ExtractedElements.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

FITwinSceneTile::ExtractedEntityVec* FITwinSceneTile::FindExtractedElement(ITwinElementID const& ElemID)
{
	auto const Found = ExtractedElements.find(ElemID);
	if (Found != ExtractedElements.end())
	{
		return &(Found->second);
	}
	else return nullptr;
}

FITwinSceneTile::ExtractedEntityMap::iterator
	FITwinSceneTile::ExtractedElement(ITwinElementID const& ElemID)
{
	return ExtractedElements.try_emplace(ElemID, ExtractedEntityVec{}).first;
}

void FITwinSceneTile::EraseExtractedElement(ExtractedEntityMap::iterator const Where)
{
	ExtractedElements.erase(Where);
}

void FITwinSceneTile::ForEachExtractedElement(std::function<void(FITwinExtractedEntity&)> const& Func)
{
	for (auto&& [ElemID, ExtractedVec] : ExtractedElements)
	{
		for (auto&& Extracted : ExtractedVec)
		{
			Func(Extracted);
		}
	}
}

template<typename ElementsCont>
void FITwinSceneTile::ForEachElementFeatures(ElementsCont const& ForElementIDs,
											 std::function<void(FITwinElementFeaturesInTile&)> const& Func)
{
	for (auto const& ElemID : ForElementIDs)
	{
		auto* Found = FindElementFeatures(ElemID);
		if (Found) Func(*Found);
	}
}

template<typename ElementsCont>
void FITwinSceneTile::ForEachExtractedElement(ElementsCont const& ForElementIDs,
											  std::function<void(FITwinExtractedEntity&)> const& Func)
{
	for (auto const& ElemID : ForElementIDs)
	{
		auto* FoundVec = FindExtractedElement(ElemID);
		if (FoundVec)
		{
			for (auto&& ExtractedElt : *FoundVec)
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

#define DEBUG_SYNCHRO4D_BGRA 0 // TODO_GCO: ignored in FITwinSceneMapping::ReplicateAnimatedElementsSetupInTile

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

#if ENABLE_DRAW_DEBUG
	float DebugBoxNextLifetime = 5.f;
#endif
} // anon ns.


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

	FColor const boxColor = (DebugBoxNextLifetime > 5)
		? FColor::MakeRandomColor()
		: FColor::Red;
	DrawDebugBox(
		World,
		Center,
		Extent,
		boxColor,
		/*bool bPersistent =*/ false,
		/*float LifeTime =*/ DebugBoxNextLifetime);
	DebugBoxNextLifetime += 5.f;
#endif //ENABLE_DRAW_DEBUG
}

bool FITwinSceneTile::SelectElement(ITwinElementID const& InElemID, bool& bHasUpdatedTextures,
									UWorld const* World)
{
	if (InElemID == SelectedElement)
	{
		// Nothing to do.
		return false;
	}
	if (MaxFeatureID == ITwin::NOT_FEATURE)
	{
		// No Feature at all.
		checkSlow(SelectedElement == ITwin::NOT_ELEMENT);
		return false;
	}
	if (!HasAnyVisibleMesh())
	{
		return false; // filter out hidden tiles
	}
	if (SelectionHighlights
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SelectionHighlights->GetTotalUsedPixels() < (MaxFeatureID.value() + 1))
	{
		ensure(false); // should not happen
		SelectionHighlights.reset(); // let's hope it doesn't crash everything...
		SelectedElement = ITwin::NOT_ELEMENT;
		for (auto&& ElementInTile : ElementsFeatures)
			ElementInTile.second.TextureFlags.SelectionFlags.Invalidate();// no cond on bTextureIsSet here
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
		if (!FeaturesToSelect->Features.empty() && HighlightsAndOpacities)
		{
			auto&& Synchro4D_BGRA = HighlightsAndOpacities->GetPixel(FeaturesToSelect->Features[0].value());
			if (Synchro4D_BGRA[3] == 0) // do not select masked Elements
				return false;
		}
		SetupSelectionHighlightsInfo();

		// Create selection texture if needed.
		if (!SelectionHighlights)
		{
			SelectionHighlights = std::make_unique<FITwinDynamicShadingBGRA8Property>(
				MaxFeatureID, std::array<uint8, 4>({ 0, 0, 0, 255 }));

			// Bake feature IDs in per-vertex UVs if needed
			BakeFeatureIDsInVertexUVs();
		}

		// Apply constant highlight color to pixels matching the Element's features
		SelectionHighlights->SetPixels(FeaturesToSelect->Features, ITwin::COLOR_SELECTED_ELEMENT_BGRA);

		FeaturesToSelect->TextureFlags.SelectionFlags.Invalidate();
		bHasUpdatedTextures = true;

		SelectedElement = InElemID;

		//// Display the bounding box of the tile
		// if (World) DrawTileBox(World);
		return true;
	}
	else return false;
}

void FITwinSceneTile::UpdateSelectionTextureInMaterials(bool& bHasUpdatedTextures)
{
	if (!SelectionHighlights)
	{
		return;
	}
	SetupFeatureIdInfo();
	bool bHasCheckedTex = false;
	for (auto&& [ElemID, FeaturesInTile] : ElementsFeatures)
	{
		if (FeaturesInTile.TextureFlags.SelectionFlags.bNeedSetupTexture)
		{
			FITwinSceneMapping::SetupFeatureIdUVIndex(*this, FeaturesInTile);
			if (!bHasCheckedTex)
			{
				// Important: we must call UpdateTexture once, *before* updating materials, but if the update
				// was actually needed and effective, we need to postpone UpdateInMaterials to the next tick
				// (or maybe later, as discussed in FITwinSynchro4DAnimator::FImpl::ApplyAnimation...)
				if (SelectionHighlights->UpdateTexture())
				{
					bHasUpdatedTextures = true;
					return;
				}
				bHasCheckedTex = true;
			}
			SelectionHighlights->UpdateInMaterials(FeaturesInTile.Materials,
				*SelectionHighlightsInfo);
			FeaturesInTile.TextureFlags.SelectionFlags.OnTextureSetInMaterials(
				(int32)FeaturesInTile.Materials.size());
		}
	}
}

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
											   FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ensure(FeatureIdInfo))
	{
		for (auto& MatPtr : ElementFeaturesInTile.Materials)
		{
			if (MatPtr.IsValid())
			{
				auto const UVIndex = SceneTile.FeatureIDsUVIndex.find(MatPtr.Get());
				if (ensure(UVIndex != SceneTile.FeatureIDsUVIndex.end()))
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
void FITwinSceneMapping::SetupHighlightsOpacities(
	FITwinSceneTile& SceneTile,
	FITwinElementFeaturesInTile& ElementFeaturesInTile)
{
	if (ElementFeaturesInTile.Materials.empty())
		return;

	auto const& HighlightOpaFlags = ElementFeaturesInTile.TextureFlags.HighlightsAndOpacitiesFlags;
	bool const bNeedUpdateHighlightOpa = HighlightOpaFlags.ShouldUpdateMaterials(
		SceneTile.HighlightsAndOpacities, (int32)ElementFeaturesInTile.Materials.size());

	// NB: those FMaterialParameterInfo info no longer depend on the material, so we can setup them at once:
	if (bNeedUpdateHighlightOpa)
	{
		SetupHighlightsAndOpacitiesInfo();
	}
	SetupFeatureIdInfo();
	if (!bNeedUpdateHighlightOpa)
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
	auto const SetupElement = [&SceneTile, bNeedUpdateHighlightOpa]
		(FITwinElementFeaturesInTile& Element)
		{
			if (!Element.Materials.empty())
			{
				SetupFeatureIdUVIndex(SceneTile, Element);
				if (bNeedUpdateHighlightOpa)
				{
					SceneTile.HighlightsAndOpacities->UpdateInMaterials(Element.Materials,
																		*HighlightsAndOpacitiesInfo);
					Element.TextureFlags.HighlightsAndOpacitiesFlags.OnTextureSetInMaterials(
						(int32)Element.Materials.size());
				}
			}
		};
#if DEBUG_SYNCHRO4D_BGRA
	// Handle all elements, otherwise only materials used by animated elements will have the debug
	// colors, so you won't probably see much of anything...(you still won't get all tiles colored, only
	// those containing at least one animated Element)
	SceneTile.ForEachElementFeatures([&SetupElement](FITwinElementFeaturesInTile& ElementFeaturesInTile)
		{
			SetupElement(ElementFeaturesInTile);
		});
#else
	SetupElement(ElementFeaturesInTile);
#endif
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

FITwinElement& FITwinSceneMapping::ElementFor(ITwinScene::ElemIdx const IndexInVec)
{
	return AllElements[IndexInVec.value()];
}

FITwinElement& FITwinSceneMapping::ElementFor(ITwinElementID const ById,
											  ITwinScene::ElemIdx* IndexInVec/*= nullptr*/)
{
	auto Known = ElementsInVec.try_emplace(ById, AllElements.size());
	if (Known.second) // was inserted
	{
		if (IndexInVec) *IndexInVec = ITwinScene::ElemIdx(AllElements.size());
		return AllElements.emplace_back(FITwinElement{ false, ById });
	}
	else
	{
		if (IndexInVec) *IndexInVec = Known.first->second;
		return ElementFor(Known.first->second);
	}
}

int FITwinSceneMapping::ParseHierarchyTree(FString const& JsonStr)
{
	auto Reader = TJsonReaderFactory<TCHAR>::Create(JsonStr);
	TSharedPtr<FJsonObject> JsonObj;
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		return 0;
	TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
	if (!JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
		return 0;
	// TODO_GCO: successive queries can hardly optimize allocations, get count first with
	// "SELECT COUNT(*) FROM [Table]"
	AllElements.reserve(AllElements.size() + (size_t)JsonRows->Num());
	int TotalGoodRows = 0;
	for (auto const& Row : *JsonRows)
	{
		auto const& Entries = Row->AsArray();
		if (!ensure(Entries.Num() == 2))
			continue;
		ITwinElementID const ElemId = ITwin::ParseElementID(Entries[0]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ElemId))
			continue;
		ITwinScene::ElemIdx ChildInVec = ITwinScene::NOT_ELEM;
		FITwinElement& ChildElem = ElementFor(ElemId, &ChildInVec);
		if (!ensure(ITwinScene::NOT_ELEM == ChildElem.ParentInVec))
			continue; // already known - how come?!
		ITwinElementID const ParentId = ITwin::ParseElementID(Entries[1]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ParentId))
			continue;
		FITwinElement& ParentElem = ElementFor(ParentId, &ChildElem.ParentInVec);
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
			Elem.ParentInVec = ITwinScene::NOT_ELEM;
		UE_LOG(ITwinSceneMap, Error, TEXT("Loop found in iModel Elements hierarchy, it will be IGNORED!"));
		return 0;
	}
	return TotalGoodRows;
}

int FITwinSceneMapping::ParseSourceElementIDs(FString const& JsonStr)
{
	auto Reader = TJsonReaderFactory<TCHAR>::Create(JsonStr);
	TSharedPtr<FJsonObject> JsonObj;
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		return 0;
	TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
	if (!JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
		return 0;
	int TotalGoodRows = 0, EmptySourceId = 0;
	for (auto const& Row : *JsonRows)
	{
		auto const& Entries = Row->AsArray();
		if (!ensure(Entries.Num() == 2))
			continue;
		ITwinElementID const ElemId = ITwin::ParseElementID(Entries[0]->AsString());
		if (!ensure(ITwin::NOT_ELEMENT != ElemId))
			continue;
		FString const SourceId = Entries[1]->AsString();
		if (SourceId.IsEmpty())
		{
			++EmptySourceId;
			continue;
		}
		auto SourceEntry = SourceElementIDs.try_emplace(SourceId, ITwinScene::NOT_ELEM);
		ITwinScene::ElemIdx ThisElemIdx = ITwinScene::NOT_ELEM;
		auto& ThisElem = ElementFor(ElemId, &ThisElemIdx);
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
		++TotalGoodRows;
	}
	if (TotalGoodRows != JsonRows->Num() || 0 != EmptySourceId)
	{
		int const OtherErrors = (JsonRows->Num() - EmptySourceId) - TotalGoodRows;
		UE_LOG(ITwinSceneMap, Warning,
			TEXT("When parsing 'Source Element ID' metadata: out of %d entries received, %d were empty%s"),
			JsonRows->Num(), EmptySourceId,
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

size_t FITwinSceneMapping::UpdateAllTextures()
{
	auto const StartTime = FPlatformTime::Seconds();
	size_t TexCount = 0;
	for (auto&& [TileID, SceneTile] : KnownTiles)
	{
		// same as bVisibleOnly above: cannot use this optim for the moment because LOD changes will not
		// trigger a call of ApplyAnimation nor UpdateAllTextures
		//if (!SceneTile.HasAnyVisibleMesh())
		//	continue;
		if (SceneTile.HighlightsAndOpacities && SceneTile.HighlightsAndOpacities->UpdateTexture())
			++TexCount;
		if (SceneTile.CuttingPlanes && SceneTile.CuttingPlanes->UpdateTexture())
			++TexCount;
		if (SceneTile.SelectionHighlights && SceneTile.SelectionHighlights->UpdateTexture())
			++TexCount;
	}
	UE_LOG(ITwinSceneMap, Verbose, TEXT("Spent %dms in UpdateAllTextures, found %llu of them 'dirty'."),
		(int)(1000 * (FPlatformTime::Seconds() - StartTime)), TexCount);
	return TexCount;
}

bool FITwinSceneMapping::NewTilesReceivedHaveTextures(bool& bHasUpdatedTextures)
{
	if (bNewTilesReceivedHaveTextures)
	{
		bNewTilesReceivedHaveTextures = false;
		bNewTileTexturesNeedUpateInMaterials = true;
		if (UpdateAllTextures())
		{
			bHasUpdatedTextures = true;
		}
		return true;
	}
	else
		return false;
}

void FITwinSceneMapping::HandleNewTileTexturesNeedUpateInMaterials()
{
	if (bNewTileTexturesNeedUpateInMaterials)
	{
		bNewTileTexturesNeedUpateInMaterials = false;
		for (auto&& [TileID, SceneTile] : KnownTiles)
		{
			if (SceneTile.HighlightsAndOpacities && SceneTile.bNeedUpdateHighlightsAndOpacitiesInMaterials)
			{
				SetupHighlightsAndOpacitiesInfo();
				SceneTile.bNeedUpdateHighlightsAndOpacitiesInMaterials = false;
				SceneTile.HighlightsAndOpacities->UpdateInMaterials(SceneTile.Materials,
																	*HighlightsAndOpacitiesInfo);
			}
			if (SceneTile.CuttingPlanes && SceneTile.bNeedUpdateCuttingPlanesInMaterials)
			{
				SetupCuttingPlanesInfo();
				SceneTile.bNeedUpdateCuttingPlanesInMaterials = false;
				SceneTile.CuttingPlanes->UpdateInMaterials(SceneTile.Materials,
														   *CuttingPlanesInfo);
			}
		}
	}
}

/*static*/
void FITwinSceneMapping::SetupHighlightsOpacities(
	FITwinSceneTile& SceneTile,
	FITwinExtractedEntity& ExtractedEntity)
{
	auto const& HighlightOpaFlags = ExtractedEntity.TextureFlags.HighlightsAndOpacitiesFlags;
	bool const bNeedUpdateHighlightOpa =
		HighlightOpaFlags.ShouldUpdateMaterials(SceneTile.HighlightsAndOpacities, 1);
	if (!bNeedUpdateHighlightOpa || !ExtractedEntity.Material.IsValid())
	{
		if (ensure(ExtractedEntity.Material.IsValid()))
		{
			CheckMaterialSetup(*ExtractedEntity.Material.Get(), true,
				(bool)SceneTile.HighlightsAndOpacities, (bool)SceneTile.CuttingPlanes,
				(bool)SceneTile.SelectionHighlights);
		}
		return;
	}
	SetupHighlightsAndOpacitiesInfo();
	SetupFeatureIdInfo();
	SetupFeatureIdUVIndex(SceneTile, ExtractedEntity);
	if (bNeedUpdateHighlightOpa)
	{
		SceneTile.HighlightsAndOpacities->UpdateInMaterial(ExtractedEntity.Material,
			*HighlightsAndOpacitiesInfo);
		ExtractedEntity.TextureFlags.HighlightsAndOpacitiesFlags.OnTextureSetInMaterials(1);
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
	ElementFeaturesInTile.TextureFlags.CuttingPlaneFlags.OnTextureSetInMaterials(
		(int32)ElementFeaturesInTile.Materials.size());
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
	ExtractedEntity.TextureFlags.CuttingPlaneFlags.OnTextureSetInMaterials(1);
}

template<typename Container>
auto GatherTimelineElemInfos(FITwinSceneMapping& SceneMapping, FITwinSceneTile& SceneTile,
							 FITwinElementTimeline const& Timeline, Container const& TimelineElements)
{
	std::vector<std::pair<FITwinElementFeaturesInTile*, ITwinScene::ElemIdx>> TimelineElemInfos;
	TimelineElemInfos.reserve(TimelineElements.size());
	for (auto&& ElementID : TimelineElements)
	{
		FITwinElementFeaturesInTile* Found = SceneTile.FindElementFeatures(ElementID);
		if (Found)
		{
			ITwinScene::ElemIdx InVec = ITwinScene::NOT_ELEM;
			SceneMapping.ElementFor(ElementID, &InVec);
			TimelineElemInfos.push_back(std::make_pair(Found, InVec));
		}
	}
	return TimelineElemInfos;
}

bool FITwinSceneMapping::ReplicateAnimatedElementsSetupInTile(
	std::pair<CesiumTileID, std::set<ITwinElementID>> const& TileElements)
{
	auto SceneTileFound = KnownTiles.find(TileElements.first);
	if (!ensure(KnownTiles.end() != SceneTileFound))
		return false;
	auto& SceneTile = SceneTileFound->second;
	FTileRequirements TileReq = {
		(bool)SceneTile.HighlightsAndOpacities, /*bNeedHiliteAndOpaTex*/
		(bool)SceneTile.CuttingPlanes			/*bNeedCuttingPlaneTex*/
	};
	for (ITwinElementID const& ElemID : TileElements.second)
	{
		if (TileReq.bNeedHiliteAndOpaTex && TileReq.bNeedCuttingPlaneTex)
			break;
		auto const InVec = ElementsInVec.find(ElemID);
		if (ElementsInVec.end() != InVec)
		{
			ITwinScene::ElemIdx IdxInVec = InVec->second;
			do
			{
				auto const& Elem = GetElement(IdxInVec);
				TileReq.bNeedHiliteAndOpaTex |= Elem.TileRequirements.bNeedHiliteAndOpaTex;
				TileReq.bNeedCuttingPlaneTex |= Elem.TileRequirements.bNeedCuttingPlaneTex;
				// If animated by a Parent Element node, no specific animation will be received, we thus need
				// to traverse ancesters here:
				IdxInVec = Elem.ParentInVec;
			}
			while (IdxInVec != ITwinScene::NOT_ELEM);
		}
	}
	// Note: doesn't account for DEBUG_SYNCHRO4D_BGRA
	if (!SceneTile.HighlightsAndOpacities && TileReq.bNeedHiliteAndOpaTex)
	{
		// TODO_GCO: facto w/ OnElementsTimelineModified if kept there
		SceneTile.HighlightsAndOpacities = std::make_unique<FITwinDynamicShadingBGRA8Property>(
			SceneTile.MaxFeatureID, std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255)));
	}
	// Even if textures were already present, we'll have to UpdateInMaterials in all (new) materials
	SceneTile.bNeedUpdateHighlightsAndOpacitiesInMaterials = TileReq.bNeedHiliteAndOpaTex;

	if (!SceneTile.CuttingPlanes && TileReq.bNeedCuttingPlaneTex)
	{
		SceneTile.CuttingPlanes = std::make_unique<FITwinDynamicShadingABGR32fProperty>(
			SceneTile.MaxFeatureID, std::array<float, 4>(S4D_CLIPPING_DISABLED));
	}
	// Even if textures were already present, we'll have to UpdateInMaterials in all (new) materials
	SceneTile.bNeedUpdateCuttingPlanesInMaterials = TileReq.bNeedCuttingPlaneTex;

	// Redo it even if textures were already present: we may have received new materials
	if (SceneTile.HighlightsAndOpacities || SceneTile.CuttingPlanes)
		SceneTile.BakeFeatureIDsInVertexUVs();
	// Even if textures were already present, we'll have to UpdateInMaterials in all (new) materials
	bNewTilesReceivedHaveTextures |= SceneTile.bNeedUpdateHighlightsAndOpacitiesInMaterials
								  || SceneTile.bNeedUpdateCuttingPlanesInMaterials;
	return bNewTilesReceivedHaveTextures;
}

bool FITwinSceneMapping::OnElementsTimelineModified(CesiumTileID const& TileID,
	FITwinSceneTile& SceneTile, FITwinElementTimeline& ModifiedTimeline,
	std::vector<ITwinElementID> const* OnlyForElements/*= nullptr*/)
{
	bool bHasUpdatedTex = false;
	if (0 == ModifiedTimeline.NumKeyframes() || ITwin::NOT_FEATURE == SceneTile.MaxFeatureID)
	{
		return bHasUpdatedTex;
	}
	auto const TimelineElemInfos = OnlyForElements
		? GatherTimelineElemInfos(*this, SceneTile, ModifiedTimeline, *OnlyForElements)
		: GatherTimelineElemInfos(*this, SceneTile, ModifiedTimeline, ModifiedTimeline.GetIModelElements());
	// FITwinIModelInternals::OnElementsTimelineModified and UITwinSynchro4DSchedules::TickSchedules call us
	// for every SceneTile, even if it contains no Element affected by this timeline!
	if (TimelineElemInfos.empty())
		return bHasUpdatedTex;
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
	bool bIsAnyElementExtracted = false;
	if (bTimelineHasPartialVisibility || bTimelineHasTransformations)
	{
		for (auto const& ElemInfo : TimelineElemInfos)
		{
			FITwinElementFeaturesInTile* ElementInTile = ElemInfo.first;
			auto& ITwinElem = ElementFor(ElemInfo.second);
			ITwinElem.TileRequirements.bNeedTranslucentMaterial |= bTimelineHasPartialVisibility;
			ITwinElem.TileRequirements.bNeedBeTransformable |= bTimelineHasTransformations;
			bool bNeedSwitchToTranslucentMat = false;
			if (bTimelineHasPartialVisibility
				&& !ElementInTile->bHasTestedForTranslucentFeaturesNeedingExtraction)
			{
				bNeedSwitchToTranslucentMat = ElementInTile->HasOpaqueOrMaskedMaterial();
				ElementInTile->bHasTestedForTranslucentFeaturesNeedingExtraction = true;
			}
			if ((bTimelineHasTransformations || bNeedSwitchToTranslucentMat)
				&& !ElementInTile->bIsElementExtracted)
			{
				// Extract the Element in this tile, and assign it a translucent material if needed.
				FITwinMeshExtractionOptions ExtractOpts;
				ExtractOpts.SceneTile = &SceneTile;
				if (bNeedSwitchToTranslucentMat && ensure(MaterialGetter))
				{
					ExtractOpts.bCreateNewMaterialInstance = true;
					ExtractOpts.BaseMaterialForNewInstance =
						MaterialGetter(ECesiumMaterialType::Translucent);
					ExtractOpts.ScalarParameterToSet.emplace(
						// Extract invisible, will be reset in ApplyAnimation loop when its timeline is handled
						GetExtractedElementForcedAlphaMaterialParameterInfo(), 0.f);
				}
				if (bTimelineHasTransformations)
				{
					// Extract the Element in all tiles
					ExtractElement(ElementInTile->ElementID, ExtractOpts);
				}
				else
				{
					// Extract the Element just in this tile (will probably be needed in other
					// tiles afterwards, so this distinction is probably useless...)
					ExtractElementFromTile(ElementInTile->ElementID, SceneTile, ExtractOpts);
				}
			}
			bIsAnyElementExtracted |= ElementInTile->bIsElementExtracted;
		}
	}
	if (SceneTile.HighlightsAndOpacities
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SceneTile.HighlightsAndOpacities->GetTotalUsedPixels() < (SceneTile.MaxFeatureID.value() + 1))
	{
		ensure(false); // see comment on FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt
		SceneTile.HighlightsAndOpacities.reset(); // let's hope it doesn't crash everything...
		for (auto const& ElemInfo : TimelineElemInfos)
		{
			ElemInfo.first->bIsAlphaSetInTextureToHideExtractedElement = false;
			ElemInfo.first->TextureFlags.HighlightsAndOpacitiesFlags.Invalidate();// no cond on bTextureIsSet
		}
	}
	if (!SceneTile.HighlightsAndOpacities
		&& (!ModifiedTimeline.Color.Values.empty() || !ModifiedTimeline.Visibility.Values.empty()
			// For each Feature, masking it or not will depend on where exactly we are in the timeline right
			// now, but here we just want to know whether we create the texture or not, which is
			// time-independent:
			|| bIsAnyElementExtracted
			// when the cutting plane fully hides an object (after a 'Remove' or 'Temporary' task), the flag
			// can be used to mask out the object using the 'Mask' shader output, which is set when the
			// alpha/visibility is set to zero in HighlightsAndOpacities
			|| ModifiedTimeline.HasFullyHidingCuttingPlaneKeyframes()))
	{
		bHasUpdatedTex = true;
		SceneTile.HighlightsAndOpacities = std::make_unique<FITwinDynamicShadingBGRA8Property>(
			SceneTile.MaxFeatureID, std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255)));
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
		SceneTile.HighlightsAndOpacities->UpdateTexture();
	#endif

		// Bake feature IDs in per-vertex UVs if needed
		SceneTile.BakeFeatureIDsInVertexUVs();
	}
	if (SceneTile.CuttingPlanes
		// (TextureDimension^^2) would do and allow a small margin, but we assert against TotalUsedPixels...
		&& SceneTile.CuttingPlanes->GetTotalUsedPixels() < (SceneTile.MaxFeatureID.value() + 1))
	{
		ensure(false); // see comment on FITwinSynchro4DSchedulesInternals::OnNewTileMeshBuilt
		SceneTile.CuttingPlanes.reset(); // let's hope it doesn't crash everything...
		for (auto const& ElemInfo : TimelineElemInfos)
			ElemInfo.first->TextureFlags.CuttingPlaneFlags.Invalidate();// no cond on bTextureIsSet here
	}
	if (!SceneTile.CuttingPlanes && !ModifiedTimeline.ClippingPlane.Values.empty())
	{
		bHasUpdatedTex = true;
		SceneTile.CuttingPlanes = std::make_unique<FITwinDynamicShadingABGR32fProperty>(
			SceneTile.MaxFeatureID, std::array<float, 4>(S4D_CLIPPING_DISABLED));

		// Bake feature IDs in per-vertex UVs if needed
		SceneTile.BakeFeatureIDsInVertexUVs();
	}
	if (SceneTile.HighlightsAndOpacities || SceneTile.CuttingPlanes)
	{
		for (auto const& ElemInfo : TimelineElemInfos)
		{
			FITwinElementFeaturesInTile* ElementInTile = ElemInfo.first;
			// Propagate upwards as long as the nodes are marked as animated by the same timeline, ie up to
			// the originally animated parent node (because non-mesh nodes are not part of TimelineElemInfos!)
			FITwinElement* ITwinElem = &ElementFor(ElemInfo.second);
			while (true)
			{
				ITwinElem->TileRequirements.bNeedHiliteAndOpaTex |= (bool)SceneTile.HighlightsAndOpacities;
				ITwinElem->TileRequirements.bNeedCuttingPlaneTex |= (bool)SceneTile.CuttingPlanes;
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
			if (SceneTile.HighlightsAndOpacities)
			{
				if (ElementInTile->bIsElementExtracted
					&& !ElementInTile->bIsAlphaSetInTextureToHideExtractedElement)
				{
					// Ensure the parts that were extracted are made invisible in the original mesh
					SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElementInTile->Features, 0);
					ElementInTile->bIsAlphaSetInTextureToHideExtractedElement = true;
				}
				ElementInTile->TextureFlags.HighlightsAndOpacitiesFlags.InvalidateOnCondition(true);
			}
			ElementInTile->TextureFlags.CuttingPlaneFlags
				.InvalidateOnCondition((bool)SceneTile.CuttingPlanes);
		}
	}
	// Check the need for opaque/translucent materials didn't just arise for extracted Elements too, in case
	// for example the Element was already extracted for a transformation, ie without a Translucent material!
	if (bTimelineHasPartialVisibility || SceneTile.CuttingPlanes)
	{
		auto const OnExtractedElementTimelineChanged =
			[this, bTimelineHasPartialVisibility, &SceneTile]
			(FITwinExtractedEntity& ExtractedEntity)
			{
				bool bNeedSwitchToTranslucentMat = false;
				if (bTimelineHasPartialVisibility && ExtractedEntity.MeshComponent.IsValid())
				{
					bNeedSwitchToTranslucentMat = ExtractedEntity.HasOpaqueOrMaskedMaterial();
				}
				if (bNeedSwitchToTranslucentMat && ensure(MaterialGetter))
				{
					ensure(false);
					ExtractedEntity.SetBaseMaterial(MaterialGetter(ECesiumMaterialType::Translucent));
					SceneTile.Materials.push_back(ExtractedEntity.Material);
					ensureMsgf(!ExtractedEntity.HasOpaqueOrMaskedMaterial(),
						   TEXT("material should be translucent now"));
				}
				ExtractedEntity.TextureFlags.CuttingPlaneFlags.InvalidateOnCondition(
					(bool)SceneTile.CuttingPlanes);
			};
		if (OnlyForElements)
		{
			SceneTile.ForEachExtractedElement(*OnlyForElements, OnExtractedElementTimelineChanged);
		}
		else
		{
			SceneTile.ForEachExtractedElement(ModifiedTimeline.GetIModelElements(), 
											  OnExtractedElementTimelineChanged);
		}
	}
	return bHasUpdatedTex;
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

FBox const& FITwinSceneMapping::GetBoundingBox(ITwinElementID const Element) const
{
	auto const& Elem = GetElement(Element);
	if (ITwin::NOT_ELEMENT != Elem.Id)
	{
		return Elem.BBox;
	}
	// For a first (naive) implementation, the map of bounding boxes is
	// filled as soon as a mesh component is created, so if we don't have
	// it in cache, there is no chance we could compute it now...
	// Note that FITwinIModelInternals::HasElementWithID uses this assumption too for the moment.
	// We may change that in the future if it's too slow or consumes too
	// much memory, using a cache
	// TODO_GCO/TODO_JDE
	static FBox EmptyBox(ForceInit);
	return EmptyBox;
}

uint32 FITwinSceneMapping::CheckAndExtractElements(std::set<ITwinElementID> const& Elements,
												   bool const bTranslucencyNeeded)
{
	SetupHighlightsAndOpacitiesInfo();
	SetupSelectionHighlightsInfo();
	SetupCuttingPlanesInfo();
	SetupFeatureIdInfo();
    uint32 nbUEEntities = 0;
	FITwinMeshExtractionOptions ExtractOpts;
	ExtractOpts.bSetupMatForTileTexturesNow = true;
	if (bTranslucencyNeeded && ensure(MaterialGetter))
	{
		ExtractOpts.bCreateNewMaterialInstance = true;
		ExtractOpts.BaseMaterialForNewInstance =
			MaterialGetter(ECesiumMaterialType::Translucent);
		ExtractOpts.ScalarParameterToSet.emplace(
			// value doesn't matter, this is called from inside the ApplyAnimation loop anyway
			GetExtractedElementForcedAlphaMaterialParameterInfo(), 1.f);
	}
	for (auto&& Elem : Elements)
	{
		for (auto& [TileID, SceneTile] : KnownTiles)
		{
			auto const ElementFeaturesInTile = SceneTile.ElementFeatures(Elem);
			if (!ElementFeaturesInTile->second.bIsElementExtracted)
				nbUEEntities += ExtractElementFromTile(Elem, SceneTile, ExtractOpts, ElementFeaturesInTile);
		}
	}
    return nbUEEntities;
}

uint32 FITwinSceneMapping::ExtractElement(
    ITwinElementID const Element,
    FITwinMeshExtractionOptions const& Options /*= {}*/)
{
    uint32 nbUEEntities = 0;
    for (auto& [TileID, SceneTile] : KnownTiles)
    {
        nbUEEntities += ExtractElementFromTile(Element, SceneTile, Options);
    }
    return nbUEEntities;
}

uint32 FITwinSceneMapping::ExtractElementFromTile(ITwinElementID const Element, FITwinSceneTile& SceneTile,
    FITwinMeshExtractionOptions const& InOptions /*= {}*/,
	std::optional<std::unordered_map<ITwinElementID, FITwinElementFeaturesInTile>::iterator>
		ElementFeaturesInTile /*= {}*/)
{
	uint32 nbUEEntities = 0;
	// Beware several primitives in the tile can contain the element to extract. That's why we store a vector
	// in ExtractedEntityMap.
	std::optional<FITwinSceneTile::ExtractedEntityMap::iterator> EntitiesVecIt;
	FITwinMeshExtractionOptions Options = InOptions;
	Options.SceneTile = &SceneTile;
	for (FITwinGltfMeshComponentWrapper& gltfMeshData : SceneTile.GltfMeshes)
	{
		if (gltfMeshData.CanExtractElement(Element))
		{
			if (!EntitiesVecIt)
			{
				EntitiesVecIt.emplace(SceneTile.ExtractedElement(Element));
				(*EntitiesVecIt)->second.clear(); // just in case we had extracted an obsolete version
			}
			auto& ExtractedEntity = (*EntitiesVecIt)->second.emplace_back(
				FITwinExtractedEntity{ Element });
			if (gltfMeshData.ExtractElement(Element, ExtractedEntity, Options))
			{
				nbUEEntities++;
			}
			else
			{
				// Don't keep half constructed extracted entity
				(*EntitiesVecIt)->second.pop_back();
			}
		}
    }
	if (nbUEEntities > 0)
	{
		if (!ElementFeaturesInTile)
			ElementFeaturesInTile.emplace(SceneTile.ElementFeatures(Element));
		// Set a flag to mark this Element as extracted.
		(*ElementFeaturesInTile)->second.bIsElementExtracted = true;
		if (Options.bSetupMatForTileTexturesNow)
		{
			for (auto&& Extracted : (*EntitiesVecIt)->second)
			{
				SetupFeatureIdUVIndex(SceneTile, Extracted);
				if (SceneTile.HighlightsAndOpacities)
				{
					SceneTile.HighlightsAndOpacities->UpdateInMaterial(Extracted.Material,
						*HighlightsAndOpacitiesInfo);
					Extracted.TextureFlags.HighlightsAndOpacitiesFlags.OnTextureSetInMaterials(1);
				}
				if (SceneTile.CuttingPlanes)
				{
					SceneTile.CuttingPlanes->UpdateInMaterial(Extracted.Material, *CuttingPlanesInfo);
					Extracted.TextureFlags.CuttingPlaneFlags.OnTextureSetInMaterials(1);
				}
				if (SceneTile.SelectionHighlights)
				{
					SceneTile.SelectionHighlights->UpdateInMaterial(Extracted.Material,
						*SelectionHighlightsInfo);
					Extracted.TextureFlags.SelectionFlags.OnTextureSetInMaterials(1);
				}
			}
		}
	}
	if (EntitiesVecIt && (*EntitiesVecIt)->second.empty())
	{
		// No need to keep an empty entry in the map
		SceneTile.EraseExtractedElement(*EntitiesVecIt);
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

bool FITwinSceneMapping::SelectElement(ITwinElementID const& InElemID, UWorld const* World)
{
	if (InElemID == SelectedElement)
	{
		return false;
	}
	bool bSelectedInATile = false;
	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		bSelectedInATile |= SceneTile.SelectElement(InElemID, bNeedUpdateSelectionHighlights, World);
	}
	SelectedElement = InElemID;
	return bSelectedInATile;
}

void FITwinSceneMapping::UpdateSelectionAndHighlightTextures()
{
	// First update pending selection textures...
	if (bNeedUpdateSelectionHighlights)
	{
		bool bHasUpdatedTextures = false;
		for (auto& [TileID, SceneTile] : KnownTiles)
		{
			SceneTile.UpdateSelectionTextureInMaterials(bHasUpdatedTextures);
		}
		if (bHasUpdatedTextures)
		{
			// If textures were actually updated, then we could not update the materials at once and have
			// to postpone those material updates to the next tick...
			return;
		}
		bNeedUpdateSelectionHighlights = false;
	}
	// ...then only select the element in all tiles, so that new tiles or newly visible tiles are updated
	// (since we must wait the next frame to call UpdateSelectionTextureInMaterials...)
	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		SceneTile.SelectElement(SelectedElement, bNeedUpdateSelectionHighlights, nullptr);
	}
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
			DebugBoxNextLifetime = 5.f;
			SceneTile.DrawTileBox(World);
			return TileID;
		}
	}
#endif //ENABLE_DRAW_DEBUG
	return std::nullopt;
}

void FITwinSceneMapping::Reset()
{
	{ decltype(AllElements) Empty; Empty.swap(AllElements); }
	ElementsInVec.clear();
	SourceElementIDs.clear();
	{ decltype(DuplicateElements) Empty; Empty.swap(DuplicateElements); }
	KnownTiles.clear();
	bNeedUpdateSelectionHighlights = false;
	IModelBBox_ITwin = {};
	IModelBBox_UE = {};
	ModelCenter_ITwin.reset();
	ModelCenter_UE.reset();
	CesiumToUnrealTransform.reset();
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


#define DO_SET_CESIUMMAT_PARAM_VALUE(_paramName)	\
	MatInstance.SetScalarParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE), \
		Intensity);									\
	MatInstance.SetScalarParameterValueByInfo(		\
		FMaterialParameterInfo(_paramName, EMaterialParameterAssociation::LayerParameter, 0), \
		Intensity);


	auto const UpdateUnrealMatFunc = [&](UMaterialInstanceDynamic& MatInstance)
	{
		// This code depends on the actual graph used in Cesium
		// Highly incomplete code, only implemented coarsely for the MVP...
		switch (Channel)
		{
		case SDK::Core::EChannelType::Metallic:
			DO_SET_CESIUMMAT_PARAM_VALUE("metallicFactor");
			break;

		case SDK::Core::EChannelType::Roughness:
			DO_SET_CESIUMMAT_PARAM_VALUE("roughnessFactor");
			break;

		case SDK::Core::EChannelType::Transparency:
			SetBaseColorAlpha(MatInstance, 1.f - Intensity);
			break;

		case SDK::Core::EChannelType::Alpha:
			SetBaseColorAlpha(MatInstance, Intensity);
			break;

		default:
			// TODO_JDE complete / refactor this after the MVP
			ensureMsgf(false, TEXT("channel %u not implemented"), Channel);
			break;
		}
	};
#undef DO_SET_CESIUMMAT_PARAM_VALUE

	for (auto& [TileID, SceneTile] : KnownTiles)
	{
		SceneTile.ForEachMaterialInstanceMatchingID(ITwinMaterialID, UpdateUnrealMatFunc);
	}
}
