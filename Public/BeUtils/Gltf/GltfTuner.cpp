/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTuner.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <BeUtils/Gltf/GltfTuner.h>

#include "GltfMaterialTuner.h"
#include <CesiumGltf/ExtensionModelExtStructuralMetadata.h>
#include <CesiumGltf/AccessorView.h>
#include <boost/pfr/functors.hpp>
#include <CesiumGltf/ExtensionExtMeshFeatures.h>
#include <boost/mpl/set.hpp>
#include <boost/mpl/front.hpp>
#include <BeUtils/Gltf/GltfBuilder.h>
#include <rapidjson/document.h>
#include <SDK/Core/Tools/Assert.h>

namespace BeUtils
{

namespace
{

//! This namespace contains utilities to create a set of (fully typed) AccessorViews.
//! This is used when processing primitives.
namespace AccessorViews
{

struct Empty
{
};

//! Used to create an AccessorView.
template<template<class> class _ViewHolder>
struct Maker
{
	template<class _T>
	using ViewHolder = _ViewHolder<_T>;
	int32_t index_ = -1;
	template<class _T, class _Super>
	struct Type: _ViewHolder<CesiumGltf::AccessorView<_T>>, _Super
	{
	};
	template<class _T, class _Super>
	static Type<_T, _Super> Make(const _Super& super, const CesiumGltf::Model& model, const CesiumGltf::Accessor* accessor)
	{
		return Type<_T, _Super>{accessor ? CesiumGltf::AccessorView<_T>(model, *accessor): CesiumGltf::AccessorView<_T>(), super};
	}
};

namespace Types
{
struct Scalar;
struct Vec3;
struct Vec4;
} // namespace Type

using AllComponentTypes = boost::mpl::set<int8_t, uint8_t, int16_t, uint16_t, uint32_t, float>;

//! Describes the indices of a primitive.
template<class _T> struct Indices
{
	using ComponentTypes = boost::mpl::set<uint8_t, uint16_t, uint32_t>;
	using Types = boost::mpl::set<Types::Scalar>;
	_T indices_;
};
//! Describes the "feature ID" attribute of a primitive.
template<class _T> struct FeatureIds
{
	using ComponentTypes = boost::mpl::set<float>;
	using Types = boost::mpl::set<Types::Scalar>;
	_T featureIds_;
};
//! Describes the "color" attribute of a primitive.
template<class _T> struct Colors
{
	using ComponentTypes = boost::mpl::set<uint8_t, uint16_t, float>;
	using Types = boost::mpl::set<Types::Vec3, Types::Vec4>;
	_T colors_;
};

} // namespace AccessorViews

//! Tells how primitive topologies are converted.
static int32_t GetConvertedPrimitiveMode(int32_t mode)
{
	switch (mode)
	{
		case CesiumGltf::MeshPrimitive::Mode::LINE_LOOP:
		case CesiumGltf::MeshPrimitive::Mode::LINE_STRIP:
			return CesiumGltf::MeshPrimitive::Mode::LINES;
		case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_STRIP:
		case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_FAN:
			return CesiumGltf::MeshPrimitive::Mode::TRIANGLES;
	}
	return mode;
}

inline bool MaterialUsingTextures(CesiumGltf::Material const& material)
{
	if (material.normalTexture
		|| material.occlusionTexture
		|| material.emissiveTexture)
	{
		return true;
	}
	if (material.pbrMetallicRoughness)
	{
		if (material.pbrMetallicRoughness->baseColorTexture
			|| material.pbrMetallicRoughness->metallicRoughnessTexture)
		{
			return true;
		}
	}
	return false;
}

//! Rules with additional precomputed derived data.
struct GltfTunerRulesEx: GltfTuner::Rules
{
	int version_ = 0; //! Used to detect if we should recompute derived data.
	//! Maps each element ID to the index of its containing group.
	std::unordered_map<uint64_t, size_t> elementToGroup_;
};

class GltfTunerHelper : public GltfMaterialTuner
{
public:
	//! See Cluster below.
	//! ClusterId is used as a key to identify in which cluster a piece (triangle etc.) should be added.
	struct ClusterId
	{
		int32_t material_ = -1; // gltf material ID (refers to the material exported by the MeshExp service)
		std::optional<uint64_t> itwinMaterialID_; // iTwin material ID (if provided as meta-data by the MeshExp service)
		int32_t mode_ = CesiumGltf::MeshPrimitive::Mode::TRIANGLES;
		bool hasNormal_ = false;
		bool hasUV_ = false;
		bool hasColor_ = false;
		bool hasFeatureId_ = false;
		bool hasMaterialFeatureId_ = false; // iTwin material IDs, in _FEATURE_1
		int elementGroupIndex_ = -1;
	};
	//! A cluster is a list of "pieces" (triangles, lines or points) that are grouped together.
	//! Eventually, for each cluster a primitive will be created.
	struct Cluster
	{
		std::vector<std::array<uint32_t, 1>> indices_;
		std::vector<std::array<float, 3>> positions_;
		std::vector<std::array<float, 3>> normals_;
		std::vector<std::array<float, 2>> uvs_;
		std::vector<std::array<uint8_t, 4>> colors_;
		std::vector<std::array<float, 1>> featureIds_;
	};
	//! Those properties are really specific to how the Mesh Export Service handles things.
	struct PrimitiveExtraProperties
	{
		bool hasMaterialFeatureId_ = false;
	};
	using ClusterList = std::unordered_map<ClusterId, Cluster,
		decltype([](const auto& x){return boost::pfr::hash_fields(x);}),
		boost::pfr::equal_to<>>;
	const CesiumGltf::Model& model_; //!< The input model.
	const GltfTunerRulesEx& rules_;
	const glm::dmat4& tileTransform_; //!< The tile transformation
	using UInt64AccessorView = CesiumGltf::AccessorView<uint64_t>;
	std::optional<UInt64AccessorView> elementPropertyTableView_;
	std::optional<UInt64AccessorView> materialPropertyTableView_;


	GltfTunerHelper(const CesiumGltf::Model& model,
		const GltfTunerRulesEx& rules,
		const std::shared_ptr<GltfMaterialHelper>& materialHelper,
		const glm::dmat4& tileTransform)
		: GltfMaterialTuner(materialHelper)
		, model_(model)
		, rules_(rules)
		, tileTransform_(tileTransform)
	{
	}
	CesiumGltf::Model Tune()
	{
		GltfBuilder gltfBuilder;
		// Look for the metadata (inside EXT_structural_metadata) generated by the mesh export service.
		const auto findPropertyTable = [&](std::optional<UInt64AccessorView>& propertyTableView,
										   std::string const& propertyName,
										   size_t startOffset = 0) -> int64_t
		{
			const auto* structuralMetadataExtension = model_.getExtension<CesiumGltf::ExtensionModelExtStructuralMetadata>();
			if (!structuralMetadataExtension)
				return -1;
			for (size_t propertyTableIndex = startOffset; propertyTableIndex < structuralMetadataExtension->propertyTables.size(); ++propertyTableIndex)
			{
				const auto& propertyTable = structuralMetadataExtension->propertyTables[propertyTableIndex];
				// Note the the table can be renamed at any time by the Mesh-Export Service team, so we
				// no longer test it here (typically, it was "MeshPart" then "features" for iTwin features...
				//	if (propertyTable.classProperty != classProperty)
				//		continue;
				//	// Found the table: now check if it contains the expected property (if not, it probably means
				//	//it comes from an outdated version of the Mesh-Export service, or another source...)
				const auto propertyIt = propertyTable.properties.find(propertyName);
				if (propertyIt == propertyTable.properties.end())
					continue;
				{
					const auto& propBufferView = model_.bufferViews[propertyIt->second.values];
					propertyTableView = UInt64AccessorView(
						model_.buffers[propBufferView.buffer].cesium.data.data(),
						sizeof(uint64_t),
						propBufferView.byteOffset,
						propertyTable.count);
				}
				// Copy the table information in the output model.
				// We do not copy all extensions, because extensions might reference data contained in buffers,
				// and we are building a new buffer from scratch, containing only what we need.
				// Thus we want to avoid having extensions that reference invalid accessors, buffers etc.
				auto& outExtension = gltfBuilder.GetModel().addExtension<CesiumGltf::ExtensionModelExtStructuralMetadata>();
				outExtension.schema = structuralMetadataExtension->schema;
				// Copy the table as it is. We are going to adjust the referenced buffer views just below.
				auto& outPropertyTable = outExtension.propertyTables.emplace_back(propertyTable);
				// Transfer the actual table data to the output model's buffer.
				// To have reproducible output (which is needed for unit tests), we fill the buffer
				// by order of the property name (eg. "category", then "element", then "model").
				{
					std::vector<const decltype(propertyTable.properties)::value_type*> sortedProperties;
					for (const auto& property: propertyTable.properties)
						sortedProperties.push_back(&property);
					std::sort(sortedProperties.begin(), sortedProperties.end(),
						[](const auto& x, const auto& y){return x->first < y->first;});
					for (const auto* property: sortedProperties)
					{
						if (property->second.values == -1)
							continue;
						const auto& inBufferView = model_.bufferViews[property->second.values];
						auto& outBufferView = gltfBuilder.GetModel().bufferViews.emplace_back(inBufferView);
						outBufferView.buffer = 0;
						outBufferView.byteOffset = (int64_t)gltfBuilder.GetModel().buffers[0].cesium.data.size();
						gltfBuilder.GetModel().buffers[0].cesium.data.insert(
							gltfBuilder.GetModel().buffers[0].cesium.data.end(),
							model_.buffers[inBufferView.buffer].cesium.data.begin()+inBufferView.byteOffset,
							model_.buffers[inBufferView.buffer].cesium.data.begin()+inBufferView.byteOffset+inBufferView.byteLength);
						// Adjust the index of the buffer view referenced in the output table.
						outPropertyTable.properties[property->first].values = (int32_t)gltfBuilder.GetModel().bufferViews.size()-1;
					}
				}
				return (int64_t)propertyTableIndex;
			}
			// classProperty was not found.
			return -1;
		};
		const int64_t elementPropertyTableIndex = findPropertyTable(elementPropertyTableView_, "element");

		// the Mesh-Export service will put material IDs in a second table
		const int64_t materialPropertyTableIndex = findPropertyTable(materialPropertyTableView_, "material",
			elementPropertyTableIndex == 0 ? 1 : 0);

		auto gltfMaterials = model_.materials;
		auto gltfTextures = model_.textures;
		auto gltfImages = model_.images;

		// Process the primitives of each mesh.
		// Note: we do not merge primitives belonging to different meshes,
		// since it would break the structure of the model's scene.
		for (const auto& mesh: model_.meshes)
		{
			ClusterList clusters;
			for (const auto& primitive: mesh.primitives)
			{
				// Look for the _FEATURE_ID_X corresponding to our metadata.
				const auto getFeatureIdsAccessorIndex = [&](std::optional<UInt64AccessorView> const& propTableView, int64_t propTableIndex)
					{
						if (!propTableView)
							return -1;
						const auto* extension = primitive.getExtension<CesiumGltf::ExtensionExtMeshFeatures>();
						if (!extension)
							return -1;
						int64_t attributeSuffix = -1;
						for (const auto& featureId: extension->featureIds)
						{
							if (featureId.propertyTable != propTableIndex)
								continue;
							if (!featureId.attribute)
								return -1;
							attributeSuffix = *featureId.attribute;
							break;
						}
						const auto attributeIt = primitive.attributes.find("_FEATURE_ID_"+std::to_string(attributeSuffix));
						return attributeIt == primitive.attributes.end() ? -1 : attributeIt->second;
					};
				const auto elementFeatIdsAccessorIndex = getFeatureIdsAccessorIndex(elementPropertyTableView_, elementPropertyTableIndex);
				const auto materialFeatIdsAccessorIndex = getFeatureIdsAccessorIndex(materialPropertyTableView_, materialPropertyTableIndex);
				const bool primHasMaterialIDs = materialFeatIdsAccessorIndex != -1;
				// material IDs, if present, should use the same buffer as features.
				// If this is no longer true, please blame here to revert to a previous version using a
				// separate buffer
				BE_ASSERT(!primHasMaterialIDs || materialFeatIdsAccessorIndex == elementFeatIdsAccessorIndex);

				const auto colorAttributeIt = primitive.attributes.find("COLOR_0");
				// Process the primitive using typed accessors for data that can may have different types.
				// For example:
				// - indices may be UNSIGNED_BYTE, UNSIGNED_SHORT etc.
				// - colors may be VEC3 or VEC4.
				ProcessPrimitive(std::make_tuple(
					AccessorViews::Maker<AccessorViews::Indices>{primitive.indices},
					AccessorViews::Maker<AccessorViews::FeatureIds>{elementFeatIdsAccessorIndex},
					AccessorViews::Maker<AccessorViews::Colors>{colorAttributeIt == primitive.attributes.end() ? -1 : colorAttributeIt->second}),
					primitive, { .hasMaterialFeatureId_ = primHasMaterialIDs }, clusters);
			}
			int32_t const meshIndex = static_cast<int32_t>(gltfBuilder.GetModel().meshes.size());
			gltfBuilder.GetModel().meshes.emplace_back();
			CesiumGltf::Node const* nodeUsingThisMesh = nullptr;
			// To have reproducible output (which is needed for unit tests), we add the primitives
			// by order of the cluster ID.
			std::vector<const decltype(clusters)::value_type*> sortedClusters;
			for (const auto& cluster: clusters)
				sortedClusters.push_back(&cluster);
			std::sort(sortedClusters.begin(), sortedClusters.end(),
				[](const auto& x, const auto& y){return boost::pfr::lt(x->first, y->first);});
			for (const auto* clusterEntry: sortedClusters)
			{
				const auto& clusterId = clusterEntry->first;
				const auto& cluster = clusterEntry->second;
				int32_t materialId = clusterId.material_;
				bool bOverrideColor = false;
				bool bCustomMaterial = false;
				if (clusterId.itwinMaterialID_ && materialId >= 0 && CanConvertITwinMaterials())
				{
					// The final primitive will have 1 iTwin material.
					// See if we should also tune the corresponding gltf material.
					materialId = ConvertITwinMaterial(*clusterId.itwinMaterialID_,
						materialId, gltfMaterials, gltfTextures, gltfImages,
						bOverrideColor, cluster.colors_);
					bCustomMaterial = (materialId >= 0 && materialId != clusterId.material_);
				}
				auto primitive = gltfBuilder.AddMeshPrimitive(meshIndex, materialId, clusterId.mode_);
				primitive.SetIndices(cluster.indices_, true);
				primitive.SetPositions(cluster.positions_);
				if (!cluster.normals_.empty())
					primitive.SetNormals(cluster.normals_);
				if (!cluster.uvs_.empty())
				{
					primitive.SetUVs(cluster.uvs_);
				}
				else if (bCustomMaterial && MaterialUsingTextures(gltfMaterials[materialId]))
				{
					// Quick fix for models not using texture originally: they are exported without UVs by
					// the Mesh Export Service (MES), but we do need UVs to map textures added by the user
					// afterwards...
					if (nodeUsingThisMesh == nullptr)
					{
						// To avoid continuity issues when a large geometry overlaps in different tiles, we
						// need a transformation, which can be retrieved from the 1st node using this mesh.
						// Note that there is often just one mesh (with several primitives) and one node in a
						// tile...
						auto itNode = std::find_if(model_.nodes.cbegin(), model_.nodes.end(),
							[meshIndex](auto const& node) { return node.mesh == meshIndex; });
						BE_ASSERT(itNode != model_.nodes.end());
						if (itNode != model_.nodes.end())
						{
							nodeUsingThisMesh = &(*itNode);
						}
					}
					gltfBuilder.ComputeFastUVs(primitive, cluster.positions_, cluster.normals_,
						cluster.indices_, tileTransform_, nodeUsingThisMesh);
				}
				if (!cluster.colors_.empty() && !bOverrideColor)
					primitive.SetColors(cluster.colors_);
				if (!cluster.featureIds_.empty())
					primitive.SetFeatureIds(cluster.featureIds_, clusterId.hasMaterialFeatureId_);
				if (clusterId.itwinMaterialID_) // the final primitive will have 1 iTwin material
					primitive.SetITwinMaterialID(*clusterId.itwinMaterialID_);
			}
		}
		// Copy everything else from the input model.
		// We skip some properties (eg skins) because they may reference data contained in buffers,
		// and we are building a new buffer from scratch, containing only what we need.
		// Thus we want to avoid having properties that reference invalid accessors, buffers etc.
		gltfBuilder.GetModel().extras = model_.extras;
		gltfBuilder.GetModel().unknownProperties = model_.unknownProperties;
		gltfBuilder.GetModel().extensionsUsed = model_.extensionsUsed;
		gltfBuilder.GetModel().extensionsRequired = model_.extensionsRequired;
		gltfBuilder.GetModel().asset = model_.asset;
		gltfBuilder.GetModel().cameras = model_.cameras;
		gltfBuilder.GetModel().images = gltfImages;
		gltfBuilder.GetModel().materials = gltfMaterials;
		gltfBuilder.GetModel().nodes = model_.nodes;
		gltfBuilder.GetModel().samplers = model_.samplers;
		gltfBuilder.GetModel().scene = model_.scene;
		gltfBuilder.GetModel().scenes = model_.scenes;
		gltfBuilder.GetModel().textures = gltfTextures;
		return std::move(gltfBuilder.GetModel());
	}
private:
	//! This function recursively "builds" specialized versions ProcessPrimitive2,
	//! one for each combination of the given accessor views.
	//! One issue with this technique is that it can result in code bloat,
	//! So we have to make sure to skip combinations which we know are not valid
	//! (eg. FLOAT for indices, or SCALAR for colors etc).
	template<class _AccessorViewMakers, size_t _curIndex = 0, class _CurAccessorViews = AccessorViews::Empty>
	void ProcessPrimitive(const _AccessorViewMakers& accessorViewMakers,
		const CesiumGltf::MeshPrimitive& primitive,
		const PrimitiveExtraProperties& primProps,
		ClusterList& clusters,
		const _CurAccessorViews& accessorViews = {})
	{
		if constexpr (_curIndex == std::tuple_size<_AccessorViewMakers>::value)
		{
			// End of recursion, specialization complete.
			ProcessPrimitive2<_CurAccessorViews>(primitive, primProps, clusters, accessorViews);
		}
		else
		{
			const auto accessorIndex = std::get<_curIndex>(accessorViewMakers).index_;
			const auto* accessor = accessorIndex == -1 ? (CesiumGltf::Accessor*)nullptr : CesiumGltf::Model::getSafe(&model_.accessors, accessorIndex);
			using Maker = std::tuple_element_t<_curIndex, _AccessorViewMakers>;
			const auto step1 = [&](auto* unusedComponentType)
				{
					using ComponentType = std::decay_t<decltype(*unusedComponentType)>;
					if constexpr (!boost::mpl::has_key< typename Maker::template ViewHolder<int>::ComponentTypes, ComponentType>::value)
						return;
					else
					{
						const auto step2 = [&](auto* unusedType)
							{
								using Type = std::decay_t<decltype(*unusedType)>;
								if constexpr (!boost::mpl::has_key<typename Maker::template ViewHolder<int>::Types, Type>::value)
									return;
								else
								{
									const auto step3 = [&](auto* unusedCompleteType)
										{
											ProcessPrimitive<_AccessorViewMakers, _curIndex+1>(accessorViewMakers, primitive, primProps, clusters,
												Maker::template Make<std::decay_t<decltype(*unusedCompleteType)>>(accessorViews, model_, accessor));
										};
									if constexpr (std::is_same_v<AccessorViews::Types::Scalar, Type>)
										step3((std::array<ComponentType, 1>*)0);
									else if constexpr (std::is_same_v<AccessorViews::Types::Vec3, Type>)
										step3((std::array<ComponentType, 3>*)0);
									else if constexpr (std::is_same_v<AccessorViews::Types::Vec4, Type>)
										step3((std::array<ComponentType, 4>*)0);
									else
										static_assert(std::is_void_v<Type>);
								}
							};
						if (!accessor)
						{
							// If there is no accessor in the model (eg. the primitive has no "color" attribute,
							// use the first supported type for this accessor.
							// This avoids generating useless code (even if it will be skipped early).
							step2((typename boost::mpl::front<typename Maker::template ViewHolder<int>::Types>::type*)0);
						}
						else if (accessor->type == CesiumGltf::Accessor::Type::SCALAR)
							step2((AccessorViews::Types::Scalar*)0);
						else if (accessor->type == CesiumGltf::Accessor::Type::VEC3)
							step2((AccessorViews::Types::Vec3*)0);
						else if (accessor->type == CesiumGltf::Accessor::Type::VEC4)
							step2((AccessorViews::Types::Vec4*)0);
						else
							return;
					}
				};
			if (!accessor)
			{
				// If there is no accessor in the model (eg. the primitive has no "color" attribute,
				// use the first supported component type for this accessor.
				// This avoids generating useless code (even if it will be skipped early).
				step1((typename boost::mpl::front<typename Maker::template ViewHolder<int>::ComponentTypes>::type*)0);
			}
			else
				switch (accessor->componentType)
				{
					case CesiumGltf::Accessor::ComponentType::BYTE:           step1((int8_t*)0);   break;
					case CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE:  step1((uint8_t*)0);  break;
					case CesiumGltf::Accessor::ComponentType::SHORT:          step1((int16_t*)0);  break;
					case CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT: step1((uint16_t*)0); break;
					case CesiumGltf::Accessor::ComponentType::UNSIGNED_INT:   step1((uint32_t*)0); break;
					case CesiumGltf::Accessor::ComponentType::FLOAT:          step1((float*)0);    break;
					default:
						return;
				}
		}
	}
	template<class _AccessorViews>
	void ProcessPrimitive2(const CesiumGltf::MeshPrimitive& primitive, const PrimitiveExtraProperties& primProps,
		ClusterList& clusters, const _AccessorViews& accessorViews)
	{
		// Retrieve the accessor views for attributes having fixed data type.
		const CesiumGltf::AccessorView<std::array<float, 3>> positions(model_, primitive.attributes.find("POSITION")->second);
		const auto getView = [&](const std::string& attributeName, auto* unusedType)
			{
				const auto it = primitive.attributes.find(attributeName);
				using View = CesiumGltf::AccessorView<std::decay_t<decltype(*unusedType)>>;
				return it == primitive.attributes.end() ? View() : View(model_, it->second);
			};
		const auto normals = getView("NORMAL", (std::array<float, 3>*)0);
		const auto uvs = getView("TEXCOORD_0", (std::array<float, 2>*)0);
		// A vertex can be referenced by multiple indices (that's the purpose of the index buffer).
		// So once a vertex has been processed (ie. added to a cluster), we record its position inside the
		// cluster's vertices.
		std::vector<int> newIndices(positions.size(), -1);
		// This function processes one "piece" (triangle, line...)
		const auto processPiece = [&](const auto& indexIndices)
			{
				const bool hasFeatureId = accessorViews.featureIds_.status() == CesiumGltf::AccessorViewStatus::Valid;
				// Get the element ID from the first vertex.
				// We assume all the vertices of this piece have the same element ID.
				const auto elementId = hasFeatureId ?
					(*elementPropertyTableView_)[accessorViews.featureIds_[accessorViews.indices_[indexIndices[0]][0]][0]] :
					0;
				// Find the group (in the rules) that contains this element ID, if any.
				const auto groupIt = rules_.elementToGroup_.find(elementId);

				// Material IDs can now be combined with features
				const bool hasMaterialFeatureId = primProps.hasMaterialFeatureId_;
				BE_ASSERT(!hasMaterialFeatureId || (hasFeatureId && materialPropertyTableView_));

				// Get the original material identifier in the iModel, if it was exported by the Mesh-Export
				// Service (should be the case in 08/2024)
				std::optional<uint64_t> itwinMatID;
				if (groupIt == rules_.elementToGroup_.end())
				{
					if (hasMaterialFeatureId)
					{
						itwinMatID = (*materialPropertyTableView_)[accessorViews.featureIds_[accessorViews.indices_[indexIndices[0]][0]][0]];
					}
				}
				else
				{
					itwinMatID = rules_.elementGroups_[groupIt->second].itwinMaterialID_;
				}
				// We should only take the iTwin material into account for the final splitting if the rules
				// say so:
				std::optional<uint64_t> itwinMatIDForCluster;
				if (itwinMatID
					&& rules_.itwinMatIDsToSplit_.find(*itwinMatID) != rules_.itwinMatIDsToSplit_.cend())
				{
					itwinMatIDForCluster = itwinMatID;
				}
				// Find the cluster where this piece will be added.
				auto& cluster = clusters[ClusterId{
					// For the material:
					// - if the element ID is in a group, use this group's material,
					// - otherwise, use the material of the primitive.
					groupIt == rules_.elementToGroup_.end() ? primitive.material :
						rules_.elementGroups_[groupIt->second].material_,
					itwinMatIDForCluster,
					GetConvertedPrimitiveMode(primitive.mode),
					normals.status() == CesiumGltf::AccessorViewStatus::Valid,
					uvs.status() == CesiumGltf::AccessorViewStatus::Valid,
					accessorViews.colors_.status() == CesiumGltf::AccessorViewStatus::Valid,
					hasFeatureId,
					hasMaterialFeatureId,
					groupIt == rules_.elementToGroup_.end() ? -1 : (int)groupIt->second}];
				for (const auto indexIndex: indexIndices)
				{
					const auto index = accessorViews.indices_[indexIndex][0];
					if (newIndices[index] == -1)
					{
						cluster.positions_.push_back(positions[index]);
						if (normals.status() == CesiumGltf::AccessorViewStatus::Valid)
							cluster.normals_.push_back(normals[index]);
						if (uvs.status() == CesiumGltf::AccessorViewStatus::Valid)
							cluster.uvs_.push_back(uvs[index]);
						if (accessorViews.colors_.status() == CesiumGltf::AccessorViewStatus::Valid)
							cluster.colors_.push_back([&]
								{
									// Convert color to VEC4<UNSIGNED_BYTE>.
									using InputColor = typename decltype(accessorViews.colors_)::value_type;
									// First, convert each component to UNSIGNED_BYTE,
									// keeping the same dimension (VEC3 or VEC4) as the input color.
									const auto color = [&]()->std::array<uint8_t, std::tuple_size_v<InputColor>>
										{
											const auto& color = accessorViews.colors_[index];
											if constexpr (std::is_same_v<uint8_t, typename InputColor::value_type>)
												return color;
											else if constexpr (std::is_same_v<uint16_t, typename InputColor::value_type>)
											{
												std::array<uint8_t, std::tuple_size_v<InputColor>> color2;
												for (auto c = 0; c < color.size(); ++c)
													color2[c] = uint8_t(color[c]>>8);
												return color2;
											}
											else if constexpr (std::is_same_v<float, typename InputColor::value_type>)
											{
												std::array<uint8_t, std::tuple_size_v<InputColor>> color2;
												for (auto c = 0; c < color.size(); ++c)
													color2[c] = std::min((uint8_t)255, uint8_t(color[c]*256));
												return color2;
											}
											else
												static_assert(std::is_void_v<InputColor>);
										}();
									// Now, adjust the dimension if needed.
									return [&]()->std::array<uint8_t, 4>
										{
											if constexpr (std::tuple_size_v<InputColor> == 3)
												return {color[0], color[1], color[2], 0xff};
											else if constexpr (std::tuple_size_v<InputColor> == 4)
												return color;
											else
												static_assert(std::is_void_v<InputColor>);
										}();
								}());
						if (accessorViews.featureIds_.status() == CesiumGltf::AccessorViewStatus::Valid)
							cluster.featureIds_.push_back({(float)accessorViews.featureIds_[index][0]});

						// Record the position of this vertex inside its cluster.
						newIndices[index] = (int)cluster.positions_.size()-1;
					}
					cluster.indices_.push_back({(uint32_t)newIndices[index]});
				}
			};
		// Process each piece, depending on the primitive topology.
		// The indices to use for triangle strips etc are specified here:
		// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview.
		switch (primitive.mode)
		{
			case CesiumGltf::MeshPrimitive::Mode::POINTS:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size(); ++pieceIndex)
					processPiece(std::to_array({pieceIndex}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::LINES:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size()/2; ++pieceIndex)
					processPiece(std::to_array({2*pieceIndex, 2*pieceIndex+1}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::LINE_LOOP:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size(); ++pieceIndex)
					processPiece(std::to_array({pieceIndex, (pieceIndex+1)%(int)accessorViews.indices_.size()}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::LINE_STRIP:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size()-1; ++pieceIndex)
					processPiece(std::to_array({pieceIndex, pieceIndex+1}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::TRIANGLES:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size()/3; ++pieceIndex)
					processPiece(std::to_array({3*pieceIndex, 3*pieceIndex+1, 3*pieceIndex+2}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_STRIP:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size()-2; ++pieceIndex)
					processPiece(std::to_array({pieceIndex, pieceIndex+1+pieceIndex%2, pieceIndex+2-pieceIndex%2}));
				break;
			case CesiumGltf::MeshPrimitive::Mode::TRIANGLE_FAN:
				for (auto pieceIndex = 0; pieceIndex < accessorViews.indices_.size()-2; ++pieceIndex)
					processPiece(std::to_array({pieceIndex+1, pieceIndex+2, 0}));
				break;
		}
	}

};

} // unnamed namespace



class GltfTuner::Impl
{
public:
	Rules rules_;
	int rulesVersion_ = 0; //! Used to detect if we should recompute rules derived data.
	GltfTunerRulesEx rulesEx_;
	// Tune() and SetRules() can be called by different threads
	// (typically Tune() is called on a background thread),
	// so we have to protect access to data used by both methods.
	std::mutex mutex_;

	std::vector<ITwinMaterialInfo> itwinMaterials_;
	std::shared_ptr<GltfMaterialHelper> materialHelper_;
};

GltfTuner::GltfTuner()
	:impl_(new Impl())
{
}

GltfTuner::~GltfTuner()
{
}

CesiumGltf::Model GltfTuner::Tune(const CesiumGltf::Model& model,
	const glm::dmat4& tileTransform, const glm::dvec4& rootTranslation)
{
	// Test if we should recompute rules derived data.
	bool isRulesExOutdated = false;
	{
		std::scoped_lock lock(impl_->mutex_);
		if (impl_->rulesVersion_ > impl_->rulesEx_.version_)
		{
			isRulesExOutdated = true;
			impl_->rulesEx_.version_ = impl_->rulesVersion_;
			(Rules&)impl_->rulesEx_ = impl_->rules_;
		}

		if (isRulesExOutdated)
		{
			impl_->rulesEx_.elementToGroup_.clear();
			for (auto groupIndex = 0; groupIndex < impl_->rulesEx_.elementGroups_.size(); ++groupIndex)
				for (const auto elementId : impl_->rulesEx_.elementGroups_[groupIndex].elements_)
					impl_->rulesEx_.elementToGroup_[elementId] = groupIndex;
		}
	}
	// Avoid numeric issues when computing fast UVs from positions, by compensating the (usually huge)
	// translation of the model.
	glm::dmat4x4 const tileTransform_shifted = tileTransform - glm::dmat4x4(
		glm::dvec4(0.),
		glm::dvec4(0.),
		glm::dvec4(0.),
		rootTranslation);
	return GltfTunerHelper(model, impl_->rulesEx_, impl_->materialHelper_, tileTransform_shifted).Tune();
}

void GltfTuner::SetRules(Rules&& rules)
{
	// Here we do not test if the new rules actually differ from the current ones.
	// For now we assume it is the responsibility of the caller to call this only when needed.
	std::scoped_lock lock(impl_->mutex_);
	impl_->rules_ = std::move(rules);
	++impl_->rulesVersion_;
}


namespace Detail
{
	inline uint64_t ToUint64(const rapidjson::Value& jsonVal)
	{
		if (jsonVal.IsString())
		{
			// convert from hexadecimal
			char* pLastUsed;
			return std::strtoull(jsonVal.GetString(), &pLastUsed, 16);
		}
		else if (jsonVal.IsUint64())
		{
			return jsonVal.GetUint64();
		}
		else if (jsonVal.IsInt64())
		{
			return static_cast<uint64_t>(jsonVal.GetInt64());
		}
		else if (jsonVal.IsUint())
		{
			return jsonVal.GetUint();
		}
		else if (jsonVal.IsInt())
		{
			return static_cast<uint64_t>(jsonVal.GetInt());
		}
		return 0;
	}
}

void GltfTuner::ParseTilesetJson(const rapidjson::Document& tilesetJson)
{
	// Detect and parse property "iTwinMaterials", if any
	// This can be added by the Mesh-Export Service for Cesium tilesets.
	const auto assetIt = tilesetJson.FindMember("asset");
	if (assetIt == tilesetJson.MemberEnd())
		return;
	const rapidjson::Value& assetJson = assetIt->value;
	const auto extrasIt = assetJson.FindMember("extras");
	if (extrasIt == assetJson.MemberEnd())
		return;
	const rapidjson::Value& extrasJson = extrasIt->value;
	const auto itwinMatsIt = extrasJson.FindMember("iTwinMaterials");
	if (itwinMatsIt == extrasJson.MemberEnd())
		return;
	const rapidjson::Value& itwinMatsJson = itwinMatsIt->value;
	if (!itwinMatsJson.IsArray())
		return;

	std::vector<ITwinMaterialInfo> itwinMaterials;
	itwinMaterials.reserve(itwinMatsJson.Size());
	for (auto matIt = itwinMatsJson.Begin();
		matIt != itwinMatsJson.End();
		++matIt)
	{
		auto idIt = matIt->FindMember("id");
		auto nameIt = matIt->FindMember("name");
		if (idIt == matIt->MemberEnd() || nameIt == matIt->MemberEnd())
		{
			// Invalid material description
			continue;
		}
		ITwinMaterialInfo& matInfo = itwinMaterials.emplace_back();
		matInfo.id = Detail::ToUint64(idIt->value);
		matInfo.name = nameIt->value.GetString(); // UTF-8 encoded
	}
	{
		std::scoped_lock lock(impl_->mutex_);
		impl_->itwinMaterials_ = itwinMaterials;
	}
	if (onMaterialInfoParsed_)
	{
		// Perform custom operation once all material IDs are read.
		onMaterialInfoParsed_(itwinMaterials);
	}
}

bool GltfTuner::HasITwinMaterialInfo() const
{
	std::scoped_lock lock(impl_->mutex_);
	return !impl_->itwinMaterials_.empty();
}

std::vector<ITwinMaterialInfo> GltfTuner::GetITwinMaterialInfo() const
{
	std::scoped_lock lock(impl_->mutex_);
	return impl_->itwinMaterials_;
}

void GltfTuner::SetMaterialInfoReadCallback(ITwinMaterialInfoReadCallback const& readCallback)
{
	onMaterialInfoParsed_ = readCallback;
}

void GltfTuner::SetMaterialHelper(std::shared_ptr<GltfMaterialHelper> const& matHelper)
{
	impl_->materialHelper_ = matHelper;
}

} // namespace BeUtils
