/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfBuilder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include <glm/fwd.hpp>

namespace CesiumGltf
{
class MeshPrimitive;
class Model;
struct Node;
}

namespace BeUtils
{

//! Helper for building a GLTF model.
//! Initially used by the GltfTuner and its unit tests.
class GltfBuilder
{
public:
	class MeshPrimitive
	{
	public:
		//! \param shouldOptimize If true, indices will use the smallest possible data type.
		//!     For example, if all indices are <= 255, they will use UNSIGNED_BYTE format.
		template<class _T>
		void SetIndices(const std::vector<std::array<_T, 1>>& indices, bool shouldOptimize);
		void SetPositions(const std::vector<std::array<float, 3>>& positions);
		void SetNormals(const std::vector<std::array<float, 3>>& normals);
		void SetUVs(const std::vector<std::array<float, 2>>& uvs);
		template<class _T, size_t _n>
		void SetColors(const std::vector<std::array<_T, _n>>& colors);
		template<class _T>
		void SetFeatureIds(const std::vector<std::array<_T, 1>>& featureIds, bool bShareBufferForMatIDs = false);
		void SetITwinMaterialID(uint64_t materialId);
	private:
		MeshPrimitive(GltfBuilder& builder, CesiumGltf::MeshPrimitive& primitive);
		GltfBuilder& builder_;
		CesiumGltf::MeshPrimitive& primitive_;
		friend class GltfBuilder;
	};
	GltfBuilder();
	~GltfBuilder();
	CesiumGltf::Model& GetModel();
	//! Currently only usable for adding the same metadata as the mesh export service,
	//! eg. "element", "model" etc.
	//! Used in unit tests.
	void AddMetadataProperty(const std::string& className,
		const std::string& propertyName,
		const std::vector<uint64_t>& values,
		size_t featureSetIndex = 0);
	MeshPrimitive AddMeshPrimitive(int32_t mesh, int32_t material, int32_t mode);
	int32_t AddMaterial();
	//! Compute some default UVs for the given primitive, when none were read from the initial glTF model
	//! but we need some for custom materials.
	bool ComputeFastUVs(MeshPrimitive& primitive,
		const std::vector<std::array<float, 3>>& positions,
		const std::vector<std::array<float, 3>>& normals,
		const std::vector<std::array<uint32_t, 1>>& indices,
		const glm::dmat4& tileTransform,
		const CesiumGltf::Node* gltfNode);

private:
	class Impl;
	const std::unique_ptr<Impl> impl_;
	template<class _T>
	int32_t AddBufferView(const std::vector<_T>& data, const std::optional<int32_t>& target);
	int32_t AddBufferView(const void* data,
		size_t byteLength,
		const std::optional<int64_t>& byteStride,
		const std::optional<int32_t>& target);
	template<class _T>
	int32_t AddAccessor(int32_t bufferView, const std::vector<_T>& data, bool normalized);
	int32_t AddAccessor(int32_t bufferView,
		int32_t componentType,
		bool normalized,
		int64_t count,
		const std::string& type,
		const std::vector<double>& max,
		const std::vector<double>& min);
};

} // namespace BeUtils

#include <BeUtils/Gltf/GltfBuilder.inl>
