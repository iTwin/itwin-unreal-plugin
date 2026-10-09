/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Caymus/IModelUsdNodeAddonUtils/Timeline.cpp
//	and vue.git/viewer/Code/RealTimeBuilder/IModel/RenderSchedule.cpp

#include "Timeline.h"
#include <Timeline/SchedulesConstants.h>
#include <Timeline/TimelineBase.h>

/*static*/ FIModelElementsKey FIModelElementsKey::NOT_ANIMATED(ITwin::NOT_ELEMENT);

namespace ITwin::Timeline {

ElementTimelineEx& MainTimeline::ElementTimelineFor(FIModelElementsKey const& IModelElementsKey,
	FElementsGroup const& IModelElements, int* pTimelineIndex /*= nullptr*/)
{
	int const Index = (int)GetContainer().size();
	const auto ItAndFlag = ElementsKeyToTimeline.try_emplace(IModelElementsKey, Index);
	if (ItAndFlag.second) // was inserted
	{
		bHasNewOrModifiedTimeline_ = true;
		auto&& ElementTimelinePtr = AddTimeline(
			std::make_shared<ElementTimelineEx>(IModelElementsKey, IModelElements));
		check(Index != (int)GetContainer().size());
		if (pTimelineIndex) *pTimelineIndex = Index;
		return *ElementTimelinePtr;
	}
	else
	{
		if (pTimelineIndex) *pTimelineIndex = ItAndFlag.first->second;
		return *GetContainer()[ItAndFlag.first->second];
	}
}

ElementTimelineEx* MainTimeline::GetElementTimelineFor(FIModelElementsKey const& IModelElementsKey,
	int* pTimelineIndex/*= nullptr*/) const
{
	const auto It = ElementsKeyToTimeline.find(IModelElementsKey);
	if (It == ElementsKeyToTimeline.end())
		return nullptr;
	else
	{
		if (pTimelineIndex)
			*pTimelineIndex = It->second;
		return GetContainer()[It->second].get();
	}
}

void MainTimeline::ResetElementTimelineFor(int TimelineIndex, FIModelElementsKey const& NewIModelElementsKey)
{
	if (!ensure(TimelineIndex < GetContainer().size()))
		return;
	auto& ExistingTimeline = GetContainer()[TimelineIndex];
	ensure(1 == ElementsKeyToTimeline.erase(ExistingTimeline->GetIModelElementsKey()));
	*ExistingTimeline = FITwinElementTimeline(NewIModelElementsKey, std::move(ExistingTimeline->IModelElementsRef()));
	ElementsKeyToTimeline[NewIModelElementsKey] = TimelineIndex;
}

void MainTimeline::SwapWithLastAndDelete(int TimelineIndex)
{
	if (!ensure(TimelineIndex < GetContainer().size()))
		return;
	auto& Slot = GetContainer()[TimelineIndex]; // timeline to delete
	ensure(1 == ElementsKeyToTimeline.erase(Slot->GetIModelElementsKey())); // erase map entry
	if (TimelineIndex < ((int)GetContainer().size() - 1))
	{
		Slot = GetContainer().back(); // replace by the one at the end
		ElementsKeyToTimeline[Slot->GetIModelElementsKey()] = TimelineIndex; // update map entry
	}
	GetContainer().pop_back();
}

void MainTimeline::OnElementsTimelineModified(ElementTimelineEx& ModifiedTimeline)
{
	// See "Note 2" in MainTimelineBase<_ObjectTimeline>::AddTimeline
	IncludeTimeRange(ModifiedTimeline);
	// No longer used to notify Animator that new tiles were received, but still used when new Elements are
	// added to existing (grouped Elements) timelines
	ModifiedTimeline.SetModified();
	// Used to notify the animator's TickAnimation that something has changed (new or modified timeline) so
	// that ApplyAnimation is called and not skipped (important when bPaused_)
	bHasNewOrModifiedTimeline_ = true;
}

void MainTimeline::AddNonAnimatedDuplicate(ITwinElementID const Elem)
{
	NonAnimatedDuplicates.insert(Elem);
}

void MainTimeline::RemoveNonAnimatedDuplicate(ITwinElementID const Elem)
{
	NonAnimatedDuplicates.erase(Elem);
}

void MainTimeline::ClearTimelinesData()
{
	ElementsKeyToTimeline.clear();
	NonAnimatedDuplicates.clear();
	bHasNewOrModifiedTimeline_ = false;
	Super::ClearTimelinesData();
}

bool ElementTimelineEx::AppliesToElement(ITwinElementID const& ElementID) const
{
	if (IModelElements.size() == 1) // also OK for groups of 1, which are not so unusual...
	{
		return (*IModelElements.begin() == ElementID);
	}
	else
	{
		return (IModelElements.end() != IModelElements.find(ElementID));
	}
}

template<typename NamedProperty, typename ValueType>
auto MakeEntry(double const Time, ValueType const& Value, EInterpolation const Interp)
{
	PropertyEntry<NamedProperty> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.Value = Value;
	return Entry;
}

void ElementTimelineEx::SetColorAt(double const Time, std::optional<FVector> InColor,
								   EInterpolation const Interp)
{
	if (InColor)
	{
		auto&& entry = MakeEntry<PColor>(Time, *InColor, Interp);
		entry.bHasColor = ITwin::Flag::Present;
		Color.Values.insert(std::move(entry));
	}
	else
	{
		auto&& entry = MakeEntry<PColor>(Time, FVector::ZeroVector, Interp);
		entry.bHasColor = ITwin::Flag::Absent;
		Color.Values.insert(std::move(entry));
	}
}

void ElementTimelineEx::SetCuttingPlaneAt(double const Time, std::optional<FVector> InPlaneOrientation,
	EGrowthStatus const InGrowthStatus, EInterpolation const Interp,
	PTransform const* const InTransformKeyframe/*=nullptr*/)
{
	PropertyEntry<PClippingPlane> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.DefrdPlaneEq = FDeferredPlaneEquation{
		.PlaneOrientation = InPlaneOrientation ? FVector3f(*InPlaneOrientation) : FVector3f::ZeroVector,
		.TransformKeyframe = InTransformKeyframe,
		.PlaneW = 0.f,/*need init, see operator==*/
		.GrowthStatus = InGrowthStatus
	};
	check(Entry.DefrdPlaneEq.IsDeferred() || !InPlaneOrientation); // otherwise need to pass W as well
	ClippingPlane.Values.insert(Entry);
}

size_t ElementTimelineEx::NumKeyframes() const
{
	return Color.Values.size() + Visibility.Values.size() + Transform.Values.size()
		+ ClippingPlane.Values.size();
}

void ElementTimelineEx::SetVisibilityAt(double const Time, std::optional<float> Alpha,
										EInterpolation const Interp)
{
	if (Alpha)
	{
		Visibility.Values.insert(MakeEntry<PVisibility>(Time, *Alpha, Interp));
	}
	else
	{
		// Hack: we'll use a special value here to identify the "use original alpha" situation.
		// Converted to (uint8)1 in the uint8 texture (see Detail::ClampCast01toU8) and back to float in the GLSL,
		// which gives around 0.0039 - we compare 0 < a < 0.005 anyway. This means a slight loss of precision, but
		// having a 4D appearance profile using an _almost_ transparent color would be quite meaningless anyway.
		Visibility.Values.insert(
			MakeEntry<PVisibility>(Time, S4D_FLOAT_ALPHA_DISABLED/*see HasPartialVisibility below*/, Interp));
	}
}

bool ElementTimelineEx::HasFullyHidingCuttingPlaneKeyframes() const
{
	for (auto&& Keyframe : ClippingPlane.Values)
	{
		if (EGrowthStatus::FullyGrown == Keyframe.DefrdPlaneEq.GrowthStatus
			|| EGrowthStatus::DeferredFullyGrown == Keyframe.DefrdPlaneEq.GrowthStatus)
		{
			return true;
		}
	}
	return false;
}

bool ElementTimelineEx::HasPartialVisibility() const
{
	for (auto It = Visibility.Values.begin(), ItEnd = Visibility.Values.end(); It != ItEnd; ++It)
	{
		if (It->Value == S4D_FLOAT_ALPHA_DISABLED)
		{
			continue; // see special value in SetVisibilityAt above, skip BOTH tests below
		}
		if (It->Value != 0.f && It->Value != 1.f)
		{
			return true;
		}
		auto NextIt = It;
		++NextIt;
		// The only way to have partial transparency between the two frames even if none of them are partially
		// transparent is obviously if going from 0 to 1 or 1 to 0 with Linear interpolation:
		if (It->Interpolation == EInterpolation::Linear && NextIt != ItEnd && It->Value != NextIt->Value)
		{
			return true;
		}
	}
	return false;
}

PTransform const& ElementTimelineEx::SetTransformationAt(double const Time, FVector const& InPosition,
	FQuat const& InRotation, FDeferredAnchor const& DefrdAnchor, EInterpolation const Interp)
{
	PropertyEntry<PTransform> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.bIsTransformed = ITwin::Flag::Present;
	Entry.Position = InPosition;
	Entry.Rotation = InRotation;
	Entry.DefrdAnchor = DefrdAnchor;
	return *Transform.Values.insert(Entry).first;
}

void ElementTimelineEx::SetTransformationDisabledAt(double const Time, EInterpolation const Interp)
{
	PropertyEntry<PTransform> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.bIsTransformed = ITwin::Flag::Absent;
	Entry.Position = FVector::ZeroVector;
	Entry.Rotation = FQuat::Identity;
	Transform.Values.insert(Entry);
}

FBox const& ElementTimelineEx::GetIModelElementsBBox(
	std::function<FBox(FElementsGroup const&)> ElementsBBoxGetter)
{
	if (bIModelElementsBBoxNeedsUpdate)
	{
		bIModelElementsBBoxNeedsUpdate = false;
		IModelElementsBoundingBox = ElementsBBoxGetter(IModelElements);
	}
	return IModelElementsBoundingBox;
}

FVector const& ElementTimelineEx::GetIModelElementOffsetInGroup(ITwinElementID const ElementID,
	std::function<FBox(FElementsGroup const&)> const& GroupBBoxGetter,
	std::function<FBox const&(ITwinElementID const)> const& SingleBBoxGetter)
{
	if (1 == IModelElements.size())
		return FVector::ZeroVector;
	auto ElemOffset = IModelElementOffsets.try_emplace(ElementID, FVector{});
	if (ElemOffset.second) // was inserted, need to compute it
	{
		ElemOffset.first->second =
			SingleBBoxGetter(ElementID).GetCenter() - GroupBBoxGetter(IModelElements).GetCenter();
	}
	return ElemOffset.first->second;
}

//FMatrix TransformToMatrix(const PTransform& t)
//{
//	// Apply pivot, then scale, then orientation, then translation (see iModel.js doc)
//	// separately (as done in display-test-app) - shouldn't we interpolate separately too,
//	// in order to benefit from the quaternion representation for orientation?
//	// <=> glm::mat4_cast(t.orientation_)*glm::scale(t.scale_)*glm::translate(t.pivot_)
//	//   followed by mat[3] += position_.xyz
//	FMatrix mat = FITwinMathExts::Conjugate(t.orientation_).ToMatrix()
//				* FITwinMathExts::MakeScalingMatrix(t.scale_)
//				* FITwinMathExts::MakeTranslationMatrix(t.pivot_);
//	// Small optim over a full matrix multiplication (or even FMatrix::ConcatTranslation for that matter)
//	mat.SetColumn(3, mat.GetColumn(3) + t.translation_);
//	return mat;
//}

} // ns ITwin::Timeline
