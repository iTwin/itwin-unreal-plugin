/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesConstants.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

/// Disable transformations-related code: can still be useful for debugging/profiling, eg. to avoid the
/// performance cost of extracting entities for transformations, not just ignoring them during replay
#define SYNCHRO4D_ENABLE_TRANSFORMATIONS() 1

/// Small time offset in seconds, used to avoid strictly superimposed key frames
#define KEYFRAME_TIME_EPSILON .01

#define PRIVATE_S4D_BGR_DISABLED_VAL 0
#define PRIVATE_S4D_BGR_DISABLED \
	PRIVATE_S4D_BGR_DISABLED_VAL, PRIVATE_S4D_BGR_DISABLED_VAL, PRIVATE_S4D_BGR_DISABLED_VAL

namespace ITwin
{
	constexpr size_t INVALID_IDX = static_cast<size_t>(-1);
}

namespace ITwin::Synchro4D
{
	/// Overwrite color components (in place) as needed to avoid using the special value (just adds 1 to the
	/// Green component)
	/// \param ColorBGR Must support operator[](int) with index R=0, G=1 and B=2
	template<typename T> T& ReplaceDisabledColorInPlace(T& ColorBGR)
	{
		if (ColorBGR[0] == PRIVATE_S4D_BGR_DISABLED_VAL
			&& ColorBGR[1] == PRIVATE_S4D_BGR_DISABLED_VAL
			&& ColorBGR[2] == PRIVATE_S4D_BGR_DISABLED_VAL)
		{
			ColorBGR[2] += 1;
		}
		return ColorBGR;
	}

	extern bool s_bMaskTilesUntilFullyAnimated; // ITwinSynchro4DAnimator.cpp
}

/// Special BGR value to signal coloring should be disabled in the material shader.
/// Note: value is currently hardcoded in the shaders! (MI_ITwin_S4D_AnimateBatchedFeatures asset and
/// corresponding Translucent asset).
#define S4D_MAT_BGR_DISABLED { PRIVATE_S4D_BGR_DISABLED }
/// Helper macro to get a usable BGRA pixel value
#define S4D_MAT_BGRA_DISABLED(alpha) { PRIVATE_S4D_BGR_DISABLED, alpha }

/// Special value to disable clipping in the material shader: a null orientation is not a valid plane normal
/// anyway, so nothing fancy here, this is the only logical 'disabled' value.
#define S4D_CLIPPING_DISABLED { 0.f, 0.f, 0.f, 0.f }
#define S4D_CLIPPING_DISABLED_VEC4 FVector4::ZeroVector

namespace ITwin_TestOverrides
{
	/// Global override for pageSize for paginated requests. Defaults to -1 (= disabled), to be set to a
	/// positive value when needed for use during unit testing.
	/// See SchedulesImport.cpp and FITwinSchedulesImport::FImpl::RequestPagination
	extern int RequestPagination;
	/// Global override for the hard cap on the size of filter ElementID arrays. Defaults to -1 (= disabled),
	/// to be set to a positive value when needed for use during unit testing.
	/// See SchedulesImport.cpp and FITwinSchedulesImport::FImpl::MaxElementIDsFilterSize
	extern int64_t MaxElementIDsFilterSize;
}
