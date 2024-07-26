/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Interpolators.h"

namespace ITwin::Timeline::Interpolators {

template<class _T>
_T Lerp(const _T& x0, const _T& x1, float u)
{
	return x0 * (1.f - u) + x1 * u;
}

template<class _T>
FContinue Default::operator ()(_T& result, const _T& x0, const _T& x1, float u, void*) const
{
	result = Lerp(x0, x1, u);
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators