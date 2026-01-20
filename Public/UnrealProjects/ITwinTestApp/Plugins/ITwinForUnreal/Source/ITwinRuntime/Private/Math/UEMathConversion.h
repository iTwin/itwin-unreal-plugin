/*--------------------------------------------------------------------------------------+
|
|     $Source: UEMathConversion.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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

inline AdvViz::SDK::double3 toAdvizSdk(FVector const& ueVec)
{
	return AdvViz::SDK::double3{ ueVec.X, ueVec.Y, ueVec.Z };
}

inline AdvViz::SDK::dmat4x4 toAdvizSdk(FMatrix const& ueMat) // SDK::dmat4x4 (glm) matrix is colomn+major, FMatrix is row-major 
{
	AdvViz::SDK::dmat4x4 sdkMat;
	for (int32 i = 0; i < 4; ++i)
		for (int32 j = 0; j < 4; ++j)
			AdvViz::SDK::ColRow4x4(sdkMat,i,j) = ueMat.M[i][j];
	return sdkMat;
}

inline FVector toUnreal(AdvViz::SDK::double3 const& sdkVec)
{
	return FVector(sdkVec[0], sdkVec[1], sdkVec[2]);
}

inline FMatrix toUnreal(AdvViz::SDK::dmat4x4 const& sdkMat) // SDK::dmat4x4 (glm) matrix is colomn+major, FMatrix is row-major 
{
	FMatrix ueMat;
	for (int32 i = 0; i < 4; ++i)
		for (int32 j = 0; j < 4; ++j)
			ueMat.M[i][j] = AdvViz::SDK::ColRow4x4(sdkMat, i, j);
	return ueMat;
}

class ITWINRUNTIME_API FITwinMathConversion
{
public:

	static AdvViz::SDK::Tools::IGCSTransformPtr transform_;
	// Vector
	[[nodiscard]] static AdvViz::SDK::double3 UEtoSDK(FVector const& ueVec, bool applyGCS = true);
	[[nodiscard]] static FVector SDKtoUE(AdvViz::SDK::double3 const& sdkVec, bool applyGCS = true);
	// Transform
	[[nodiscard]] static AdvViz::SDK::dmat3x4 UEtoSDK(FTransform const& ueTransform, bool applyGCS = true);
	[[nodiscard]] static FTransform SDKtoUE(AdvViz::SDK::dmat3x4 const& sdkTransform, bool applyGCS = true);
	
};