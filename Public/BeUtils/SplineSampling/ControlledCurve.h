/*--------------------------------------------------------------------------------------+
|
|     $Source: ControlledCurve.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <BeUtils/SplineSampling/MathTypes.h>

#include <optional>
#include <vector>

namespace BeUtils
{
namespace path
{
	// Some generic interfaces (used in Engine/EffectPath.h, spline/engine/Spline.hpp)

	/// Simplest interface describing a path, either linear or Bezier, etc.
	template <typename V>
	class GenericCurve
	{
	public:
		typedef V vector_type;
		typedef double value_type;

		virtual ~GenericCurve();

		/// Get position at a given linear abscissa.
		virtual V GetPositionAtCoord(value_type const & u) const = 0;
		/// Get tangent at a given linear abscissa.
		virtual V GetTangentAtCoord(value_type const & u) const = 0;

		///// Get "TBN" (Tangent, Binormal, Normal) vector triplet at a given linear abscissa.
		///// Note that we enforce "stability" of the base (no abrupt orientation changes in the
		///// middle of the curve) as long as the tangent vector is not (almost) colinear to Z.
		///// This means the normal do not necessarily points towards the center of the circle
		///// tangent to the curve (inside the curvature).
		//virtual void GetLocalTBN(value_type const & u, V & tangent, V & binormal,
		//	V & normal, bool useFrenetTwist = false) const = 0;

		/// \return true if the curve forms a closed path.
		virtual bool IsCyclic() const { return false; }
		virtual bool SetCyclic() { return false; }

		//virtual BoundingBox GetAABB() const = 0;
	};

	/// Curve with some notion of control points.
	template <typename V>
	class ControlledCurve : public virtual GenericCurve<V>
	{
	public:
		/// Number of control points.
		/// \param accountForCyclicity If the curve is cyclic (ie. closed) and
		///		this parameter is set to true, the first control point (if any)
		///		should be counted twice to account for the fact that it serves
		///		also as last control point to close the curve.
		virtual size_t PointCount(const bool accountForCyclicity) const = 0;

		/// Get position of a control point.
		virtual V GetPositionAtIndex(size_t idx) const = 0;
	};

	extern template class GenericCurve<glm::dvec3>;

} // namespace path

	using SplineCurve = path::ControlledCurve<glm::dvec3>;

} // namespace BeUtils
