/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneAPICameraUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "SceneAPIJsonTypes.h"

namespace AdvViz::SDK::SceneAPIDetails
{
	// Columns of the stored 3x4: (direction, right, up, position) = UE (X, Y, Z, T).
	constexpr size_t kCamDir[3] = { 0, 4, 8 };
	constexpr size_t kCamRight[3] = { 1, 5, 9 };
	constexpr size_t kCamUp[3] = { 2, 6, 10 };
	constexpr size_t kCamPos[3] = { 3, 7, 11 };


	//! Fills up/direction/position from an already filled ecefTransform.
	//! Precondition: the transform must be a valid camera placement, i.e. its columns
	//! (direction, right, up) must form an orthonormal triad whose handedness matches
	//! the GCS currently in use.
	//! \param handedness +1 if the client -> ECEF map preserves orientation, -1 if it is
	//!        a reflection (see IGCSTransform::GetHandedness).
	//!        Note the stored triad is (direction, right, up): a reflecting map stores
	//!        right = cross(direction, up), an orientation-preserving one
	//!        right = cross(up, direction). Equivalently, always:
	//!        dot(cross(direction, up), right) == -handedness.
	inline void FillCameraVectorsFromTransform(SJsonCamera& cam, [[maybe_unused]] double handedness)
	{
		if (!cam.ecefTransform)
			return;
		auto const& m = *cam.ecefTransform;
		cam.direction = { m[kCamDir[0]], m[kCamDir[1]], m[kCamDir[2]] };
		cam.up = { m[kCamUp[0]],  m[kCamUp[1]],  m[kCamUp[2]] };
		cam.position = { m[kCamPos[0]], m[kCamPos[1]], m[kCamPos[2]] };

#ifndef RELEASE_CONFIG
		double3 const dir{ m[kCamDir[0]], m[kCamDir[1]], m[kCamDir[2]] };
		double3 const upv{ m[kCamUp[0]],  m[kCamUp[1]],  m[kCamUp[2]] };
		double3 const storedRight{ m[kCamRight[0]], m[kCamRight[1]], m[kCamRight[2]] };
		double const alignment = Dot3(Cross3(dir, upv), storedRight);
		// Engine-agnostic: the triad must be orthonormal...
		BE_ASSERT(std::fabs(alignment) > 0.99,
			"camera basis is not an orthonormal triad");
		// ...and oriented as the current GCS produces it.
		BE_ASSERT(alignment * handedness < 0.,
			"camera basis handedness does not match the GCS");
#endif
	}

	//! Rebuilds the 3x4 link transform from up/direction/position (iTwin viewer case,
	//! where ecefTransform is identity). Returns false on degenerate input.
	//! \param handedness see FillCameraVectorsFromTransform.
	inline bool TransformFromCameraVectors(JsonVector const& direction,
		JsonVector const& up,
		JsonVector const& position,
		double handedness,
		dmat3x4& outTransform)
	{
		double3 dir{ direction.x, direction.y, direction.z };
		double3 upv{ up.x, up.y, up.z };
		if (!Normalize3(dir) || !Normalize3(upv))
			return false;
		double const d = Dot3(upv, dir);
		upv = { upv[0] - d * dir[0], upv[1] - d * dir[1], upv[2] - d * dir[2] };
		if (!Normalize3(upv))
			return false; // colinear up/direction
		// A reflecting GCS (left-handed client space, e.g. Unreal) yields this
		// cross-product order; an orientation-preserving one yields the opposite.
		double3 const right = (handedness < 0.) ? Cross3(dir, upv) : Cross3(upv, dir);

		outTransform = Identity34;
		for (size_t i = 0; i < 3; ++i)
		{
			outTransform[kCamDir[i]] = dir[i];
			outTransform[kCamRight[i]] = right[i];
			outTransform[kCamUp[i]] = upv[i];
		}
		outTransform[kCamPos[0]] = position.x;
		outTransform[kCamPos[1]] = position.y;
		outTransform[kCamPos[2]] = position.z;
		return true;
	}
}
