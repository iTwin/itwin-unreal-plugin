/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineSampling.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "SplineSampling.h"

#include "OcclusionMap.h"
#include "SplineHelper.h"
#include "SplinePattern.h"


namespace BeUtils
{
	static void SampleSplineInterior(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		if (!IsInitialized(samplingBox_World))
		{
			BE_ISSUE("invalid sampling box");
			return;
		}
		if (averageInstanceDims_World.x <= 0. || averageInstanceDims_World.y <= 0.)
		{
			BE_ISSUE("invalid mean instance dimension");
			return;
		}
		auto const boxDims = GetBoxDimensions(samplingBox_World);
		const double areaToPopulate = boxDims.x * boxDims.y;
		const double objAvgSurface = averageInstanceDims_World.x * averageInstanceDims_World.y;
		const double objAvgLengthWidthRatio = averageInstanceDims_World.y / averageInstanceDims_World.x;

		int nInstances = static_cast<int>(ceil(areaToPopulate / objAvgSurface));

		// compute number of instances for current density
		float popDensity = params.density;
		popDensity *= 100.0f;
		nInstances *= (int)(popDensity * popDensity);
		nInstances /= (100 * 100);

		// compute cellsAlongX and cellsAlongY
		// we assume here that the objects are right next to each other
		double cellSizeX = std::sqrt(areaToPopulate / nInstances);
		double cellSizeY = cellSizeX * objAvgLengthWidthRatio;
		if (params.forceAligned)
		{
			cellSizeX = params.fixedSpacing ? params.fixedSpacing.value().x : averageInstanceDims_World.x;
			cellSizeY = params.fixedSpacing ? params.fixedSpacing.value().y : averageInstanceDims_World.y;
		}
		const int cellsAlongX = static_cast<int>(std::ceil(boxDims.x / cellSizeX));
		const int cellsAlongY = static_cast<int>(std::ceil(boxDims.y / cellSizeY));

		OcclusionMap surfaceGrid(samplingBox_World, cellsAlongX, cellsAlongY);

		SplineHelper splineHelper(&spline);
		SplinePattern spline2DEffect(transform, splineHelper);
		spline2DEffect.SetOcclusion(false);
		surfaceGrid.BuildFrom2DPattern(spline2DEffect);

		surfaceGrid.GetSampledPositions(outPositions, params.forceAligned, params.randSeed);
	}

	static void SampleSplinePath(SplineCurve const& spline,
		TransformHolder const& transform,
		glm::dvec3 const& /*averageInstanceDims_World*/,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		SplineHelper const splineHelper(&spline);
		SplineHelper::EPathRegularSamplingMode const mode = params.fixedSpacing
			? SplineHelper::EPathRegularSamplingMode::FixedSpacing
			: SplineHelper::EPathRegularSamplingMode::FixedNbSamples;
		std::variant<size_t, double> fixedCountOrDistance;
		if (params.fixedSpacing)
		{
			fixedCountOrDistance = params.fixedSpacing->x;
		}
		else if (params.fixedNbInstances)
		{
			fixedCountOrDistance = *params.fixedNbInstances;
		}
		else
		{
			BE_ISSUE("invalid sampling parameters");
			return;
		}
		splineHelper.GetRegularSamples(outPositions, mode, fixedCountOrDistance,
			transform, E2DProjection::Z_Axis);
	}

	void SampleSpline(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		if (params.samplingMode == ESplineSamplingMode::Interior)
		{
			SampleSplineInterior(spline, transform, samplingBox_World, averageInstanceDims_World, params, outPositions);
		}
		else
		{
			SampleSplinePath(spline, transform, averageInstanceDims_World, params, outPositions);
		}
	}
}
