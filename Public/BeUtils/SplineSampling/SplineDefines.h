/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineDefines.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <glm/vec2.hpp>

namespace BeUtils
{
	enum class E2DProjection
	{
		None = -1, /* no projection at all */
		X_Axis = 0,
		Y_Axis = 1,
		Z_Axis = 2
	};

	struct Segment_2D
	{
		using vec2_type = glm::dvec2;

		vec2_type posStart_, posEnd_;
		/// Curvilinear abscissa of the middle of the segment (except for the final
		/// segment closing a curve, for which a value of 0. seems accepted - see
		/// SplineHelper::ComputeSegments).
		double uCoord_;

		Segment_2D(vec2_type const& posStart,
				   vec2_type const& posEnd,
				   double uCoord = -1.0)
			: posStart_(posStart)
			, posEnd_(posEnd)
			, uCoord_(uCoord)
		{
		}
	};
}

// In Carrot, we only deal with population zone of path
#define HAS_SPLINE_RIBBON_PATTERN() 0

// Former e-on "Dev" mode
#define IS_EON_DEV() 0
