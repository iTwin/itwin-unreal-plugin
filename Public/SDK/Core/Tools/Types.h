/*--------------------------------------------------------------------------------------+
|
|     $Source: Types.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <glm/glm.hpp>

namespace crtmath
{
	typedef glm::vec4	float4;
	typedef glm::vec3	float3;
	typedef glm::vec2	float2;

	typedef glm::dmat4 mat4x4d;
	typedef glm::dvec2 double2;
	typedef glm::dvec3 double3;
	typedef glm::dvec4 double4;
	typedef glm::mat4	Matrix4x4;
	typedef glm::mat4x3 mat4x3;
	typedef glm::mat3x4 mat3x4;
	typedef glm::dmat4x3 dmat4x3;
	typedef glm::dmat3x4 dmat3x4;
	typedef mat4x4d	Matrix4x4d;

	typedef Matrix4x4 float4x4;

	typedef glm::ivec2 int2;
	typedef glm::ivec3 int3;
	typedef glm::ivec4 int4;
	
	typedef glm::u32vec2 uint2;
	typedef glm::u32vec3 uint3;
	typedef glm::u32vec4 uint4;

	typedef glm::bvec2 bool2;
	typedef glm::bvec3 bool3;
	typedef glm::bvec4 bool4;
}
