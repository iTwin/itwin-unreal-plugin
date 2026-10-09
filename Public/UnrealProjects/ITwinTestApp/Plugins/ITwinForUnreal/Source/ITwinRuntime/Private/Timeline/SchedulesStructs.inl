/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "SchedulesStructs.h"
#include "SchedulesKeyframes.h"

template<typename BindingIndexIterator>
bool FITwinSchedule::HasOnlyNeutralBindings(BindingIndexIterator First, BindingIndexIterator Last) const
{
	return Last == std::find_if(
		First, Last, [this](const auto& BindingIndex)
		{
			return EProfileAction::Neutral
				!= this->AppearanceProfiles[this->AnimationBindings[BindingIndex].AppearanceProfileInVec].ProfileType;
		});
}

namespace Detail
{
	struct TimedProfile
	{
		double Time;
		EProfileAction ProfileType;
		FSimpleAppearance const& Appearance;
		FStaticTransformAssignment const* StaticTransfoAssignment = nullptr;
		FPathTransformAssignment const* PathTransfoAssignment = nullptr;
		bool bUseTransfoStart = false;
	};
	using OptTimedProfile = std::optional<TimedProfile>;

	inline void UpdateTimedProfiles(FScheduleTask const& Task, FScheduleTask const& OtherTask,
		FAppearanceProfile const& OtherProfile,
		OptTimedProfile& LatestBefore, OptTimedProfile& EarliestAfter,
		FStaticTransformAssignment const* StaticTransfoAssignment, FPathTransformAssignment const* PathTransfoAssignment)
	{
		// Note: testing OtherTask's end vs. this task's end date, not start date, even though we are looking for a
		// task that finished "before": this will handle some overlap situations + handle the case where both
		// tasks are exactly contiguous (my test project has a situation like this)
		if (OtherTask.TimeRange.second < Task.TimeRange.second)
		{
			if (!LatestBefore || (LatestBefore->Time < OtherTask.TimeRange.second))
			{
				LatestBefore.emplace(TimedProfile{
					OtherTask.TimeRange.second, OtherProfile.ProfileType, OtherProfile.FinishAppearance,
					StaticTransfoAssignment, PathTransfoAssignment, /*bUseTransfoStart*/false });
			}
		}
		if (OtherTask.TimeRange.first > Task.TimeRange.first) // see comment above, same reasoning
		{
			if (!EarliestAfter || (EarliestAfter->Time > OtherTask.TimeRange.first))
			{
				EarliestAfter.emplace(TimedProfile{
					OtherTask.TimeRange.first, OtherProfile.ProfileType, OtherProfile.StartAppearance,
					StaticTransfoAssignment, PathTransfoAssignment, /*bUseTransfoStart*/true });
			}
		}
	}

	inline void SetTransfoAssignmentDataDeps(FITwinSchedule const& Schedule,
		ITwin::Timeline::FTaskDependenciesData& TaskDeps, TimedProfile const& Profile)
	{
		if (!Profile.StaticTransfoAssignment && !Profile.PathTransfoAssignment)
			return;
		TaskDeps.ProfileForcedStaticTransfoAssignOutside = Profile.StaticTransfoAssignment;
		TaskDeps.ProfileForcedPathTransfoAssignOutside = Profile.PathTransfoAssignment;
		if (Profile.PathTransfoAssignment
			&& ::ITwin::INVALID_IDX != Profile.PathTransfoAssignment->Animation3DPathInVec)
		{
			TaskDeps.ProfileForced3DPathOutside = &Schedule.Animation3DPaths[
				Profile.PathTransfoAssignment->Animation3DPathInVec];
			TaskDeps.bProfileForced3DPathOutsideIsAtPathStart = Profile.bUseTransfoStart;
		}
	}
}

template<typename BindingIndexIterator>
void FITwinSchedule::FindAnyPriorityAppearances(BindingIndexIterator First, BindingIndexIterator Last,
	FAnimationBinding const& ThisBinding, EProfileAction const ThisAction,
	ITwin::Timeline::FTaskDependenciesData& TaskDeps) const
{
	FScheduleTask const& Task = Tasks[ThisBinding.TaskInVec];
	// Latest finish time of a priority task (Install or Remove) found before the start of the Maintain task
	Detail::OptTimedProfile LatestPrioFinishBefore;
	// Earliest start time of a priority task (Install or Remove) found after the end of the Maintain task
	Detail::OptTimedProfile EarliestPrioStartAfter;
	// Latest finish time of a Temporary task found before the start of the Maintain task
	Detail::OptTimedProfile LatestTempFinishBefore;
	// Earliest start time of a Temporary task found after the end of the Maintain task
	Detail::OptTimedProfile EarliestTempStartAfter;
	// Weird special case needed for Temp followed by Maintain
	Detail::OptTimedProfile EarliestMaintainStartAfter;
	// Weird special case needed for Temp preceded by Maintain and followed by Remove...
	Detail::OptTimedProfile LatestMaintainFinishBefore;
	for (auto It = First; It != Last; ++It)
	{
		FAnimationBinding const& OtherBinding = AnimationBindings[*It];
		FAppearanceProfile const& OtherProfile = AppearanceProfiles[OtherBinding.AppearanceProfileInVec];
		FStaticTransformAssignment const* StaticTransfoAssignment =
			(ITwin::INVALID_IDX == OtherBinding.StaticTransfoAssignmentInVec) ? nullptr
				: (&StaticTransfoAssignments[OtherBinding.StaticTransfoAssignmentInVec]);
		FPathTransformAssignment const* PathTransfoAssignment =
			(ITwin::INVALID_IDX == OtherBinding.PathTransfoAssignmentInVec) ? nullptr
				: (&PathTransfoAssignments[OtherBinding.PathTransfoAssignmentInVec]);
		if (EProfileAction::Install == OtherProfile.ProfileType
			|| EProfileAction::Remove == OtherProfile.ProfileType)
		{
			UpdateTimedProfiles(Task, Tasks[OtherBinding.TaskInVec], OtherProfile,
				LatestPrioFinishBefore, EarliestPrioStartAfter,
				StaticTransfoAssignment, PathTransfoAssignment);
		}
		else if (EProfileAction::Temporary == OtherProfile.ProfileType)
		{
			UpdateTimedProfiles(Task, Tasks[OtherBinding.TaskInVec], OtherProfile,
				LatestTempFinishBefore, EarliestTempStartAfter,
				StaticTransfoAssignment, PathTransfoAssignment);
		}
		else if (EProfileAction::Maintenance == OtherProfile.ProfileType)
		{
			UpdateTimedProfiles(Task, Tasks[OtherBinding.TaskInVec], OtherProfile,
				LatestMaintainFinishBefore, EarliestMaintainStartAfter,
				StaticTransfoAssignment, PathTransfoAssignment);
		}
	}
	if (EProfileAction::Maintenance == ThisAction)
	{
		if (LatestPrioFinishBefore)
		{
			if (EProfileAction::Install == LatestPrioFinishBefore->ProfileType)
			{
				TaskDeps.ProfileForcedAppearanceBefore = &LatestPrioFinishBefore->Appearance;
				TaskDeps.ProfileForcedAppearanceAfter = &LatestPrioFinishBefore->Appearance;
				Detail::SetTransfoAssignmentDataDeps(*this, TaskDeps, *LatestPrioFinishBefore);
			}
			else
			{
				TaskDeps.ProfileForcedAppearanceBefore = nullptr;
				TaskDeps.ProfileForcedAppearanceAfter = nullptr;
				TaskDeps.ProfileForcedVisibilityBefore.emplace(false);
				TaskDeps.ProfileForcedVisibilityAfter.emplace(false);
			}
		}
		else if (EarliestPrioStartAfter)
		{
			if (EProfileAction::Remove == EarliestPrioStartAfter->ProfileType)
			{
				if (!EarliestTempStartAfter || EarliestTempStartAfter->Time > EarliestPrioStartAfter->Time)
				{
					TaskDeps.ProfileForcedAppearanceBefore = &EarliestPrioStartAfter->Appearance;
					TaskDeps.ProfileForcedAppearanceAfter = &EarliestPrioStartAfter->Appearance;
					Detail::SetTransfoAssignmentDataDeps(*this, TaskDeps, *EarliestPrioStartAfter);
				}
				else // same as "else if (EarliestTempStartAfter)" below
				{
					TaskDeps.ProfileForcedAppearanceBefore = nullptr;
					TaskDeps.ProfileForcedAppearanceAfter = nullptr;
					TaskDeps.ProfileForcedVisibilityBefore.emplace(false);
					TaskDeps.ProfileForcedVisibilityAfter.emplace(false);
				}
			}
			else
			{
				TaskDeps.ProfileForcedAppearanceBefore = nullptr;
				TaskDeps.ProfileForcedAppearanceAfter = nullptr;
				TaskDeps.ProfileForcedVisibilityBefore.emplace(false);
				TaskDeps.ProfileForcedVisibilityAfter.emplace(false);
			}
		}
		else if (EarliestTempStartAfter) // Maintain acts as Temp!
		{
			TaskDeps.ProfileForcedAppearanceBefore = nullptr;
			TaskDeps.ProfileForcedAppearanceAfter = nullptr;
			TaskDeps.ProfileForcedVisibilityBefore.emplace(false);
			TaskDeps.ProfileForcedVisibilityAfter.emplace(false);
		}
		// Even if preceded by an Install _before_ the Temp, or followed by a Remove
		if (LatestTempFinishBefore)
		{
			if (!LatestPrioFinishBefore || EProfileAction::Remove == LatestPrioFinishBefore->ProfileType
				|| LatestPrioFinishBefore->Time < LatestTempFinishBefore->Time)
			{
				TaskDeps.ProfileForcedAppearanceAfter = nullptr;
				TaskDeps.ProfileForcedVisibilityAfter.emplace(false);
			}
		}
	}
	else if (EProfileAction::Temporary == ThisAction)
	{
		if (LatestPrioFinishBefore)
		{
			if (EProfileAction::Install == LatestPrioFinishBefore->ProfileType)
			{
				TaskDeps.ProfileForcedAppearanceBefore = &LatestPrioFinishBefore->Appearance;
				TaskDeps.ProfileForcedAppearanceAfter = &LatestPrioFinishBefore->Appearance;
				TaskDeps.ProfileForcedVisibilityBefore.emplace(true);
				TaskDeps.ProfileForcedVisibilityAfter.emplace(true);
				Detail::SetTransfoAssignmentDataDeps(*this, TaskDeps, *LatestPrioFinishBefore);
			}
			// else: Temp already behaves as if preceded by a Remove
		}
		else if (EarliestPrioStartAfter)
		{
			if (EProfileAction::Remove == EarliestPrioStartAfter->ProfileType && !LatestMaintainFinishBefore)
			{
				TaskDeps.ProfileForcedAppearanceBefore = &EarliestPrioStartAfter->Appearance;
				TaskDeps.ProfileForcedAppearanceAfter = &EarliestPrioStartAfter->Appearance;
				TaskDeps.ProfileForcedVisibilityBefore.emplace(true);
				TaskDeps.ProfileForcedVisibilityAfter.emplace(true);
				Detail::SetTransfoAssignmentDataDeps(*this, TaskDeps, *EarliestPrioStartAfter);
			}
			// else: Temp already behaves as if followed by an Install (but don't fallback into the
			// "EarliestMaintainStartAfter" case below when there is one! eg. "Q" in my test project...
		}
		else if (EarliestMaintainStartAfter)
		{
			TaskDeps.ProfileForcedAppearanceBefore = &EarliestMaintainStartAfter->Appearance;
			TaskDeps.ProfileForcedAppearanceAfter = &EarliestMaintainStartAfter->Appearance;
			TaskDeps.ProfileForcedVisibilityBefore.emplace(true);
			TaskDeps.ProfileForcedVisibilityAfter.emplace(true);
			Detail::SetTransfoAssignmentDataDeps(*this, TaskDeps, *EarliestMaintainStartAfter);
		}
	}
}
