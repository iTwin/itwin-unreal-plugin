/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMappingBuilder.h"

#include <IncludeITwin3DTileset.h>
#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>
#include "ITwinMetadataPropertyAccess.h"
#include <ITwinSceneMapping.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Material/ITwinMaterialParameters.inl>
#include <Math/UEMathExts.h>

#include <CesiumFeatureIdSet.h>
#include <CesiumModelMetadata.h>
#include <CesiumPrimitiveFeatures.h>

#include <Cesium3DTilesSelection/TileContent.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltf/MeshPrimitive.h>

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
}

//=======================================================================================
// class FITwinSceneMappingBuilder
//=======================================================================================

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

void FITwinSceneMappingBuilder::OnMeshConstructed(Cesium3DTilesSelection::Tile& Tile,
	UStaticMeshComponent& MeshComponent, UMaterialInstanceDynamic& Material,
	FCesiumMeshData const& CesiumData)
{
	ITwinScene::TileIdx TileRank;
	auto& SceneMapping = GetInternals(IModel).SceneMapping;
	FITwinSceneTile& SceneTile = SceneMapping.KnownTileSLOW(Tile, &TileRank);

	const TObjectPtr<UStaticMesh> StaticMesh = CheckedGetStaticMesh(MeshComponent);

#if DEBUG_ITWIN_MATERIAL_IDS()
	auto const itBaseColorOverride = materialColorOverrides.find(&Material);
	if (itBaseColorOverride != materialColorOverrides.end())
	{
		Material.SetVectorParameterValueByInfo(
			FMaterialParameterInfo(
				"baseColorFactor",
				EMaterialParameterAssociation::GlobalParameter,
				INDEX_NONE),
			itBaseColorOverride->second);
		Material.SetVectorParameterValueByInfo(
			FMaterialParameterInfo(
				"baseColorFactor",
				EMaterialParameterAssociation::LayerParameter,
				0),
			itBaseColorOverride->second);
	}
#endif // DEBUG_ITWIN_MATERIAL_IDS

	ACesium3DTileset* Tileset = nullptr;
	// MeshComponent is a UCesiumGltfPrimitiveComponent, a USceneComponent which 'AttachParent' is
	// a UCesiumGltfComponent, which Owner is the cesium tileset, thus:
	if (ensure(MeshComponent.GetAttachParent()))
	{
		Tileset = Cast<ACesium3DTileset>(MeshComponent.GetAttachParent()->GetOwner());
		//ensure(Tileset == Cast<UCesiumGltfPrimitiveComponent PRIVATE!>(MeshComponent)->pTilesetActor);
	}
	if (!ensure(Tileset))
		return;
	// Note: geoloc must have been set before, MeshComponent.GetComponentTransform depends on it!
	// Note 2: despite GetComponentTransform's doc, tileset and iModel transforms are not accounted,
	// I think this is because the component is not attached yet: when it happens (see
	// "if (pGltf->GetAttachParent() == nullptr)" in ACesium3DTileset::showTilesToRender), the transform
	// of the pGltf and all its children primitive components are updated with the right value...
	auto const Transform = MeshComponent.GetComponentTransform();

	// always look in 1st set (_FEATURE_ID_0)
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FCesiumPrimitiveFeatures& Features(CesiumData.Features);
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
				Features,
				CesiumData.Metadata,
				MetaDataNames[i],
				FeatureIDSetIndex);
	}
	if (pProperties[to_underlying(EPropertyType::Element)] == nullptr)
	{
		return;
	}
	// Material IDs are stored in a separate table.
	const FCesiumPropertyTableProperty* pMaterialProp = FITwinMetadataPropertyAccess::FindValidProperty(
		Features, CesiumData.Metadata,
		ITwinCesium::Metada::MATERIAL_NAME,
		ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);

	// Add a wrapper for this GLTF mesh: used in case we need to extract sub-parts
	// matching a given ElementID (for Synchro4D animation), or if we need to bake
	// feature IDs in its vertex UVs.
	int32_t const GltfMeshWrapIdx = (int32_t)SceneTile.GltfMeshes.size();
	auto& Wrapper =
		SceneTile.GltfMeshes.emplace_back(FITwinGltfMeshComponentWrapper(MeshComponent, CesiumData));
	// Actual baking was already done in a background thread during cesium's loadPrimitive, thanks to the
	// UCesiumFeaturesMetadataComponent component we attach to the tileset - this here only updates the
	// scene tile's map:
	SceneMapping.SetupFeatureIDsInVertexUVs(SceneTile, Wrapper);
	// note that this has already been checked:
	// if no featureIDSet exists in features, pElementProperty would be null...
	const FCesiumFeatureIdSet& FeatureIdSet =
		UCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features)[FeatureIDSetIndex];

	const FStaticMeshLODResources& LODResources = StaticMesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PositionBuffer = LODResources.VertexBuffers.PositionVertexBuffer;

	bool bHasAddedMaterialToSceneTile = false;
	std::unordered_map<ITwinFeatureID, ITwinElementID> FeatureToElemID;
	std::unordered_set<ITwinScene::ElemIdx> MeshElemSceneRanks;
	const uint32 NumVertices = PositionBuffer.GetNumVertices();
	FITwinElement Dummy;
	FITwinElement* pElemStruct = &Dummy;
	ITwinElementID LastElem = ITwin::NOT_ELEMENT;
	ITwinFeatureID LastFeature = ITwin::NOT_FEATURE;
	for (uint32 vtxIndex(0); vtxIndex < NumVertices; vtxIndex++)
	{
		const int64 FeatureID =
			UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
				FeatureIdSet,
				static_cast<int64>(vtxIndex));
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
				ElementID = FeatureIDToITwinID<ITwinElementID>(pProperties[to_underlying(EPropertyType::Element)], FeatureID);
				if (ElementID != ITwin::NOT_ELEMENT)
				{
					if (ElementID != LastElem) // thus skipping below code 99% of the time
					{
						// fetch the CategoryID and ModelID corresponding to this feature
						ITwinElementID CategoryID =
							FeatureIDToITwinID<ITwinElementID>(pProperties[to_underlying(EPropertyType::Category)], FeatureID);
						CategoryID--;
						ITwinElementID const ModelID =
							FeatureIDToITwinID<ITwinElementID>(pProperties[to_underlying(EPropertyType::Model)], FeatureID);
						SceneMapping.CategoryIDToElementIDs[CategoryID].insert(ElementID);
						SceneMapping.ModelIDToElementIDs[ModelID].insert(ElementID);
						if (pProperties[to_underlying(EPropertyType::Geometry)])
						{
							uint8_t GeometryID =
								FeatureIDToITwinID<uint8_t>(pProperties[to_underlying(EPropertyType::Geometry)], FeatureID);
							SceneMapping.GeometryIDToElementIDs[GeometryID].insert(ElementID);
						}
					}
					// There can be duplicates here (as several primitives can have the
					// same features in a given tile) so we filter them here.
					// TODO_JDE: We should profile a bit, and see if we can use a set optimized for small
					// sizes? (Note: flat_set is based on std::vector by default, and its ordering requirement
					// probably makes it slower than a mere vector for our use case)
					pElemInTile = &SceneTile.ElementFeaturesSLOW(ElementID);
					if (std::find(pElemInTile->Features.begin(), pElemInTile->Features.end(), ITwinFeatID)
						== pElemInTile->Features.end())
					{
						pElemInTile->Features.push_back(ITwinFeatID);
					}
					// Each primitive is notified only once and has its own specific material instance,
					// BUT an ElementID can be found associated to different FeatureIDs (witnessed in
					// LumenRT_Synchro_002's GSW Stadium), which led to duplicate meshes and materials:
					if (std::find(pElemInTile->Meshes.begin(), pElemInTile->Meshes.end(), GltfMeshWrapIdx)
						== pElemInTile->Meshes.end())
					{
						pElemInTile->Meshes.push_back(GltfMeshWrapIdx);
						pElemInTile->Materials.push_back(&Material);
					}
					if (!bHasAddedMaterialToSceneTile)
					{
						SceneTile.Materials.push_back(&Material);
						bHasAddedMaterialToSceneTile = true;
					}
				}
				// Also fetch the (iTwin) Material ID related to this feature.
				ITwinMaterialID const MaterialID = pMaterialProp ? FeatureIDToITwinID<ITwinMaterialID>(
					pMaterialProp, FeatureID) : ITwin::NOT_MATERIAL;
				if (MaterialID != ITwin::NOT_MATERIAL)
				{
					FITwinMaterialFeaturesInTile& MatFeaturesInTile = SceneTile.MaterialFeaturesSLOW(MaterialID);
					if (std::find(MatFeaturesInTile.Features.begin(), MatFeaturesInTile.Features.end(), ITwinFeatID)
						== MatFeaturesInTile.Features.end())
					{
						MatFeaturesInTile.Features.push_back(ITwinFeatID);
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
				pElemStruct = &SceneMapping.ElementForSLOW(LastElem, &ElemRank);
				MeshElemSceneRanks.insert(ElemRank);
				if (pElemInTile)
					pElemInTile->SceneRank = ElemRank;
			}
			pElemStruct->bHasMesh = true;
			pElemStruct->BBox +=
				Transform.TransformPosition(FVector3d(PositionBuffer.VertexPosition(vtxIndex)));
		}
	}
	if (IModel.Synchro4DSchedules)
		GetInternals(*IModel.Synchro4DSchedules).OnNewTileMeshBuilt(TileRank, std::move(MeshElemSceneRanks));
}

void FITwinSceneMappingBuilder::OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile)
{
	GetInternals(IModel).OnNewTileBuilt(Tile.getTileID());
}

void FITwinSceneMappingBuilder::OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible)
{
	GetInternals(IModel).OnVisibilityChanged(TileID, visible);
}

FITwinSceneMappingBuilder::FITwinSceneMappingBuilder(AITwinIModel& InIModel)
	: IModel(InIModel)
{
}

void FITwinSceneMappingBuilder::SelectSchedulesBaseMaterial(
	UStaticMeshComponent const& MeshComponent, UMaterialInterface*& pBaseMaterial,
	FCesiumModelMetadata const& Metadata, FCesiumPrimitiveFeatures const& Features) const
{
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FCesiumPropertyTableProperty* PropTable = FITwinMetadataPropertyAccess::
		FindValidProperty(Features, Metadata, ITwinCesium::Metada::ELEMENT_NAME, FeatureIDSetIndex);
	if (!PropTable)
		return;
	// note that this has already been checked:
	// if no featureIDSet exists in features, pElementProperty would be null...
	const FCesiumFeatureIdSet& FeatureIdSet =
		UCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features)[FeatureIDSetIndex];
	const TObjectPtr<UStaticMesh> StaticMesh = CheckedGetStaticMesh(MeshComponent);
	if (0 >= StaticMesh->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices())
		return;
	const int64 FeatureID = UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(FeatureIdSet, 0);
	if (FeatureID < 0)
		return;
	// fetch the ElementID corresponding to this feature
	ITwinElementID const ElementID = FeatureIDToITwinID<ITwinElementID>(PropTable, FeatureID);
	if (ElementID == ITwin::NOT_ELEMENT)
		return;
	auto& SceneMapping = GetInternals(IModel).SceneMapping;
	auto const& Elem = SceneMapping.ElementForSLOW(ElementID);
	if (Elem.Requirements.bNeedTranslucentMat)
		pBaseMaterial = IModel.Synchro4DSchedules->BaseMaterialTranslucent;
}

UMaterialInstanceDynamic* FITwinSceneMappingBuilder::CreateMaterial_GameThread(
	Cesium3DTilesSelection::Tile const& Tile, UStaticMeshComponent const& MeshComponent,
	CesiumGltf::MeshPrimitive const* pMeshPrimitive, UMaterialInterface*& pBaseMaterial,
	FCesiumModelMetadata const& Metadata, FCesiumPrimitiveFeatures const& Features,
	UObject* InOuter, FName const& Name)
{
	auto const* Sched = IModel.Synchro4DSchedules;
	if (IsValid(Sched) && IsValid(Sched->BaseMaterialTranslucent)
		&& Sched->bUseGltfTunerInsteadOfMeshExtraction
		&& !(Sched->bDisableVisibilities || Sched->bDisablePartialVisibilities))
	{
		auto const MinTuneVer = GetInternals(*Sched).GetMinGltfTunerVersionForAnimation();
		if (auto* RenderContent = Tile.getContent().getRenderContent())
		{
			if (RenderContent->getModel()._tuneVersion >= MinTuneVer)
				SelectSchedulesBaseMaterial(MeshComponent, pBaseMaterial, Metadata, Features);
		}
	}
	std::optional<uint64_t> iTwinMaterialID;
	if (pMeshPrimitive)
	{
		auto const* matIdExt = pMeshPrimitive->getExtension<BeUtils::ExtensionITwinMaterialID>();
		if (matIdExt)
			iTwinMaterialID = matIdExt->materialId;
	}
	UMaterialInterface* CustomBaseMaterial = nullptr;
	// TODO_GCO: 4D animation can force to use BaseMaterialTranslucent: I guess BaseMaterialGlass is
	// compatible with translucency, but this is just luck. We may have to formalize the base material
	// process better in the future if we need to accomodate more varied use cases.
	if (iTwinMaterialID)
	{
		// Test whether we should use the Glass base material
		if (IModel.GetMaterialKind(*iTwinMaterialID) == AdvViz::SDK::EMaterialKind::Glass
			&& IsValid(Sched) && IsValid(Sched->BaseMaterialGlass))
		{
			CustomBaseMaterial = Sched->BaseMaterialGlass;
		}
		if (CustomBaseMaterial)
		{
			pBaseMaterial = CustomBaseMaterial;
		}
	}
	UMaterialInstanceDynamic* pMat =
		Super::CreateMaterial_GameThread(Tile, MeshComponent, pMeshPrimitive, pBaseMaterial,
										 Metadata, Features, InOuter, Name);

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


void FITwinSceneMappingBuilder::TuneMaterial(
	CesiumGltf::Material const& glTFmaterial,
	CesiumGltf::MaterialPBRMetallicRoughness const& /*pbr*/,
	UMaterialInstanceDynamic* pMaterial,
	EMaterialParameterAssociation association,
	int32 index) const
{
	// Implement the normal mapping and AO intensities.
	if (glTFmaterial.normalTexture)
	{
		pMaterial->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("normalFlatness"),
				association,
				index),
			static_cast<float>(1. - glTFmaterial.normalTexture->scale));
	}
	if (glTFmaterial.occlusionTexture)
	{
		pMaterial->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("occlusionTextureStrength"),
				association,
				index),
			static_cast<float>(glTFmaterial.occlusionTexture->strength));
	}
	auto const* iTwinMaterialExt = glTFmaterial.getExtension<BeUtils::ExtensionITwinMaterial>();
	if (iTwinMaterialExt)
	{
		pMaterial->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("specularFactor"),
				association,
				index),
			static_cast<float>(iTwinMaterialExt->specularFactor));
		// Color texture intensity slider (added lately).
		pMaterial->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("baseColorTextureFactor"),
				association,
				index),
			static_cast<float>(iTwinMaterialExt->baseColorTextureFactor));
	}
	auto const* uvTsfExt = glTFmaterial.getExtension<CesiumGltf::ExtensionKhrTextureTransform>();
	if (uvTsfExt)
	{
		ITwin::SetUVTransformInMaterialInstance(*uvTsfExt, *pMaterial, association, index);
	}
}

void FITwinSceneMappingBuilder::BeforeTileDestruction(
	const Cesium3DTilesSelection::Tile& Tile,
	USceneComponent* TileGltfComponent)
{
	GetInternals(IModel).UnloadKnownTile(Tile.getTileID());
}

// static
void FITwinSceneMappingBuilder::BuildFromNonCesiumMesh(FITwinSceneMapping& SceneMapping,
	const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
	uint64_t ITwinMaterialID)
{
	ensure(SceneMapping.KnownTiles.empty());

	ITwinScene::TileIdx const TileRank(0);
	auto& ByRank = SceneMapping.KnownTiles.get<IndexByRank>();
	auto const It = ByRank.emplace_back(Cesium3DTilesSelection::TileID("tileId0")).first;
	auto& SceneTile = const_cast<FITwinSceneTile&>(*It);

	const FCesiumModelMetadata NoMetadata;
	const FCesiumPrimitiveFeatures NoFeatures;
	FCesiumToUnrealTexCoordMap DummyGltfToUnrealTexCoordMap;

	auto& Wrapper = SceneTile.GltfMeshes.emplace_back(*MeshComponent.Get(),
		ICesiumMeshBuildCallbacks::FCesiumMeshData
		{
			nullptr,
			NoMetadata,
			NoFeatures,
			DummyGltfToUnrealTexCoordMap
		});
	Wrapper.EnforceITwinMaterialID(ITwinMaterialID);
}
