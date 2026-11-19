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

/// Controls whether negative zeros (-0.0) produced by the conversion are
/// normalized to +0.0.
/// Note: the original need for this normalization is undocumented; it was
/// observed to matter for consumers comparing/serializing matrix components
/// (e.g. textual output or exact equality checks), where -0.0 and +0.0 differ
/// even though they compare equal numerically.
enum class ENegativeZeroPolicy : uint8
{
	Preserve,	//!< Leave the raw conversion result untouched (default).
	Normalize	//!< Replace any -0.0 component by +0.0.
};

/// Controls what happens when the converted matrix has a degenerate (zero on
/// one or more axes) scale, which makes rotation extraction undefined.
/// Note: the original reason for this guard is undocumented; it was observed
/// on transforms loaded from persisted decoration data where a null scale
/// produced a garbage/NaN rotation in FTransform::SetFromMatrix.
enum class EDegenerateScalePolicy : uint8
{
	Allow,			//!< Build the transform from the matrix as-is (default).
	IgnoreRotation	//!< On zero scale, keep identity rotation/scale and log an error.
};

class ITWINRUNTIME_API FITwinMathConversion
{
public:

	static AdvViz::SDK::Tools::IGCSTransformPtr transform_;

	// Vector
	[[nodiscard]] static AdvViz::SDK::double3 UEtoSDK(FVector const& ueVec, bool applyGCS = true);
	[[nodiscard]] static FVector SDKtoUE(AdvViz::SDK::double3 const& sdkVec, bool applyGCS = true);

	// Transform
	[[nodiscard]] static AdvViz::SDK::dmat3x4 UEtoSDK(FTransform const& ueTransform,
		bool applyGCS = true,
		ENegativeZeroPolicy negativeZeroPolicy = ENegativeZeroPolicy::Preserve);

	[[nodiscard]] static FTransform SDKtoUE(AdvViz::SDK::dmat3x4 const& sdkTransform,
		bool applyGCS = true,
		EDegenerateScalePolicy degenerateScalePolicy = EDegenerateScalePolicy::Allow);
};
