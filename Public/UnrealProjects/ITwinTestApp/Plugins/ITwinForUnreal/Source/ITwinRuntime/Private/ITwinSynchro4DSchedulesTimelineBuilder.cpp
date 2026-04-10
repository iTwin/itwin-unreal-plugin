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

template<typename ElemDesignationContainer>
void InsertAnimatedMeshSubElemsRecursively(FIModelElementsKey const& AnimationKey,
	FITwinSceneMapping& Scene, ElemDesignationContainer const& Elements,
	FITwinScheduleTimeline& MainTimeline, FElementsGroup& OutSet,
	bool const bPrefetchAllElementAnimationBindings,
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
		auto const FirstGreaterOrEqual = std::lower_bound(
			Elem.AnimationKeys.begin(), Elem.AnimationKeys.end(), AnimationKey,
			[](const FIModelElementsKey& a, const FIModelElementsKey& b)
			{
				if (a.Key.index() == b.Key.index())
				{
					bool Result = false;
					std::visit([&b, &Result](auto&& Key)
						{
							using T = std::decay_t<decltype(Key)>;
							return Key < std::get<T>(b.Key);
						},
						a.Key);
					return Result;
				}
				else return (a.Key.index() < b.Key.index());
			});
		if (FirstGreaterOrEqual == Elem.AnimationKeys.end() || AnimationKey != (*FirstGreaterOrEqual))
		{
			Elem.AnimationKeys.insert(FirstGreaterOrEqual, AnimationKey);
		}
		// When pre-fetching bindings, bHasMesh is not set at this point, since we may not have received a tile containing
		// it yet. Let's rely on Elem.BBox instead. We used to rely on the list of child elements, assuming only leaves
		// had geometries, but this proved wrong (ADO#2020662).
		if (Elem.BBox.IsValid && (bPrefetchAllElementAnimationBindings || Elem.bHasMesh))
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
			OutSet, bPrefetchAllElementAnimationBindings, OutElemsDiff);
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
	UITwinSynchro4DSchedules const* Owner;
	FITwinCoordConversions const* CoordConversions;
	FITwinScheduleTimeline MainTimeline;
	FOnElementsTimelineModified OnElementsTimelineModified;

	void CreateAnimationBindingKeyframes(FITwinSchedule const& Schedule, FITwinElementTimeline& ElemTimeline,
		size_t const AnimationBindingIndex, bool const bHasOnlyNeutralTasks);

	// Need to split (or add) a timeline if:
	// * it has at least one Neutral task and some Elements are also bound to Install/Remove tasks
	//		while some others are not
	// * it has a Maintain task for which some Elements are bound to Install/Remove tasks before or after it
	//		while some others are not: in that case, both the appearance profile and the transform of the Install
	//		task have priority over the Maintain task's outside its timerange.
	//
	// \return True if task dependencies were found and thus all keyframes created, false otherwise (ie caller will
	//		create the keyframes the usual way, for the whole input timeline).
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
		// TODO_GCO: need an "ElemTimeline.GetIModelElementsRanks()"
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
		FIModelElementsKey const UnsplitAnimKey = ElemTimeline.GetIModelElementsKey();
		FITwinElementTimeline::FBindings const UnsplitBindings = ElemTimeline.GetAnimationBindings();
		for (auto& [CommonAnimationKeys, ElementsSubGroup] : (*SplitElemGroups))
		{
			if (!KeyframedSubgroups.insert(ElementsSubGroup).second) // !inserted = already present thus handled
				continue;
			FIModelElementsKey const SubgroupAnimKey(Schedule.NumGroups());
			for (auto&& Elem : ElementsSubGroup)
			{
				auto&& AnimKeys = SceneMapping.ElementForSLOW(Elem).AnimationKeys;
				std::replace(AnimKeys.begin(), AnimKeys.end(), UnsplitAnimKey, SubgroupAnimKey);
			}
			FITwinElementTimeline* pSubgroupTimeline;
			if (bUseExisting)
			{
				bUseExisting = false;
				MainTimeline.ResetElementTimelineFor(TimelineIndex, SubgroupAnimKey);
				ElemTimeline.IModelElementsRef() = ElementsSubGroup;
				pSubgroupTimeline = &ElemTimeline;
			}
			else
			{
				pSubgroupTimeline = &MainTimeline.ElementTimelineFor(SubgroupAnimKey, ElementsSubGroup);
			}
			for (auto&& AnimationKey : CommonAnimationKeys)
			{
				if (AnimationKey == UnsplitAnimKey) // timeline reused ie. no longer mapped to AnimationKey!
				{
					pSubgroupTimeline->AnimationBindings().insert(pSubgroupTimeline->AnimationBindings().end(),
						UnsplitBindings.begin(), UnsplitBindings.end());
				}
				else if (auto* Timeline = MainTimeline.GetElementTimelineFor(AnimationKey))
					pSubgroupTimeline->AnimationBindings().insert(pSubgroupTimeline->AnimationBindings().end(),
						Timeline->GetAnimationBindings().begin(), Timeline->GetAnimationBindings().end());
			}
			Schedule.CreateNextGroup(std::move(ElementsSubGroup));
			bool const bHasOnlyNeutralTasks = Schedule.HasOnlyNeutralBindings(
				pSubgroupTimeline->AnimationBindings().begin(), pSubgroupTimeline->AnimationBindings().end());
			for (size_t AnimationBindingIndex : ElemTimeline.AnimationBindings())
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

void FITwinScheduleTimelineBuilder::OnReceivedScheduleStats(FITwinScheduleStats const& Stats, FSchedLock&)
{
	// TODO_GCO: could reserve MainTimeline's container
}

void FITwinScheduleTimelineBuilder::AddAnimationBindingToTimeline(FITwinSchedule const& Schedule,
	size_t const AnimationBindingIndex, FSchedLock&)
{
	auto&& Binding = Schedule.AnimationBindings[AnimationBindingIndex];
	if (!ensure(Binding.NotifiedVersion == VersionToken::None))
		return;
	std::optional<FIModelElementsKey> AnimationKey;
	std::visit([&](auto&& Ident)
		{
			using T = std::decay_t<decltype(Ident)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
			{
				AnimationKey.emplace(Ident);
			}
			else if constexpr (std::is_same_v<T, FGuid>)
			{
				AnimationKey.emplace(Ident);
			}
			else if constexpr (std::is_same_v<T, FString>)
			{
				AnimationKey.emplace(Binding.GroupInVec);
			}
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		Binding.AnimatedEntities);
	if (!AnimationKey)
		return;
	FITwinElementTimeline& ElementTimeline = Impl->MainTimeline.ElementTimelineFor(*AnimationKey, {});
	ElementTimeline.AnimationBindings().emplace_back(AnimationBindingIndex);
	if (Impl->Owner && Impl->Owner->bDebugWithDummyTimelines)
	{
		ITwin::Timeline::CreateTestingTimeline(ElementTimeline, *Impl->CoordConversions);
	}
}

//! Handle inter-task dependencies: some constraints need to be applied per-Element, like showing Elements
//! outside tasks only after the first Install task (except if none): the first Install task for Elements
//! of a same assignment can differ, so the case cannot be handled by merely adding a keyframe to any of the
//! existing ElementTimelineEx.
void FITwinScheduleTimelineBuilder::FinalizeTimeline(FITwinSchedule& Schedule)
{
	AITwinIModel* IModel = Impl->Owner ? Cast<AITwinIModel>(Impl->Owner->GetOwner()) : nullptr;
	if (!ensure(IModel || IsUnitTesting()))
		return;
	FITwinSceneMapping* pScene = nullptr;
	if (!IsUnitTesting()) // TODO_GCO: Source ID mapping not loaded yet for unit testing :/
	{
		pScene = &GetInternals(*IModel).SceneMapping;
	}
	for (auto ElemTimelinePtr : Impl->MainTimeline.GetContainer())
	{
		if (!ensure(!ElemTimelinePtr->AnimationBindings().empty()))
			continue;
		// All bindings listed necessarily animate the same Elements since are part of the same timeline
		auto&& Binding = Schedule.AnimationBindings[*ElemTimelinePtr->AnimationBindings().begin()];
		if (!ensure(Binding.NotifiedVersion == VersionToken::InitialVersion))
			return;
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
					if (pScene && pScene->FindElementIDForGUID(Ident, SingleElementID))
					{
						BoundElements.insert(SingleElementID);
					}
				}
				else if constexpr (std::is_same_v<T, FString>)
				{
					auto&& FedGUID2ElemID = std::bind(&FITwinSceneMapping::FindElementIDForGUID, pScene,
													  std::placeholders::_1, std::placeholders::_2);
					BoundElements = Schedule.GetGroupAsElementIDs(Binding.GroupInVec, FedGUID2ElemID);
				}
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Binding.AnimatedEntities);
		FElementsGroup AnimatedMeshElements;
		// until we load iModel metadata for it, Unit Testing can only support "flat" iModels
		// and no SourceID-duplicates!
		if (IsUnitTesting())
		{
			AnimatedMeshElements = BoundElements;
		}
		else
		{
			if (!ensure(!BoundElements.empty()))
				return;
			Detail::InsertAnimatedMeshSubElemsRecursively(ElemTimelinePtr->GetIModelElementsKey(), *pScene,
				BoundElements, Impl->MainTimeline, AnimatedMeshElements,
				GetInternals(*Impl->Owner).PrefetchWholeSchedule());
			Detail::HideNonAnimatedDuplicates(*pScene, AnimatedMeshElements, Impl->MainTimeline);
		}
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
		if (IsUnitTesting() // TODO_GCO: no iModel nor SceneMapping at all for unit testing, right?
			|| !Impl->CreateTimelineKeyframesWithTaskDependencies(*pScene, Schedule, *ElemTimelinePtr,
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
	for (auto&& ConstrDetailParent : pScene->GetConstructionDetailingParentsToHide())
	{
		auto const& Elem = pScene->ElementFor(ConstrDetailParent);
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
	if (Owner && !Owner->bDebugWithDummyTimelines)
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
		if (ITwin::INVALID_IDX != Binding.TransfoAssignmentInVec) // optional
		{
			// Animation binding can have both static transfo and 3D path (with same Id, see azdev#1689132),
			// In that case, we store both assignments separately (see KnownTransfoAssignments's bool subkey),
			// to avoid having to worry about concurrent writes to the TransfoAssignment variant.
			// But the static transform is ignored like in Synchro Pro (TransfoAssignment.bStaticTransform is
			// set to false in FITwinSchedulesImport::FImpl::RequestAnimationBindings).
			auto&& TransfoAssignment = Schedule.TransfoAssignments[Binding.TransfoAssignmentInVec];
			if (Binding.bStaticTransform
				&& std::holds_alternative<FTransform>(TransfoAssignment.Transformation))
			{
				TransformKeyframe = &ITwin::Timeline::AddStaticTransformToTimeline(ElementTimeline,
					Task.TimeRange, std::get<0>(TransfoAssignment.Transformation), *CoordConversions, TaskDeps);
			}
			else if (!Binding.bStaticTransform
				&& std::holds_alternative<FPathAssignment>(TransfoAssignment.Transformation))
			{
				auto&& PathAssignment = std::get<1>(TransfoAssignment.Transformation);
				if (ensure(ITwin::INVALID_IDX != PathAssignment.Animation3DPathInVec))
				{
					auto&& Path3D = Schedule.Animation3DPaths[PathAssignment.Animation3DPathInVec].Keyframes;
					ITwin::Timeline::Add3DPathTransformToTimeline(&ElementTimeline, Task.TimeRange,
						PathAssignment, Path3D, *CoordConversions, TaskDeps);
				}
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
	}
	if (OnElementsTimelineModified) OnElementsTimelineModified(ElementTimeline, nullptr);
}

/*static*/
FITwinScheduleTimelineBuilder FITwinScheduleTimelineBuilder::CreateForUnitTesting(
	FITwinCoordConversions const& InCoordConv)
{
	FITwinScheduleTimelineBuilder Builder;
	Builder.Impl->Owner = nullptr;
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
	Impl->Owner = Other.Impl->Owner;
	Impl->CoordConversions = Other.Impl->CoordConversions;
	Impl->MainTimeline = Other.Impl->MainTimeline;
	Impl->OnElementsTimelineModified = Other.Impl->OnElementsTimelineModified;
	ensure(EInit::Pending == Other.InitState);
	InitState = Other.InitState;
	Other.InitState = EInit::Disposable;
	return *this;
}

FITwinScheduleTimelineBuilder::~FITwinScheduleTimelineBuilder()
{
	ensure(EInit::Ready != InitState); // Pending or Disposable are both OK
}

void FITwinScheduleTimelineBuilder::Uninitialize()
{
	if (!ensure(EInit::Disposable != InitState))
		return;
	if (EInit::Pending != InitState)
	{
		for (auto const& ElementTimelinePtr : Impl->MainTimeline.GetContainer())
			if (ElementTimelinePtr->ExtraData)
				delete (static_cast<FTimelineToScene*>(ElementTimelinePtr->ExtraData));

		AITwinIModel* IModel = Cast<AITwinIModel>(Impl->Owner->GetOwner());
		if (ensure(IModel))
		{
			auto& SceneMapping = GetInternals(*IModel).SceneMapping;
			SceneMapping.ForEachKnownTile([](FITwinSceneTile& SceneTile)
				{
					SceneTile.TimelinesIndices.clear();
				});
			SceneMapping.MutateElements([](FITwinElement& Elem)
				{
					Elem.AnimationKeys.clear();
					Elem.Requirements = {};
				});
		}
	}
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
	FString CoordConvPath;
	if (RelPath.EndsWith(".json"))
	{
		CoordConvPath = Path.LeftChop(4);
	}
	else
	{
		CoordConvPath = Path;
		Path.Append(".json");
	}
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
}
