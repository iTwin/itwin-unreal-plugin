/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfBuilder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <BeUtils/Gltf/GltfBuilder.h>
#include <BeUtils/Gltf/ExtensionITwinMaterialID.h>
#include <CesiumGltf/Model.h>
#include <CesiumGltf/ExtensionModelExtStructuralMetadata.h>
#include <CesiumGltfContent/GltfUtilities.h>
#include <SDK/Core/Tools/Assert.h>

#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace BeUtils
{

class GltfBuilder::Impl
{
public:
	CesiumGltf::Model model_;
};

GltfBuilder::MeshPrimitive::MeshPrimitive(GltfBuilder& builder, CesiumGltf::MeshPrimitive& primitive)
	:builder_(builder)
	,primitive_(primitive)
{
}

void GltfBuilder::MeshPrimitive::SetPositions(const std::vector<std::array<float, 3>>& positions)
{
	primitive_.attributes["POSITION"] = builder_.AddAccessor(
		builder_.AddBufferView(positions, CesiumGltf::BufferView::Target::ARRAY_BUFFER),
		positions, false);
}

void GltfBuilder::MeshPrimitive::SetNormals(const std::vector<std::array<float, 3>>& normals)
{
	primitive_.attributes["NORMAL"] = builder_.AddAccessor(
		builder_.AddBufferView(normals, CesiumGltf::BufferView::Target::ARRAY_BUFFER),
		normals, false);
}

void GltfBuilder::MeshPrimitive::SetUVs(const std::vector<std::array<float, 2>>& uvs)
{
	primitive_.attributes["TEXCOORD_0"] = builder_.AddAccessor(
		builder_.AddBufferView(uvs, CesiumGltf::BufferView::Target::ARRAY_BUFFER),
		uvs, false);
}

void GltfBuilder::MeshPrimitive::SetITwinMaterialID(uint64_t materialId)
{
	auto& matIdExt = primitive_.addExtension<BeUtils::ExtensionITwinMaterialID>();
	matIdExt.materialId = materialId;
}

GltfBuilder::GltfBuilder()
	:impl_(new Impl())
{
	impl_->model_.buffers.emplace_back();
}

GltfBuilder::~GltfBuilder()
{
}

CesiumGltf::Model& GltfBuilder::GetModel()
{
	return impl_->model_;
}

void GltfBuilder::AddMetadataProperty(const std::string& className,
	const std::string& propertyName,
	const std::vector<uint64_t>& values,
	size_t featureSetIndex /*= 0*/)
{
	auto& extension = impl_->model_.addExtension<CesiumGltf::ExtensionModelExtStructuralMetadata>();
	if (!extension.schema)
	{
		extension.schema.emplace();
	}
	if (featureSetIndex >= extension.propertyTables.size())
	{
		extension.schema->classes.emplace(className, CesiumGltf::Class());

		extension.propertyTables.resize(featureSetIndex + 1);
		auto& propertyTable = extension.propertyTables[featureSetIndex];
		propertyTable.classProperty = className;
		propertyTable.count = values.size();
	}
	{
		auto& property = extension.schema->classes[className].properties[propertyName];
		property.type = CesiumGltf::ClassProperty::ComponentType::UINT64;
	}
	{
		auto& propertyTable = extension.propertyTables[featureSetIndex];
		BE_ASSERT(propertyTable.classProperty == className);
		BE_ASSERT(propertyTable.count == values.size());

		auto& property = propertyTable.properties[propertyName];
		property.values = AddBufferView(values, {});
	}
}

auto GltfBuilder::AddMeshPrimitive(int32_t mesh, int32_t material, int32_t mode)->MeshPrimitive
{
	MeshPrimitive primitive(*this, impl_->model_.meshes[mesh].primitives.emplace_back());
	primitive.primitive_.material = material;
	primitive.primitive_.mode = mode;
	return primitive;
}

int32_t GltfBuilder::AddMaterial()
{
	impl_->model_.materials.emplace_back();
	return int32_t(impl_->model_.materials.size()-1);
}

namespace
{
	// Very naive and fast normal computation
	void ComputeFlatNormals(
		const std::vector<std::array<float, 3>>& positions,
		std::vector<std::array<float, 3>>& outNormals,
		const std::vector<std::array<uint32_t, 1>>& indices)
	{
		size_t const nbVerts = positions.size();
		size_t const nbIndices = indices.size();
		std::vector<bool> isNormalSet(nbVerts, false);
		std::vector<std::array<float, 3>> normals(nbVerts, { 0., 0., 1. });

		auto const setIfNeeded = [&](uint32_t const ind, glm::vec3 const& n)
		{
			if (!isNormalSet[ind]) {
				auto& normal(normals[ind]);
				normal = { n.x, n.y, n.z };
				isNormalSet[ind] = true;
			}
		};

		for (size_t curIndex(0); curIndex + 2 < nbIndices; curIndex += 3)
		{
			uint32_t const ind0 = indices[curIndex + 0][0];
			uint32_t const ind1 = indices[curIndex + 1][0];
			uint32_t const ind2 = indices[curIndex + 2][0];
			auto const& pos0 = positions[ind0];
			auto const& pos1 = positions[ind1];
			auto const& pos2 = positions[ind2];
			glm::vec3 const p0(pos0[0], pos0[1], pos0[2]);
			glm::vec3 const p1(pos1[0], pos1[1], pos1[2]);
			glm::vec3 const p2(pos2[0], pos2[1], pos2[2]);
			glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
			if (fabs(normal.x) + fabs(normal.y) + fabs(normal.z) > 1e-7f)
			{
				normal = glm::normalize(normal);
				setIfNeeded(ind0, normal);
				setIfNeeded(ind1, normal);
				setIfNeeded(ind2, normal);
			}
		}
		outNormals.swap(normals);
	}
}

bool GltfBuilder::ComputeFastUVs(MeshPrimitive& primitive,
	const std::vector<std::array<float, 3>>& positions,
	const std::vector<std::array<float, 3>>& srcNormals,
	const std::vector<std::array<uint32_t, 1>>& indices,
	const glm::dmat4& tileTransform,
	const CesiumGltf::Node* gltfNode)
{
	if (positions.empty())
	{
		return false;
	}
	// If no normals are provided, use some flat normals:
	std::vector<std::array<float, 3>> flatNormals;
	if (srcNormals.empty())
	{
		ComputeFlatNormals(positions, flatNormals, indices);
	}
	auto const& normals = srcNormals.empty() ? flatNormals : srcNormals;
	if (positions.size() != normals.size())
	{
		BE_ISSUE("expecting one position and one normal per vertex");
		return false;
	}

	using namespace CesiumGltfContent;
	glm::dmat4 rootTransform = tileTransform;
	rootTransform = GltfUtilities::applyRtcCenter(impl_->model_, rootTransform);
	rootTransform = GltfUtilities::applyGltfUpAxisTransform(impl_->model_, rootTransform);
	glm::dmat4 fullTransform;
	if (gltfNode)
		fullTransform = rootTransform * GltfUtilities::getNodeTransform(*gltfNode).value_or(glm::dmat4x4(1.0));
	else
		fullTransform = rootTransform;
	const glm::dmat3 normalTsf = glm::inverseTranspose(glm::dmat3(fullTransform));

	const size_t nbVerts = positions.size();

	std::vector<std::array<float, 2>> uvs;
	uvs.resize(nbVerts);

	// Very fast and basic UV computation
	for (size_t i(0); i < nbVerts; ++i)
	{
		auto& uv = uvs[i];

		auto const& pos_i = positions[i];
		const glm::vec3 position = { pos_i[0], pos_i[1], pos_i[2] };
		const glm::dvec3 p =
			glm::dvec3(fullTransform * glm::dvec4(position, 1.0));

		auto const& n_i = normals[i];
		const glm::vec3 normal = { n_i[0], n_i[1], n_i[2] };
		const glm::dvec3 n =
			normalTsf * glm::dvec3(normal);

		const auto nx = std::fabs(n.x);
		const auto ny = std::fabs(n.y);
		const auto nz = std::fabs(n.z);

		if (nz > ny && nz > nx)
		{
			// projection along Z axis
			uv = { (float)p.x, (float)p.y };
		}
		else if (ny > nx && ny > nz)
		{
			// projection along Y axis
			uv = { (float)p.x, (float)p.z };
		}
		else
		{
			// projection along X axis
			uv = { (float)p.y, (float)p.z };
		}
	}
	primitive.SetUVs(uvs);
	return true;
}

int32_t GltfBuilder::AddBufferView(const void* data,
	size_t byteLength,
	const std::optional<int64_t>& byteStride,
	const std::optional<int32_t>& target)
{
	auto& bufferView = impl_->model_.bufferViews.emplace_back();
	bufferView.buffer = 0;
	bufferView.byteOffset = impl_->model_.buffers[0].cesium.data.size();;
	bufferView.byteLength = (byteLength+7)/8*8;
	bufferView.byteStride = byteStride;
	bufferView.target = target;
	[[maybe_unused]] const auto oldBufferSize = impl_->model_.buffers[0].cesium.data.size();
	impl_->model_.buffers[0].cesium.data.resize(impl_->model_.buffers[0].cesium.data.size()+bufferView.byteLength);
	memcpy(impl_->model_.buffers[0].cesium.data.data()+bufferView.byteOffset, data, byteLength);
	return int32_t(impl_->model_.bufferViews.size()-1);
}

int32_t GltfBuilder::AddAccessor(int32_t bufferView,
	int32_t componentType,
	bool normalized,
	int64_t count,
	const std::string& type,
	const std::vector<double>& max,
	const std::vector<double>& min)
{
	auto& accessor = impl_->model_.accessors.emplace_back();
	accessor.bufferView = bufferView;
	accessor.componentType = componentType;
	accessor.normalized = normalized;
	accessor.count = count;
	accessor.type = type;
	accessor.max = max;
	accessor.min = min;
	return int32_t(impl_->model_.accessors.size()-1);
}

} //namespace BeUtils
