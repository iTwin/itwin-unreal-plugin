/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSceneMappingTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/StrongTypes/TaggedValue.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwinScene
{
	DEFINE_STRONG_UINT32(TileIdx);
	constexpr TileIdx NOT_TILE{ (uint32_t)-1 };
	DEFINE_STRONG_UINT64(ElemIdx);
	constexpr ElemIdx NOT_ELEM{ (size_t)-1 };
	DEFINE_STRONG_UINT64(DuplIdx);
	constexpr DuplIdx NOT_DUPL{ (size_t)-1 };
}

namespace ITwinTile
{
	/// For a given tile, this is a random access index into the tile's FElementFeaturesCont member
	DEFINE_STRONG_UINT32(ElemIdx);
	constexpr ElemIdx NOT_ELEM{ (uint32_t)-1 };
	/// For a given tile, this is a random access index into the tile's FExtractedElementCont member
	DEFINE_STRONG_UINT32(ExtrIdx);
	constexpr ExtrIdx NOT_EXTR{ (uint32_t)-1 };
}
