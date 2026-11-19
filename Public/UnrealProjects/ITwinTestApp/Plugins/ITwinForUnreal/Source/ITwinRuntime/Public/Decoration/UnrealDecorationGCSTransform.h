/*--------------------------------------------------------------------------------------+
|
|     $Source: UnrealDecorationGCSTransform.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/GCSTransform.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class FUnrealDecorationGCSTransform : public AdvViz::SDK::Tools::IGCSTransform
{
private:
	//! When false, keep the historical (mathematically incorrect) composition order, so that
	//! scenes authored before the fix are reloaded exactly as they were saved.
	//! Set once at Init() from the ENGAGE scene tag, like UseGCSForLinks()/UseGCSForCameraKF().
	bool bUseFixedMatrixComposition = true;

public:
	FMatrix ENU2ECEF; // from ENU (unreal local East North Up) to ECEF (Earth-Centered, Earth-Fixed)
	FMatrix ECEF2ENU; // from ECEF (Earth-Centered, Earth-Fixed) to ENU (unreal local East North Up)
	FVector scale;
	FVector invscale;
	

	void Init(const AdvViz::SDK::double3& latLonHeightDeg, bool bInUseFixedMatrixComposition);

	virtual AdvViz::SDK::double3 PositionFromClient(const AdvViz::SDK::double3& p) const override;
	virtual AdvViz::SDK::double3 PositionToClient(const AdvViz::SDK::double3& p) const override;
	
	virtual AdvViz::SDK::dmat4x4 MatrixFromClient(const AdvViz::SDK::dmat4x4& m) const override;
	virtual AdvViz::SDK::dmat4x4 MatrixToClient(const AdvViz::SDK::dmat4x4& m) const override;
};
