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
_T Default::operator ()(const _T& x0, const _T& x1, float u) const
{
	return x0 * (1.f - u) + x1 * u;
}

} // namespace ITwin::Timeline::Interpolators