/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinePattern.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "SplinePattern.h"

#include "SplineHelper.h"


namespace BeUtils
{
	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
	//		class PopulationPath_1D
	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
	PopulationPath_1D::~PopulationPath_1D()
	{

	}

	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
	//		class SplineBase2DPattern
	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

	SplineBase2DPattern::SplineBase2DPattern(TransformHolder const& splineObject,
											SplineHelper const& spline,
											E2DProjection projection /*= Z_Axis*/,
											float fQuality /*= 1.f*/)
	:	Population2DPattern(splineObject, projection, fQuality),
		spline_(spline)
	{}

	double SplineBase2DPattern::GetMeanVelocity() const
	{
		return spline_.GetMeanVelocity(splineObject_);
	}

	size_t SplineBase2DPattern::Bake2DSegmentsOnSpline(
		std::vector<Segment_2D>& segments,
		double dS, double splineLen,
		BoundingBox& bbox,
		E2DProjection projection /*= Pr2D_Z_Axis*/) const
	{
		return spline_.ComputeSegments(segments, dS, splineLen, bbox, splineObject_, projection);
	}

	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//		class SplinePattern
	//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

	SplinePattern::SplinePattern(TransformHolder const& splineObject, SplineHelper const& spline,
								 E2DProjection projection /*= Z_Axis*/,
								 float fQuality /*= 1.f*/)
	:	SplineBase2DPattern(splineObject, spline, projection, fQuality)
	{}

	double SplinePattern::GetMaxVelocity(double &splineLenEvaluation) const
	{
		return spline_.EvalMaxVelocity(splineObject_, splineLenEvaluation, projection_);
	}

	size_t SplinePattern::Bake2DSegments(
		std::vector<Segment_2D>& segments,
		double dS, double splineLen,
		BoundingBox& bbox,
		E2DProjection projection /*= Pr2D_Z_Axis*/) const
	{
		return spline_.ComputeSegments(segments, dS, splineLen, bbox, splineObject_, projection);
	}
}

