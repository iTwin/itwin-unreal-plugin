/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneAPIJsonTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Core/Tools/Types.h"

#include <optional>
#include <rfl/Rename.hpp>

namespace AdvViz::SDK::SceneAPIDetails
{
	// Types exchanged with the Scene API, in JSON format.
	// Never modify these types, as they are used for serialization/deserialization of JSON data.

	struct JsonVector
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	struct SJsonQuaternion
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		double w = 1.0;
	};

	inline bool IsNullVector(JsonVector const& v, double epsilon = 1e-9)
	{
		return std::fabs(v.x) <= epsilon
			&& std::fabs(v.y) <= epsilon
			&& std::fabs(v.z) <= epsilon;
	}

	//! Returns true if the given 3x4 transform is (close to) the identity.
	//! Note that the JSON is produced by third-party clients and round-tripped through
	//! decimal text, so an exact comparison with Identity34 is not reliable: a term may
	//! come back as 0.9999999999999999 or 1e-17 and would then be considered non-identity.
	inline bool IsIdentity34(dmat3x4 const& m, double epsilon = 1e-9)
	{
		for (size_t i = 0; i < 12; ++i)
		{
			if (std::fabs(m[i] - Identity34[i]) > epsilon)
				return false;
		}
		return true;
	}

	//! Same as IsIdentity34, but for a 4x4 transform.
	inline bool IsIdentity44(dmat4x4 const& m, double epsilon = 1e-9)
	{
		for (size_t i = 0; i < 16; ++i)
		{
			if (std::fabs(m[i] - Identity44[i]) > epsilon)
				return false;
		}
		return true;
	}

	// Known as View3d in scenes-client - corresponds to this schema:
	// https://github.com/iTwin/scenes-client/blob/main/docs/schemas/core/View3d/View3d.1.0.0.json
	struct SJsonCamera
	{
		JsonVector up;
		JsonVector direction;
		JsonVector position;
		bool isOrthographic = false;
		double aspectRatio = 1.0;
		// 'near' and 'far' are legacy macros from <windef.h>: they cannot be used as
		// member names in code reachable from Unreal, hence the renaming.
		rfl::Rename<"far", double> farPlane = 10000000000.;
		rfl::Rename<"near", double> nearPlane = 0.1;
		std::optional<double> fov;   //!< Field of view in radians (perspective views).
		std::optional<double> width; //!< Frustum width in meters (orthographic views).
		std::optional< std::array<double, 16> > ecefTransform;
	};

	struct CameraBasis
	{
		JsonVector direction;
		JsonVector up;
		JsonVector position;
	};


	inline double Dot3(double3 const& a, double3 const& b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}
	inline double3 Cross3(double3 const& a, double3 const& b)
	{
		return { a[1] * b[2] - a[2] * b[1],
				 a[2] * b[0] - a[0] * b[2],
				 a[0] * b[1] - a[1] * b[0] };
	}
	//! Normalizes in place; returns false if the vector is (almost) null.
	inline bool Normalize3(double3& v)
	{
		double const len2 = Dot3(v, v);
		if (len2 < 1e-24)
			return false;
		double const invLen = 1.0 / std::sqrt(len2);
		v[0] *= invLen; v[1] *= invLen; v[2] *= invLen;
		return true;
	}

}
