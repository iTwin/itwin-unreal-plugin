/*--------------------------------------------------------------------------------------+
|
|     $Source: UEMathConversion.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#   include <Core/Tools/Tools.h>
#	include <Core/Tools/Types.h>
#include <Compil/AfterNonUnrealIncludes.h>

class FITwinMathConversion
{
public:
	// Vector
	[[nodiscard]] static AdvViz::SDK::double3 UEtoSDK(FVector const& ueVec)
	{
		AdvViz::SDK::double3 sdkVec;
		sdkVec[0] = ueVec.X;
		sdkVec[1] = ueVec.Y;
		sdkVec[2] = ueVec.Z;
		return sdkVec;
	}

	[[nodiscard]] static FVector SDKtoUE(AdvViz::SDK::double3 const& sdkVec)
	{
		FVector ueVec;
		ueVec.X = sdkVec[0];
		ueVec.Y = sdkVec[1];
		ueVec.Z = sdkVec[2];
		return ueVec;
	}

	// Transform
	[[nodiscard]] static AdvViz::SDK::dmat3x4 UEtoSDK(FTransform const& ueTransform)
	{
		using namespace AdvViz::SDK;

		AdvViz::SDK::dmat3x4 sdkTransform;

		FMatrix srcMat = ueTransform.ToMatrixWithScale();
		FVector srcPos = ueTransform.GetTranslation();

		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				ColRow3x4(sdkTransform,j,i) = srcMat.M[i][j];
			}
		}
		ColRow3x4(sdkTransform, 0, 3) = srcPos.X;
		ColRow3x4(sdkTransform, 1, 3) = srcPos.Y;
		ColRow3x4(sdkTransform, 2, 3) = srcPos.Z;
		return sdkTransform;
	}

	[[nodiscard]] static FTransform SDKtoUE(AdvViz::SDK::dmat3x4 const& sdkTransform)
	{
		using namespace AdvViz::SDK;

		FMatrix ueMat(FMatrix::Identity);
		for (unsigned i = 0; i < 3; ++i)
		{
			for (unsigned j = 0; j < 3; ++j)
			{
				ueMat.M[j][i] = ColRow3x4(sdkTransform,i, j);
			}
		}

		FVector uePos(
			ColRow3x4(sdkTransform,0, 3),
			ColRow3x4(sdkTransform,1, 3),
			ColRow3x4(sdkTransform,2, 3));

		FTransform ueTransform;
		ueTransform.SetFromMatrix(ueMat);
		ueTransform.SetTranslation(uePos);

		return ueTransform;
	}
};