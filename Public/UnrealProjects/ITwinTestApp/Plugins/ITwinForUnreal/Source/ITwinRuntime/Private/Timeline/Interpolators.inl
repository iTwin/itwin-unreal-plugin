/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Interpolators.h"
#include <Math/Quat.h>

namespace ITwin::Timeline::Interpolators {

template<class _T>
_T Lerp(const _T& x0, const _T& x1, float u)
{
	return x0 * (1.f - u) + x1 * u;
}

/// FQuat interpolation: default implementation compiles but I guess this is better:
template<> inline
FContinue Default::operator ()<FQuat>(FQuat& Out, const FQuat& x0, const FQuat& x1, float u, void*) const
{
	Out = FQuat::Slerp(x0, x1, u);
	return Continue;
}

template<class _T>
FContinue Default::operator ()(_T& result, const _T& x0, const _T& x1, float u, void*) const
{
	result = Lerp(x0, x1, u);
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators