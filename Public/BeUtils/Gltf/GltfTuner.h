/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTuner.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/GltfModifier.h>
#include <CesiumUtility/JsonValue.h>

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <rapidjson/document.h>

#include <functional>
#include <unordered_set>
#include <variant>


namespace BeUtils {

struct ITwinMaterialInfo
{
	//! Material ID - ie ID of the original iModel RenderMaterial element, filled by the Mesh Export Service in
	//! the glTF's extras, and used as reference for material customizations.
	uint64_t id = 0;
	//! Human-readable name of the source material, encoded in UTF-8.
	std::string name;
};

class GltfMaterialHelper;

using ITwinMaterialInfoReadCallback = std::function<void(std::vector<ITwinMaterialInfo> const&)>;

template <class T, std::size_t N> using SmallVec = boost::container::small_vector<T, N>;
using Anim4DId = std::variant<uint64_t/*ITwinElementID*/, SmallVec<int32_t, 2>/*Timeline indices*/>;

class GltfTuner : public Cesium3DTilesSelection::GltfModifier
{
public:
	//! Callback type for primitive-level pre-fetching in worker thread
	//! Called during glTF model processing for each primitive before clustering/tuning.
	//! This allows Unreal-specific metadata extraction to occur in the same worker thread
	//! as the GltfTuner processing.
	//! 
	//! @param model The glTF model being processed
	//! @param primitive The specific primitive being processed
	//! @param meshIndex Index of the mesh containing this primitive
	//! @param primitiveIndex Index of the primitive within its mesh
	//! @param userData User-provided context data (set via SetPrimitivePreFetchCallback)
	using PrimitivePreFetchCallback = std::function<void(
		const CesiumGltf::Model& model,
		const CesiumGltf::MeshPrimitive& primitive,
		int32_t meshIndex,
		int32_t primitiveIndex,
		void* userData)>;

	//! Specifies how primitives should be merged or split.
	struct Rules
	{
		//! A list of element IDs that could be merged together (unless contradicted by an anim4D group).
		//! This means that even if 2 faces have different gltf materials but their element ID belongs
		//! to the same group, they will be merged into the same primitive.
		//! Besides anim4D, merging can still be prevented in these cases:
		//! - primitives do not have the same topology (eg. we cannot merge lines and triangles,
		//!   but we can merge triangle lists and triangle strips, as strips are converted to lists),
		//! - primitives do not have the same attribute list (eg. primitive 1 has UVs, but not primitive 2).
		//! Note that 2 MaterialGroup's with identical material_ and itwinMaterialID_ members are still
		//! different groups and that must prevent merging.
		struct MaterialGroup
		{
			std::vector<uint64_t> elements_; ///< IDs of Elements in this group
			int32_t material_ = -1; ///< glTF material index
			//! iTwin Material ID: provided if the material has to be customized (generally different from
			//! the iModel RenderMaterial ID found in the original iModel's meta-data).
			std::optional<uint64_t> itwinMaterialID_;
		};
		//! The list of element groups.
		//! Elements belonging to different groups (material or anim4D groups) cannot be merged together.
		//! All Elements not contained in any group can be merged together, as long as the primitives have
		//! same topology, same material, same attribute list.
		//! An Element can belong to a single material group (as opposed to anim4Ds)
		std::vector<MaterialGroup> materialGroups_;
		//! iTwin material IDs to split (when the user wants to edit/replace materials, typically)
		std::unordered_set<uint64_t> itwinMatIDsToSplit_;

		struct Anim4DGroup
		{
			std::vector<uint64_t> elements_; ///< IDs of Elements in this group
			//! Identifier of the 4D animation "group" to which the Elements refer: see anim4DGroups_ below
			Anim4DId ids_;
		};
		std::vector<Anim4DGroup> anim4DGroups_;

		//! Callback for primitive-level pre-fetching (optional, for Unreal integration)
		PrimitivePreFetchCallback primitivePreFetchCallback_;
		void* primitivePreFetchUserData_ = nullptr;
	};
	
	/// \param bTuneWithoutRules Tells whether tuning should occur even with empty rules, eg. with a default
	///		constructed tuner. Useful for unit tests.
	GltfTuner(bool const bTuneWithoutRules = false);
	~GltfTuner();
	CesiumAsync::Future<std::optional<Cesium3DTilesSelection::GltfModifierOutput>> apply(
		Cesium3DTilesSelection::GltfModifierInput&& input) override;
	/// Pass a new set of rules for glTF modification needs because of material customizations.
	/// Do NOT test if the new rules actually differ from the current ones. For now we assume it is the responsibility
	/// of the caller to call this only when needed.
	/// \return The result of a call to getCurrentVersion()
	int64_t SetMaterialRules(Rules&& rules);
	/// Pass a new set of rules for glTF modification needs because of 4D schedule animation. This method WILL test if
	/// the new rules actually differ from the current ones.
	/// \param newCurrentVersion The result of a call to getCurrentVersion(), whatever the boolean value returned
	/// \param bOptimizeForRegularUpdates In practice this is for NextGen incremental schedule updates: we don't want
	///		to retune when not strictly necessary so we'll compare new rules with current rules, and thus ensure the
	///		structures are sorted to facilitate comparisons. No need to incur the CPU cost for Legacy schedules where
	///		the rules are never updated without reloading the tiles from a new changeset, so pass false in that case.
	/// \return Whether the new rules were actually found to differ from the old ones, hence incrementing the version
	bool SetAnim4DRules(Rules&& rules, int64_t& currentVersion, bool bOptimizeForRegularUpdates);

	//! Set a callback to be invoked for each primitive during glTF processing in the worker thread.
	//! This allows Unreal-specific code to pre-fetch metadata alongside GltfTuner's processing.
	//! 
	//! @param callback The callback function to invoke (can be nullptr to disable)
	//! @param userData User-provided context pointer passed to callback (not owned by GltfTuner)
	//! @note Thread-safety: This should be called before tiles are loaded, typically during initialization
	void SetPrimitivePreFetchCallback(PrimitivePreFetchCallback const& callback, void* userData = nullptr);

	bool HasITwinMaterialInfo() const;
	std::vector<ITwinMaterialInfo> GetITwinMaterialInfo() const;
	void SetMaterialInfoReadCallback(ITwinMaterialInfoReadCallback const&);

	void ParseExtras(CesiumUtility::JsonValue::Object const& extras);

	void SetMaterialHelper(std::shared_ptr<GltfMaterialHelper> const& matHelper);

	bool applyForUnitTest(const CesiumGltf::Model& model, const glm::dmat4& tileTransform,
		CesiumGltf::Model& tunedModel);

private:
	CesiumAsync::Future<void> onRegister(
		const CesiumAsync::AsyncSystem& asyncSystem,
		const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor,
		const std::shared_ptr<spdlog::logger>& pLogger,
		const Cesium3DTilesSelection::TilesetMetadata& tilesetMetadata,
		const Cesium3DTilesSelection::Tile& rootTile) override;

	struct Impl;
	const std::unique_ptr<Impl> impl_;

	ITwinMaterialInfoReadCallback onMaterialInfoParsed_;
};

} // namespace BeUtils

namespace std
{
	template <>
	struct hash<BeUtils::SmallVec<int32_t, 2>>
	{
	public:
		std::size_t operator()(BeUtils::SmallVec<int32_t, 2> const& ids) const
		{
			size_t res = 0;
			for (auto&& id : ids)
				boost::hash_combine(res, id);
			return res;
		}
	};

	template <>
	struct hash<BeUtils::Anim4DId>
	{
	public:
		std::size_t operator()(BeUtils::Anim4DId const& ids) const
		{
			if (ids.index() == 0)
			{
				size_t res = 0xFADAFADA;
				boost::hash_combine(res, std::get<0>(ids));
				return res;
			}
			else
			{
				return std::hash<BeUtils::SmallVec<int32_t, 2>>()(std::get<1>(ids));
			}
		}
	};

} // ns std
