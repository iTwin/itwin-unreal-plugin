/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

namespace ITwin::Schedule::Interpolators {

//! Default interpolator does a linear interpolation.
class Default
{
public:
	template<class _T>
	_T operator ()(const _T& x0, const _T& x1, float u) const;

	template<class _T>
	void WillInterpolateBetween(const _T& /*x0*/, const _T& /*x1*/, void* /*userData*/) const
		{ /*no-op for almost everything*/ }
};

//! Interpolates plane equations.
class PlaneEquation
{
public:
	FVector4f operator ()(const FVector4f& x0, const FVector4f& x1, float u) const;
	void WillInterpolateBetween(const FVector4f&, const FVector4f&, void*) const {}
};

//! Interpolates bool values by doing an "and" between them (for 0 < u < 1).
class BoolAnd
{
public:
	bool operator ()(bool x0, bool x1, float u) const;
	void WillInterpolateBetween(const bool&, const bool&, void*) const {}
};

//! Interpolates bool values by doing an "or" between them (for 0 < u < 1).
class BoolOr
{
public:
	bool operator ()(bool x0, bool x1, float u) const;
	void WillInterpolateBetween(const bool&, const bool&, void*) const {}
};

} // namespace ITwin::Schedule::Interpolators

#include "Interpolators.inl"
