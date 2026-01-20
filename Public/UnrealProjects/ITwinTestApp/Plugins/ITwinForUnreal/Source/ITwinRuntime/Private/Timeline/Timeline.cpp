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
#include <Timeline/TimelineBase.h>
#include <Math/UEMathExts.h>

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Math/UnrealMathUtility.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Math/UEMathExts.h>

/*static*/ FIModelElementsKey FIModelElementsKey::NOT_ANIMATED(ITwin::NOT_ELEMENT);

namespace ITwin::Timeline {

ElementTimelineEx& MainTimeline::ElementTimelineFor(FIModelElementsKey const ElementsKey,
													FElementsGroup const& IModelElements)
{
	int const Index = (int)GetContainer().size();
	const auto ItAndFlag = ElementsKeyToTimeline.try_emplace(ElementsKey, Index);
	if (ItAndFlag.second) // was inserted
	{
		bHasNewOrModifiedTimeline_ = true;
		auto&& ElementTimelinePtr = AddTimeline(
			std::make_shared<ElementTimelineEx>(ElementsKey, IModelElements));
		check(Index != (int)GetContainer().size());
		return *ElementTimelinePtr;
	}
	else
	{
		return *GetContainer()[ItAndFlag.first->second];
	}
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

ElementTimelineEx* MainTimeline::GetElementTimelineFor(FIModelElementsKey const& ElementsKey,
													   int* Index/*= nullptr*/) const
{
	const auto It = ElementsKeyToTimeline.find(ElementsKey);
	if (It == ElementsKeyToTimeline.end())
		return nullptr;
	else
	{
		if (Index)
			*Index = It->second;
		return GetContainer()[It->second].get();
	}
}

void MainTimeline::AddNonAnimatedDuplicate(ITwinElementID const Elem)
{
	NonAnimatedDuplicates.insert(Elem);
}

void MainTimeline::RemoveNonAnimatedDuplicate(ITwinElementID const Elem)
{
	NonAnimatedDuplicates.erase(Elem);
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
		// assuming Alpha is multiplied, so 1. indeed means "use original alpha"
		Visibility.Values.insert(MakeEntry<PVisibility>(Time, 1.f, Interp));
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
		if (It->Value != 0.f && It->Value != 1.f)
		{
			return true;
		}
		auto NextIt = It;
		++NextIt;
		// The only way to have transparency between the two frames is if going from 0 to 1 or 1 to 0 with
		// Linear interpolation:
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

TSharedPtr<FJsonValue> ToJsonValue(PVisibility const& Prop)
{
	return MakeShared<FJsonValueNumber>(Prop.Value);
}

TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop)
{
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueBoolean>(Prop.bHasColor == ITwin::Flag::Present),
		  MakeShared<FJsonValueNumber>(Prop.Value.X), MakeShared<FJsonValueNumber>(Prop.Value.Y),
		  MakeShared<FJsonValueNumber>(Prop.Value.Z) }));
}

FString GetGrowthStatusString(EGrowthStatus const GrowthStatus)
{
	switch (GrowthStatus)
	{
	case EGrowthStatus::DeferredFullyRemoved: return TEXT("DeferredFullyRemoved");
	case EGrowthStatus::DeferredFullyGrown:	  return TEXT("DeferredFullyGrown");
	case EGrowthStatus::FullyRemoved:		  return TEXT("FullyRemoved");
	case EGrowthStatus::FullyGrown:			  return TEXT("FullyGrown");
	case EGrowthStatus::Partial:			  return TEXT("PartiallyGrown");
	default:								  return TEXT("<InvalidGrowthStatus>");
	}
}

TSharedPtr<FJsonValue> ToJsonValue(PClippingPlane const& Prop)
{
	auto&& PlaneDir = Prop.DefrdPlaneEq.PlaneOrientation;
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueString>(GetGrowthStatusString(Prop.DefrdPlaneEq.GrowthStatus)),
		  MakeShared<FJsonValueNumber>(PlaneDir.X), MakeShared<FJsonValueNumber>(PlaneDir.Y),
		  MakeShared<FJsonValueNumber>(PlaneDir.Z),
		  MakeShared<FJsonValueNumber>(Prop.DefrdPlaneEq.PlaneW) }));
}

TSharedPtr<FJsonValue> ToJsonValue(PTransform const& Prop)
{
	if (Prop.bIsTransformed)
	{
		auto JsonObj = MakeShared<FJsonObject>();
		auto&& Translation = Prop.Position;
		JsonObj->SetArrayField(TEXT("translation"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Translation.X),
			  MakeShared<FJsonValueNumber>(Translation.Y),
			  MakeShared<FJsonValueNumber>(Translation.Z) }));
		FVector Orientation = Prop.Rotation.ToRotationVector();
		double LenIsAngle = Orientation.SquaredLength();
		if (LenIsAngle != 0.)
		{
			LenIsAngle = std::sqrt(LenIsAngle);
			Orientation /= LenIsAngle; // yes, see FQuat::ToRotationVector's doc
			JsonObj->SetArrayField(TEXT("rotationAxis"), TArray<TSharedPtr<FJsonValue>>(
				{ MakeShared<FJsonValueNumber>(Orientation.X),
				  MakeShared<FJsonValueNumber>(Orientation.Y),
				  MakeShared<FJsonValueNumber>(Orientation.Z) }));
			JsonObj->SetNumberField(TEXT("rotationAngleDegrees"), FMath::RadiansToDegrees(LenIsAngle));
		}
		if (Prop.DefrdAnchor.IsDeferred() || EAnchorPoint::Static == Prop.DefrdAnchor.AnchorPoint)
		{
			JsonObj->SetStringField(TEXT("anchor"), GetAnchorPointString(Prop.DefrdAnchor.AnchorPoint));
		}
		else
		{
			JsonObj->SetArrayField(TEXT("anchor"), TArray<TSharedPtr<FJsonValue>>(
				{ MakeShared<FJsonValueNumber>(Prop.DefrdAnchor.Offset.X),
					MakeShared<FJsonValueNumber>(Prop.DefrdAnchor.Offset.Y),
					MakeShared<FJsonValueNumber>(Prop.DefrdAnchor.Offset.Z) }));
		}
		return MakeShared<FJsonValueObject>(std::move(JsonObj));
	}
	else
	{
		return MakeShared<FJsonValueString>(TEXT("Untransformed"));
	}
}

namespace Detail
{
	std::set<ITwinElementID> GetSortedElements(MainTimeline::ObjectTimeline const& A)
	{
		std::set<ITwinElementID> Sorted;
		for (auto&& Elem : A.GetIModelElements())
			Sorted.insert(Elem);
		return Sorted;
	}
}

void ElementTimelineEx::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	// See MainTimeline::ToJsonString
	auto SortedElems = Detail::GetSortedElements(*this);
	TArray<TSharedPtr<FJsonValue>> JsonElems;
	for (auto&& Elem : SortedElems)
	{
		// no uint64 json value :/
		JsonElems.Add(MakeShared<FJsonValueString>(ITwin::ToString(Elem)));
	}
	JsonObj->SetArrayField(TEXT("elementIds"), JsonElems);
	Super::ToJson(JsonObj);
}

template<typename JsonPrintPolicy> FString ElementTimelineEx::ToJsonString() const
{
	auto JsonObj = MakeShared<FJsonObject>();
	ToJson(JsonObj);
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> JsonWriter =
		TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	return JsonString;
}

FString ElementTimelineEx::ToCondensedJsonString() const
{
	return ToJsonString<TCondensedJsonPrintPolicy<TCHAR>>();
}

FString ElementTimelineEx::ToPrettyJsonString() const
{
	return ToJsonString<TPrettyJsonPrintPolicy<TCHAR>>();
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

struct TimelineCompareOrderedElementIDs
{
	bool operator()(MainTimeline::ObjectTimelinePtr const& A, MainTimeline::ObjectTimelinePtr const& B) const
	{
		// The FElementsGroup must be sorted, too...
		std::set<ITwinElementID> SortedA = Detail::GetSortedElements(*A);
		std::set<ITwinElementID> SortedB = Detail::GetSortedElements(*B);
		auto ItA = SortedA.begin(), ItB = SortedB.begin();
		for (; ItA != SortedA.end() && ItB != SortedB.end(); ++ItA, ++ItB)
		{
			if ((*ItA) != (*ItB))
			{
				return (*ItA) < (*ItB);
			}
		}
		// Sets are equal until reaching the end of either or both
		if (ItA == SortedA.end() && ItB == SortedB.end()) // A == B => !(A < B)
			return false;
		else if (ItA == SortedA.end()) // A is shorter (ie subset of B) => A < B
			return true;
		else // A superset of B => A > B
			return false;
	}
};

template<typename JsonPrintPolicy> FString MainTimeline::ToJsonString() const
{
	TArray<TSharedPtr<FJsonValue>> TimelinesArray;
	TimelinesArray.Reserve((int32)GetContainer().size());
	// Use a deterministic ordering, since the order of timelines in the container can depend on the order
	// of data received from 4D api. ElementsKeyToTimeline isn't suitable either (even with a fixed hash func)
	// because FIModelElementsKey can be the Elements group index, which also depends on http replies ordering.
	std::set<ObjectTimelinePtr, TimelineCompareOrderedElementIDs> OrderedTimelines;
	for (auto&& ElementTimeline : GetContainer())
	{
		OrderedTimelines.insert(ElementTimeline);
	}
	for (auto&& ElementTimeline : OrderedTimelines)
	{
		auto ElementTimelineJsonObj = MakeShared<FJsonObject>();
		ElementTimeline->ToJson(ElementTimelineJsonObj);
		TimelinesArray.Add(MakeShared<FJsonValueObject>(ElementTimelineJsonObj));
	}
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> JsonWriter =
		TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&JsonString);
	FJsonSerializer::Serialize(TimelinesArray, JsonWriter);
	return JsonString;
}

FString MainTimeline::ToCondensedJsonString() const
{
	return ToJsonString<TCondensedJsonPrintPolicy<TCHAR>>();
}

FString MainTimeline::ToPrettyJsonString() const
{
	return ToJsonString<TPrettyJsonPrintPolicy<TCHAR>>();
}

/// Not using this at the moment because when doing "Stop", all animation properties are removed anyway.
/// But in Pineapple ("iModel viewer") the default state is just the end state of the schedule, so that
/// temporary and removed Elements are not visible for example: if FixColor was added because there is a
/// requirement that the end state of the schedule should no longer show colors, then IMHO we should do it
/// explicitly by resetting color textures (with or without alpha) like in FITwinSynchro4DAnimator::Stop(),
/// rather than adding a keyframe to each and every timeline, which spoils the optimization in ApplyAnimation 
/// (see comment about FixColor there). In fact the requirement is maybe only to keep the visible/hidden state
/// of the schedule animation and reset all other properties?
void MainTimeline::FixColor()
{
	for (const auto& ObjectTimeline : GetContainer())
	{
		// The color animation must be fixed if:
		// - there is a color animation
		// - and the color is enabled at the end of the animation
		// - and the object is visible at the end of the animation
		//   (no need to fix the color if the object is no longer visible anyway).
		if (ObjectTimeline->Color.Values.empty() ||
			!ObjectTimeline->Color.Values.rbegin()->bHasColor ||
			(!ObjectTimeline->Visibility.Values.empty() &&
				ObjectTimeline->Visibility.Values.rbegin()->Value == 0))
			continue;
		// Create an entry at the end of the entire timeline animation,
		// with a disabled color overlay.
		PropertyEntry<PColor> Entry;
		Entry.Time = GetTimeRange().second;
		Entry.bHasColor = ITwin::Flag::Absent;
		ObjectTimeline->Color.Values.insert(Entry);
	}
}

} // ns ITwin::Timeline
