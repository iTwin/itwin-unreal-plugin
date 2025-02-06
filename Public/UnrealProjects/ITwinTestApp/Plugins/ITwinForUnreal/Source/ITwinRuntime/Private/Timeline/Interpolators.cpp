/*--------------------------------------------------------------------------------------+
|
|     $Source: Interpolators.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Interpolators.h"

namespace ITwin::Timeline::Interpolators
{

FContinue BoolAnd::operator ()(bool& out, bool x0, bool x1, float u, void*) const
{
	out = (u == 0) ? x0 : (u == 1 ? x1 : (x0 && x1));
	return Continue;
}

FContinue BoolOr::operator ()(bool& out, bool x0, bool x1, float u, void*) const
{
	out = (u == 0) ? x0 : (u == 1 ? x1 : (x0 || x1));
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators
