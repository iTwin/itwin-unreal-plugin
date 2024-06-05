/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfBuilder.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <cassert>
#include <array>
#include <CesiumGltf/BufferView.h>
#include <algorithm>
#include <boost/fusion/include/at_key.hpp>
#include <boost/fusion/include/make_map.hpp>
#include <CesiumGltf/Accessor.h>
#include <CesiumGltf/MeshPrimitive.h>
#include <CesiumGltf/ExtensionExtMeshFeatures.h>

namespace BeUtils
{

template<class _T>
void GltfBuilder::MeshPrimitive::SetIndices(const std::vector<std::array<_T, 1>>& indices, bool shouldOptimize)
{
	primitive_.indices = [&]
		{
			if (!shouldOptimize)
				return builder_.AddAccessor(
					builder_.AddBufferView(indices, CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER),
					indices, false);
			const auto maxValue = (*std::max_element(indices.begin(), indices.end()))[0];
			if constexpr (sizeof(_T) > 1)
				if (maxValue <= 0xff)
				{
					std::vector<std::array<uint8_t, 1>> indices2(indices.size());
					std::transform(indices.begin(), indices.end(), indices2.begin(), [](auto x){return std::to_array({(uint8_t)x[0]});});
					return builder_.AddAccessor(
						builder_.AddBufferView(indices2, CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER),
						indices2, false);
				}
			if constexpr (sizeof(_T) > 2)
				if (maxValue <= 0xffff)
				{
					std::vector<std::array<uint16_t, 1>> indices2(indices.size());
					std::transform(indices.begin(), indices.end(), indices2.begin(), [](auto x){return std::to_array({(uint16_t)x[0]});});
					return builder_.AddAccessor(
						builder_.AddBufferView(indices2, CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER),
						indices2, false);
				}
			return builder_.AddAccessor(
				builder_.AddBufferView(indices, CesiumGltf::BufferView::Target::ELEMENT_ARRAY_BUFFER),
				indices, false);
		}();
}

template<class _T, size_t _n>
void GltfBuilder::MeshPrimitive::SetColors(const std::vector<std::array<_T, _n>>& colors)
{
	primitive_.attributes["COLOR_0"] = builder_.AddAccessor(
		builder_.AddBufferView(colors, CesiumGltf::BufferView::Target::ARRAY_BUFFER),
		colors, true);
}

template<class _T>
void GltfBuilder::MeshPrimitive::SetFeatureIds(const std::vector<std::array<_T, 1>>& featureIds)
{
	primitive_.attributes["_FEATURE_ID_0"] = builder_.AddAccessor(
		builder_.AddBufferView(featureIds, CesiumGltf::BufferView::Target::ARRAY_BUFFER),
		featureIds, false);
	auto& featureId = primitive_.addExtension<CesiumGltf::ExtensionExtMeshFeatures>().featureIds.emplace_back();
	featureId.featureCount = (*std::max_element(featureIds.begin(), featureIds.end()))[0];
	featureId.attribute = 0;
	featureId.propertyTable = 0;
}

template<class _T>
int32_t GltfBuilder::AddBufferView(const std::vector<_T>& data, const std::optional<int32_t>& target)
{
	return AddBufferView(data.data(), data.size()*sizeof(data[0]), sizeof(data[0]), target);
}

namespace GltfBuilder_Detail
{

static constexpr auto g_gltfAccessorComponentTypes = boost::fusion::make_map<
	int8_t,
	uint8_t,
	int16_t,
	uint16_t,
	uint32_t,
	float
>(
	CesiumGltf::Accessor::ComponentType::BYTE,
	CesiumGltf::Accessor::ComponentType::UNSIGNED_BYTE,
	CesiumGltf::Accessor::ComponentType::SHORT,
	CesiumGltf::Accessor::ComponentType::UNSIGNED_SHORT,
	CesiumGltf::Accessor::ComponentType::UNSIGNED_INT,
	CesiumGltf::Accessor::ComponentType::FLOAT
);

static const auto g_gltfAccessorTypes = boost::fusion::make_map<
	std::integral_constant<std::size_t, 1>,
	std::integral_constant<std::size_t, 2>,
	std::integral_constant<std::size_t, 3>,
	std::integral_constant<std::size_t, 4>
>(
	CesiumGltf::Accessor::Type::SCALAR,
	CesiumGltf::Accessor::Type::VEC2,
	CesiumGltf::Accessor::Type::VEC3,
	CesiumGltf::Accessor::Type::VEC4
);

} // namespace GltfBuilder_Detail

template<class _T>
int32_t GltfBuilder::AddAccessor(int32_t bufferView, const std::vector<_T>& data, bool normalized)
{
	using namespace GltfBuilder_Detail;
	const auto max = *std::max_element(data.begin(), data.end());
	const auto min = *std::min_element(data.begin(), data.end());
	return AddAccessor(bufferView, boost::fusion::at_key<typename _T::value_type>(g_gltfAccessorComponentTypes), normalized,
		data.size(), boost::fusion::at_key<std::integral_constant<size_t, std::tuple_size<_T>::value>>(g_gltfAccessorTypes),
		std::vector<double>(max.begin(), max.end()), std::vector<double>(min.begin(), min.end()));
}

} // namespace BeUtils