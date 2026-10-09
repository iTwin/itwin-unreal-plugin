/*--------------------------------------------------------------------------------------+
|
|     $Source: ScheduleComparison.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "SchedulesStructs.h"

/// In a separate file because SchedulesStructsOps.inl is included before FITwinSchedule's definition as it defines
/// the hash operators that it needs
inline bool operator==(const FITwinSchedule& A, const FITwinSchedule& B)
{
	if (A.Id != B.Id || A.Name != B.Name || A.Generation != B.Generation)
		return false;

	if (A.BindingsDeltaToken != B.BindingsDeltaToken
		|| A.AppearanceProfilesDeltaToken != B.AppearanceProfilesDeltaToken
		|| A.TasksDeltaToken != B.TasksDeltaToken
		|| A.StaticTransfosDeltaToken != B.StaticTransfosDeltaToken
		|| A.Anim3DPathsAssignmentsDeltaToken != B.Anim3DPathsAssignmentsDeltaToken
		|| A.Anim3DPathsDeltaToken != B.Anim3DPathsDeltaToken
		|| A.Anim3DPathKeyframesDeltaToken != B.Anim3DPathKeyframesDeltaToken)
	{
		return false;
	}

	if (A.StatisticsTotal != B.StatisticsTotal)
		return false;

	auto CompareVectorWithMap = [](auto const& VecA, auto const& MapA, auto const& VecB, auto const& MapB)
		{
			if (VecA.size() != VecB.size() || MapA.size() != MapB.size())
				return false;
			for (auto const& [Id, IndexA] : MapA)
			{
				auto itB = MapB.find(Id);
				if (itB == MapB.end() || !(VecA[IndexA] == VecB[itB->second]))
					return false;
			}
			return true;
		};

	if (!CompareVectorWithMap(A.Tasks, A.KnownTasks, B.Tasks, B.KnownTasks))
		return false;
	if (!CompareVectorWithMap(A.AppearanceProfiles, A.KnownAppearanceProfiles,
							  B.AppearanceProfiles, B.KnownAppearanceProfiles))
		return false;
	if (!CompareVectorWithMap(A.StaticTransfoAssignments, A.KnownStaticTransfoAssignments,
							  B.StaticTransfoAssignments, B.KnownStaticTransfoAssignments))
		return false;
	if (!CompareVectorWithMap(A.PathTransfoAssignments, A.KnownPathTransfoAssignments,
							  B.PathTransfoAssignments, B.KnownPathTransfoAssignments))
		return false;
	if (!CompareVectorWithMap(A.Animation3DPaths, A.KnownAnimation3DPaths,
							  B.Animation3DPaths, B.KnownAnimation3DPaths))
		return false;

	// AnimationBindings map uses the struct itself as key
	if (A.AnimationBindings.size() != B.AnimationBindings.size()
		|| A.KnownAnimationBindings.size() != B.KnownAnimationBindings.size())
	{
		return false;
	}
	for (auto const& [Binding, IndexA] : A.KnownAnimationBindings)
	{
		if (B.KnownAnimationBindings.find(Binding) == B.KnownAnimationBindings.end())
			return false;
	}

	// Groups
	if (A.KnownGroups.size() != B.KnownGroups.size())
		return false;
	for (auto const& [GrpId, IdxA] : A.KnownGroups)
	{
		auto itB = B.KnownGroups.find(GrpId);
		if (itB == B.KnownGroups.end())
			return false;
		size_t IdxB = itB->second;
		if (A.Generation == EITwinSchedulesGeneration::Legacy)
		{
			// Note: assumes ElemIDGroups covers same indices as KnownGroups
			if (A.ElemIDGroups[IdxA] != B.ElemIDGroups[IdxB])
				return false;
		}
		else
		{
			if (A.FedGUIDGroups[IdxA] != B.FedGUIDGroups[IdxB])
				return false;
		}
	}

	return true;
}