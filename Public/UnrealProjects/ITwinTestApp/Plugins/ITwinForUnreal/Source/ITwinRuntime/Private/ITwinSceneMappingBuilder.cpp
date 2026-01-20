/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMappingBuilder.h"

#include <IncludeCesium3DTileset.h>
#include <ITwinCesiumTileID.inl>
#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>
#include "ITwinMetadataPropertyAccess.h"
#include <ITwinSceneMapping.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>
#include <Material/ITwinMaterialParameters.inl>
#include <Math/UEMathExts.h>

#include <CesiumFeatureIdSet.h>
#include <CesiumMaterialUserData.h>
#include <CesiumModelMetadata.h>
#include <CesiumPrimitiveFeatures.h>

#include <Cesium3DTilesSelection/TileContent.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltfContent/GltfUtilities.h>
#include <CesiumGltfContent/SkirtMeshMetadata.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <StaticMeshResources.h>

#include <set>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/ExtensionITwinMaterial.h>
#	include <BeUtils/Gltf/ExtensionITwinMaterialID.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>

// Activate this to test the retrieval of iTwin material IDs by overriding the base color
// on a per material ID basis.
#define DEBUG_ITWIN_MATERIAL_IDS() 0


#if DEBUG_ITWIN_MATERIAL_IDS()
namespace
{
	// Temporary code to test material IDs
	static std::unordered_map<UMaterialInstanceDynamic*, FLinearColor> materialColorOverrides;
}
#endif

// From FITwinVecMath::createMatrix
static FMatrix CreateMatrixFromGlm(const glm::dmat4& m) noexcept
{
  return FMatrix(
	  FVector(m[0].x, m[0].y, m[0].z),
	  FVector(m[1].x, m[1].y, m[1].z),
	  FVector(m[2].x, m[2].y, m[2].z),
	  FVector(m[3].x, m[3].y, m[3].z));
}

namespace
{
	enum class EITwinPropertyType : uint8
	{
		Element,
		Category,
		Model,
		Geometry
	};

	template <typename T>
	T FeatureIDToITwinID(const FCesiumPropertyTableProperty* pProperty, const int64 FeatureID)
	{
		return T(
			FCesiumMetadataValueAccess::GetUnsignedInteger64(
				UCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
					*pProperty,
					FeatureID),
				ITwin::NOT_ELEMENT.value()));
	}

	template <typename E>
	constexpr typename std::underlying_type<E>::type to_underlying(E e) {
		return static_cast<typename std::underlying_type<E>::type>(e);
	}

	template<typename IDType, typename FeatureContainerFunc>
	void AddFeatureIfAbsent(IDType id, FeatureContainerFunc&& getContainer, const ITwinFeatureID& featID)
	{
		auto& features = getContainer(id).Features;
		if (std::find(features.begin(), features.end(), featID) == features.end())
		{
			features.push_back(featID);
		}
	}
}

//=======================================================================================
// class UITwinSceneMappingBuilder
//=======================================================================================

static
TObjectPtr<UStaticMesh> CheckedGetStaticMesh(UStaticMeshComponent const& MeshComponent)
{
	const TObjectPtr<UStaticMesh> StaticMesh = MeshComponent.GetStaticMesh();
	if (!StaticMesh
		|| !StaticMesh->GetRenderData()
		|| !StaticMesh->GetRenderData()->LODResources.IsValidIndex(0))
	{
		checkf(false, TEXT("incomplete mesh"));
		// should not happen with the version of cesium-unreal we initially
		// used - if you get there, it's probably that we upgraded the module
		// cesium-unreal, and that there are some substantial changes in the
		// way meshes are created for Unreal!
		return nullptr;
	}
	return StaticMesh;
}

void UITwinSceneMappingBuilder::OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim)
{
	ITwinScene::TileIdx TileRank;
	auto& SceneMapping = GetInternals(*IModel).SceneMapping;
	auto& LoadedTile = TilePrim.GetLoadedTile();
	FITwinSceneTile& SceneTile = SceneMapping.KnownTileSLOW(LoadedTile, &TileRank);

	auto& MeshComponent = TilePrim.GetMeshComponent();

	auto* ClippingHelper = IModel->GetClippingHelper();
	if (ClippingHelper)
	{
		// Set Custom Primitive Data needed to activate clipping effects.
		ClippingHelper->ApplyCPDFlagsToMeshComponent(MeshComponent);
	}

	const TObjectPtr<UStaticMesh> StaticMesh = CheckedGetStaticMesh(MeshComponent);
	auto* pMaterial = Cast<UMaterialInstanceDynamic>(StaticMesh->GetMaterial(0));
	if (!ensure(pMaterial))
		return;
#if DEBUG_ITWIN_MATERIAL_IDS()
	auto const itBaseColorOverride = materialColorOverrides.find(pMaterial);
	if (itBaseColorOverride != materialColorOverrides.end())
	{
		pMaterial->SetVectorParameterValueByInfo(
			FMaterialParameterInfo(
				"baseColorFactor",
				EMaterialParameterAssociation::GlobalParameter,
				INDEX_NONE),
			itBaseColorOverride->second);
		pMaterial->SetVectorParameterValueByInfo(
			FMaterialParameterInfo(
				"baseColorFactor",
				EMaterialParameterAssociation::LayerParameter,
				0),
			itBaseColorOverride->second);
	}
#endif // DEBUG_ITWIN_MATERIAL_IDS

	// Note: geoloc must have been set before, MeshComponent.GetComponentTransform depends on it!
	// Note 2: despite GetComponentTransform's doc, tileset and iModel transforms are not accounted,
	// I think this is because the component is not attached yet: when it happens (see
	// "if (pGltf->GetAttachParent() == nullptr)" in ACesium3DTileset::showTilesToRender), the transform
	// of the pGltf and all its children primitive components are updated with the right value...
	auto const Transform = MeshComponent.GetComponentTransform();

	// always look in 1st set (_FEATURE_ID_0)
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FCesiumPrimitiveFeatures& PrimitiveFeatures(TilePrim.GetPrimitiveFeatures());
	constexpr int MetaDataNamesCount = 4;
	static const std::array<FString, MetaDataNamesCount> MetaDataNames = {
		ITwinCesium::Metada::ELEMENT_NAME,
		ITwinCesium::Metada::SUBCATEGORY_NAME,
		ITwinCesium::Metada::MODEL_NAME,
		ITwinCesium::Metada::GEOMETRYCLASS_NAME
	};
	std::array<const FCesiumPropertyTableProperty*, MetaDataNamesCount> pProperties;
	for (int i = 0; i < MetaDataNamesCount; i++)
	{
		pProperties[i] =
			FITwinMetadataPropertyAccess::FindValidProperty(
				PrimitiveFeatures,
				LoadedTile.GetModelMetadata(),
				MetaDataNames[i],
				FeatureIDSetIndex);
	}
	if (pProperties[to_underlying(EITwinPropertyType::Element)] == nullptr)
	{
		return;
	}
	// Get index of UV layer into which Feature IDs were baked (formerly in FITwinGltfMeshComponentWrapper's
	// constructor)
	std::optional<uint32> uvIndexForFeatures;
	auto* pMeshPrimitive = TilePrim.GetMeshPrimitive();
	if (!ensure(pMeshPrimitive))
		return;
	auto featAccessorIt = pMeshPrimitive->attributes.find("_FEATURE_ID_0");
	if (featAccessorIt != pMeshPrimitive->attributes.end())
	{
		uvIndexForFeatures = TilePrim.FindTextureCoordinateIndexForGltfAccessor(featAccessorIt->second);
		if (uvIndexForFeatures && -1 == (*uvIndexForFeatures))
			uvIndexForFeatures.reset(); // we don't support "implicit FeatureID" (= vertex index)
	}
	ensure(uvIndexForFeatures);
	// Material IDs are stored in a separate table.
	const FCesiumPropertyTableProperty* pMaterialProp = FITwinMetadataPropertyAccess::FindValidProperty(
		TilePrim.GetPrimitiveFeatures(), LoadedTile.GetModelMetadata(),
		ITwinCesium::Metada::MATERIAL_NAME,
		ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);
	// Add a wrapper for this GLTF mesh: used in case we need to extract sub-parts
	// matching a given ElementID (for Synchro4D animation), or if we need to bake
	// feature IDs in its vertex UVs.
	int32_t const GltfMeshWrapIdx = (int32_t)SceneTile.GltfMeshes.size();
	auto& Wrapper =
		SceneTile.GltfMeshes.emplace_back(FITwinGltfMeshComponentWrapper(TilePrim, uvIndexForFeatures));
	// Actual baking was already done in a background thread during cesium's loadPrimitive, thanks to the
	// UCesiumFeaturesMetadataComponent component we attach to the tileset - this here only updates the
	// scene tile's map:
	SceneMapping.SetupFeatureIDsInVertexUVs(SceneTile, Wrapper);
	// note that this has already been checked:
	// if no featureIDSet exists in features, FindValidProperty would have returned null...
	const FCesiumFeatureIdSet& FeatureIdSet =
		UCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(PrimitiveFeatures)[FeatureIDSetIndex];

	CesiumGltf::Model const* Gltf = LoadedTile.GetGltfModel();
	if (!ensure(Gltf))
		return;
	auto PositionIt = pMeshPrimitive->attributes.find("POSITION");
	if (PositionIt == pMeshPrimitive->attributes.end())
		return;
	const int PositionAccessorIndex = PositionIt->second;
	if (PositionAccessorIndex < 0 || PositionAccessorIndex >= static_cast<int>(Gltf->accessors.size()))
		return;
	const CesiumGltf::AccessorView<glm::vec3> PositionView(*Gltf, PositionAccessorIndex);
	if (PositionView.status() != CesiumGltf::AccessorViewStatus::Valid)
		return;
	std::optional<CesiumGltfContent::SkirtMeshMetadata> SkirtMeshMetadata =
		CesiumGltfContent::SkirtMeshMetadata::parseFromGltfExtras(pMeshPrimitive->extras);
	int64_t VertexBegin = 0, VertexEnd = PositionView.size();
	if (SkirtMeshMetadata.has_value())
	{
		VertexBegin = SkirtMeshMetadata->noSkirtVerticesBegin;
		VertexEnd = SkirtMeshMetadata->noSkirtVerticesBegin +
			SkirtMeshMetadata->noSkirtVerticesCount;
	}
	FVector const PositionScaleFactor = LoadedTile.GetGltfToUnrealLocalVertexPositionScaleFactor();
	bool bHasAddedMaterialToSceneTile = false;
	std::unordered_map<ITwinFeatureID, ITwinElementID> FeatureToElemID;
	std::unordered_set<ITwinScene::ElemIdx> MeshElemSceneRanks;
	FITwinElement Dummy;
	FITwinElement* pElemStruct = &Dummy;
	ITwinElementID LastElem = ITwin::NOT_ELEMENT;
	ITwinFeatureID LastFeature = ITwin::NOT_FEATURE;
	for (int64_t VtxIndex = VertexBegin; VtxIndex < VertexEnd; ++VtxIndex)
	{
		const int64 FeatureID =
			UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
				FeatureIdSet,
				static_cast<int64>(VtxIndex));
		if (FeatureID < 0)
			continue;

		const ITwinFeatureID ITwinFeatID = ITwinFeatureID(FeatureID);
		FITwinElementFeaturesInTile* pElemInTile = nullptr;
		if (ITwinFeatID != LastFeature) // almost always the same => optimize
		{
			auto Known = FeatureToElemID.try_emplace(ITwinFeatID, ITwinElementID{});
			ITwinElementID& ElementID = Known.first->second;
			LastFeature = ITwinFeatID;
			// only record a given feature once (obviously, many vertices belong to a same feature...)
			if (Known.second) // was inserted -> first time encountered
			{
				if (ITwinFeatID > SceneTile.MaxFeatureID || SceneTile.MaxFeatureID == ITwin::NOT_FEATURE)
				{
					SceneTile.MaxFeatureID = ITwinFeatID;
				}
				// fetch the ElementID corresponding to this feature
				ElementID = FeatureIDToITwinID<ITwinElementID>(
					pProperties[to_underlying(EITwinPropertyType::Element)], FeatureID);
				if (ElementID != ITwin::NOT_ELEMENT)
				{
					if (ElementID != LastElem) // thus skipping below code 99% of the time
					{
						// fetch the CategoryID and ModelID corresponding to this feature
						ITwinElementID CategoryID = FeatureIDToITwinID<ITwinElementID>(
							pProperties[to_underlying(EITwinPropertyType::Category)], FeatureID);
						CategoryID--;
						ITwinElementID const ModelID = FeatureIDToITwinID<ITwinElementID>(
							pProperties[to_underlying(EITwinPropertyType::Model)], FeatureID);
						SceneMapping.CategoryIDToElementIDs[CategoryID].insert(ElementID);
						SceneMapping.ModelIDToElementIDs[ModelID].insert(ElementID);
						if (pProperties[to_underlying(EITwinPropertyType::Geometry)])
						{
							uint8_t GeometryID = FeatureIDToITwinID<uint8_t>(
								pProperties[to_underlying(EITwinPropertyType::Geometry)], FeatureID);
							SceneMapping.GeometryIDToElementIDs[GeometryID].insert(ElementID);
						}
					}
					// There can be duplicates here (as several primitives can have the
					// same features in a given tile) so we filter them here.
					// TODO_JDE: We should profile a bit, and see if we can use a set optimized for small
					// sizes? (Note: flat_set is based on std::vector by default, and its ordering requirement
					// probably makes it slower than a mere vector for our use case)
					AddFeatureIfAbsent(ElementID,
						[&SceneTile, &pElemInTile](const ITwinElementID& Id) -> FITwinElementFeaturesInTile& {
							pElemInTile = &SceneTile.ElementFeaturesSLOW(Id);
							return *pElemInTile;
						},
						ITwinFeatID);
					// Each primitive is notified only once and has its own specific material instance,
					// BUT an ElementID can be found associated to different FeatureIDs (witnessed in
					// LumenRT_Synchro_002's GSW Stadium), which led to duplicate meshes and materials:
					if (std::find(pElemInTile->Meshes.begin(), pElemInTile->Meshes.end(), GltfMeshWrapIdx)
						== pElemInTile->Meshes.end())
					{
						pElemInTile->Meshes.push_back(GltfMeshWrapIdx);
						pElemInTile->Materials.push_back(pMaterial);
					}
					if (!bHasAddedMaterialToSceneTile)
					{
						SceneTile.Materials.push_back(pMaterial);
						bHasAddedMaterialToSceneTile = true;
					}
				}
				// Also fetch the (iTwin) Material ID related to this feature.
				ITwinMaterialID const MaterialID = pMaterialProp ? FeatureIDToITwinID<ITwinMaterialID>(
					pMaterialProp, FeatureID) : ITwin::NOT_MATERIAL;
				if (MaterialID != ITwin::NOT_MATERIAL)
				{
					AddFeatureIfAbsent(MaterialID,
						[&SceneTile](const ITwinMaterialID& Id) -> FITwinMaterialFeaturesInTile& {
							return SceneTile.MaterialFeaturesSLOW(Id);
						},
						ITwinFeatID);
				}
				// Fetch the Model ID related to this feature.
				ITwinElementID const modelID = FeatureIDToITwinID<ITwinElementID>(
					pProperties[to_underlying(EITwinPropertyType::Model)], FeatureID);
				if (modelID != ITwin::NOT_ELEMENT)
				{
					AddFeatureIfAbsent(modelID,
						[&SceneTile](const ITwinElementID& Id) -> FITwinModelFeaturesInTile& {
							return SceneTile.ModelFeaturesSLOW(Id);
						},
						ITwinFeatID);
				}
				// Fetch the Category ID and CategoryPerModel ID related to this feature.
				ITwinElementID categoryID = FeatureIDToITwinID<ITwinElementID>(
					pProperties[to_underlying(EITwinPropertyType::Category)], FeatureID);
				categoryID--;
				if (categoryID != ITwin::NOT_ELEMENT)
				{
					AddFeatureIfAbsent(categoryID,
						[&SceneTile](const ITwinElementID& Id) -> FITwinCategoryFeaturesInTile& {
							return SceneTile.CategoryFeaturesSLOW(Id);
						},
						ITwinFeatID);
					if (modelID != ITwin::NOT_ELEMENT)
					{
						auto& features = SceneTile.CategoryPerModelFeaturesSLOW(categoryID, modelID).Features;
						if (std::find(features.begin(), features.end(), ITwinFeatID) == features.end())
						{
							features.push_back(ITwinFeatID);
						}
					}
				}
			}
			LastElem = ElementID;
		}
		// update BBox with each new vertex position
		if (LastElem != ITwin::NOT_ELEMENT)
		{
			// Almost always the same => optimize
			if (LastElem != pElemStruct->ElementID)
			{
				ITwinScene::ElemIdx ElemRank;
				// Safe to use ElementForSLOW here: we are in game thread
				pElemStruct = &SceneMapping.ElementForSLOW(LastElem, &ElemRank);
				MeshElemSceneRanks.insert(ElemRank);
				if (pElemInTile)
					pElemInTile->SceneRank = ElemRank;
			}
			pElemStruct->bHasMesh = true;
			glm::vec3 const& Position = PositionView[VtxIndex];
			pElemStruct->BBox += Transform.TransformPosition(
				PositionScaleFactor * FVector(Position.x, Position.y, Position.z));
		}
	}
	if (IModel->Synchro4DSchedules)
		GetInternals(*IModel->Synchro4DSchedules).OnNewTileMeshBuilt(TileRank, std::move(MeshElemSceneRanks));
}

void UITwinSceneMappingBuilder::OnTileLoaded(ICesiumLoadedTile& LoadedTile)
{
	GetInternals(*IModel).OnNewTileBuilt(ITwin::GetCesiumTileID(LoadedTile));
}

void UITwinSceneMappingBuilder::OnTileVisibilityChanged(ICesiumLoadedTile& LoadedTile, bool visible)
{
	GetInternals(*IModel).OnVisibilityChanged(ITwin::GetCesiumTileID(LoadedTile), visible);
}

void UITwinSceneMappingBuilder::SetIModel(AITwinIModel& InIModel)
{
	IModel = &InIModel;
}

void UITwinSceneMappingBuilder::SelectSchedulesBaseMaterial(
	UStaticMeshComponent const& MeshComponent, UMaterialInterface*& pBaseMaterial,
	FCesiumModelMetadata const& Metadata, FCesiumPrimitiveFeatures const& Features) const
{
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FCesiumPropertyTableProperty* PropTable = FITwinMetadataPropertyAccess::
		FindValidProperty(Features, Metadata, ITwinCesium::Metada::ELEMENT_NAME, FeatureIDSetIndex);
	if (!PropTable)
		return;
	// note that this has already been checked:
	// if no featureIDSet exists in features, PropTable would be null...
	const FCesiumFeatureIdSet& FeatureIdSet =
		UCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features)[FeatureIDSetIndex];
	// No need to check that, GetFeatureIDForVertex does it and returns -1 for an empty mesh
	//if (0 >= (...)->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices()) return;
	const int64 FeatureID = UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(FeatureIdSet, 0);
	if (FeatureID < 0)
		return;
	// fetch the ElementID corresponding to this feature
	ITwinElementID const ElementID = FeatureIDToITwinID<ITwinElementID>(PropTable, FeatureID);
	if (ElementID == ITwin::NOT_ELEMENT)
		return;
	auto& SceneMapping = GetInternals(*IModel).SceneMapping;
	// Safe to use ElementForSLOW here: we are in game thread
	auto const& Elem = SceneMapping.ElementForSLOW(ElementID);
	if (Elem.Requirements.bNeedTranslucentMat)
		pBaseMaterial = IModel->Synchro4DSchedules->BaseMaterialTranslucent;
}

UMaterialInstanceDynamic* UITwinSceneMappingBuilder::CreateMaterial(ICesiumLoadedTilePrimitive& TilePrim,
	UMaterialInterface* pBaseMaterial, FName const& Name)
{
	auto const* Sched = IModel->Synchro4DSchedules;
	if (IsValid(Sched) && IsValid(Sched->BaseMaterialTranslucent)
		&& Sched->bUseGltfTunerInsteadOfMeshExtraction
		&& !(Sched->bDisableVisibilities || Sched->bDisablePartialVisibilities))
	{
		auto const& LoadedTile = TilePrim.GetLoadedTile();
		auto const MinTuneVer = GetInternals(*Sched).GetMinGltfTunerVersionForAnimation();
		if (auto* Model = LoadedTile.GetGltfModel())
		{
			if (Model->version && (*Model->version) >= MinTuneVer)
			{
				SelectSchedulesBaseMaterial(TilePrim.GetMeshComponent(), pBaseMaterial,
					LoadedTile.GetModelMetadata(), TilePrim.GetPrimitiveFeatures());
			}
		}
	}
	std::optional<uint64_t> iTwinMaterialID;
	if (auto MeshPrim = TilePrim.GetMeshPrimitive())
	{
		auto const* matIdExt = MeshPrim->getExtension<BeUtils::ExtensionITwinMaterialID>();
		if (matIdExt)
			iTwinMaterialID = matIdExt->materialId;
	}
	UMaterialInterface* CustomBaseMaterial = nullptr;
	// TODO_GCO: 4D animation can force to use BaseMaterialTranslucent: I guess BaseMaterialGlass is
	// compatible with translucency, but this is just luck. We may have to formalize the base material
	// process better in the future if we need to accommodate more varied use cases.
	if (iTwinMaterialID && IsValid(Sched))
	{
		// Test whether we should use the Glass base material.
		// Also, when the user turns an opaque material to half-transparent from the Material Editor, we will
		// use the 2-sided version of the translucent material, to make sure some parts of the mesh will not
		// turn instantly invisible (even with 99% opacity ;-))
		// See https://dev.azure.com/bentleycs/e-onsoftware/_workitems/edit/1539818 for the full history...
		AdvViz::SDK::EMaterialKind matKind = AdvViz::SDK::EMaterialKind::PBR;
		bool bCustomDefinitionRequiresTranslucency = false;
		if (IModel->GetMaterialCustomRequirements(*iTwinMaterialID, matKind, bCustomDefinitionRequiresTranslucency))
		{
			if (matKind == AdvViz::SDK::EMaterialKind::Glass)
			{
				if (IsValid(Sched->BaseMaterialGlass))
					CustomBaseMaterial = Sched->BaseMaterialGlass;
			}
			else if (bCustomDefinitionRequiresTranslucency)
			{
				if (IsValid(Sched->BaseMaterialTranslucent_TwoSided))
					CustomBaseMaterial = Sched->BaseMaterialTranslucent_TwoSided;
			}
		}
		if (CustomBaseMaterial)
		{
			pBaseMaterial = CustomBaseMaterial;
		}
	}
	UMaterialInstanceDynamic* pMat = ICesium3DTilesetLifecycleEventReceiver::CreateMaterial(
		TilePrim, pBaseMaterial, Name);

#if DEBUG_ITWIN_MATERIAL_IDS()
	if (iTwinMaterialID && !CustomBaseMaterial)
	{
		// Temporary code to visualize iTwin material IDs
		const auto MatID = *iTwinMaterialID;
		static std::unordered_map<uint64_t, FLinearColor> matIDColorMap;
		FLinearColor matColor;
		auto const itClr = matIDColorMap.find(MatID);
		if (itClr != matIDColorMap.end())
		{
			matColor = itClr->second;
		}
		else
		{
			matColor = (*iTwinMaterialID == 0) ? FLinearColor::White : FLinearColor::MakeRandomColor();
			matColor.A = 1.;
			matIDColorMap.emplace(MatID, matColor);
		}
		materialColorOverrides.emplace(pMat, matColor);
	}
#endif // DEBUG_ITWIN_MATERIAL_IDS

	return pMat;
}

void UITwinSceneMappingBuilder::CustomizeMaterial(ICesiumLoadedTilePrimitive& TilePrim,
	UMaterialInstanceDynamic& Material, const UCesiumMaterialUserData* pCesiumData,
	CesiumGltf::Material const& glTFmaterial)
{
	if (!pCesiumData || pCesiumData->LayerNames.IsEmpty())
		return;
	// Implement the normal mapping and AO intensities.
	if (glTFmaterial.normalTexture)
	{
		Material.SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("normalFlatness"),
				EMaterialParameterAssociation::LayerParameter,
				0),
			static_cast<float>(1. - glTFmaterial.normalTexture->scale));
	}
	if (glTFmaterial.occlusionTexture)
	{
		Material.SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("occlusionTextureStrength"),
				EMaterialParameterAssociation::LayerParameter,
				0),
			static_cast<float>(glTFmaterial.occlusionTexture->strength));
	}
	auto const* iTwinMaterialExt = glTFmaterial.getExtension<BeUtils::ExtensionITwinMaterial>();
	if (iTwinMaterialExt)
	{
		Material.SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("specularFactor"),
				EMaterialParameterAssociation::LayerParameter,
				0),
			static_cast<float>(iTwinMaterialExt->specularFactor));
		// Color texture intensity slider (added lately).
		Material.SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("baseColorTextureFactor"),
				EMaterialParameterAssociation::LayerParameter,
				0),
			static_cast<float>(iTwinMaterialExt->baseColorTextureFactor));
	}
	auto const* uvTsfExt = glTFmaterial.getExtension<CesiumGltf::ExtensionKhrTextureTransform>();
	if (uvTsfExt)
	{
		ITwin::SetUVTransformInMaterialInstance(*uvTsfExt, Material,
												EMaterialParameterAssociation::LayerParameter, 0);
	}
}

void UITwinSceneMappingBuilder::OnTileUnloading(ICesiumLoadedTile& LoadedTile)
{
	GetInternals(*IModel).UnloadKnownTile(ITwin::GetCesiumTileID(LoadedTile));
}

// static
void UITwinSceneMappingBuilder::BuildFromNonCesiumMesh(FITwinSceneMapping& SceneMapping,
	const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent, uint64_t ITwinMaterialID)
{
	ensure(SceneMapping.KnownTiles.empty());

	ITwinScene::TileIdx const TileRank(0);
	auto& ByRank = SceneMapping.KnownTiles.get<IndexByRank>();
	auto const It = ByRank.emplace_back(CesiumTileID{"tileId0", ""}).first;
	auto& SceneTile = const_cast<FITwinSceneTile&>(*It);
	SceneTile.GltfMeshes.emplace_back(*MeshComponent.Get(), ITwinMaterialID);
}
