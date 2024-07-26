/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Caymus/IModelUsdNodeAddonUtils/Timeline.cpp
//	and vue.git/viewer/Code/IModelJs/IModelModule/RenderSchedule.cpp

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

namespace ITwin::Timeline {

ElementTimelineEx& MainTimeline::ElementTimelineFor(FIModelElementsKey const ElementsKey,
													std::set<ITwinElementID> const& IModelElements)
{
	int const Index = (int)GetContainer().size();
	const auto ItAndFlag = ElementsKeyToTimeline.try_emplace(ElementsKey, Index);
	if (ItAndFlag.second) // was inserted
	{
		auto&& ElementTimelinePtr = Add(std::make_shared<ElementTimelineEx>(ElementsKey, IModelElements));
		check(Index != (int)GetContainer().size());
		return *ElementTimelinePtr;
	}
	else
	{
		return *GetContainer()[ItAndFlag.first->second];
	}
}

ElementTimelineEx* MainTimeline::GetElementTimelineFor(FIModelElementsKey const ElementsKey) const
{
	const auto It = ElementsKeyToTimeline.find(ElementsKey);
	if (It == ElementsKeyToTimeline.end())
		return nullptr;
	else
		return GetContainer()[It->second].get();
}

void MainTimeline::ForEachElementTimeline(ITwinElementID const& ElementID,
										  std::function<void(ElementTimelineEx&)> const& Func)
{
	for (auto& ElementTimelinePtr : GetContainer())
	{
		if (ElementTimelinePtr->AppliesToElement(ElementID))
			Func(*ElementTimelinePtr);
	}
}

void MainTimeline::ForEachElementTimeline(ITwinElementID const& ElementID,
										  std::function<void(ElementTimelineEx const&)> const& Func) const
{
	for (auto const& ElementTimelinePtr : GetContainer())
	{
		if (ElementTimelinePtr->AppliesToElement(ElementID))
			Func(*ElementTimelinePtr);
	}
}

bool MainTimeline::HasTimelineForElement(ITwinElementID const& ElementID) const
{
	bool bFound = false;
	ForEachElementTimeline(ElementID, [&bFound](FITwinElementTimeline const&) {bFound = true; });
	return bFound;
}

bool ElementTimelineEx::AppliesToElement(ITwinElementID const& ElementID) const
{
	// this and std::holds_alternative<ITwinElementID>(ElementsKey.Key) are equivalent conditions,
	// but which would be the fastest?
	if (IModelElements.size() == 1)
	{
		// or: std::get<0>(ElementsKey.Key) == ElementID)
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

void ElementTimelineEx::SetCuttingPlaneAt(double const Time, std::optional<FVector> PlaneOrientation,
	EGrowthStatus const GrowthStatus, EInterpolation const Interp)
{
	PropertyEntry<PClippingPlane> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.DefrdPlaneEq = FDeferredPlaneEquation{
		PlaneOrientation ? FVector3f(*PlaneOrientation) : FVector3f::ZeroVector,
		/*need init, see operator ==*/0.f,
		GrowthStatus
	};
	check(Entry.DefrdPlaneEq.IsDeferred() || !PlaneOrientation); // otherwise need to pass W as well
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
	for (auto&& Keyframe : Visibility.Values)
	{
		// TODO_GCO: a bit too conservative: Linear interpolation between visibilities of 0 or 1 at both end of a time segment does not mean the timeline has partial visibility. But the case is probably quite unlikely and rather an issue with the timeline building
		if (Keyframe.Interpolation == EInterpolation::Linear
			|| (Keyframe.Value != 0.f && Keyframe.Value != 1.f))
		{
			return true;
		}
	}
	return false;
}

/*static*/
std::shared_ptr<FDeferredAnchorPos> FDeferredAnchorPos::MakeShared(EAnchorPoint const Anchor)
{
	check(Anchor != EAnchorPoint::Custom);
	return std::make_shared<FDeferredAnchorPos>(FDeferredAnchorPos{ Anchor, FVector::Zero() });
}

/*static*/
std::shared_ptr<FDeferredAnchorPos> FDeferredAnchorPos::MakeSharedCustom(FVector const& CustomAnchor)
{
	return std::make_shared<FDeferredAnchorPos>(FDeferredAnchorPos{ EAnchorPoint::Custom, CustomAnchor });
}

void ElementTimelineEx::SetTransformationAt(double const Time, FTransform const& InTransform,
	std::shared_ptr<FDeferredAnchorPos> SharedAnchor, EInterpolation const Interp)
{
	check(SharedAnchor);
	PropertyEntry<PTransform> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.bIsTransformed = ITwin::Flag::Present;
	Entry.Transform = InTransform;
	Entry.DefrdAnchor = SharedAnchor;
	Transform.Values.insert(Entry);
}

void ElementTimelineEx::SetTransformationDisabledAt(double const Time, EInterpolation const Interp)
{
	PropertyEntry<PTransform> Entry;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.bIsTransformed = ITwin::Flag::Absent;
	Entry.Transform = FTransform::Identity;
	Transform.Values.insert(Entry);
}

void ElementTimelineEx::AddIModelElements(std::vector<ITwinElementID> const& NewElements)
{
	check(IModelElementsKey.Key.index() == 1);
	for (auto&& Elem : NewElements)
	{
		if (IModelElements.insert(Elem).second)
			bIModelElementsBBoxNeedsUpdate = true;
	}
}

FBox const& ElementTimelineEx::GetIModelElementsBBox(
	std::function<FBox(std::set<ITwinElementID> const&)> ElementsBBoxGetter)
{
	if (bIModelElementsBBoxNeedsUpdate)
	{
		bIModelElementsBBoxNeedsUpdate = false;
		IModelElementsBoundingBox = ElementsBBoxGetter(IModelElements);
	}
	return IModelElementsBoundingBox;
}

FVector const& ElementTimelineEx::GetIModelElementOffsetInGroup(ITwinElementID const ElementID,
	std::function<FBox(std::set<ITwinElementID> const&)> const& GroupBBoxGetter,
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
		auto&& Translation = Prop.Transform.GetTranslation();
		JsonObj->SetArrayField(TEXT("translation"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Translation.X),
			  MakeShared<FJsonValueNumber>(Translation.Y),
			  MakeShared<FJsonValueNumber>(Translation.Z) }));
		FQuat const OrientationQuat = Prop.Transform.GetRotation().GetNormalized();
		FVector Orientation = OrientationQuat.ToRotationVector();
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
		auto&& Scale = Prop.Transform.GetScale3D();
		if (Scale != FVector::OneVector)
		{
			JsonObj->SetArrayField(TEXT("scale"), TArray<TSharedPtr<FJsonValue>>(
				{ MakeShared<FJsonValueNumber>(Scale.X),
				  MakeShared<FJsonValueNumber>(Scale.Y),
				  MakeShared<FJsonValueNumber>(Scale.Z) }));
		}
		// should be the case, even though static transforms are defined without an anchor at the moment
		if (Prop.DefrdAnchor)
		{
			if (Prop.DefrdAnchor->AnchorPoint != EAnchorPoint::Custom)
			{
				JsonObj->SetStringField(TEXT("anchor"), GetAnchorPointString(Prop.DefrdAnchor->AnchorPoint));
			}
			else
			{
				JsonObj->SetArrayField(TEXT("anchor"), TArray<TSharedPtr<FJsonValue>>(
					{ MakeShared<FJsonValueNumber>(Prop.DefrdAnchor->Pos.X),
						MakeShared<FJsonValueNumber>(Prop.DefrdAnchor->Pos.Y),
						MakeShared<FJsonValueNumber>(Prop.DefrdAnchor->Pos.Z) }));
			}
		}
		return MakeShared<FJsonValueObject>(std::move(JsonObj));
	}
	else
	{
		return MakeShared<FJsonValueString>(TEXT("Untransformed"));
	}
}

void ElementTimelineEx::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	TArray<TSharedPtr<FJsonValue>> JsonElems;
	for (auto&& Elem : IModelElements)
	{
		// no uint64 json value :/
		JsonElems.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("0x%I64x"), Elem.value())));
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

bool operator ==(const PColor& x, const PColor& y)
{
	// If bHasColor is false in both, do not check Value.
	return x.bHasColor == y.bHasColor &&
		(!x.bHasColor || x.Value == y.Value);
}

bool operator ==(const PClippingPlane& x, const PClippingPlane& y)
{
	return (((int)x.DefrdPlaneEq.GrowthStatus & Detail::GrowthStatus::IgnoreDeferred)
				== ((int)y.DefrdPlaneEq.GrowthStatus & Detail::GrowthStatus::IgnoreDeferred))
		&& x.DefrdPlaneEq.PlaneOrientation == y.DefrdPlaneEq.PlaneOrientation
		&& (x.DefrdPlaneEq.IsDeferred() || x.DefrdPlaneEq.PlaneW == y.DefrdPlaneEq.PlaneW);
}

std::size_t hash_value(const FDeferredPlaneEquation& v) noexcept
{
	std::size_t seed = 0;
	boost::hash_combine(seed, v.PlaneOrientation);
	boost::hash_combine(seed, (int)v.GrowthStatus & Detail::GrowthStatus::IgnoreDeferred);
	if (!v.IsDeferred())
		boost::hash_combine(seed, v.PlaneW);
	return seed;
}

// Only suited to compare exact keyframes (FP comparisons...) applying to the same Elements group
bool operator ==(const PTransform& x, const PTransform& y)
{
	if (x.bIsTransformed == y.bIsTransformed
		&& (!x.bIsTransformed
			// should we use approximate equality?
			|| FITwinMathExts::StrictlyEqualTransforms(x.Transform, y.Transform)))
	{
		if (x.DefrdAnchor->AnchorPoint != y.DefrdAnchor->AnchorPoint) return false;
		return (x.DefrdAnchor->AnchorPoint != EAnchorPoint::Custom
			|| x.DefrdAnchor->Pos == y.DefrdAnchor->Pos);
	}
	return false;
}

std::size_t hash_value(const ElementTimelineEx& Timeline) noexcept
{
	size_t seed = hash_value((ElementTimelineEx::Super const&)Timeline);
	if (std::holds_alternative<ITwinElementID>(Timeline.GetIModelElementsKey().Key))
		boost::hash_combine(seed, std::get<0>(Timeline.GetIModelElementsKey().Key).value());
	else
		boost::hash_combine(seed, std::get<1>(Timeline.GetIModelElementsKey().Key));
	return seed;
}

bool operator ==(const ElementTimelineEx& A, const ElementTimelineEx& B)
{
	return (const ElementTimelineEx::Super&)A == (const ElementTimelineEx::Super&)B
		&& A.GetIModelElementsKey() == B.GetIModelElementsKey();
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

template<typename JsonPrintPolicy> FString MainTimeline::ToJsonString() const
{
	TArray<TSharedPtr<FJsonValue>> TimelinesArray;
	TimelinesArray.Reserve((int32)GetContainer().size());
	for (auto&& ElementTimeline : GetContainer())
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