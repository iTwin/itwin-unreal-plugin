/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingPlaneInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingPlaneInfo.h>

#include <Clipping/ITwinPlaneTileExcluder.h>

//---------------------------------------------------------------------------------------
// struct FITwinClippingPlaneInfo
//---------------------------------------------------------------------------------------

void FITwinClippingPlaneInfo::DoSetInvertEffect(bool bInvert)
{
	Super::DoSetInvertEffect(bInvert);

	this->bInvertEffect = bInvert;

	// Update tile excluders accordingly.
	for (auto const& TileExcluder : TileExcluders)
	{
		if (TileExcluder.IsValid()
			&& ensure(TileExcluder->IsA(UITwinPlaneTileExcluder::StaticClass())))
		{
			Cast<UITwinPlaneTileExcluder>(TileExcluder.Get())->SetInvertEffect(bInvert);
		}
	}
}

