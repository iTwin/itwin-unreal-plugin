/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineSampling.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <BeUtils/SplineSampling/ControlledCurve.h>
#include <BeUtils/SplineSampling/Spline2DProjector.h>

#include <memory>
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
		float gridRotAngle = 0.f; // in radians

		// 1-D options (path sampling)
		std::optional<uint32_t> fixedNbInstances;
		std::optional<glm::dvec2> randomSpacing; // range [min, max] distance between instances

		// Common options
		bool forceAligned = false;
		bool forbidOverlap = false;
		// Fixed spacing between instances. 'x' must be > 0.
		// - 1-D (path) mode: only 'x' is used.
		// - 2-D (interior) aligned mode: 'x' and 'y' are the cell sizes; if 'y' <= 0, 'x' is used for
		//   both axes (square cells).
		std::optional<glm::dvec2> fixedSpacing;
		std::unique_ptr<Spline2DProjector> customProjector;

		uint32_t randSeed = 0xbac1981;
	};

	//! Sample a spline interior (Fill mode).
	void SampleSplineInterior(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions);

	//! Sample a spline path (Stroke mode).
	void SampleSplinePath(SplineCurve const& spline,
		TransformHolder const& transform,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions);

	//! Sample a spline (either interior or path, depending on params.samplingMode).
	void SampleSpline(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions);
}
