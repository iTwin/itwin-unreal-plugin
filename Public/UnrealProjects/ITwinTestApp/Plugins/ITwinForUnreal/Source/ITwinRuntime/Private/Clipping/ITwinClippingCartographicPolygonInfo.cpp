/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingCartographicPolygonInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingCartographicPolygonInfo.h>

#include <Spline/ITwinSplineHelper.h>

//---------------------------------------------------------------------------------------
// struct FITwinClippingCartographicPolygonInfo
//---------------------------------------------------------------------------------------

bool FITwinClippingCartographicPolygonInfo::GetInvertEffect() const
{
	return Properties.bInvertEffect;
}

void FITwinClippingCartographicPolygonInfo::DoSetInvertEffect(bool bInvert)
{
	Properties.bInvertEffect = bInvert;
}

void FITwinClippingCartographicPolygonInfo::DoSetEnabled(bool bInEnabled)
{
	if (SplineHelper.IsValid())
	{
		SplineHelper->EnableEffect(bInEnabled);
	}
}
