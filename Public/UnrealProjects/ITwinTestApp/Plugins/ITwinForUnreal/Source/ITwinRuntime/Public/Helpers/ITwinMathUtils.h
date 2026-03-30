/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMathUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <Math/Plane.h>
#include <VectorUtil.h>

namespace ITwin
{
	/// Same as FMath::RayPlaneIntersection, but with additional check on the mathematical conditions for 
	/// a valid impact.
	template<typename T>
	inline bool RayPlaneIntersection(
		const UE::Math::TVector<T>& RayOrigin,
		const UE::Math::TVector<T>& RayDirection,
		const UE::Math::TPlane<T>& Plane,
		UE::Math::TVector<T>& OutHitPoint,
		T& OutDistance)
	{
		using TVector = UE::Math::TVector<T>;
		const TVector PlaneNormal = TVector(Plane.X, Plane.Y, Plane.Z);
		const TVector PlaneOrigin = PlaneNormal * Plane.W;

		const T NormalDot = TVector::DotProduct(RayDirection, PlaneNormal);
		if (std::fabs(NormalDot) < TMathUtil<T>::ZeroTolerance)
		{
			return false;
		}
		OutDistance = TVector::DotProduct((PlaneOrigin - RayOrigin), PlaneNormal) / NormalDot;
		if (OutDistance < 0)
		{
			return false;
		}
		OutHitPoint = RayOrigin + RayDirection * OutDistance;
		return true;
	}
}
