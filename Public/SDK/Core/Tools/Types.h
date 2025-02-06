/*--------------------------------------------------------------------------------------+
|
|     $Source: Types.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <array>

namespace SDK::Core
{
	typedef std::array<float, 4> float4;
	typedef std::array<float, 3> float3;
	typedef std::array<float, 2> float2;
	typedef std::array<float, 12> mat4x3;
	typedef std::array<float, 12> mat3x4;
	typedef std::array<float, 16> mat4x4;

	typedef std::array<double, 4> double4;
	typedef std::array<double, 3> double3;
	typedef std::array<double, 2> double2;
	typedef std::array<double, 12> dmat4x3;
	typedef std::array<double, 12> dmat3x4;
	typedef std::array<double, 16> dmat4x4;

	template<typename T>
	inline T& ColRow(std::array<T, 16>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)4);
		BE_ASSERT(row < (unsigned)4);
		return m[col * 4 + row];
	}

	template<typename T>
	inline T& ColRow4x3(std::array<T, 12>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)4);
		BE_ASSERT(row < (unsigned)3);
		return m[col * 3 + row];
	}

	template<typename T>
	inline const T& ColRow4x3(const std::array<T, 12>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)4);
		BE_ASSERT(row < (unsigned)3);
		return m[col * 3 + row];
	}

	template<typename T>
	inline const T& ColRow3x4(const std::array<T, 12>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)3);
		BE_ASSERT(row < (unsigned)4);
		return m[col * 4 + row];
	}

	template<typename T>
	inline T& ColRow3x4(std::array<T, 12>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)3);
		BE_ASSERT(row < (unsigned)4);
		return m[col * 4 + row];
	}
	
}
