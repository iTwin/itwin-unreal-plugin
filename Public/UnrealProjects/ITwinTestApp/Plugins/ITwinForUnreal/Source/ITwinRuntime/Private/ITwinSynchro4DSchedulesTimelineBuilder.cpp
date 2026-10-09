/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DSchedulesTimelineBuilder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSynchro4DSchedulesTimelineBuilder.h"
#include "ITwinIModel.h"
#include "ITwinIModelInternals.h"
#include "ITwinSceneMapping.h"
#include "ITwinSynchro4DSchedules.h"
#include "ITwinSynchro4DSchedulesInternals.h"
#include <Timeline/SchedulesKeyframes.h>
#include <Timeline/SchedulesStructs.inl>
#include <Timeline/Timeline.h>
#include <Timeline/TimelineTypes.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <JsonObjectConverter.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace Detail {

void InsertAnimationKey(FITwinElement::FAnimKeysVec& AnimationKeys, FIModelElementsKey const& AnimationKey)
{
	// Note: skipped the LessAnimationKey comparator appearing in
	// https://github.com/iTwin/itwin-unreal-plugin/pull/121/changes/2bd35a3655464d768c16ae3c3bd94dee823c74a9
	// because the proper operator exists in TimelineTypes.h (it had a bug previously which is now fixed, which may
	// explain why the PR author needed LessAnimationKey to fix the timeline splitting issue).
	auto const FirstGreaterOrEqual = std::lower_bound(AnimationKeys.begin(), AnimationKeys.end(), AnimationKey);
	if (FirstGreaterOrEqual == AnimationKeys.end() || AnimationKey != *FirstGreaterOrEqual)
		AnimationKeys.insert(FirstGreaterOrEqual, AnimationKey);
}

void ReplaceAnimationKey(FITwinElement::FAnimKeysVec& AnimationKeys,
	FIModelElementsKey const& ExistingAnimationKey, FIModelElementsKey const& NewAnimationKey)
{
	if (ExistingAnimationKey == NewAnimationKey)
		return;
	auto ExistingAnimationKeyIt = std::lower_bound(AnimationKeys.begin(), AnimationKeys.end(), ExistingAnimationKey);
	if (!ensure(ExistingAnimationKeyIt != AnimationKeys.end() && ExistingAnimationKey == *ExistingAnimationKeyIt))
	{
		InsertAnimationKey(AnimationKeys, NewAnimationKey);
		return;
	}
	AnimationKeys.erase(ExistingAnimationKeyIt);
	InsertAnimationKey(AnimationKeys, NewAnimationKey);
}

void AddAnimationKeyToAnimatedParents(FITwinSceneMapping& SceneMapping, ITwinScene::ElemIdx ParentIdx,
	FIModelElementsKey const& ExistingAnimationKey, FIModelElementsKey const& NewAnimationKey)
{
	while (ITwinScene::NOT_ELEM != ParentIdx)
	{
		auto& ParentElem = SceneMapping.ElementFor(ParentIdx);
		auto const ExistingAnimationKeyIt =
			std::lower_bound(ParentElem.AnimationKeys.begin(), ParentElem.AnimationKeys.end(), ExistingAnimationKey);
		// Note: this break relies on the unwritten convention that keys are fonud contiguously from the bound node
		// down to all children. Convention also witnessed in FITwinSceneMapping::OnElementsTimelineModified and
		// implemented by InsertAnimatedMeshSubElemsRecursively's stragtegy.
		if (ExistingAnimationKeyIt == ParentElem.AnimationKeys.end()
			|| ExistingAnimationKey != *ExistingAnimationKeyIt)
			break;
		// If it contained the old key, add the new one, but don't erase the old one right now, as other children may
		// still need it (since a parent contains all the keys of its children). We'll erase the old keys from all
		// parents after all timelines have been processed.
		InsertAnimationKey(ParentElem.AnimationKeys, NewAnimationKey);
		ParentIdx = ParentElem.ParentInVec;
	}
}

void ReassignAnimationKeyForSplit(FITwinSceneMapping& SceneMapping, FElementsGroup const& ElementsSubGroup,
	FIModelElementsKey const& ExistingAnimationKey, FIModelElementsKey const& NewAnimationKey)
{
	for (ITwinElementID const ElemID : ElementsSubGroup)
	{
		ITwinScene::ElemIdx ElemIdx = ITwinScene::NOT_ELEM;
		auto* Elem = SceneMapping.GetElementForSLOW(ElemID, &ElemIdx);
		if (!ensure(Elem))
			continue;
		ReplaceAnimationKey(Elem->AnimationKeys, ExistingAnimationKey, NewAnimationKey);
		AddAnimationKeyToAnimatedParents(SceneMapping, Elem->ParentInVec,
			ExistingAnimationKey, NewAnimationKey);
	}
}

template<typename ElemDesignationContainer>
void InsertAnimatedMeshSubElemsRecursively(FIModelElementsKey const& AnimationKey,
	FITwinSceneMapping& Scene, ElemDesignationContainer const& Elements,
	FITwinScheduleTimeline& MainTimeline, FElementsGroup& OutSet,
	std::vector<ITwinElementID>* OutElemsDiff = nullptr)
{
	for (auto const ElementDesignation : Elements)
	{
		FITwinElement* pElem;
		// NOT safe to use ElementForSLOW here: we may be in a worker thread!
		if constexpr (std::is_same_v<ITwinElementID, typename ElemDesignationContainer::value_type>)
		{
			pElem = Scene.GetElementForSLOW(ElementDesignation);
			if (!pElem)
				continue; // Element not known after querying metadata! (azdev#1704016)
		}
		else
			pElem = &Scene.ElementFor(ElementDesignation);
		FITwinElement& Elem = *pElem;
		// Insert without duplication, and using a deterministic ordering, because concurrent 4D queries could
		// obviously be received in an arbitrary order: necessary for CreateTimelineKeyframesWithTaskDependencies
		// which can thus compare the Elem.AnimationKeys arrays directly.
		InsertAnimationKey(Elem.AnimationKeys, AnimationKey);
		// When pre-fetching bindings, bHasMesh is not set at this point, since we may not have received a tile with
		// it yet. Let's rely on Elem.BBox instead. We used to rely on the list of child elements, assuming only leaves
		// had geometries, but this proved wrong (ADO#2020662).
		if (Elem.BBox.IsValid)
		{
			if (!OutSet.insert(Elem.ElementID).second)
				continue; // already in set: no need for RemoveNonAnimatedDuplicate nor recursion
			else
			{
				MainTimeline.RemoveNonAnimatedDuplicate(Elem.ElementID);
				if (OutElemsDiff)
					OutElemsDiff->push_back(Elem.ElementID);
			}
		}
		Detail::InsertAnimatedMeshSubElemsRecursively(AnimationKey, Scene, Elem.SubElemsInVec, MainTimeline,
													  OutSet, OutElemsDiff);
	}
}

template<typename ContainerToHandle>
void HideNonAnimatedDuplicates(FITwinSceneMapping const& Scene, ContainerToHandle const& ElemIDs,
							   FITwinScheduleTimeline& MainTimeline)
{
	for (ITwinElementID ElemID : ElemIDs)
	{
		auto const& Duplicates = Scene.GetDuplicateElements(ElemID);
		bool bOneIsAnimated = false;
		for (auto Dupl : Duplicates)
		{
			auto const& Elem = Scene.GetElement(Dupl);
			if (!Elem.AnimationKeys.empty())
			{
				bOneIsAnimated = true;
				break;
			}
		}
		if (!bOneIsAnimated)
			continue;
		for (auto Dupl : Duplicates)
		{
			auto const& Elem = Scene.GetElement(Dupl);
			if (Elem.AnimationKeys.empty())
			{
				MainTimeline.AddNonAnimatedDuplicate(Elem.ElementID);
			}
		}
	}
}

} // ns Detail

class FITwinScheduleTimelineBuilder::FImpl
{
public:
	UITwinSynchro4DSchedules const* Owner = nullptr;
	TSceneMappingPtr* SceneMappingPtr = nullptr;
	FITwinCoordConversions const* CoordConversions = nullptr;
	FITwinScheduleTimeline MainTimeline;
	FOnElementsTimelineModified OnElementsTimelineModified;

	void CreateAnimationBindingKeyframes(FITwinSchedule const& Schedule, FITwinElementTimeline& ElemTimeline,
		size_t const AnimationBindingIndex, bool const bHasOnlyNeutralTasks);
	void ClearTimelinesData();

	// Need to split (or add) a timeline if:
	// * it has at least one Neutral task and some Elements are also bound to Install/Remove tasks
	//		while some others are not
	// * it has a Maintain task for which some Elements are bound to Install/Remove tasks before or after it
	//		while some others are not: in that case, both the appearance profile and the transform of the Install
	//		task have priority over the Maintain task's outside its timerange.
	//
	// \return True if task dependencies were found leading to splitting a timeline, and thus all keyframes have been
	//		created inside this method. False otherwise (ie caller will create the keyframes the usual way, for the
	//		whole input timeline).
	bool CreateTimelineKeyframesWithTaskDependencies(FITwinSceneMapping& SceneMapping,
		FITwinSchedule& Schedule, // not const coz I'm adding the subgroups to it but could use separate counter
		FITwinElementTimeline& ElemTimeline, int const TimelineIndex,
		std::unordered_set<FElementsGroup>& KeyframedSubgroups)
	{
		bool bOnlyInstallOrRemove = true;
		for (size_t AnimationBindingIndex : ElemTimeline.AnimationBindings())
		{
			auto&& Binding = Schedule.AnimationBindings[AnimationBindingIndex];
			auto Action = Schedule.AppearanceProfiles[Binding.AppearanceProfileInVec].ProfileType;
			if (EProfileAction::Install != Action && EProfileAction::Remove != Action)
			{
				bOnlyInstallOrRemove = false;
				break;
			}
		}
		if (bOnlyInstallOrRemove)
			return false;
		// Split the timeline's elements set into subgroups sharing the same list of (sorted) animation keys.
		// Not optimal, but seems less CPU-intensive than the alternative (which could be for example to create the
		// timelines element by element but merge them on the fly using the whole timeline as key?).
		// We could also check that bindings not shared by all Elements would not actually interfere, like having no
		// Inst/Rem tasks (but there is also the case of the Temp task acting as Rem regarding visibility
		// outside subsequent Temp/Maint tasks), but it could be a lot of logic to code for a minor perf gain.
		auto ElemIt = ElemTimeline.GetIModelElements().begin();
		// TODO_GCO: to optimize, I need an "ElemTimeline.GetIModelElementsRanks()"...
		FITwinElement::FAnimKeysVec const RefAnimKeys = SceneMapping.ElementForSLOW(*ElemIt).AnimationKeys;
		++ElemIt;
		std::optional<std::vector<std::pair<FITwinElement::FAnimKeysVec, FElementsGroup>>> SplitElemGroups;
		for (; ElemIt != ElemTimeline.GetIModelElements().end(); ++ElemIt)
		{
			auto const& Elem = SceneMapping.ElementForSLOW(*ElemIt);
			if (!SplitElemGroups)
			{
				if (RefAnimKeys == Elem.AnimationKeys)
				{
					continue;
				}
				else
				{
					// Create the first split group with all the elements tested so far
					SplitElemGroups.emplace();
					SplitElemGroups->emplace_back(std::make_pair(
						RefAnimKeys, FElementsGroup(ElemTimeline.GetIModelElements().begin(), ElemIt)));
				}
			}
			auto SplitGroupExists = std::find_if(SplitElemGroups->begin(), SplitElemGroups->end(),
				[&Elem](auto const& SplitGroup) { return SplitGroup.first == Elem.AnimationKeys; });
			if (SplitGroupExists != SplitElemGroups->end())
				SplitGroupExists->second.insert(*ElemIt);
			else
				SplitElemGroups->emplace_back(std::make_pair(
					Elem.AnimationKeys, FElementsGroup{ *ElemIt }));
		}
		// If all Elems have the same bindings (belong to the same timelines), nothing to do:
		if (!SplitElemGroups)
			return false;
		ensure(SplitElemGroups->size() > 1);
		bool bUseExisting = true;
		FIModelElementsKey const ExistingAnimationKey = ElemTimeline.GetIModelElementsKey();
		FITwinElementTimeline::FBindings const ExistingAnimationBindings = ElemTimeline.GetAnimationBindings();
		for (auto& [CommonAnimationKeys, ElementsSubGroup] : (*SplitElemGroups))
		{
			if (!KeyframedSubgroups.insert(ElementsSubGroup).second) // !inserted = already present thus handled
				continue;
			FITwinElementTimeline* pSubgroupTimeline;
			bool const bReusingExistingTimeline = bUseExisting;
			FIModelElementsKey const SubgroupElementsKey(Schedule.NumGroups());
			Detail::ReassignAnimationKeyForSplit(
				SceneMapping, ElementsSubGroup, ExistingAnimationKey, SubgroupElementsKey);
			if (bReusingExistingTimeline)
			{
				bUseExisting = false;
				ElemTimeline.IModelElementsRef() = ElementsSubGroup;
				// We just reuse the existing timeline index, but we need to reassign it to the new Elements key,
				// and reset its members
				MainTimeline.ResetElementTimelineFor(TimelineIndex, SubgroupElementsKey);
				pSubgroupTimeline = &ElemTimeline;
			}
			else
			{
				pSubgroupTimeline = &MainTimeline.ElementTimelineFor(SubgroupElementsKey, ElementsSubGroup);
			}
			// ExistingAnimationKey is in every subgroup's CommonAnimationKeys by construction, and is no longer
			// registered after ResetElementTimelineFor, so seed from the saved copy and skip it in the loop.
			// We *do* need these bindings, and not a subset of them: the split isn't dividing the original bindings
			// between subgroups — the original bindings apply to all of the original elements, by definition.
			// The split exists so each subgroup can additionally see the bindings of the other timelines its elements
			// belong to, because CreateAnimationBindingKeyframes needs the complete per-element task set to resolve
			// inter-task dependencies.
			pSubgroupTimeline->AnimationBindings() = ExistingAnimationBindings;
			for (auto&& AnimationKey : CommonAnimationKeys)
			{
				if (AnimationKey == ExistingAnimationKey)
					continue;
				if (auto* Timeline = MainTimeline.GetElementTimelineFor(AnimationKey))
					pSubgroupTimeline->AnimationBindings().insert(pSubgroupTimeline->AnimationBindings().end(),
						Timeline->GetAnimationBindings().begin(), Timeline->GetAnimationBindings().end());
			}
			Schedule.CreateNextGroup(std::move(ElementsSubGroup));
			bool const bHasOnlyNeutralTasks = Schedule.HasOnlyNeutralBindings(
				pSubgroupTimeline->AnimationBindings().begin(), pSubgroupTimeline->AnimationBindings().end());
			for (size_t AnimationBindingIndex : pSubgroupTimeline->AnimationBindings())
			{
				CreateAnimationBindingKeyframes(Schedule, *pSubgroupTimeline, AnimationBindingIndex,
												bHasOnlyNeutralTasks);
			}
		}
		return true;
	}
};

bool FITwinScheduleTimelineBuilder::IsUnitTesting() const
{
	return nullptr == Impl->Owner;
}

void FITwinScheduleTimelineBuilder::OnReceivedScheduleStats(FITwinScheduleStats const& Stats)
{
	// TODO_GCO: could reserve MainTimeline's container
}

void FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline(FITwinSchedule& Schedule,
	size_t const AnimationBindingIndex)
{
	auto&& Binding = Schedule.AnimationBindings[AnimationBindingIndex];
	if (Binding.bDeleted
		// normally bindings would have been deleted too, but for unit tests it's easier to support this
		|| Schedule.Tasks[Binding.TaskInVec].bDeleted
		|| Schedule.AppearanceProfiles[Binding.AppearanceProfileInVec].bDeleted)
	{
		return;
	}
	auto SceneMappingLock = (*Impl->SceneMappingPtr)->GetAutoLock();
	FITwinSceneMapping& SceneMapping = *SceneMappingLock.GetPtr();
	auto AnimationKey = std::visit([&](auto&& Ident) -> std::optional<FIModelElementsKey>
		{
			using T = std::decay_t<decltype(Ident)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
			{
				return FIModelElementsKey(Ident);
			}
			else if constexpr (std::is_same_v<T, FGuid>)
			{
				ITwinElementID ElemID;
				if (SceneMapping.FindElementIDForGUID(Ident, ElemID))
					return FIModelElementsKey(Ident);
			}
			else if constexpr (std::is_same_v<T, FString>) // group GUID => GroupInVec must be set
			{
				auto&& FedGUID2ElemID = std::bind(&FITwinSceneMapping::FindElementIDForGUID, &SceneMapping,
												  std::placeholders::_1, std::placeholders::_2);
				FElementsGroup const& BoundElements =
					Schedule.GetGroupAsElementIDs(Binding.GroupInVec, FedGUID2ElemID);
				if (!BoundElements.empty())
					return FIModelElementsKey(Binding.GroupInVec);
				else
					return std::nullopt;
			}
			else
				static_assert(always_false_v<T>, "non-exhaustive visitor!");
			return std::nullopt;
		},
		Binding.AnimatedEntities);
	if (!AnimationKey)
		return;
	FITwinElementTimeline& ElementTimeline = Impl->MainTimeline.ElementTimelineFor(*AnimationKey, {});
	ElementTimeline.AnimationBindings().emplace_back(AnimationBindingIndex);
}

//! Handle inter-task dependencies: some constraints need to be applied per-Element, like showing Elements
//! outside tasks only after the first Install task (except if none): the first Install task for Elements
//! of a same assignment can differ, so the case cannot be handled by merely adding a keyframe to any of the
//! existing ElementTimelineEx.
void FITwinScheduleTimelineBuilder::FinalizeTimeline(FITwinSchedule& Schedule)
{
	Impl->ClearTimelinesData();
	if (!Impl->SceneMappingPtr)
	{
		AITwinIModel * IModel = Impl->Owner ? Cast<AITwinIModel>(Impl->Owner->GetOwner()) : nullptr;
		if (IModel)
			Impl->SceneMappingPtr = &GetInternals(*IModel).SceneMapping;
	}
	for (size_t AnimIdx = 0; AnimIdx < Schedule.AnimationBindings.size(); ++AnimIdx)
	{
		AddAnimationBindingToTimeline(Schedule, AnimIdx);
	}
	auto SceneMappingLock = (*Impl->SceneMappingPtr)->GetAutoLock();
	FITwinSceneMapping& SceneMapping = *SceneMappingLock.GetPtr();
	for (auto ElemTimelinePtr : Impl->MainTimeline.GetContainer())
	{
		if (!ensure(!ElemTimelinePtr->AnimationBindings().empty()))
			continue;
		// All bindings listed necessarily animate the same Elements since they are part of the same timeline
		auto&& Binding = Schedule.AnimationBindings[*ElemTimelinePtr->AnimationBindings().begin()];
		FElementsGroup BoundElements;
		std::visit([&](auto&& Ident)
			{
				using T = std::decay_t<decltype(Ident)>;
				if constexpr (std::is_same_v<T, ITwinElementID>)
				{
					BoundElements.insert(Ident);
				}
				else if constexpr (std::is_same_v<T, FGuid>)
				{
					ITwinElementID SingleElementID;
					if (SceneMapping.FindElementIDForGUID(Ident, SingleElementID))
					{
						BoundElements.insert(SingleElementID);
					}
				}
				else if constexpr (std::is_same_v<T, FString>) // group GUID => GroupInVec must be set
				{
					auto&& FedGUID2ElemID = std::bind(&FITwinSceneMapping::FindElementIDForGUID, &SceneMapping,
													  std::placeholders::_1, std::placeholders::_2);
					BoundElements = Schedule.GetGroupAsElementIDs(Binding.GroupInVec, FedGUID2ElemID);
				}
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Binding.AnimatedEntities);
		FElementsGroup AnimatedMeshElements;
		if (!ensure(!BoundElements.empty()))
			return;
		Detail::InsertAnimatedMeshSubElemsRecursively(ElemTimelinePtr->GetIModelElementsKey(), SceneMapping,
			BoundElements, Impl->MainTimeline, AnimatedMeshElements);
		Detail::HideNonAnimatedDuplicates(SceneMapping, AnimatedMeshElements, Impl->MainTimeline);
		ElemTimelinePtr->IModelElementsRef().swap(AnimatedMeshElements);
		// Respecting the task dependencies may mean splitting the group of Elements, this is why it must be done
		// AFTER InsertAnimatedMeshSubElemsRecursively because child Elems can be assigned independently
		// from their Parents.
	}
	// Subgroups split from timelines will be encountered in several calls to CreateAnimationBindingKeyframes,
	// so we need to keep track of them because the first call will already have created their keyframes.
	std::unordered_set<FElementsGroup> KeyframedSubgroups;
	auto&& Timelines = Impl->MainTimeline.GetContainer();
	for (int TimelineIndex = 0; TimelineIndex < (int)Timelines.size(); ++TimelineIndex)
	{
		auto ElemTimelinePtr = Timelines[TimelineIndex];
		// no 'ensure', it happens in rare cases, like some line Elements in GSW Stadium which are discarded
		// by InsertAnimatedMeshSubElemsRecursively because the iModel query has returned a null/empty BBox for them!
		if (ElemTimelinePtr->GetIModelElements().empty())
			continue;
		if (!Impl->CreateTimelineKeyframesWithTaskDependencies(SceneMapping, Schedule, *ElemTimelinePtr,
															   TimelineIndex, KeyframedSubgroups))
		{
			bool const bHasOnlyNeutralTasks = Schedule.HasOnlyNeutralBindings(
				ElemTimelinePtr->AnimationBindings().begin(), ElemTimelinePtr->AnimationBindings().end());
			for (size_t AnimationBindingIndex : ElemTimelinePtr->AnimationBindings())
			{
				Impl->CreateAnimationBindingKeyframes(Schedule, *ElemTimelinePtr, AnimationBindingIndex,
													  bHasOnlyNeutralTasks);
			}
		}
	}
	// After all timelines have been processed (including subgroup timelines created during splits,
	// which the loop above visits too since it re-evaluates Timelines.size()), drop animation keys
	// whose timeline was unregistered by a split. Elements and parents still referencing them would
	// otherwise trip ensure(Timeline) in UpdateGltfTunerRules and be skipped by ForEachElementTimeline.
	auto const& MainTimeline = Impl->MainTimeline;
	SceneMapping.MutateElements([&MainTimeline](FITwinElement& Elem)
		{
			if (Elem.AnimationKeys.empty())
				return;
			auto const NewEnd = std::remove_if(Elem.AnimationKeys.begin(), Elem.AnimationKeys.end(),
				[&MainTimeline](FIModelElementsKey const& Key)
				{ return nullptr == MainTimeline.GetElementTimelineFor(Key); });
			Elem.AnimationKeys.erase(NewEnd, Elem.AnimationKeys.end());
		});
	// CreateTimelineKeyframesWithTaskDependencies can empty some timelines
	for (int TimelineIndex = 0; TimelineIndex < (int)Timelines.size(); )
	{
		auto ElemTimelinePtr = Timelines[TimelineIndex];
		if (ElemTimelinePtr->GetIModelElements().empty())
			Impl->MainTimeline.SwapWithLastAndDelete(TimelineIndex);
		else
			++TimelineIndex;
	}
	for (auto&& ConstrDetailParent : SceneMapping.GetConstructionDetailingParentsToHide())
	{
		auto const& Elem = SceneMapping.ElementFor(ConstrDetailParent);
		if (Elem.AnimationKeys.empty())
			Impl->MainTimeline.AddNonAnimatedDuplicate(Elem.ElementID);
	}
}

/// @brief Actually creates the keyframes needed to animate the Elements according to their
///			schedule's binding.
/// Can be called several times for the same ElementTimeline but different AnimationBindingIndex's,
/// when a group of Elements is bound to several tasks in a way that remains compatible to their
/// being animated by the same timeline, ie inter-task dependencies do not require further splitting
/// of the group.
/// @param Schedule Owner's schedule data, filled with the result from *all* needed 4D queries.
/// @param ElementTimeline Timeline for *some* (or all) Elements associated to the binding. Properties
///			resulting from inter-task dependencies must have been resolved at this point.
/// @param AnimationBindingIndex Index in Schedule's array of animation bindings.
void FITwinScheduleTimelineBuilder::FImpl::CreateAnimationBindingKeyframes(FITwinSchedule const& Schedule,
	FITwinElementTimeline& ElementTimeline, size_t const AnimationBindingIndex, bool const bHasOnlyNeutralTasks)
{
	auto&& Binding = Schedule.AnimationBindings[AnimationBindingIndex];
	auto&& AppearanceProfile = Schedule.AppearanceProfiles[Binding.AppearanceProfileInVec];
	auto&& Task = Schedule.Tasks[Binding.TaskInVec];
	ITwin::Timeline::FTaskDependenciesData TaskDeps{ .bHasOnlyNeutralTasks = bHasOnlyNeutralTasks };
	if (EProfileAction::Maintenance == AppearanceProfile.ProfileType
		|| EProfileAction::Temporary == AppearanceProfile.ProfileType)
	{
		Schedule.FindAnyPriorityAppearances(ElementTimeline.GetAnimationBindings().begin(),
			ElementTimeline.GetAnimationBindings().end(), Binding, AppearanceProfile.ProfileType, TaskDeps);
		ensure(!(TaskDeps.ProfileForcedVisibilityBefore && (*TaskDeps.ProfileForcedVisibilityBefore == false)
					&& TaskDeps.ProfileForcedAppearanceBefore != nullptr));
		ensure(!(TaskDeps.ProfileForcedVisibilityAfter && (*TaskDeps.ProfileForcedVisibilityAfter == false)
					&& TaskDeps.ProfileForcedAppearanceAfter != nullptr));
	}
	ITwin::Timeline::AddColorToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange, TaskDeps);
	ITwin::Timeline::AddVisibilityToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange, TaskDeps);
	ITwin::Timeline::PTransform const* TransformKeyframe = nullptr;
#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
	FStaticTransformAssignment const* StaticTransfoAssignment =
		(ITwin::INVALID_IDX == Binding.StaticTransfoAssignmentInVec) ? nullptr
			: (&Schedule.StaticTransfoAssignments[Binding.StaticTransfoAssignmentInVec]);
	FPathTransformAssignment const* PathTransfoAssignment =
		(ITwin::INVALID_IDX == Binding.PathTransfoAssignmentInVec) ? nullptr
			: (&Schedule.PathTransfoAssignments[Binding.PathTransfoAssignmentInVec]);
	// Should mean schedule is not consistent (binding should have been updated), but can be useful for testing:
	if (StaticTransfoAssignment && StaticTransfoAssignment->bDeleted)
		StaticTransfoAssignment = nullptr;
	if (PathTransfoAssignment && PathTransfoAssignment->bDeleted)
		PathTransfoAssignment = nullptr;
	if (StaticTransfoAssignment || PathTransfoAssignment)
	{
		// Animation binding can have both static transfo and 3D path (with same Id, see azdev#1689132),
		// In that case, we store both assignments separately (see KnownTransfoAssignments's bool subkey),
		// to avoid having to worry about concurrent writes to the TransfoAssignment variant.
		// But the static transform is ignored like in Synchro Pro (TransfoAssignment.bStaticTransform is
		// set to false in FITwinSchedulesImport::FImpl::RequestAnimationBindings).
		if (StaticTransfoAssignment && !PathTransfoAssignment)
		{
			TransformKeyframe = &ITwin::Timeline::AddStaticTransformToTimeline(ElementTimeline,
				Task.TimeRange, StaticTransfoAssignment->Transform, *CoordConversions, TaskDeps);
		}
		else if (PathTransfoAssignment && ensure(ITwin::INVALID_IDX != PathTransfoAssignment->Animation3DPathInVec))
		{
			auto&& Path3D = Schedule.Animation3DPaths[PathTransfoAssignment->Animation3DPathInVec].Keyframes;
			ITwin::Timeline::Add3DPathTransformToTimeline(&ElementTimeline, Task.TimeRange,
				// The static transform is actually ignored, as mentioned above and confirmed in #2070419
				nullptr, //Was: StaticTransfoAssignment ? (&StaticTransfoAssignment->Transform) : nullptr,
				*PathTransfoAssignment, Path3D, *CoordConversions, TaskDeps);
		}
		else
		{
			ensureMsgf(false, TEXT("Inconsistent transformation assignment interpretation"));
		}
	}
	else
	{
		ElementTimeline.SetTransformationDisabledAt(Task.TimeRange.first, ITwin::Timeline::EInterpolation::Step);
		ITwin::Timeline::HandleFallbackTransfoOutsideTaskIfNeeded(
			ElementTimeline, Task.TimeRange, *CoordConversions, TaskDeps);
	}
#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS
	ITwin::Timeline::AddCuttingPlaneToTimeline(ElementTimeline, AppearanceProfile, Task.TimeRange,
												*CoordConversions, TransformKeyframe);

	if (OnElementsTimelineModified) OnElementsTimelineModified(ElementTimeline, nullptr);
}

/*static*/
FITwinScheduleTimelineBuilder FITwinScheduleTimelineBuilder::CreateForUnitTesting(
	TSceneMappingPtr& SceneMappingPtr, FITwinCoordConversions const& InCoordConv)
{
	FITwinScheduleTimelineBuilder Builder;
	Builder.Impl->SceneMappingPtr = &SceneMappingPtr;
	Builder.Impl->CoordConversions = &InCoordConv;
	return Builder;
}

// private, for CreateForUnitTesting
FITwinScheduleTimelineBuilder::FITwinScheduleTimelineBuilder()
	: Impl(MakePimpl<FImpl>())
{
}

FITwinScheduleTimelineBuilder::FITwinScheduleTimelineBuilder(UITwinSynchro4DSchedules const& InOwner,
															 FITwinCoordConversions const& InCoordConv)
	: Impl(MakePimpl<FImpl>())
{
	Impl->Owner = &InOwner;
	Impl->CoordConversions = &InCoordConv;
}

void FITwinScheduleTimelineBuilder::Initialize(FOnElementsTimelineModified&& InOnElementsTimelineModified)
{
	ensure(EInit::Pending == InitState);
	InitState = EInit::Ready;
	Impl->OnElementsTimelineModified = std::move(InOnElementsTimelineModified);
}

FITwinScheduleTimelineBuilder& FITwinScheduleTimelineBuilder::operator=(FITwinScheduleTimelineBuilder&& Other)
{
	Impl = std::move(Other.Impl);
	ensure(EInit::Pending == Other.InitState);
	InitState = Other.InitState;
	Other.InitState = EInit::Disposable;
	return *this;
}

FITwinScheduleTimelineBuilder::~FITwinScheduleTimelineBuilder()
{
	ensure(EInit::Ready != InitState); // Pending or Disposable are both OK
}

void FITwinScheduleTimelineBuilder::FImpl::ClearTimelinesData()
{
	if (!Owner) // <=> unit test
		return;
	for (auto const& ElementTimelinePtr : MainTimeline.GetContainer())
		if (ElementTimelinePtr->ExtraData)
		{
			delete (static_cast<FTimelineToScene*>(ElementTimelinePtr->ExtraData));
			ElementTimelinePtr->ExtraData = nullptr;
		}
	MainTimeline.ClearTimelinesData();

	AITwinIModel* IModel = Cast<AITwinIModel>(Owner->GetOwner());
	if (ensure(IModel))
	{
		auto SceneMappingLocked = GetInternals(*IModel).SceneMapping->GetAutoLock();
		SceneMappingLocked->ForEachKnownTile([](const TITwinSceneTilePtr& SceneTilePtr)
			{
				auto SceneTileLock = SceneTilePtr->GetAutoLock();
				SceneTileLock->TimelinesIndices.clear();
				SceneTileLock->ClearExtractedElements();
			});
		SceneMappingLocked->MutateElements([](FITwinElement& Elem)
			{
				Elem.AnimationKeys.clear();
				Elem.Requirements = {};
			});
	}
}

void FITwinScheduleTimelineBuilder::Uninitialize()
{
	if (!ensure(EInit::Disposable != InitState))
		return;
	if (EInit::Pending != InitState)
		Impl->ClearTimelinesData();
	InitState = EInit::Disposable;
}

FITwinScheduleTimeline& FITwinScheduleTimelineBuilder::Timeline() { return Impl->MainTimeline; }

FITwinScheduleTimeline const& FITwinScheduleTimelineBuilder::GetTimeline() const { return Impl->MainTimeline; }

void FITwinScheduleTimelineBuilder::DebugDumpFullTimelinesAsJson(FString const& RelPath) const
{
	FString TimelineAsJson = Impl->MainTimeline.ToPrettyJsonString();
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	Path.Append(RelPath);
	FString CoordConvPath, ElemDataPath;
	if (RelPath.EndsWith(".json"))
	{
		CoordConvPath = Path.LeftChop(4);
	}
	else
	{
		CoordConvPath = Path;
		Path.Append(".json");
	}
	ElemDataPath = CoordConvPath + TEXT(".ElemData.json");
	CoordConvPath += TEXT(".CoordConv.json");
	if (FileManager.FileExists(*Path))
		FileManager.DeleteFile(*Path);
	FFileHelper::SaveStringToFile(TimelineAsJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	if (!IsUnitTesting() && Impl->CoordConversions)
	{
		if (FileManager.FileExists(*CoordConvPath))
			FileManager.DeleteFile(*CoordConvPath);
		FString CCString;
		FJsonObjectConverter::UStructToJsonObjectString(*Impl->CoordConversions, CCString, 0, 0);
		FFileHelper::SaveStringToFile(CCString, *CoordConvPath, FFileHelper::EEncodingOptions::ForceUTF8);
	}
	if (Impl->Owner)
	{
		AITwinIModel* IModel = Cast<AITwinIModel>(Impl->Owner->GetOwner());
		if (ensure(IModel))
		{
			if (FileManager.FileExists(*ElemDataPath))
				FileManager.DeleteFile(*ElemDataPath);
			TSharedPtr<FJsonObject> AllElemsJson;
			{
				auto SceneMappingLocked = GetInternals(*IModel).SceneMapping->GetAutoLock();
				AllElemsJson = SceneMappingLocked->ToJson();
			}
			FString ElemJsonStr;
			auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&ElemJsonStr);
			FJsonSerializer::Serialize(AllElemsJson.ToSharedRef(), JsonWriter);
			FFileHelper::SaveStringToFile(ElemJsonStr, *ElemDataPath, FFileHelper::EEncodingOptions::ForceUTF8);
		}
	}
}

#if WITH_TESTS
bool FITwinScheduleTimelineBuilder::TestOnlyCreateTimelineKeyframesWithTaskDependencies(
	FITwinSceneMapping& SceneMapping,
	FITwinSchedule& Schedule,
	FITwinElementTimeline& ElemTimeline,
	int TimelineIndex,
	std::unordered_set<FElementsGroup>& KeyframedSubgroups)
{
	return Impl->CreateTimelineKeyframesWithTaskDependencies(
		SceneMapping, Schedule, ElemTimeline, TimelineIndex, KeyframedSubgroups);
}
#endif
