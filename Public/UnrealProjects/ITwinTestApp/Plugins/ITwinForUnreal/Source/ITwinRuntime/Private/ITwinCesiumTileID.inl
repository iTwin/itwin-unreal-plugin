/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCesiumTileID.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "ITwinSceneMapping.h"

namespace ITwin
{
	inline CesiumTileID GetCesiumTileID(const ICesiumLoadedTile& CesiumTile)
	{
		auto const* pTile = CesiumTile.GetTile();
		if (ensure(pTile))
		{
			// In some cases (raster overlay...) tiles are dynamically subdivided, and in such cases, their
			// IDs are no longer unique inside a given tileset => to ensure uniqueness in such cases, we aggregate
			// the first "non subdivided" parent's ID to the final identifier:
			CesiumTileID uniqueID;
			uniqueID.first = pTile->getTileID();

			// Find the first "explicit" tile ID among parents (if we already have one at this level, ignore
			// it).
			auto const* pCurTile = pTile;
			int curParentingLevel = 0;
			do {
				const std::string* pRealTileID =
					std::get_if<std::string>(&pCurTile->getTileID());
				if (pRealTileID)
				{
					if (curParentingLevel > 0)
					{
						uniqueID.second = *pRealTileID;
					}
					break;
				}
				pCurTile = pCurTile->getParent();
				curParentingLevel++;
			} while (pCurTile != nullptr);

			return uniqueID;
		}
		return std::make_pair(CesiumTile.GetTileID(), std::string());
	}
}
