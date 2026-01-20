/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinePattern.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "SplineDefines.h"
#include <BeUtils/SplineSampling/MathTypes.h>

#include <optional>


namespace BeUtils
{
	struct ImpactInfo;

	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//	Basic class for 1D-population (population along a path)
	//		-> provides positions and normal along a curvilinear abscissa (defined on segment [0..1])
	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	class PopulationPath_1D
	{
	public:
		PopulationPath_1D(TransformHolder const* pSplineObject) : pSplineObject_(pSplineObject) {}
		virtual ~PopulationPath_1D();

		virtual double GetPathLength() const = 0;
		virtual double GetCurveWidth() const = 0;
		virtual glm::dvec3 GetInstancePositionAt(double s, ImpactInfo& impactInfo) const = 0; //!< Returns the (world) position at abscissa s. Additional information (UV coords, normal, position in Object coordinates) can be provided in impactInfo.
		virtual glm::dvec3 GetTangentAtCoord(double u) const = 0;
		virtual bool MoveInstancePositionWithProfile(glm::dvec3& vLocation, double s, double radiusAmplitude, double angle, ImpactInfo& impactInfo) const = 0;

		TransformHolder const* GetSplineObject() const { return pSplineObject_; }

	protected:
		TransformHolder const* pSplineObject_; // needed to get world transformation.
	};


	class SplineHelper;

	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	// Base class for all 2D population patterns.
	//	-> for now, it can be either a ribbon, or an enclosure (special case of Spline)
	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	class Population2DPattern
	{
	public:
		enum class EPatternType
		{
			Ribbon,
			Enclosure
		};

		Population2DPattern(TransformHolder const& splineObject,
			E2DProjection projection = E2DProjection::Z_Axis, float fQuality = 1.f)
			: splineObject_(splineObject)
			, projection_(projection)
			, fSamplingQuality_(fQuality)
		{}
		virtual ~Population2DPattern() {}

		// Accessors.
		E2DProjection GetProjection() const { return projection_; }
		float GetSamplingQuality() const { return fSamplingQuality_; }
		bool IsOcclusion() const { return bIsOcclusion_; }
		double GetOcclusionInfluence() const { return dInfluence_; }
		TransformHolder const& GetSplineObject() const { return splineObject_; }
		/// Can return NULL (see Polygon2DPattern).
		virtual const SplineHelper * GetSpline() const = 0;

		// Modifiers.
		void SetProjection(E2DProjection proj) { projection_ = proj; }
		void SetSamplingQuality(float qual) { fSamplingQuality_ = qual; }
		void SetOcclusion(bool isoccl) { bIsOcclusion_ = isoccl; }
		void SetOcclusionInfluence(double dInfl) { dInfluence_ = dInfl; }

		virtual EPatternType GetType() const = 0;
		virtual double GetMeanVelocity() const { return 0; }
		virtual double GetMaxVelocity(double &splineLenEvaluation) const { splineLenEvaluation = 0.0; return 0.0; }

		/// Baking 2D segments: see doc on #SplineHelper::ComputeSegments
		virtual size_t Bake2DSegments(
			std::vector<Segment_2D>& segments,
			double dS,
			double splineLen,
			BoundingBox& bbox,
			E2DProjection projection) const = 0;
		virtual size_t Bake2DSegmentsOnSpline(
			std::vector<Segment_2D>& segments,
			double dS,
			double splineLen,
			BoundingBox& bbox,
			E2DProjection projection) const = 0;

	protected:
		TransformHolder const& splineObject_; // needed to get world transformation.
		E2DProjection projection_ = E2DProjection::None;
		float fSamplingQuality_ = 1.f;
		double dInfluence_ = 1.0;
		bool bIsOcclusion_ = true;
	};


	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//	Base class for all 2D patterns defined by a spline.
	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	class SplineBase2DPattern : public Population2DPattern
	{
	public:
		SplineBase2DPattern(TransformHolder const& splineObject, SplineHelper const& spline,
			E2DProjection projection = E2DProjection::Z_Axis, float fQuality = 1.f);

		SplineHelper const* GetSpline() const override { return &spline_; }
		double GetMeanVelocity() const override;
		size_t Bake2DSegmentsOnSpline(
			std::vector<Segment_2D>& segments,
			double dS, double splineLen, BoundingBox& bbox,
			E2DProjection projection) const override;

	protected:
		SplineHelper const& spline_;
	};

	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	//	2D patterns defined by a closed spline.
	//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	class SplinePattern : public SplineBase2DPattern
	{
	public:
		SplinePattern(TransformHolder const& splineObject, SplineHelper const& spline,
			E2DProjection projection = E2DProjection::Z_Axis, float fQuality = 1.f);

		EPatternType GetType() const override { return EPatternType::Enclosure; }
		double GetMaxVelocity(double &splineLenEvaluation) const override;
		size_t Bake2DSegments(
			std::vector<Segment_2D>& segments,
			double dS, double splineLen, BoundingBox& bbox,
			E2DProjection projection) const override;
	};
}
