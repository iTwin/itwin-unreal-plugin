/*--------------------------------------------------------------------------------------+
|
|     $Source: UEMathExts.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

#include <array>
#include <type_traits> // std::hash<size_t>?

class FITwinMathExts
{
public:
	static FQuat Conjugate(FQuat const& q)
	{
		return FQuat(q.W, -q.X, -q.Y, -q.Z); // glm/ext/quaternion_common.inl
	}

	static FMatrix MakeTranslationMatrix(FVector const& translation)
	{
		FMatrix mat;
		mat.SetIdentity();
		mat.SetColumn(3, translation);
		return mat;
	}

	static FMatrix MakeScalingMatrix(FVector const& scale)
	{
		FMatrix mat;
		mat.SetIdentity();
		mat.M[0][0] = scale.X;
		mat.M[1][1] = scale.Y;
		mat.M[2][2] = scale.Z;
		return mat;
	}

	/// Generate a unique random but deterministic *opaque* BGRA8 color for each index
	static std::array<uint8, 4> RandomBGRA8ColorFromIndex(size_t const Idx, bool const bOpaque)
	{
		size_t const h = std::hash<size_t>()(Idx);
		return { (uint8)(h & 0xFF), (uint8)((h & 0xFF00) >> 8), (uint8)((h & 0xFF0000) >> 16),
				 bOpaque ? (uint8)255 : (uint8)((h & 0xFF000000) >> 24) };
	}

	/// Generate a unique random but deterministic color for each index, as a double-precision float RGB
	/// vector in [0;1].
	/// Note that the subspace of possible colors is still that of the 8bpc RandomBGRA8ColorFromIndex
	static FVector RandomFloatColorFromIndex(size_t const Idx, float* OutAlpha = nullptr)
	{
		auto&& Color = RandomBGRA8ColorFromIndex(Idx, OutAlpha == nullptr);
		if (OutAlpha) *OutAlpha = Color[3] / 255.f;
		return FVector(Color[2], Color[1], Color[0]) / 255.;
	}
};
