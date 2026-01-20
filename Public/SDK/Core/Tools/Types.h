/*--------------------------------------------------------------------------------------+
|
|     $Source: Types.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <array>
#include <limits>

#include "../Tools/Assert.h"

namespace AdvViz::SDK
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
	typedef std::array<double, 9> dmat3x3;
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

	template<typename T>
	inline T& ColRow4x4(std::array<T, 16>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)4);
		BE_ASSERT(row < (unsigned)4);
		return m[col * 4 + row];
	}

	template<typename T>
	inline const T& ColRow4x4(const std::array<T, 16>& m, unsigned col, unsigned row)
	{
		BE_ASSERT(col < (unsigned)4);
		BE_ASSERT(row < (unsigned)4);
		return m[col * 4 + row];
	}

	struct GCS
	{
		std::string wkt;
		double3 center; // lat, lon, height 
	};

	struct BoundingBox {
		double3 min = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
		double3 max = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };
		template<typename T>
		bool Contains(const std::array<T, 3>& v) const
		{
			return min[0] <= v[0] && v[0] <= max[0]
				&& min[1] <= v[1] && v[1] <= max[1]
				&& min[2] <= v[2] && v[2] <= max[2];
		}
	};

	struct TimeRange
	{
		float begin = 0.f, end = 0.f;

		bool operator<(const TimeRange& t) const
		{
			if (t.begin == begin)
				return t.end < end;
			return t.begin < begin;
		}
	};

	template<typename T, unsigned N, typename T2 >
	inline void Copy(const T2& src, std::array<T, N>& dest)
	{
		for (unsigned i = 0; i < N; ++i)
			dest[i] = src[i];
	}

	template<typename T, unsigned N, typename T2 >
	inline void Copy(const std::array<T, N>& src, T2& dest)
	{
		for (unsigned i = 0; i < N; ++i)
			dest[i] = src[i];
	}
}
