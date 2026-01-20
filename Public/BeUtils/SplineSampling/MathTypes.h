/*--------------------------------------------------------------------------------------+
|
|     $Source: MathTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <SDK/Core/Tools/Types.h>

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace BeUtils
{
	using BoundingBox = AdvViz::SDK::BoundingBox;

	inline bool IsInitialized(BoundingBox const& bbox)
	{
		return bbox.min[0] <= bbox.max[0]
			&& bbox.min[1] <= bbox.max[1]
			&& bbox.min[2] <= bbox.max[2];
	}

	inline void ExtendBox(BoundingBox& bbox, glm::dvec3 const& pt)
	{
		if (IsInitialized(bbox))
		{
			if (pt.x < bbox.min[0]) bbox.min[0] = pt.x;
			if (pt.y < bbox.min[1]) bbox.min[1] = pt.y;
			if (pt.z < bbox.min[2]) bbox.min[2] = pt.z;

			if (pt.x > bbox.max[0]) bbox.max[0] = pt.x;
			if (pt.y > bbox.max[1]) bbox.max[1] = pt.y;
			if (pt.z > bbox.max[2]) bbox.max[2] = pt.z;
		}
		else
		{
			bbox.min[0] = pt.x;
			bbox.min[1] = pt.y;
			bbox.min[2] = pt.z;

			bbox.max[0] = pt.x;
			bbox.max[1] = pt.y;
			bbox.max[2] = pt.z;
		}
	}

	inline glm::dvec3 GetBoxDimensions(BoundingBox const& bbox)
	{
		if (IsInitialized(bbox))
		{
			return glm::dvec3(
				bbox.max[0] - bbox.min[0],
				bbox.max[1] - bbox.min[1],
				bbox.max[2] - bbox.min[2]
			);
		}
		else
		{
			return glm::dvec3(0.0);
		}
	}

	struct TransformHolder
	{
		glm::dmat3x3 transfrom_ = glm::dmat3x3(1.0);
		glm::dvec3 pos_ = glm::dvec3(0.0);
	};
}
