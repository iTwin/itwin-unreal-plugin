/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "SplineDefines.h"

#include <BeUtils/SplineSampling/MathTypes.h>

#include <BeUtils/SplineSampling/ControlledCurve.h>

#include <array>
#include <variant>


namespace BeUtils
{
	//------------------------------------------------------------
	//
	//	Caching system for baked splines.
	//
	//------------------------------------------------------------
	struct CachedSegments
	{
		std::vector<Segment_2D> segments_; // set of lines representing the spline.
		BoundingBox bbox_; // 3D bounding box of the spline, for given resolution.
		double dU_; // resolution of the baked set of lines.
	};

	class CachedSegmentsContainer
	{
	public:
		CachedSegmentsContainer() {}
		~CachedSegmentsContainer() {}

		//! Inserts the given set of segments for future use.
		bool RecordSegments(const std::vector<Segment_2D>& segments, BoundingBox const& bbox,
			E2DProjection projection, double dU, double dUTolerance = 1e-8);

		//! Retrieves the given set of segments at given resolution and projection,
		//! if it exists (with given tolerance).
		bool RetrieveSegments(std::vector<Segment_2D>& segments, BoundingBox& bbox,
			E2DProjection projection, double dU, double dUTolerance = 1e-8) const;

		void Clear();

	private:
		typedef std::vector<CachedSegments> CacheArray;
		std::array<CacheArray, 3> perProjectionData_;
	};


	//------------------------------------------------------------
	//
	//	SplineHelper -> encapsulates a spline for use in the
	//		EcosystemPopulator.
	//
	//------------------------------------------------------------

	class SplineHelper
	{
	public:
		typedef SplineCurve::vector_type vector_type; // type of the KeyPoints
		typedef SplineCurve::value_type value_type; // type of the abscissa
		using vec2_type = glm::dvec2;

		SplineHelper(SplineCurve const* splineCurve);
		SplineHelper(SplineHelper const& other);
		virtual ~SplineHelper();

		SplineHelper& operator = (SplineHelper const& rhs);

		void SetSplineCurve(SplineCurve const* splineCurve);
		SplineCurve const* GetSplineCurve() const { return pCurve_; }

		//! Returns the Object position of the spline for curvilinear abscissa u.
		vector_type GetPosition(value_type const& u) const;
		//! Returns the World position of the spline for curvilinear abscissa u.
		vector_type GetPosition_world(value_type const& u, TransformHolder const& transform) const;
		//! Returns the tangent of the spline for curvilinear abscissa u (in Object coordinates).
		vector_type GetTangentAtCoord(value_type const& u) const;

		//! Returns the number of control points in the spline.
		size_t CountControlPoints() const;
		//! Returns the position of the control point of given index, in Object coordinates.
		//! @param index Index of the control point - must be in range [0, CountControlPoints() - 1]
		vector_type GetControlPointPosition(size_t index) const;
		//! Returns the world position of the control point of given index.
		//! @param index Index of the control point - must be in range [0, CountControlPoints() - 1]
		vector_type GetControlPointPosition_world(size_t index, TransformHolder const& transform) const;

		////! Returns the (tangent,binormal,normal) of the spline for curvilinear abscissa u (in World coordinates).
		//void GetWorldTBNAtCoord(value_type const& u,
		//	vector_type& tangent, vector_type& binormal, vector_type& normal,
		//	TransformHolder const& transform) const;

		enum class EPathRegularSamplingMode
		{
			FixedNbSamples,
			FixedSpacing
		};
		//! Sample the spline regularly, targeting either a fixed number of positions or a fixed spacing.
		size_t GetRegularSamples(
			std::vector<vector_type>& samples,
			EPathRegularSamplingMode samplingMode,
			std::variant<size_t, double> const& fixedCountOrDistance,
			TransformHolder const& transform,
			E2DProjection projection = E2DProjection::None) const;


		//! Clean cache (data recorded to avoid recomputing the spline for a given resolution several times).
		void InvalidateCache();


		//---------------------------------------------------------------------------------------------------------
		// Conversion to segments (useful for display, conversion to a map, etc.)
		//---------------------------------------------------------------------------------------------------------
		//! Converts the spline into a set of segments, projected onto a plane, and expressed
		//! in world coordinates.
		//! @param segments (out) Resulting collection of segments.
		//! @param dU The resolution which is the increment of curvilinear abscissa between two
		//!		consecutive sampled positions. It should always be in ]0.;1.].
		//! @param dCurveLen Length of the whole curve.
		//! @param bbox (out) Bounding box of the returned segments.
		//! @param trfObject Object to use for transformations.
		//! @param projection Direction of the projection specifying the projection plane.
		//! @return Size of the #segments vector upon exit (= number of segments produced?).
		size_t ComputeSegments(std::vector<Segment_2D>& segments, value_type const& dU,
			value_type const& dCurveLen, BoundingBox& bbox, TransformHolder const& transform,
			E2DProjection projection = E2DProjection::Z_Axis) const;

		//---------------------------------------------------------------------------------------------------------
		// Curve length and Velocity
		//	- the name comes from the fact the curvilinear abscissa can be understood as a time
		//---------------------------------------------------------------------------------------------------------
		//void MakeConstantVelocity();

		//! Returns an approximation of the total length of the spline projected along given axis, based on
		//! the control points only.
		double GetControlPointsPathLength(TransformHolder const& transform, E2DProjection projection = E2DProjection::None) const;

		//! Returns an approximation of the total length of the spline projected along given axis, with dS as
		//! time increment.
		double EvalSplineLength(TransformHolder const& transform, double& dMaxVelocity, value_type const& dU, E2DProjection projection = E2DProjection::None) const;

		//! Returns average speed along the spline projected along given axis, if the curvilinear abscissa is
		//! understood as a time) - based on the control points only.
		value_type GetMeanVelocity(TransformHolder const& transform, E2DProjection projection = E2DProjection::None) const;

		//! Returns maximum speed along the spline projected along given axis, if the curvilinear abscissa is
		//! understood as a time) - based on the control points only.
		value_type EvalMaxVelocity(TransformHolder const& transform, value_type& splineLenEvaluation, E2DProjection projection = E2DProjection::None) const;

		//! Returns average speed along the spline projected along given axis, if the curvilinear abscissa is
		//! understood as a time).
		value_type EvalMeanVelocity(TransformHolder const& transform, double& dMaxVelocity, value_type const& dU, E2DProjection projection = E2DProjection::None) const;

	private:
		value_type GetTotalDeltaU() const;
		value_type GetTotalTime() const { return GetTotalDeltaU(); } //!< Added for convenience (S understood as time)

	private:
		SplineCurve const* pCurve_;

		mutable CachedSegmentsContainer cache_; //!< Cache used to avoid recomputing the same spline at a given resolution several times).
	};
}
