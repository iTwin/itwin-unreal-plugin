/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfTuner.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/TilesetExternals.h>
#include <functional>
#include <unordered_set>
#include <rapidjson/document.h>

namespace BeUtils
{

struct ITwinMaterialInfo
{
	uint64_t id = 0;
	std::string name;
};

using ITwinMaterialInfoReadCallback = std::function<void(std::vector<ITwinMaterialInfo> const&)>;

class GltfTuner: public Cesium3DTilesSelection::GltfTuner
{
public:
	//! Specifies how primitives should be merged or split.
	struct Rules
	{
		//! A list of element IDs that should be merged together and assigned the given material.
		//! This means that even if 2 faces have different materials but their element ID belongs
		//! to the same group, they will be merged in the same primitive.
		//! Merging can still be prevented in these cases:
		//! - primitives do not have the same topology (eg. we cannot merge lines and triangles,
		//!   but we can merge triangle lists and triangle strips, as strips are converted to lists),
		//! - primitives do not have the same attribute list (eg. primitive 1 has UVs, but primitive 2 does not).
		struct ElementGroup
		{
			std::vector<uint64_t> elements_;
			int32_t material_ = -1; // gltf material index
			std::optional<uint64_t> itwinMaterialID_; // identifier in the original iModel
		};
		//! The list of element groups.
		//! IDs belonging to different groups cannot be merged together.
		//! All IDs not contained in any group can be merged together, as long as the primitives have
		//! same topology, same material, same attribute list.
		std::vector<ElementGroup> elementGroups_;
		//! iTwin material IDs to split (when the user wants to edit/replace materials, typically)
		std::unordered_set<uint64_t> itwinMatIDsToSplit_;
	};
	GltfTuner();
	~GltfTuner();
	virtual CesiumGltf::Model Tune(const CesiumGltf::Model& model) override;
	virtual void ParseTilesetJson(const rapidjson::Document& tilesetJson) override;
	void SetRules(Rules&& rules);

	bool HasITwinMaterialInfo() const;
	std::vector<ITwinMaterialInfo> GetITwinMaterialInfo() const;
	void SetMaterialInfoReadCallback(ITwinMaterialInfoReadCallback const&);

private:
	class Impl;
	const std::unique_ptr<Impl> impl_;

	ITwinMaterialInfoReadCallback onMaterialInfoParsed_;
};

} // namespace BeUtils
