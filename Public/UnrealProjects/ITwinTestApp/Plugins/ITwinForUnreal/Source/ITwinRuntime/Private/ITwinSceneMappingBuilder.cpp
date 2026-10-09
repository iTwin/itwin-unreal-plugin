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
#include <Clipping/ITwinClipping3DTilesetHelper.h>
#include <Compil/IsUsingBentleyUnreal.h>
#include <Material/ITwinMaterialParameters.inl>
#include <Math/UEMathExts.h>

#include <CesiumFeatureIdSet.h>
#include <CesiumMaterialUserData.h>
#include <CesiumModelMetadata.h>
#include <CesiumPrimitiveFeatures.h>

#include <Cesium3DTilesSelection/GltfModifierVersionExtension.h>
#include <Cesium3DTilesSelection/TileContent.h>
#include <CesiumGltf/ExtensionKhrTextureTransform.h>
#include <CesiumGltf/ExtensionExtMeshFeatures.h>
#include <CesiumGltf/ExtensionModelExtStructuralMetadata.h>
#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltf/PropertyTableView.h>
#include <CesiumGltfContent/GltfUtilities.h>
#include <CesiumGltfContent/SkirtMeshMetadata.h>

// Include the full definition of LoadedPrimitiveResult for worker thread callback
#include "../../CesiumRuntime/Private/CesiumEncodedMetadataUtility.h"
#include "../../CesiumRuntime/Private/LoadGltfResult.h"

#if BE_IS_USING_BENTLEY_UNREAL
#include <Chaos/TriangleMeshImplicitObject.h>
#include <PhysicsEngine/BodySetup.h>
#endif

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <StaticMeshResources.h>

#include <set>
#include <charconv> 
#include <cstdlib>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/ExtensionITwinMaterial.h>
#	include <BeUtils/Gltf/ExtensionITwinMaterialID.h>
#	include <BeUtils/Gltf/GltfTuner.h>
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
			CesiumMetadataValueAccess::GetUnsignedInteger64(
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

	/// Helper to get uint64 value from a glTF PropertyTableView property
	/// This mimics CesiumMetadataValueAccess::GetUnsignedInteger64 behavior
	uint64 GetGltfPropertyValueAsUInt64(
		const CesiumGltf::Model& Model,
		const CesiumGltf::PropertyTable& PropertyTable,
		const std::string& PropertyName,
		int64 FeatureID,
		uint64 DefaultValue)
	{
		CesiumGltf::PropertyTableView PropertyTableView(Model, PropertyTable);

		uint64 Result = DefaultValue;

		PropertyTableView.getPropertyView(
			PropertyName,
			[&Result, FeatureID, DefaultValue](const std::string& /*Name*/, auto PropertyView) {
				if (PropertyView.status() != CesiumGltf::PropertyTablePropertyViewStatus::Valid)
					return;

				auto OptValue = PropertyView.get(FeatureID);
				if (!OptValue)
					return;

				const auto& Value = *OptValue;

				// Use decltype to get the actual value type
				using RawType = std::remove_cvref_t<decltype(Value)>;

				// Handle different types similar to CesiumMetadataValueAccess::GetUnsignedInteger64
				if constexpr (std::is_same_v<RawType, bool>)
				{
					Result = Value ? 1 : 0;
				}
				else if constexpr (std::is_integral_v<RawType>)
				{
					// For both signed and unsigned integers, reinterpret the bits as uint64
					// This matches how CesiumMetadataValueAccess handles the conversion
					if constexpr (sizeof(RawType) == sizeof(uint64))
					{
						// For 64-bit integers, reinterpret the bit pattern
						Result = static_cast<uint64>(static_cast<std::make_unsigned_t<RawType>>(Value));
					}
					else
					{
						// For smaller integers, sign extension might occur for signed types
						// but we want the value as if it were unsigned
						if constexpr (std::is_signed_v<RawType>)
						{
							// Reinterpret the bits: cast to same-size unsigned first
							using UnsignedType = std::make_unsigned_t<RawType>;
							Result = static_cast<uint64>(static_cast<UnsignedType>(Value));
						}
						else
						{
							Result = static_cast<uint64>(Value);
						}
					}
				}
				else if constexpr (std::is_floating_point_v<RawType>)
				{
					// Floating point - truncate if in valid range
					if (Value >= 0.0 && Value <= static_cast<RawType>(std::numeric_limits<uint64>::max()))
						Result = static_cast<uint64>(Value);
					// else keep DefaultValue
				}
				else if constexpr (std::is_same_v<RawType, std::string_view>)
				{
					// String - try to parse as number
					std::string_view Sv = Value;
					if (Sv.empty())
						return;

					// Try hex format first (0x...)
					if (Sv.size() > 2 && Sv[0] == '0' && (Sv[1] == 'x' || Sv[1] == 'X'))
					{
						uint64 Parsed = 0;
						auto [Ptr, Ec] = std::from_chars(Sv.data() + 2, Sv.data() + Sv.size(), Parsed, 16);
						if (Ec == std::errc() && Ptr == Sv.data() + Sv.size())
						{
							Result = Parsed;
							return;
						}
					}

					// Try decimal integer
					{
						uint64 Parsed = 0;
						auto [Ptr, Ec] = std::from_chars(Sv.data(), Sv.data() + Sv.size(), Parsed);
						if (Ec == std::errc() && Ptr == Sv.data() + Sv.size())
						{
							Result = Parsed;
							return;
						}
					}

					// Try floating point and truncate
					{
						// std::from_chars for float may not be available on all platforms
						// Use strtod as fallback
						char* End = nullptr;
						std::string TempStr(Sv);
						double Parsed = std::strtod(TempStr.c_str(), &End);
						if (End == TempStr.c_str() + TempStr.size() &&
							Parsed >= 0.0 &&
							Parsed <= static_cast<double>(std::numeric_limits<uint64>::max()))
						{
							Result = static_cast<uint64>(Parsed);
						}
					}
				}
			});

		return Result;
	}

	/// Helper to check if a property exists in the glTF property table
	bool HasGltfProperty(
		const CesiumGltf::Model& Model,
		const CesiumGltf::PropertyTable& PropertyTable,
		const std::string& PropertyName)
	{
		CesiumGltf::PropertyTableView PropertyTableView(Model, PropertyTable);
		bool bFound = false;

		PropertyTableView.getPropertyView(
			PropertyName,
			[&bFound](const std::string& /*Name*/, auto PropertyView) {
				if (PropertyView.status() == CesiumGltf::PropertyTablePropertyViewStatus::Valid)
					bFound = true;
			});

		return bFound;
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

FITwinPreFetchedPrimitiveData UITwinSceneMappingBuilder::TakePreFetchedData(int32 MeshIndex, int32 PrimitiveIndex)
{
	std::lock_guard<std::mutex> Lock(PreFetchedDataMutex);
	FPrimitiveKey Key(MeshIndex, PrimitiveIndex);
	auto It = PreFetchedPrimitiveData.find(Key);
	if (It != PreFetchedPrimitiveData.end())
	{
		FITwinPreFetchedPrimitiveData Data = std::move(It->second);
		PreFetchedPrimitiveData.erase(It);
		return Data;
	}
	return FITwinPreFetchedPrimitiveData{};
}

// Static callback that GltfTuner will call
void UITwinSceneMappingBuilder::OnPrimitivePreFetchFromGltfTuner(
	const CesiumGltf::Model& Model,
	const CesiumGltf::MeshPrimitive& Primitive,
	int32_t MeshIndex,
	int32_t PrimitiveIndex,
	void* UserData)
{
	if (!UserData)
		return;

	UITwinSceneMappingBuilder* pBuilder = static_cast<UITwinSceneMappingBuilder*>(UserData);
	pBuilder->PreFetchPrimitiveData(Model, Primitive, MeshIndex, PrimitiveIndex);
}

// The actual pre-fetching logic (formerly in OnPrimitiveLoadedWorkerThread)
void UITwinSceneMappingBuilder::PreFetchPrimitiveData(
	const CesiumGltf::Model& Model,
	const CesiumGltf::MeshPrimitive& Primitive,
	int32 MeshIndex,
	int32 PrimitiveIndex)
{
	// Pre-fetch ITwin feature data in worker thread using raw glTF property tables
	// This avoids needing FCesiumModelMetadata which isn't available in the worker thread

	// Get the EXT_mesh_features extension from the primitive
	const auto* pMeshFeatures = Primitive.getExtension<CesiumGltf::ExtensionExtMeshFeatures>();
	if (!pMeshFeatures || pMeshFeatures->featureIds.empty())
		return;

	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	if (FeatureIDSetIndex >= static_cast<int64>(pMeshFeatures->featureIds.size()))
		return;

	const auto& featureIdDef = pMeshFeatures->featureIds[FeatureIDSetIndex];

	// Get the EXT_structural_metadata extension from the model
	const auto* pMetadataExt = Model.getExtension<CesiumGltf::ExtensionModelExtStructuralMetadata>();
	if (!pMetadataExt)
		return;

	// Get vertex range from skirt metadata
	int64 VertexBegin = 0;
	int64 VertexEnd = 0;

	auto PositionIt = Primitive.attributes.find("POSITION");
	if (PositionIt != Primitive.attributes.end())
	{
		const int PositionAccessorIndex = PositionIt->second;
		if (PositionAccessorIndex >= 0 && PositionAccessorIndex < static_cast<int>(Model.accessors.size()))
		{
			VertexEnd = Model.accessors[PositionAccessorIndex].count;
		}
	}

	std::optional<CesiumGltfContent::SkirtMeshMetadata> SkirtMeshMetadata =
		CesiumGltfContent::SkirtMeshMetadata::parseFromGltfExtras(Primitive.extras);
	if (SkirtMeshMetadata.has_value())
	{
		VertexBegin = SkirtMeshMetadata->noSkirtVerticesBegin;
		VertexEnd = SkirtMeshMetadata->noSkirtVerticesBegin + SkirtMeshMetadata->noSkirtVerticesCount;
	}

	if (VertexEnd <= VertexBegin)
		return;

	FITwinPreFetchedPrimitiveData PreFetchedData;

	// Extract feature IDs directly from glTF accessors (no Cesium wrapper needed)
	if (featureIdDef.attribute)
	{
		// Feature IDs are stored as vertex attribute
		auto featureIdAttrIt = Primitive.attributes.find("_FEATURE_ID_" + std::to_string(*featureIdDef.attribute));
		if (featureIdAttrIt != Primitive.attributes.end())
		{
			const int accessorIndex = featureIdAttrIt->second;
			if (accessorIndex >= 0 && accessorIndex < static_cast<int>(Model.accessors.size()))
			{
				CesiumGltf::AccessorView<float> featureIdView(Model, accessorIndex);
				if (featureIdView.status() == CesiumGltf::AccessorViewStatus::Valid)
				{
					for (int64 VtxIndex = VertexBegin; VtxIndex < VertexEnd; ++VtxIndex)
					{
						if (VtxIndex < featureIdView.size())
						{
							int64 FeatureID = static_cast<int64>(featureIdView[VtxIndex]);
							if (FeatureID >= 0)
								PreFetchedData.UniqueFeatureIDs.insert(FeatureID);
						}
					}
				}
			}
		}
	}
	else
	{
		// Implicit feature IDs
		for (int64 VtxIndex = VertexBegin; VtxIndex < VertexEnd; ++VtxIndex)
		{
			PreFetchedData.UniqueFeatureIDs.insert(VtxIndex);
		}
	}

	if (PreFetchedData.UniqueFeatureIDs.empty())
		return;

	// Get the property table for element features
	// In CesiumGltf, propertyTable is an int32_t where -1 means no property table
	if (featureIdDef.propertyTable < 0)
		return;

	int32 elementPropertyTableIndex = featureIdDef.propertyTable;
	if (elementPropertyTableIndex >= static_cast<int32>(pMetadataExt->propertyTables.size()))
		return;

	const CesiumGltf::PropertyTable& elementPropertyTable = pMetadataExt->propertyTables[elementPropertyTableIndex];
	CesiumGltf::PropertyTableView elementPropertyTableView(Model, elementPropertyTable);

	// Property names as std::string for cesium-native API
	static const std::string ElementPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::ELEMENT_NAME);
	static const std::string CategoryNewPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::SUBCATEGORY_NAME);
	static const std::string CategoryLegacyPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::SUBCATEGORY_LEGACY_NAME);
	static const std::string ModelPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::MODEL_NAME);
	static const std::string GeometryPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::GEOMETRYCLASS_NAME);
	static const std::string MaterialPropertyName = TCHAR_TO_UTF8(*ITwinCesium::Metada::MATERIAL_NAME);

	// Check if we have Element property (required)
	if (!HasGltfProperty(Model, elementPropertyTable, ElementPropertyName))
		return;

	// Check for Geometry property availability
	const bool bHasGeometryProperty = HasGltfProperty(Model, elementPropertyTable, GeometryPropertyName);

	// Check for Category property availability (2 possible names: "subCategory" or "subcategory")
	const bool bHasCategoryNewProperty = HasGltfProperty(Model, elementPropertyTable, CategoryNewPropertyName);
	const bool bHasCategoryLegacyProperty = HasGltfProperty(Model, elementPropertyTable, CategoryLegacyPropertyName);
	const bool bHasCategoryProperty = bHasCategoryNewProperty || bHasCategoryLegacyProperty;
	const std::string& CategoryPropertyName = bHasCategoryNewProperty ? CategoryNewPropertyName : CategoryLegacyPropertyName;

	// Get Material property table (may be different from element property table)
	const CesiumGltf::PropertyTable* pMaterialPropertyTable = nullptr;
	const int64 MaterialFeatureIDSetIndex = ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT;
	if (MaterialFeatureIDSetIndex < static_cast<int64>(pMeshFeatures->featureIds.size()))
	{
		const auto& materialFeatureIdDef = pMeshFeatures->featureIds[MaterialFeatureIDSetIndex];
		// propertyTable is an int32_t where -1 means no property table
		if (materialFeatureIdDef.propertyTable >= 0)
		{
			int32 materialPropertyTableIndex = materialFeatureIdDef.propertyTable;
			if (materialPropertyTableIndex < static_cast<int32>(pMetadataExt->propertyTables.size()))
			{
				pMaterialPropertyTable = &pMetadataExt->propertyTables[materialPropertyTableIndex];
			}
		}
	}

	// Pre-fetch property data for each unique feature ID
	for (int64 FeatureID : PreFetchedData.UniqueFeatureIDs)
	{
		FITwinPreFetchedPrimitiveData::FFeatureData data{};

		// Fetch Element ID using the new helper
		data.ElementID = GetGltfPropertyValueAsUInt64(Model, elementPropertyTable, ElementPropertyName,
			FeatureID, ITwin::NOT_ELEMENT.value());

		// Fetch Category ID
		if (bHasCategoryProperty)
		{
			data.CategoryID = GetGltfPropertyValueAsUInt64(Model, elementPropertyTable, CategoryPropertyName,
				FeatureID, ITwin::NOT_ELEMENT.value());
			if (data.CategoryID != ITwin::NOT_ELEMENT.value())
				data.CategoryID--;
		}
		else
		{
			data.CategoryID = ITwin::NOT_ELEMENT.value();
		}

		// Fetch Model ID
		data.ModelID = GetGltfPropertyValueAsUInt64(Model, elementPropertyTable, ModelPropertyName,
			FeatureID, ITwin::NOT_ELEMENT.value());

		// Fetch Geometry ID
		data.bHasGeometry = bHasGeometryProperty;
		if (bHasGeometryProperty)
		{
			data.GeometryID = static_cast<uint8>(GetGltfPropertyValueAsUInt64(Model, elementPropertyTable,
				GeometryPropertyName, FeatureID, 0));
		}

		// Fetch Material ID (from potentially different property table)
		if (pMaterialPropertyTable)
		{
			data.MaterialID = GetGltfPropertyValueAsUInt64(Model, *pMaterialPropertyTable, MaterialPropertyName,
				FeatureID, ITwin::NOT_IMODEL_MATERIAL.value());
		}
		else
		{
			data.MaterialID = ITwin::NOT_IMODEL_MATERIAL.value();
		}


		PreFetchedData.FeatureDataMap[FeatureID] = data;
	}

	PreFetchedData.bHasData = true;
	PreFetchedData.pOriginalModel = &Model;

	// Store the pre-fetched data keyed by mesh/primitive index
	{
		std::lock_guard<std::mutex> Lock(PreFetchedDataMutex);
		PreFetchedPrimitiveData[FPrimitiveKey(MeshIndex, PrimitiveIndex)] = std::move(PreFetchedData);
	}
}

void UITwinSceneMappingBuilder::OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim)
{
	ITwinScene::TileIdx TileRank;
	auto sceneMappingPtr = GetInternals(*IModel).SceneMapping;
	auto& LoadedTile = TilePrim.GetLoadedTile();
	auto& MeshComponent = TilePrim.GetMeshComponent();

	// Short lock just to get/create the tile
	TITwinSceneTilePtr SceneTilePtr;
	{
		auto sceneMappingW = sceneMappingPtr->GetAutoLock();
		SceneTilePtr = sceneMappingW->KnownTileSLOW(LoadedTile, &TileRank);
	}

	auto const& ClippingHelperPtr = GetInternals(*IModel).ClippingHelper;
	if (ClippingHelperPtr)
	{
		// Set Custom Primitive Data needed to activate clipping effects.
		ClippingHelperPtr->ApplyCPDFlagsToMeshComponent(MeshComponent);
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

	// always look in 1st set (_FEATURE_ID_0)
	const int64 FeatureIDSetIndex = ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT;
	const FCesiumPrimitiveFeatures& PrimitiveFeatures(TilePrim.GetPrimitiveFeatures());
	constexpr int MetaDataNamesCount = 4;
	static const std::array<FString, MetaDataNamesCount> MetaDataNames = {
		ITwinCesium::Metada::ELEMENT_NAME,
		ITwinCesium::Metada::CATEGORY_NAME,
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
	const FCesiumPropertyTableProperty* pElementPropertyTable =
		pProperties[to_underlying(EITwinPropertyType::Element)];
	if (pElementPropertyTable == nullptr)
	{
		return;
	}
	bool bUseSubcategory = false;
	if (!pProperties[to_underlying(EITwinPropertyType::Category)])
	{
		// Try again with subcategory slot if category slot returns null property
		// Note that FindValidProperty is case-insensitive (because the default comparator for FString is
		// case-insensitive), so it will find either "subCategory" or "subcategory" - not need to check both
		// names here.
		pProperties[to_underlying(EITwinPropertyType::Category)] =
			FITwinMetadataPropertyAccess::FindValidProperty(
				PrimitiveFeatures,
				LoadedTile.GetModelMetadata(),
				ITwinCesium::Metada::SUBCATEGORY_NAME,
				FeatureIDSetIndex);
		bUseSubcategory = true;
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
	int32_t const GltfMeshWrapIdx = [&]
		{
			auto SceneTileLock = SceneTilePtr->GetAutoLock();
			auto& SceneTile = *SceneTileLock;

			int32_t GltfMeshWrapIdx = (int32_t)SceneTile.GltfMeshes.size();
			auto& Wrapper =
				SceneTile.GltfMeshes.emplace_back(FITwinGltfMeshComponentWrapper(TilePrim, uvIndexForFeatures));
			// Actual baking was already done in a background thread during cesium's loadPrimitive, thanks to the
			// UCesiumFeaturesMetadataComponent component we attach to the tileset - this here only updates the
			// scene tile's map:
			{
				auto sceneMapping = sceneMappingPtr->GetAutoLock();
				sceneMapping->SetupFeatureIDsInVertexUVs(SceneTile, Wrapper);
			}
			return GltfMeshWrapIdx;
		}();

	// note that this has already been checked:
	// if no featureIDSet exists in features, FindValidProperty would have returned null...
	const FCesiumFeatureIdSet& FeatureIdSet =
		UCesiumPrimitiveFeaturesBlueprintLibrary::GetFeatureIDSets(PrimitiveFeatures)[FeatureIDSetIndex];
#if BE_IS_USING_BENTLEY_UNREAL // using BeUE <=> CMake's BE_USE_OFFICIAL_UNREAL is OFF
	for (auto const& pCollisionMesh : MeshComponent.GetBodySetup()->TriMeshGeometries)
	{
		pCollisionMesh->SetTriangleHitFilter(
			[pIModel = this->IModel, &FeatureIdSet, pElementPropertyTable, &ClippingHelperPtr, &MeshComponent]
			(FVector const& Position, uint32/*FaceIndex*/,
				uint32 VertexIndex, uint32/*VertexIndexB*/, uint32/*VertexIndexC*/)
			{
				if (!IsValid(pIModel))
					return true;
				auto&& CalcElemID = [VertexIndex, &FeatureIdSet, pElementPropertyTable]()
					{
						const int64 FeatureID = UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
							FeatureIdSet, static_cast<int64>(VertexIndex));
						if (FeatureID < 0)
							return ITwin::NOT_ELEMENT;
						const ITwinFeatureID ITwinFeatID = ITwinFeatureID(FeatureID);
						return FeatureIDToITwinID<ITwinElementID>(pElementPropertyTable, FeatureID);
					};
				FVector const WorldPosition = MeshComponent.GetComponentTransform().TransformPosition(Position);
				return GetInternals(*pIModel).IsVisibleAtPoint(CalcElemID, WorldPosition);
			});
	}
#endif // BE_IS_USING_BENTLEY_UNREAL

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
	auto LoadedTileId = ITwin::GetCesiumTileID(LoadedTile);

	// Try to retrieve pre-fetched data from worker thread
	// We need mesh/primitive indices - get them from the primitive component if available
	// For now, we'll need to find the indices from the glTF model
	int32 MeshIndex = -1;
	int32 PrimitiveIndex = -1;

	// Find the mesh and primitive indices by searching the model
	for (int32 MeshIdx = 0; MeshIdx < static_cast<int32>(Gltf->meshes.size()); ++MeshIdx)
	{
		const auto& Mesh = Gltf->meshes[MeshIdx];
		for (int32 PrimIdx = 0; PrimIdx < static_cast<int32>(Mesh.primitives.size()); ++PrimIdx)
		{
			if (&Mesh.primitives[PrimIdx] == pMeshPrimitive)
			{
				MeshIndex = MeshIdx;
				PrimitiveIndex = PrimIdx;
				break;
			}
		}
		if (MeshIndex >= 0)
			break;
	}

	// Take the pre-fetched data (removes it from the map)
	FITwinPreFetchedPrimitiveData PreFetchedData = TakePreFetchedData(MeshIndex, PrimitiveIndex);

	bool bHasAddedMaterialToSceneTile = false;
	std::unordered_set<ITwinScene::ElemIdx> MeshElemSceneRanks;
	FITwinElement Dummy;
	FITwinElement* pElemStruct = &Dummy;
	ITwinElementID LastElem = ITwin::NOT_ELEMENT;
	ITwinFeatureID LastFeature = ITwin::NOT_FEATURE;

	// Use pre-fetched data only if the model hasn't been replaced since worker thread
	// The GltfModifier can replace the model between worker thread and game thread callbacks
	const bool bModelWasReplaced = PreFetchedData.pOriginalModel != nullptr &&
		PreFetchedData.pOriginalModel != Gltf;
	const bool bUsePreFetchedData = PreFetchedData.bHasData &&
		!PreFetchedData.UniqueFeatureIDs.empty() &&
		!bModelWasReplaced;

	// Process unique feature IDs from pre-fetched data
	std::unordered_set<int64> uniqueFeatureIDs;
	if (bUsePreFetchedData)
	{
		uniqueFeatureIDs = std::move(PreFetchedData.UniqueFeatureIDs);
	}
	else
	{
		// Collect unique feature IDs from vertices
		for (int64_t VtxIndex = VertexBegin; VtxIndex < VertexEnd; ++VtxIndex)
		{
			int64 FeatureID = UCesiumFeatureIdSetBlueprintLibrary::GetFeatureIDForVertex(
				FeatureIdSet, static_cast<int64>(VtxIndex));
			if (FeatureID >= 0)
				uniqueFeatureIDs.insert(FeatureID);
		}
	}

	// Process unique feature IDs in parallel using UE's task system
	const int32 NumFeatures = uniqueFeatureIDs.size();
	if (NumFeatures == 0)
		return;

	// Convert set to vector for indexed access
	std::vector<int64> featureIDsArray;
	featureIDsArray.reserve(NumFeatures);
	for (int64 FeatureID : uniqueFeatureIDs)
	{
		featureIDsArray.push_back(FeatureID);
	}

	// Thread-local storage for per-thread results
	struct FThreadLocalData
	{
		std::unordered_set<ITwinScene::ElemIdx> MeshElemSceneRanks;
		std::vector<std::tuple<ITwinElementID, ITwinFeatureID, int32>> ElemFeatureMeshTuples; // ElementID, FeatureID, GltfMeshWrapIdx

		// Use maps with sets for automatic deduplication during parallel processing
		//std::unordered_map<ITwinElementID, std::unordered_set<ITwinElementID>> CategoryToElement;  // Category -> Elements
		//std::unordered_map<ITwinElementID, std::unordered_set<ITwinElementID>> ModelToElement;     // Model -> Elements
		std::unordered_map<uint8, std::unordered_set<ITwinElementID>> GeometryToElement;           // Geometry -> Elements

		std::unordered_map<ITwinRenderMaterialElementID, std::unordered_set<ITwinFeatureID>> MaterialFeatures;  // Material -> Features
		std::unordered_map<ITwinElementID, std::unordered_set<ITwinFeatureID>> ModelFeatures;      // Model -> Features
		std::unordered_map<ITwinElementID, std::unordered_set<ITwinFeatureID>> CategoryFeatures;   // Category -> Features

		std::unordered_map<ITwinElementID, std::unordered_map<ITwinElementID, std::unordered_set<ITwinFeatureID>>> CategoryModelFeatures; // Category -> Model -> Features

		ITwinFeatureID MaxFeatureID = ITwin::NOT_FEATURE;
	};

	// Determine chunk size for parallel processing
	const int32 MinChunkSize = 100;
	const int32 MaxThreads = FMath::Max(FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1, 8), 1);
	const int32 ChunkSize = FMath::Max(MinChunkSize, NumFeatures / MaxThreads);
	const int32 NumChunks = (NumFeatures + ChunkSize - 1) / ChunkSize;

	TArray<FThreadLocalData> ThreadResults;
	ThreadResults.SetNum(NumChunks);

	// Parallel processing of feature IDs
	ParallelFor(NumChunks, [&](int32 ChunkIndex)
		{
			FThreadLocalData& LocalData = ThreadResults[ChunkIndex];
			const int32 StartIdx = ChunkIndex * ChunkSize;
			const int32 EndIdx = FMath::Min(StartIdx + ChunkSize, NumFeatures);

			for (int32 Idx = StartIdx; Idx < EndIdx; ++Idx)
			{
				const int64 FeatureID = featureIDsArray[Idx];
				if (FeatureID < 0)
					continue;

				const ITwinFeatureID ITwinFeatID = ITwinFeatureID(FeatureID);

				// Update thread-local MaxFeatureID
				if (ITwinFeatID > LocalData.MaxFeatureID || LocalData.MaxFeatureID == ITwin::NOT_FEATURE)
				{
					LocalData.MaxFeatureID = ITwinFeatID;
				}

				ITwinElementID ElementID;

				// Fetch the ElementID
				if (bUsePreFetchedData)
				{
					auto FeatureDataIt = PreFetchedData.FeatureDataMap.find(FeatureID);
					ElementID = (FeatureDataIt != PreFetchedData.FeatureDataMap.end())
						? ITwinElementID(FeatureDataIt->second.ElementID)
						: ITwin::NOT_ELEMENT;
				}
				else
				{
					ElementID = FeatureIDToITwinID<ITwinElementID>(pElementPropertyTable, FeatureID);
				}

				if (ElementID != ITwin::NOT_ELEMENT)
				{
					// Store data for later merging
					LocalData.ElemFeatureMeshTuples.emplace_back(ElementID, ITwinFeatID, GltfMeshWrapIdx);

					ITwinElementID CategoryID, ModelID;
					if (bUsePreFetchedData)
					{
						auto FeatureDataIt = PreFetchedData.FeatureDataMap.find(FeatureID);
						if (FeatureDataIt != PreFetchedData.FeatureDataMap.end())
						{
							CategoryID = ITwinElementID(FeatureDataIt->second.CategoryID);
							ModelID = ITwinElementID(FeatureDataIt->second.ModelID);
						}
						else
						{
							CategoryID = ITwin::NOT_ELEMENT;
							ModelID = ITwin::NOT_ELEMENT;
						}
					}
					else
					{
						CategoryID = FeatureIDToITwinID<ITwinElementID>(
							pProperties[to_underlying(EITwinPropertyType::Category)], FeatureID);
						if (bUseSubcategory)
							CategoryID--;
						ModelID = FeatureIDToITwinID<ITwinElementID>(
							pProperties[to_underlying(EITwinPropertyType::Model)], FeatureID);
					}

					// Store mappings with automatic deduplication via insert()
					if (CategoryID != ITwin::NOT_ELEMENT || ModelID != ITwin::NOT_ELEMENT)
					{
						if (CategoryID != ITwin::NOT_ELEMENT)
						{
							//LocalData.CategoryToElement[CategoryID].insert(ElementID);
							LocalData.CategoryFeatures[CategoryID].insert(ITwinFeatID);
						}
						if (ModelID != ITwin::NOT_ELEMENT)
						{
							//LocalData.ModelToElement[ModelID].insert(ElementID);
							LocalData.ModelFeatures[ModelID].insert(ITwinFeatID);
						}

						// Handle geometry mapping
						if (bUsePreFetchedData)
						{
							auto FeatureDataIt = PreFetchedData.FeatureDataMap.find(FeatureID);
							if (FeatureDataIt != PreFetchedData.FeatureDataMap.end() &&
								FeatureDataIt->second.bHasGeometry)
							{
								LocalData.GeometryToElement[FeatureDataIt->second.GeometryID].insert(ElementID);
							}
						}
						else if (pProperties[to_underlying(EITwinPropertyType::Geometry)])
						{
							uint8 GeometryID = FeatureIDToITwinID<uint8>(
								pProperties[to_underlying(EITwinPropertyType::Geometry)], FeatureID);
							LocalData.GeometryToElement[GeometryID].insert(ElementID);
						}
					}

					// Material ID processing
					ITwinRenderMaterialElementID MaterialID;
					if (bUsePreFetchedData)
					{
						auto FeatureDataIt = PreFetchedData.FeatureDataMap.find(FeatureID);
						MaterialID = (FeatureDataIt != PreFetchedData.FeatureDataMap.end())
							? ITwinRenderMaterialElementID(FeatureDataIt->second.MaterialID)
							: ITwin::NOT_IMODEL_MATERIAL;
					}
					else
					{
						MaterialID = pMaterialProp ? FeatureIDToITwinID<ITwinRenderMaterialElementID>(
							pMaterialProp, FeatureID) : ITwin::NOT_IMODEL_MATERIAL;
					}

					if (MaterialID != ITwin::NOT_IMODEL_MATERIAL)
						LocalData.MaterialFeatures[MaterialID].insert(ITwinFeatID);

					// Category/Model combination
					if (CategoryID != ITwin::NOT_ELEMENT && ModelID != ITwin::NOT_ELEMENT)
						LocalData.CategoryModelFeatures[CategoryID][ModelID].insert(ITwinFeatID);
				}
			}
		}, EParallelForFlags::None);

	
	{
		auto sceneMapping = sceneMappingPtr->GetRAutoLock();
		auto GeometryIDToElementIDsLock = sceneMapping->GeometryIDToElementIDs->GetAutoLock();
		auto& GeometryIDToElementIDs = *GeometryIDToElementIDsLock;
		// Merge results back on game thread for sceneMapping
		for (FThreadLocalData& LocalData : ThreadResults)
		{
			// Merge category/model/geometry to element mappings - BULK INSERT
			//for (auto& [CategoryID, ElementSet] : LocalData.CategoryToElement)
			//{
			//	auto sceneMapping = sceneMappingPtr->GetRAutoLock();
			//	sceneMapping->CategoryIDToElementIDs[CategoryID].merge(std::move(ElementSet));
			//}
			//for (auto& [ModelID, ElementSet] : LocalData.ModelToElement)
			//{
			//	sceneMapping->ModelIDToElementIDs[ModelID].merge(std::move(ElementSet));
			//}
			for (auto& [GeometryID, ElementSet] : LocalData.GeometryToElement)
			{
				GeometryIDToElementIDs[GeometryID].merge(std::move(ElementSet));
			}
		}
	}

	{
		auto SceneTileLock = SceneTilePtr->GetAutoLock();
		FITwinSceneTile& SceneTile = *SceneTileLock;
		// Merge results back on game thread for SceneTile only
		for (FThreadLocalData& LocalData : ThreadResults)
		{
			// Update MaxFeatureID from each thread's local maximum
			if (LocalData.MaxFeatureID > SceneTile.MaxFeatureID || SceneTile.MaxFeatureID == ITwin::NOT_FEATURE)
			{
				SceneTile.MaxFeatureID = LocalData.MaxFeatureID;
			}
			// Merge material/model/category features for SceneTile - BULK INSERT
			for (auto& [MaterialID, FeatureSet] : LocalData.MaterialFeatures)
			{
				auto& Features = SceneTile.MaterialFeaturesSLOW(MaterialID).Features;
				Features.merge(FeatureSet);
			}
			for (auto& [ModelID, FeatureSet] : LocalData.ModelFeatures)
			{
				auto& Features = SceneTile.ModelFeaturesSLOW(ModelID).Features;
				Features.merge(FeatureSet);
			}
			for (auto& [CategoryID, FeatureSet] : LocalData.CategoryFeatures)
			{
				auto& Features = SceneTile.CategoryFeaturesSLOW(CategoryID).Features;
				Features.merge(FeatureSet);
			}
			for (auto& [CategoryID, ModelMap] : LocalData.CategoryModelFeatures)
			{
				for (auto& [ModelID, FeatureSet] : ModelMap)
				{
					auto& Features = SceneTile.CategoryPerModelFeaturesSLOW(CategoryID, ModelID).Features;
					Features.merge(FeatureSet);
				}
			}

			if (!bHasAddedMaterialToSceneTile)
			{
				SceneTile.Materials.push_back(pMaterial);
				bHasAddedMaterialToSceneTile = true;
			}
		}
		{
			auto sceneMapping = sceneMappingPtr->GetAutoLock();
			// Process element features
			for (FThreadLocalData& LocalData : ThreadResults)
			{
				for (const auto& [ElementID, ITwinFeatID, MeshIdx] : LocalData.ElemFeatureMeshTuples)
				{
					ITwinScene::ElemIdx ElemRank;
					FITwinElement& Element = sceneMapping->ElementForSLOW(ElementID, &ElemRank);
					Element.bHasMesh = true;
					MeshElemSceneRanks.insert(ElemRank);

					FITwinElementFeaturesInTile& ElemInTile = SceneTile.ElementFeaturesSLOW(ElementID);
					AddFeatureIfAbsent(ElementID,
						[&](const ITwinElementID&) -> FITwinElementFeaturesInTile& { return ElemInTile; },
						ITwinFeatID);

					ElemInTile.SceneRank = ElemRank;
					if (std::find(ElemInTile.Meshes.begin(), ElemInTile.Meshes.end(), MeshIdx) == ElemInTile.Meshes.end())
					{
						ElemInTile.Meshes.push_back(MeshIdx);
						ElemInTile.Materials.push_back(pMaterial);
					}
				}
			}
		}
	}
	if (IModel->Synchro4DSchedules)
	{
		GetInternals(*IModel->Synchro4DSchedules).OnNewTileMeshBuilt(TileRank, std::move(MeshElemSceneRanks));
	}
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

	// Register the primitive pre-fetch callback with GltfTuner
	auto GltfTuner = InIModel.GetGltfTuner();
	if (GltfTuner)
	{
		GltfTuner->SetPrimitivePreFetchCallback(
			&UITwinSceneMappingBuilder::OnPrimitivePreFetchFromGltfTuner,
			this); // Pass 'this' as user data so the callback can access the instance
	}
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
	auto SceneMappingLocked = SceneMapping->GetAutoLock();
	auto const& Elem = SceneMappingLocked->ElementForSLOW(ElementID);
	if (Elem.Requirements.bNeedTranslucentMat)
		pBaseMaterial = IModel->Synchro4DSchedules->BaseMaterialTranslucent;
}

UMaterialInstanceDynamic* UITwinSceneMappingBuilder::CreateMaterial(ICesiumLoadedTilePrimitive& TilePrim,
	UMaterialInterface* pBaseMaterial, FName const& Name)
{
	auto const* Sched = IModel->Synchro4DSchedules;
	if (IsValid(Sched) && IsValid(Sched->BaseMaterialTranslucent)
		&& !(Sched->bDisableVisibilities || Sched->bDisablePartialVisibilities))
	{
		auto const& LoadedTile = TilePrim.GetLoadedTile();
		auto const MinTuneVer = GetInternals(*Sched).GetMinGltfTunerVersionForAnimation();
		if (auto* Model = LoadedTile.GetGltfModel())
		{
			auto const ModelVer = Cesium3DTilesSelection::GltfModifierVersionExtension::getVersion(*Model);
			if (ModelVer && (*ModelVer) >= MinTuneVer)
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
void UITwinSceneMappingBuilder::BuildFromNonCesiumMesh(TSceneMappingPtr& SceneMapping,
	UStaticMeshComponent& MeshComponent, uint64_t ITwinMaterialID)
{
	auto SceneMappingLocked = SceneMapping->GetAutoLock();
	ensure(SceneMappingLocked->KnownTiles.empty());

	ITwinScene::TileIdx const TileRank(0);
	auto& ByRank = SceneMappingLocked->KnownTiles.get<IndexByRank>();
	TITwinSceneTilePtr NewTile(std::make_shared<AdvViz::SDK::Tools::RWLockableObject<FITwinSceneTile, AdvViz::SDK::Tools::TSharedRecursiveMutex>>(
		CesiumTileID{ "tileId0", "" }
	));
	auto const It = ByRank.emplace_back(std::move(NewTile)).first;
	auto SceneTileLock = It->get()->GetAutoLock();
	SceneTileLock->GltfMeshes.emplace_back(MeshComponent, ITwinMaterialID);
}
