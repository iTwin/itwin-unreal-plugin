/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTuner.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/GltfModifier.h>

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <rapidjson/document.h>

#include <functional>
#include <unordered_set>
#include <variant>

namespace BeUtils {

struct ITwinMaterialInfo
{
	uint64_t id = 0;
	std::string name; // UTF-8 encoding
};

class GltfMaterialHelper;

using ITwinMaterialInfoReadCallback = std::function<void(std::vector<ITwinMaterialInfo> const&)>;

template <class T, std::size_t N> using SmallVec = boost::container::small_vector<T, N>;
using Anim4DId = std::variant<uint64_t/*ITwinElementID*/, SmallVec<int32_t, 2>/*Timeline indices*/>;

class GltfTuner : public Cesium3DTilesSelection::GltfModifier
{
public:
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
			int32_t material_ = -1; ///< gltf material index
			std::optional<uint64_t> itwinMaterialID_; ///< identifier in the original iModel
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
		//! Elements must be assigned to distinct anim4D "groups" when they cannot be merged together,
		//! because either:
		//!	 * they don't have the same material translucency requirement (only 4D-related, I guess the
		//!	   original material's translucency depends on each ClusterId::material_),
		//!  * or they have different transformation requirements (either none, or one or more task/3Dpath
		//!    assignments)
		//!
		//! This means that all Elements that need to be made translucent _and_ have no transformation can
		//! be put in the same group (much more efficient than the previous extraction method btw).
		//! To investigate severe flickering artifacts (even with alpha=1) when large meshes intertwine
		//! with all other non-translucent meshes, an option has been added to customize the rules, so
		//! we are actually able to use unlimited (per tile!) grouping of translucent Elements, or to
		//! limit grouping per common set of translucent timelines, or to isolate translucent Elements
		//! individually, like with the legacy extraction method. Even though in the end the flickering
		//! does not seem related to tuning, since in fact only extracted meshes do not flicker, all
		//! others do, even untuned gltf meshes...
		//!
		//! For Elements which need to be transformable, we will use as "group Id" the list of the indices
		//! (in FITwinScheduleTimeline's container) of the transform-needing timelines they belong to.
		//! Usually there will be only one, but Elements can refer to several timelines, when belonging to
		//! several transformed resources, or to a transformed resource assigned to different tasks along
		//! with other resources.
		//! Note that anim4DGroups_ will never change (at least with Legacy schedules), hence the separate
		//! SetAnim4DRules method to avoid rebuilding the multimap everytime a material is edited :/
		std::vector<Anim4DGroup> anim4DGroups_;
	};
	/// \param bTuneWithoutRules Tells whether tuning should occur even with empty rules, eg. with a default
	///		constructed tuner. Useful for unit tests.
	GltfTuner(bool const bTuneWithoutRules = false);
	~GltfTuner();
	virtual bool apply(const CesiumGltf::Model& model, const glm::dmat4& tileTransform,
		const glm::dvec4& rootTranslation, CesiumGltf::Model& tunedModel) override;
	virtual void parseTilesetJson(const rapidjson::Document& tilesetJson) override;
	int SetMaterialRules(Rules&& rules);
	int SetAnim4DRules(Rules&& rules);

	bool HasITwinMaterialInfo() const;
	std::vector<ITwinMaterialInfo> GetITwinMaterialInfo() const;
	void SetMaterialInfoReadCallback(ITwinMaterialInfoReadCallback const&);

	void SetMaterialHelper(std::shared_ptr<GltfMaterialHelper> const& matHelper);

private:
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
