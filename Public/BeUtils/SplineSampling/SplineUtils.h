/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "SplineHelper.h"

// MUST be included before EnumSwitchCoverage...
#include <SDK/Core/Tools/Assert.h>
#include <BeHeaders/Compil/EnumSwitchCoverage.h>

#include <glm/gtx/norm.hpp>

namespace BeUtils
{
	struct Basic2DProjector
	{
		typedef SplineHelper::vector_type vector_type;
		typedef SplineHelper::vec2_type vec2_type;

		E2DProjection const proj_;

		Basic2DProjector(E2DProjection proj)
			: proj_(proj)
		{
		}

		vec2_type operator()(vector_type const& pos) const
		{
			switch (proj_)
			{
			BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case E2DProjection::None:)
			case E2DProjection::X_Axis: return vec2_type(pos.y, pos.z);
			case E2DProjection::Y_Axis: return vec2_type(pos.z, pos.x);
			case E2DProjection::Z_Axis: return vec2_type(pos.x, pos.y);
			}
		}

		[[nodiscard]] vector_type Project(vector_type const& pos) const
		{
			switch (proj_)
			{
			case E2DProjection::X_Axis: return vector_type(0.0, pos.y, pos.z);
			case E2DProjection::Y_Axis: return vector_type(pos.x, 0.0, pos.z);
			case E2DProjection::Z_Axis: return vector_type(pos.x, pos.y, 0.0);
			case E2DProjection::None: return pos;
			BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN(pos);
			}
		}
	};

	struct SplinePointInfo
	{
		SplineHelper::value_type uCoord_; // curvilinear abscissa
		SplineHelper::vector_type vWorldPos_; // world position
		SplineHelper::vec2_type normal2d_; // 2d normal

		SplinePointInfo(double u, SplineHelper::vector_type const& pos)
			: uCoord_(u)
			, vWorldPos_(pos)
			, normal2d_(0.)
		{}
	};

	struct SplineSampler
	{
		typedef SplineHelper::value_type value_type;
		typedef SplineHelper::vector_type vector_type;
		typedef SplineHelper::vec2_type vec2_type;

		SplineSampler() = default;

		size_t Sample(
			SplineHelper const& splineHelper,
			TransformHolder const& splineObj,
			value_type dU,
			value_type dCurveLen);

		void Compute2dNormals(
			Basic2DProjector const& projector,
			double dU_delta,
			SplineHelper const& splineHelper,
			TransformHolder const& splineObj);

		inline vec2_type const& Get2dNormalAtIndex(size_t index) const
		{
			return pts_[index].normal2d_;
		}

		std::vector<SplinePointInfo> pts_;
	};


	inline void Normalize2dNormal(SplineHelper::vec2_type& avgNorm)
	{
		auto const lengthSquared = glm::length2(avgNorm);
		if (lengthSquared < 1e-8f) {
			avgNorm = SplineHelper::vec2_type(0.0, 1.0);
		}
		else {
			avgNorm /= glm::sqrt(lengthSquared);
		}
	}

	[[nodiscard]] inline
	SplineHelper::vec2_type Get2dNormal(SplineHelper::vec2_type const& pos1, SplineHelper::vec2_type const& pos2)
	{
		SplineHelper::vec2_type pos2d_delta = pos2 - pos1;
		SplineHelper::vec2_type normal2d;
		normal2d.x = -pos2d_delta.y;
		normal2d.y = pos2d_delta.x;
		// normalize
		Normalize2dNormal(normal2d);
		return normal2d;
	}
}
