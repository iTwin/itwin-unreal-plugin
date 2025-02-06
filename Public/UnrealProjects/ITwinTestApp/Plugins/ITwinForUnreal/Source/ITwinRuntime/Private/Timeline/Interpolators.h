/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/StrongTypes/TaggedValue.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwin::Flag {

DEFINE_STRONG_BOOL(FContinue);
constexpr FContinue Continue(true);
constexpr FContinue Stop(false);

}

namespace ITwin::Timeline::Interpolators {

using ITwin::Flag::FContinue;
using ITwin::Flag::Continue;
using ITwin::Flag::Stop;

//! Default interpolator does a linear interpolation.
class Default
{
public:
	template<class _T>
	FContinue operator ()(_T& result, const _T& x0, const _T& x1, float u, void* userData) const;
};

//! Interpolates bool values by doing an "and" between them (for 0 < u < 1).
class BoolAnd
{
public:
	FContinue operator ()(bool& result, bool x0, bool x1, float u, void* userData) const;
};

//! Interpolates bool values by doing an "or" between them (for 0 < u < 1).
class BoolOr
{
public:
	FContinue operator ()(bool& result, bool x0, bool x1, float u, void* userData) const;
};

} // namespace ITwin::Timeline::Interpolators

#include "Interpolators.inl"
