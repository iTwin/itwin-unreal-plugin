/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingConstants.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

namespace ITwin
{
	// Maximum number of clipping primitives supported by the shaders.
	// These drive the generation of MPC_Clipping (see UITwinClippingMPCGenerator::RegenerateClippingMPC):
	// changing them requires regenerating the asset AND a full material recompile.
	// Budget check (MPC caps at 1024 scalars and 1024 vectors):
	//   vectors = 4*MAX_CLIPPING_BOXES + MAX_CLIPPING_PLANES
	//   scalars = 6 + CLIPPING_MASK_WORDS * (MAX_CLIPPING_BOXES + MAX_CLIPPING_PLANES)
	static constexpr int MAX_CLIPPING_PLANES = 64;
	static constexpr int MAX_CLIPPING_BOXES = 128;

	// Per-primitive "affected model groups" bitmask width.
	// 2 words x 24 bits = 48 model groups (group 0 reserved for "influenced by nothing").
	// Must match ITWIN_CLIPPING_MASK_WORDS in ClippingCommon.ush.
	static constexpr int CLIPPING_MASK_WORDS = 2;

	// 24 bits: integers up to 2^24 are exactly representable in a float32 mantissa, and MPC
	// scalars are floats. Do NOT raise to 32 without switching to asuint() round-tripping.
	static constexpr int CLIPPING_BITS_PER_MASK_WORD = 24;
}
