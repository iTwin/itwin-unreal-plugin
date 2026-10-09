/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructsOps.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "SchedulesStructs.h"

inline bool operator==(FITwinScheduleStats const& A, FITwinScheduleStats const& B)
{
	return A.Animation3dPathAssignmentCount == B.Animation3dPathAssignmentCount
		&& A.Animation3dPathCount == B.Animation3dPathCount
		&& A.Animation3dPathKeyframeCount == B.Animation3dPathKeyframeCount
		&& A.Animation3dTransformCount == B.Animation3dTransformCount
		&& A.AnimationBindingCount == B.AnimationBindingCount
		&& A.AppearanceProfileCount == B.AppearanceProfileCount
		&& A.TaskCount == B.TaskCount;
}

inline bool operator==(FAnimProperty const& A, FAnimProperty const& B)
{
	return A.Id == B.Id && A.bDeleted == B.bDeleted;
}

inline bool operator==(FSimpleAppearance const& A, FSimpleAppearance const& B)
{
	if (A.bUseOriginalColor != B.bUseOriginalColor || A.bUseOriginalAlpha != B.bUseOriginalAlpha)
		return false;
	if (!A.bUseOriginalColor && !A.Color.Equals(B.Color, KINDA_SMALL_NUMBER))
		return false;
	if (!A.bUseOriginalAlpha && !FMath::IsNearlyEqual(A.Alpha, B.Alpha, KINDA_SMALL_NUMBER))
		return false;
	return true;
}

inline bool operator==(FActiveAppearance const& A, FActiveAppearance const& B)
{
	if (!(A.Base == B.Base))
		return false;
	// FinishAlpha's relevant only when !bUseOriginalAlpha (both A and B since A.Base == B.Base at this point):
	if (!A.Base.bUseOriginalAlpha && !FMath::IsNearlyEqual(A.FinishAlpha, B.FinishAlpha, KINDA_SMALL_NUMBER))
		return false;
	if (A.GrowthSimulationMode != B.GrowthSimulationMode)
		return false;
	if (A.GrowthSimulationMode == EGrowthSimulationMode::Custom
		&& !A.GrowthDirectionCustom.Equals(B.GrowthDirectionCustom, KINDA_SMALL_NUMBER))
		return false;
	if (A.bGrowthSimulationBasedOnPercentComplete != B.bGrowthSimulationBasedOnPercentComplete)
		return false;
	if (A.bGrowthSimulationPauseDuringNonWorkingTime != B.bGrowthSimulationPauseDuringNonWorkingTime)
		return false;
	if (A.bInvertGrowth != B.bInvertGrowth)
		return false;
	return true;
}

inline bool operator==(FAppearanceProfile const& A, FAppearanceProfile const& B)
{
	return static_cast<const FAnimProperty&>(A) == static_cast<const FAnimProperty&>(B)
		&& A.ProfileType == B.ProfileType
		&& A.StartAppearance == B.StartAppearance
		&& A.ActiveAppearance == B.ActiveAppearance
		&& A.FinishAppearance == B.FinishAppearance;
}

inline bool operator==(FTransformKey const& A, FTransformKey const& B)
{
	return static_cast<const FAnimProperty&>(A) == static_cast<const FAnimProperty&>(B)
		&& A.Transform.Equals(B.Transform)
		&& FMath::IsNearlyEqual(A.RelativeTime, B.RelativeTime, KINDA_SMALL_NUMBER);
}

inline bool operator<(FTransformKey const& A, FTransformKey const& B)
{
	return A.RelativeTime < B.RelativeTime;
}

inline bool operator==(FAnimation3DPath const& A, FAnimation3DPath const& B)
{
	return static_cast<const FAnimProperty&>(A) == static_cast<const FAnimProperty&>(B)
		&& A.Name == B.Name
		&& A.Color.Equals(B.Color, KINDA_SMALL_NUMBER)
		&& A.Keyframes == B.Keyframes;
}

inline bool operator==(const FPathTransformAssignment& A, const FPathTransformAssignment& B)
{
	if (static_cast<const FAnimProperty&>(A) != static_cast<const FAnimProperty&>(B))
		return false;
	if (A.Animation3DPathId != B.Animation3DPathId)
		return false;
	if (A.b3DPathReverseDirection != B.b3DPathReverseDirection)
		return false;
	if (A.TransformAnchor.index() != B.TransformAnchor.index())
		return false;
	if (std::holds_alternative<FVector>(A.TransformAnchor))
	{
		if (!std::get<FVector>(A.TransformAnchor)
			.Equals(std::get<FVector>(B.TransformAnchor), KINDA_SMALL_NUMBER))
			return false;
	}
	else if (std::get<ITwin::Timeline::EAnchorPoint>(A.TransformAnchor)
				!= std::get<ITwin::Timeline::EAnchorPoint>(B.TransformAnchor))
		return false;
	if (FMath::Abs(A.MotionStart - B.MotionStart) > KINDA_SMALL_NUMBER)
		return false;
	if (FMath::Abs(A.MotionEnd - B.MotionEnd) > KINDA_SMALL_NUMBER)
		return false;
	return true;
}

inline bool operator==(FStaticTransformAssignment const& A, FStaticTransformAssignment const& B)
{
	return static_cast<const FAnimProperty&>(A) == static_cast<const FAnimProperty&>(B)
		&& A.Transform.Equals(B.Transform);
}

inline bool operator==(FScheduleTask const& A, FScheduleTask const& B)
{
	return static_cast<const FAnimProperty&>(A) == static_cast<const FAnimProperty&>(B)
		&& A.Name == B.Name
		&& A.TimeRange == B.TimeRange;
}

template <>
struct std::hash<FAnimationBinding>
{
public:
	size_t operator()(FAnimationBinding const& Key) const
	{
		size_t Res = GetTypeHash(Key.TaskId);
		std::visit([&Res](auto&& ElemOrGroupId)
			{
				using T = std::decay_t<decltype(ElemOrGroupId)>;
				if constexpr (std::is_same_v<T, ITwinElementID>)
					boost::hash_combine(Res, std::hash<uint64_t>()(ElemOrGroupId.value()));
				else if constexpr (std::is_same_v<T, FGuid>)
					boost::hash_combine(Res, GetTypeHash(ElemOrGroupId));
				else if constexpr (std::is_same_v<T, FString>)
					boost::hash_combine(Res, GetTypeHash(ElemOrGroupId));
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Key.AnimatedEntities);
		boost::hash_combine(Res, GetTypeHash(Key.AppearanceProfileId));
		boost::hash_combine(Res, GetTypeHash(Key.StaticTransfoAssignmentId));
		boost::hash_combine(Res, GetTypeHash(Key.PathTransfoAssignmentId));
		return Res;
	}
};

inline bool operator ==(FAnimationBinding const& A, FAnimationBinding const& B)
{
	return A.TaskId == B.TaskId
		&& A.AnimatedEntities.index() == B.AnimatedEntities.index()
		&& (2 == A.AnimatedEntities.index()
			? std::get<2>(A.AnimatedEntities) == std::get<2>(B.AnimatedEntities)
			: (1 == A.AnimatedEntities.index()
				? std::get<1>(A.AnimatedEntities) == std::get<1>(B.AnimatedEntities)
				: std::get<0>(A.AnimatedEntities) == std::get<0>(B.AnimatedEntities)))
		&& A.AppearanceProfileId == B.AppearanceProfileId
		&& A.StaticTransfoAssignmentId == B.StaticTransfoAssignmentId
		&& A.PathTransfoAssignmentId == B.PathTransfoAssignmentId;
}
