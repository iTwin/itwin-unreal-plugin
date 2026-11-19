/*--------------------------------------------------------------------------------------+
|
|     $Source: UEMathConversion.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "UEMathConversion.h"

AdvViz::SDK::Tools::IGCSTransformPtr FITwinMathConversion::transform_;

/*static*/ AdvViz::SDK::double3 FITwinMathConversion::UEtoSDK(FVector const& ueVec, bool applyGCS /*= true*/)
{
	if (applyGCS && transform_)
		return transform_->PositionFromClient(toAdvizSdk(ueVec));
	return toAdvizSdk(ueVec);
}

/*static*/ FVector FITwinMathConversion::SDKtoUE(AdvViz::SDK::double3 const& sdkVec, bool applyGCS/* = true*/)
{
	if (applyGCS && transform_)
		return toUnreal(transform_->PositionToClient(sdkVec));
	return toUnreal(sdkVec);
}

// Transform
/*static*/ AdvViz::SDK::dmat3x4 FITwinMathConversion::UEtoSDK(FTransform const& ueTransform,
	bool applyGCS /*= true*/,
	ENegativeZeroPolicy negativeZeroPolicy /*= Preserve*/)
{
	using namespace AdvViz::SDK;

	AdvViz::SDK::dmat3x4 sdkTransform;

	FMatrix srcMat = ueTransform.ToMatrixWithScale();
	FVector srcPos = ueTransform.GetTranslation();
	srcMat.M[3][0] = srcPos[0];
	srcMat.M[3][1] = srcPos[1];
	srcMat.M[3][2] = srcPos[2];

	if (applyGCS && transform_)
		srcMat = toUnreal(transform_->MatrixFromClient(toAdvizSdk(srcMat)));

	for (int32 i = 0; i < 4; ++i)
		for (int32 j = 0; j < 3; ++j)
			ColRow3x4(sdkTransform,j,i) = srcMat.M[i][j];

	if (negativeZeroPolicy == ENegativeZeroPolicy::Normalize)
	{
		for (double& v : sdkTransform)
		{
			if (v == -0.0)
				v = 0.0;
		}
	}
	return sdkTransform;
}

/*static*/ FTransform FITwinMathConversion::SDKtoUE(AdvViz::SDK::dmat3x4 const& sdkTransform,
	bool applyGCS /*= true*/,
	EDegenerateScalePolicy degenerateScalePolicy /*= Allow*/)
{
	using namespace AdvViz::SDK;

	AdvViz::SDK::dmat4x4 ueMat;
	for (unsigned i = 0; i < 3; ++i)
	{
		for (unsigned j = 0; j < 4; ++j)
			ColRow4x4(ueMat, j, i) = ColRow3x4(sdkTransform, i, j);
		ColRow4x4(ueMat, i, 3) = 0.0f;
	}
	ueMat[15] = 1.;

	FMatrix ueMatF;
	if (applyGCS && transform_)
		ueMatF = toUnreal(transform_->MatrixToClient(ueMat));
	else
		ueMatF = toUnreal(ueMat);

	FVector uePos(
		ueMatF.M[3][0],
		ueMatF.M[3][1],
		ueMatF.M[3][2]);

	FTransform ueTransform;
	if (degenerateScalePolicy == EDegenerateScalePolicy::IgnoreRotation)
	{
		// NB: ExtractScaling normalizes ueMatF in place, so the transform built
		// below is scale-free (unit scale) - this is intentional, it matches the
		// original decoration-loading behavior.
		FVector const scale = ueMatF.ExtractScaling(0);
		if (scale.X != 0. && scale.Y != 0. && scale.Z != 0.)
			ueTransform.SetFromMatrix(ueMatF);
		else
			BE_LOGE("ITwinDecoration", "Invalid transformation loaded, ignoring rotation");
	}
	else
	{
		ueTransform.SetFromMatrix(ueMatF);
	}
	ueTransform.SetTranslation(uePos);

	return ueTransform;
}
