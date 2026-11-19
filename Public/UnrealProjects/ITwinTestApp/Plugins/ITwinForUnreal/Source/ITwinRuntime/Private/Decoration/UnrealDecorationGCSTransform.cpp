/*--------------------------------------------------------------------------------------+
|
|     $Source: UnrealDecorationGCSTransform.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Decoration/UnrealDecorationGCSTransform.h>

#include <Math/UEMathConversion.h>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <numbers>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


void FUnrealDecorationGCSTransform::Init(const AdvViz::SDK::double3& latLonHeightDeg, bool bInUseFixedMatrixComposition)
{
	using namespace AdvViz::SDK::Tools;

	bUseFixedMatrixComposition = bInUseFixedMatrixComposition;
	AdvViz::SDK::double3 latLonHeightRad = { latLonHeightDeg[0] * (std::numbers::pi / 180.0),
		latLonHeightDeg[1] * (std::numbers::pi / 180.0),
		latLonHeightDeg[2] };

	AdvViz::SDK::dmat4x4 ecf2enu = GCSTransform::WGS84ECEFToENUMatrix(latLonHeightRad);
	AdvViz::SDK::dmat4x4 enu2ecf = GCSTransform::WGS84ENUToECEFMatrix(latLonHeightRad);
	ENU2ECEF = toUnreal(enu2ecf);
	ECEF2ENU = toUnreal(ecf2enu);

	scale = FVector(1. / 100.0, -1. / 100.0, 1. / 100.0);
	invscale = FVector(100.0, -100.0, 100.0);
}

AdvViz::SDK::double3 FUnrealDecorationGCSTransform::PositionFromClient(const AdvViz::SDK::double3& p) const
{
	FVector v = toUnreal(p);
	FVector r = ENU2ECEF.TransformPosition(v * scale);
	return toAdvizSdk(r);
}

AdvViz::SDK::double3 FUnrealDecorationGCSTransform::PositionToClient(const AdvViz::SDK::double3& p) const
{
	FVector v = toUnreal(p);
	FVector r = ECEF2ENU.TransformPosition(v) * invscale;
	return toAdvizSdk(r);
}

namespace
{
	static const FMatrix AxisFlip = FScaleMatrix(FVector(1., -1., 1.));
}

AdvViz::SDK::dmat4x4 FUnrealDecorationGCSTransform::MatrixFromClient(const AdvViz::SDK::dmat4x4& m) const
{
	FMatrix um = toUnreal(m);
	// transform translation
	FVector trans = FVector(um.M[3][0], um.M[3][1], um.M[3][2]);
	trans = ENU2ECEF.TransformPosition(trans * scale);

	// Placement composition in Unreal's row-vector convention: object -> client -> ECEF.
	// AxisFlip carries the same handedness flip (negative Y) as 'scale'; without it,
	// positions are mirrored but orientations are not.
	FMatrix rm = bUseFixedMatrixComposition ? (um * AxisFlip * ENU2ECEF)
		: (ENU2ECEF * um); // legacy
	rm.M[3][0] = trans[0];
	rm.M[3][1] = trans[1];
	rm.M[3][2] = trans[2];
	return toAdvizSdk(rm);
}

AdvViz::SDK::dmat4x4 FUnrealDecorationGCSTransform::MatrixToClient(const AdvViz::SDK::dmat4x4& m) const
{
	FMatrix um = toUnreal(m);
	// transform translation
	FVector trans = FVector(um.M[3][0], um.M[3][1], um.M[3][2]);
	trans = ECEF2ENU.TransformPosition(trans) * invscale;

	FMatrix rm = bUseFixedMatrixComposition ? (um * ECEF2ENU * AxisFlip)
		: (ECEF2ENU * um); // legacy
	rm.M[3][0] = trans[0];
	rm.M[3][1] = trans[1];
	rm.M[3][2] = trans[2];
	return toAdvizSdk(rm);
}
