/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Spline/ITwinSplineHelper.h>


template <typename TFunc>
void AITwinSplineHelper::IterateAllCartographicPolygons(TFunc const& Func) const
{
	for (auto& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon)
		{
			Func(*Polygon);
		}
	}
}
