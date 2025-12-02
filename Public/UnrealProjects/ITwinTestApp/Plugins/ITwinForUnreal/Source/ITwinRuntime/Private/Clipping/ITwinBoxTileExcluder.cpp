/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinBoxTileExcluder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinBoxTileExcluder.h>

bool UITwinBoxTileExcluder::ContainsBox(SharedProperties const& BoxProperties) const
{
	return std::find(BoxPropertiesArray.begin(), BoxPropertiesArray.end(), BoxProperties) != BoxPropertiesArray.end();
}

void UITwinBoxTileExcluder::RemoveBox(SharedProperties const& BoxProperties)
{
	auto it = std::find(BoxPropertiesArray.begin(), BoxPropertiesArray.end(), BoxProperties);
	if (it != BoxPropertiesArray.end())
	{
		BoxPropertiesArray.erase(it);
	}
}

inline bool UITwinBoxTileExcluder::ShouldExcludeTileForBox(const UCesiumTile* TileObject,
	const SharedProperties& BoxProperties) const
{
	if (BoxProperties->bInvertEffect)
	{
		// The box behaves as an eraser: we should ignore a given tile only if it is fully
		// inside the erasing box.
		return BoxProperties->BoxBounds.GetBox().IsInsideOrOn(TileObject->Bounds.GetBox());
	}
	else
	{
		const bool bOverlap = FBoxSphereBounds::BoxesIntersect(TileObject->Bounds, BoxProperties->BoxBounds);
		return !bOverlap;
	}
}

bool UITwinBoxTileExcluder::ShouldExclude_Implementation(const UCesiumTile* TileObject)
{
	int BoxesAskingExclusion = 0;
	for (auto const& BoxProperties : BoxPropertiesArray)
	{
		if (ShouldExcludeTileForBox(TileObject, BoxProperties))
		{
			BoxesAskingExclusion++;
		}
		else
		{
			return false;
		}
	}
	return BoxesAskingExclusion > 0;
}
