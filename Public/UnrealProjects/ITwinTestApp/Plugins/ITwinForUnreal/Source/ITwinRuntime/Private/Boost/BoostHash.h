/*--------------------------------------------------------------------------------------+
|
|     $Source: BoostHash.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Math/Matrix.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

namespace UE::Math { // DON'T CHANGE, this is the NS the UE types below are in!

template<typename UEMathType> struct hasher
{
	std::size_t operator()(UEMathType const& v) const noexcept
	{
		return GetTypeHash(v);
	}
};

template<> struct hasher<TRotator<double>>
{
	std::size_t operator()(const TRotator<double>& v) noexcept
	{
		return GetTypeHash(v.Vector());
	}
};

template<> struct hasher<TMatrix<double>>
{
	std::size_t operator()(const TMatrix<double>& m) noexcept
	{
		// ComputeHash is "for debugging purposes", but we don't need it to be stable
		return m.ComputeHash();
	}
};

template<typename UEMathType>
std::size_t hash_value(const UEMathType& v) noexcept
{
	return hasher<UEMathType>()(v);
}

} // ns UE::Math

inline std::size_t hash_value(FString const& v) noexcept
{
	return GetTypeHash(v);
}
