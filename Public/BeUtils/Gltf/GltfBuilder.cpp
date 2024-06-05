/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfBuilder.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <BeUtils/Gltf/GltfBuilder.h>
#include <CesiumGltf/Model.h>
#include <CesiumGltf/ExtensionModelExtStructuralMetadata.h>

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

void GltfBuilder::AddMetadataProperty(const std::string& name, const std::vector<uint64_t> values)
{
	auto& extension = impl_->model_.addExtension<CesiumGltf::ExtensionModelExtStructuralMetadata>();
	if (!extension.schema)
	{
		extension.schema.emplace();
		auto& propertyTable = extension.propertyTables.emplace_back();
		propertyTable.classProperty = "MeshPart";
		propertyTable.count = values.size();
	}
	{
		auto& property = extension.schema->classes["MeshPart"].properties[name];
		property.type = CesiumGltf::ClassProperty::ComponentType::UINT64;
	}
	{
		auto& property = extension.propertyTables.back().properties[name];
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
	const auto oldBufferSize = impl_->model_.buffers[0].cesium.data.size();
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
