/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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

/// FQuat interpolation: default implementation compiles but I thought using FQuat::Slerp would be better: it did
/// interpolate smoothly, but it took the shortest route between the two quaternions, whereas SYNCHRO Pro does not
/// (see AI explanation of SP's code in #2129717).
/// Default implementation does not actually work: it interpolates the components linearly, which is not a valid
/// quaternion interpolation and thus moves the objects around in a beautiful but irrelevant ballet ^_^
template<> inline
FContinue Default::operator ()<FQuat>(FQuat& Out, const FQuat& x0, const FQuat& x1, float u, void*) const
{
	// Doc says explicitly that angle between quaternions must be less than 180 degrees
	//Out = FQuat::Slerp(x0, x1, u);
	Out = FQuat::SlerpFullPath(x0, x1, u); // #2129717 - this version seems to match CQuat::Interpolate
	return Continue;

	// If other differences are found, this version reproduces CQuat::Interpolate
	// in Synchro\dev\product\src\common\basictypes\Quat.cpp, https://dev.azure.com/bentleycs/Synchro/_git/SynchroCore
	//if (u <= 0.f)
	//	Out = x0;
	//else if (u >= 1.f)
	//	Out = x1;
	//else
	//{
	//	// We get passed an FQuat constructed from FQuat(unit axis, angle) so the quaternion is also normalized, right?
	//	//x0.Normalize();
	//	//x1.Normalize();
	//	double omega, cosom, sinom, scale0, scale1;
	//	cosom = x0.X * x1.X + x0.Y * x1.Y + x0.Z * x1.Z + x0.W * x1.W;
	//	// AdvViz: this being commented out is the reason why it behaves differently than FQuat::Slerp
	//	// if ( cosom < 0.0 )
	//	//	{
	//	//	cosom = -cosom;
	//	//	x1 = FQuat( -x1.X, -x1.Y, -x1.Z, -x1.W );
	//	//	}
	//	if (cosom > 0.999999)
	//	{
	//		// small angle - simple linear interpolation by u
	//		scale0 = 1.0 - u;
	//		scale1 = u;
	//	}
	//	else
	//	{
	//#define QUAT_ACOS(x) (((x)<-1)?(UE_PI):(((x)>1)?(0):(acos(x))))
	//		// default (slerp)
	//		omega = QUAT_ACOS(cosom);
	//		sinom = sin(omega);
	//		scale0 = sin((1.0 - u) * omega) / sinom;
	//		scale1 = sin(u * omega) / sinom;
	//	}
	//	Out.X = scale0 * x0.X + scale1 * x1.X;
	//	Out.Y = scale0 * x0.Y + scale1 * x1.Y;
	//	Out.Z = scale0 * x0.Z + scale1 * x1.Z;
	//	Out.W = scale0 * x0.W + scale1 * x1.W;
	//	Out.Normalize();
	//}
	//return Continue;
}

template<class _T>
FContinue Default::operator ()(_T& result, const _T& x0, const _T& x1, float u, void*) const
{
	result = Lerp(x0, x1, u);
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators