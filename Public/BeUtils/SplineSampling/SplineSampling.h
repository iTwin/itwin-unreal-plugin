/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineSampling.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <BeUtils/SplineSampling/ControlledCurve.h>

#include <optional>

namespace BeUtils
{
	enum class ESplineSamplingMode
	{
		AlongPath,
		Stroke = AlongPath,

		Interior,
		Fill = Interior
	};

	struct SplineSamplingParameters
	{
		ESplineSamplingMode samplingMode = ESplineSamplingMode::Interior;

		// 2-D options (interior sampling)
		float density = 0.5f; // 50%
		float allowedCoverage = 0.8f; // 80%

		// 1-D options (path sampling)
		std::optional<uint32_t> fixedNbInstances;

		// Common options
		bool forceAligned = false;
		bool forbidOverlap = false;
		std::optional<glm::dvec2> fixedSpacing; // use 'x' coordinate for 1-D case

		uint32_t randSeed = 0xbac1981;
	};

	//! Sample a spline.
	void SampleSpline(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions);
}
