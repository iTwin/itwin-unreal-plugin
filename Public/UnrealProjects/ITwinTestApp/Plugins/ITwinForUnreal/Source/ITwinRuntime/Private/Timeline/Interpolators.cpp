/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Interpolators.h"

namespace ITwin::Timeline::Interpolators
{

FVector4f PlaneEquation::operator ()(const FVector4f& x0, const FVector4f& x1, float u) const
{
	// TODO_AW fix this, it only works when directions are same
	return x0 * (1.f - u) + x1 * u;
}

bool BoolAnd::operator ()(bool x0, bool x1, float u) const
{
	return u == 0 ?
		x0 :
		u == 1 ?
			x1 :
			x0 && x1;
}

bool BoolOr::operator ()(bool x0, bool x1, float u) const
{
	return u == 0 ?
		x0 :
		u == 1 ?
			x1 :
			x0 || x1;
}

} // namespace ITwin::Timeline::Interpolators
