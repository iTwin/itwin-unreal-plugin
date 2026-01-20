/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumTileID.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Cesium3DTilesSelection/TileID.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/container_hash/hash.hpp>
	#include <boost/functional/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{
	// Cesium3DTilesSelection::TileID are not unique in a given tileset (as stated in the documentation), but
	// we do need unique identifiers for our scene mapping, so we introduced a second element of
	// identification for the known cases of non uniqueness which are (at least) the up-sampling mechanism
	// used by raster overlays (cartographic polygons).
	using CesiumTileID = std::pair<Cesium3DTilesSelection::TileID, std::string>;
}

namespace std
{
	// provide with missing hashing support for Cesium TileID
	// (please note that the hashing of a std::variant is automatically
	// generated, provided each type of the variant exists).

	template <>
	struct hash<CesiumGeometry::OctreeTileID>
	{
	public:
		std::size_t operator()(const CesiumGeometry::OctreeTileID& key) const
		{
			std::size_t  res = std::hash<std::size_t>()(uint64(key.level));
			boost::hash_combine(res, uint64(key.x));
			boost::hash_combine(res, uint64(key.y));
			boost::hash_combine(res, uint64(key.z));
			return res;
		}
	};

	template <>
	struct hash<CesiumGeometry::UpsampledQuadtreeNode>
	{
	public:
		std::size_t operator()(const CesiumGeometry::UpsampledQuadtreeNode& key) const
		{
			return std::hash<CesiumGeometry::QuadtreeTileID>()(key.tileID);
		}
	};

} // ns std

namespace CesiumGeometry
{
	inline bool operator == (
		const UpsampledQuadtreeNode& key1,
		const UpsampledQuadtreeNode& key2)
	{
		return key1.tileID == key2.tileID;
	}

	inline std::size_t hash_value(OctreeTileID const& v) noexcept
	{
		return std::hash<OctreeTileID>()(v);
	}

	inline std::size_t hash_value(QuadtreeTileID const& v) noexcept
	{
		return std::hash<QuadtreeTileID>()(v);
	}

	inline std::size_t hash_value(UpsampledQuadtreeNode const& v) noexcept
	{
		return std::hash<UpsampledQuadtreeNode>()(v);
	}

} // ns CesiumGeometry

namespace Cesium3DTilesSelection
{
	inline std::size_t hash_value(TileID const& v) noexcept
	{
		return std::hash<TileID>()(v);
	}

} // ns Cesium3DTilesSelection
