/*--------------------------------------------------------------------------------------+
|
|     $Source: Internal_mathConv.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Types.h"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

namespace AdvViz::SDK::internal
{

#define GLMCONV(type1, type2)\
	static_assert(sizeof(type1) == sizeof(glm::type2));\
	inline glm::type2& toGlm(type1& m) {return *(glm::type2*)(&m);}\
	inline const glm::type2& toGlm(const type1& m) {return *(const glm::type2*)(&m);}\
	inline type1& toSDK(glm::type2& m) {return *(type1*)(&m);} \
	inline const type1& toSDK(const glm::type2& m) {return *(const type1*)(&m); }

	GLMCONV(double2, dvec2);
	GLMCONV(double3, dvec3);
	GLMCONV(double4, dvec4);
	GLMCONV(dmat4x4, dmat4x4);
	GLMCONV(dmat3x3, dmat3x3);

}