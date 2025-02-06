/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingBuilder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSceneMappingBuilder.h"

#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>
#include <ITwinSceneMapping.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <Material/ITwinMaterialParameters.inl>
#include <Math/UEMathExts.h>

#include <ITwinCesium3DTileset.h>
#include <ITwinCesiumFeatureIdSet.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumModelMetadata.h>
#include <ITwinCesiumPrimitiveFeatures.h>

#include <CesiumGltf/ExtensionITwinMaterial.h>
#include <CesiumGltf/ExtensionITwinMaterialID.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltf/MeshPrimitive.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <StaticMeshResources.h>

#include <set>

#include <Compil/BeforeNonUnrealIncludes.h>
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
	T FeatureIDToITwinID(const FITwinCesiumPropertyTableProperty* pProperty, const int64 FeatureID)
	{
		return T(
			UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
				UITwinCesiumPropertyTablePropertyBlueprintLibrary::GetValue(
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

void FITwinSceneMappingBuilder::OnMeshConstructed(
	Cesium3DTilesSelection::Tile& Tile,
	const TWeakObjectPtr<UStaticMeshComponent>& MeshComponent,
	const TWeakObjectPtr<UMaterialInstanceDynamic>& pMaterial,
	const FITwinCesiumMeshData& CesiumData)
{
	ITwinScene::TileIdx TileRank;
	auto& SceneMapping = GetInternals(IModel).SceneMapping;
	FITwinSceneTile& SceneTile = SceneMapping.KnownTileSLOW(Tile, &TileRank);

	const TObjectPtr<UStaticMesh> StaticMesh = MeshComponent->GetStaticMesh();
	if (!StaticMesh
		|| !StaticMesh->GetRenderData()
		|| !StaticMesh->GetRenderData()->LODResources.IsValidIndex(0))
	{
		checkf(false, TEXT("incomplete mesh"));
		// should not happen with the version of cesium-unreal we initially
		// used - if you get there, it's probably that we upgraded the module
		// cesium-unreal, and that there are some substantial changes in the
		// way meshes are created for Unreal!
		return;
	}

#if DEBUG_ITWIN_MATERIAL_IDS()
	auto const itBaseColorOverride = materialColorOverrides.find(pMaterial.Get());
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

	AITwinCesium3DTileset* Tileset = nullptr;
	// MeshComponent is a UITwinCesiumGltfPrimitiveComponent, a USceneComponent which 'AttachParent' is
	// a UITwinCesiumGltfComponent, which Owner is the cesium tileset, thus:
	if (ensure(MeshComponent->GetAttachParent()))
	{
		Tileset = Cast<AITwinCesium3DTileset>(MeshComponent->GetAttachParent()->GetOwner());
		//ensure(Tileset == Cast<UITwinCesiumGltfPrimitiveComponent PRIVATE!>(MeshComponent)->pTilesetActor);
	}
	if (!ensure(Tileset))
		return;
	// Note: geoloc must have been set before, MeshComponent->GetComponentTransform depends on it!
	// Note 2: despite GetComponentTransform's doc, tileset and iModel transforms are not accounted!!
	auto const Transform = MeshComponent->GetComponentTransform();

	// always look in 1st set (_FEATURE_ID_0)
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FITwinCesiumPrimitiveFeatures& Features(CesiumData.Features);
	constexpr int MetaDataNamesCount = 4;
	std::array<FString, MetaDataNamesCount> MetaDataNames = {
		ITwinCesium::Metada::ELEMENT_NAME,
		ITwinCesium::Metada::SUBCATEGORY_NAME,
		ITwinCesium::Metada::MODEL_NAME,
		ITwinCesium::Metada::GEOMETRYCLASS_NAME
	};
	std::array<const FITwinCesiumPropertyTableProperty*, MetaDataNamesCount> pProperties;
	for (int i = 0; i < MetaDataNamesCount; i++)
	{
		pProperties[i] =
			UITwinCesiumMetadataPickingBlueprintLibrary::FindValidProperty(
				Features,
				CesiumData.Metadata,
				MetaDataNames[i],
				FeatureIDSetIndex);
	}
	if (pProperties[to_underlying(EPropertyType::Element)] == nullptr)
	{
		return;
	}

	// Add a wrapper for this GLTF mesh: used in case we need to extract sub-parts
	// matching a given ElementID (for Synchro4D animation), or if we need to bake
	// feature IDs in its vertex UVs.
	auto& Wrapper = SceneTile.GltfMeshes.emplace_back(MeshComponent, CesiumData);
	// Actual baking was already done in a background thread when we made cesium's loadPrimitive call
	// FITwinSceneMappingBuilder::BakeFeatureIDsInVertexUVs, this here only updates the scene tile's map:
	SceneMapping.SetupFeatureIDsInVertexUVs(SceneTile, Wrapper);
	// note that this has already been checked:
	// if no featureIDSet exists in features, pElementProperty would be null...
	const FITwinCesiumFeatureIdSet& FeatureIdSet =
		UITwinCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(Features)[FeatureIDSetIndex];

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
			UITwinCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
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
					if (ElementID != LastElem) // actually the case 99% of the time
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
					// The material is always different (each primitive uses its own material instance).
					pElemInTile->Materials.push_back(pMaterial);

					if (!bHasAddedMaterialToSceneTile)
					{
						SceneTile.Materials.push_back(pMaterial);
						bHasAddedMaterialToSceneTile = true;
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
		GetInternals(*IModel.Synchro4DSchedules).OnNewTileMeshBuilt(
			TileRank, std::move(MeshElemSceneRanks), pMaterial, SceneTile);
}

void FITwinSceneMappingBuilder::OnTileConstructed(const Cesium3DTilesSelection::Tile& Tile)
{
	GetInternals(IModel).OnNewTileBuilt(Tile.getTileID());
}

void FITwinSceneMappingBuilder::OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible)
{
	GetInternals(IModel).OnVisibilityChanged(TileID, visible);
}

std::optional<uint32> FITwinSceneMappingBuilder::BakeFeatureIDsInVertexUVs(
	std::optional<uint32> featuresAccessorIndex,
	ICesiumMeshBuildCallbacks::FITwinCesiumMeshData const& CesiumMeshData,
	bool duplicateVertices,
	TArray<FStaticMeshBuildVertex>& vertices,
	TArray<uint32> const& indices) const
{
	return FITwinGltfMeshComponentWrapper::BakeFeatureIDsInVertexUVs(
		featuresAccessorIndex, CesiumMeshData, duplicateVertices, vertices, indices);
}

FITwinSceneMappingBuilder::FITwinSceneMappingBuilder(AITwinIModel& InIModel)
	: IModel(InIModel)
{
}

UMaterialInstanceDynamic* FITwinSceneMappingBuilder::CreateMaterial_GameThread(
	CesiumGltf::MeshPrimitive const* pMeshPrimitive,
	UMaterialInterface*& pBaseMaterial,
	UObject* InOuter,
	FName const& Name) {
	std::optional<uint64_t> iTwinMaterialID;
	if (pMeshPrimitive)
	{
		auto const* matIdExt = pMeshPrimitive->getExtension<CesiumGltf::ExtensionITwinMaterialID>();
		if (matIdExt)
			iTwinMaterialID = matIdExt->materialId;
	}
	UMaterialInterface* CustomBaseMaterial = nullptr;
	if (iTwinMaterialID)
	{
        // Test whether we should use the Glass base material
        if (IModel.GetMaterialKind(*iTwinMaterialID) == SDK::Core::EMaterialKind::Glass
            && IsValid(IModel.Synchro4DSchedules)
            && IsValid(IModel.Synchro4DSchedules->BaseMaterialGlass))
        {
            CustomBaseMaterial = IModel.Synchro4DSchedules->BaseMaterialGlass;
        }

		if (CustomBaseMaterial)
		{
			pBaseMaterial = CustomBaseMaterial;
		}
	}
	UMaterialInstanceDynamic* pMat = Super::CreateMaterial_GameThread(pMeshPrimitive, pBaseMaterial, InOuter, Name);

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
	auto const* iTwinMaterialExt = glTFmaterial.getExtension<CesiumGltf::ExtensionITwinMaterial>();
	if (iTwinMaterialExt)
	{
		pMaterial->SetScalarParameterValueByInfo(
			FMaterialParameterInfo(
				TEXT("specularFactor"),
				association,
				index),
			static_cast<float>(iTwinMaterialExt->specularFactor));
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
	GetInternals(IModel).SceneMapping.UnloadKnownTile(Tile.getTileID());
}
