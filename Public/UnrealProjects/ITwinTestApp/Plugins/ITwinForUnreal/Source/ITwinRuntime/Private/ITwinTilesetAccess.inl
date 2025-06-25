/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTilesetAccess.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <ITwinTilesetAccess.h>
#include <IncludeITwin3DTileset.h>

namespace ITwin
{

template <typename TilesetWithConstness>
TilesetWithConstness* TGetTileset(AActor const& TilesetOwner)
{
	for (auto& Child : TilesetOwner.Children)
	{
		if (auto* Tileset = Cast<TilesetWithConstness>(Child.Get()))
		{
			return Tileset;
		}
	}
	return nullptr;
}

}
