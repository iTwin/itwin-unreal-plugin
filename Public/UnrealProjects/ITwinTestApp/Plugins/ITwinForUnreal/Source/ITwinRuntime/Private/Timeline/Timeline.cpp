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
#include "TimelineBase.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Math/UEMathExts.h>
#include <variant>

namespace ITwin::Timeline {

MainTimeline::ObjectTimelinePtr MainTimeline::Add(const ObjectTimelinePtr& object)
{
	int const Index = (int)GetContainer().size();
	auto&& ret = Super::Add(object);
	if (Index != (int)GetContainer().size())
	{
		IModelElementToTimeline[((ElementTimelineEx const*)object.get())->IModelElementID] = Index;
	}
	return std::move(ret);
}

int MainTimeline::GetElementTimelineIndex(ITwinElementID const IModelElementId) const
{
	const auto it = IModelElementToTimeline.find(IModelElementId);
	return it == IModelElementToTimeline.end() ? -1 : it->second;
}

ElementTimelineEx& MainTimeline::ElementTimelineFor(ITwinElementID const IModelElementID)
{
	int const TimelineIdx = GetElementTimelineIndex(IModelElementID);
	if (-1 != TimelineIdx)
	{
		return *Container()[TimelineIdx];
	}
	else
	{
		return *(Add(std::make_shared<ElementTimelineEx>(IModelElementID)));
	}
}

ElementTimelineEx const& MainTimeline::GetElementTimelineByIndex(int TimelineIdx) const
{
	return *GetContainer()[TimelineIdx];
}

template<typename NamedProperty, typename ValueType>
auto MakeEntry(double const Time, ValueType const& Value, ITwin::Timeline::Interpolation const Interp)
{
	ITwin::Timeline::PropertyEntry<NamedProperty> Entry;
	Entry.time_ = Time;
	Entry.interpolation_ = Interp;
	Entry.value_ = Value;
	return Entry;
}

void ElementTimelineEx::SetColorAt(double const Time, std::optional<FVector> Color,
								   ITwin::Timeline::Interpolation const Interp)
{
	if (Color)
	{
		auto&& entry = MakeEntry<PColor>(Time, *Color, Interp);
		entry.hasColor_ = true;
		color_.list_.insert(std::move(entry));
	}
	else
	{
		auto&& entry = MakeEntry<PColor>(Time, FVector::ZeroVector, Interp);
		entry.hasColor_ = false;
		color_.list_.insert(std::move(entry));
	}
}

template<class> inline constexpr bool always_false_v = false;
/*static*/const double ElementTimelineEx::DeferredPlaneEquationW = std::numeric_limits<double>::max();

void ElementTimelineEx::SetCuttingPlaneAt(double const Time,
	std::variant<FDeferredPlaneEquation, bool> const& OrientationOrFullVisibility,
	ITwin::Timeline::Interpolation const Interp)
{
    std::visit([this, &Time, &Interp](auto&& arg)
		{
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, FDeferredPlaneEquation>)
			{
				ITwin::Timeline::PropertyEntry<PClippingPlane> Entry;
				Entry.time_ = Time;
				Entry.interpolation_ = Interp;
				Entry.deferredPlaneEquation_ = arg;
				Entry.fullyHidden_ = false;
				Entry.fullyVisible_ = false;
				clippingPlane_.list_.insert(Entry);
			}
			else if constexpr (std::is_same_v<T, bool>)
			{
				ITwin::Timeline::PropertyEntry<PClippingPlane> Entry;
				Entry.time_ = Time;
				Entry.interpolation_ = Interp;
				Entry.fullyHidden_ = !arg;
				Entry.fullyVisible_ = arg;
				clippingPlane_.list_.insert(Entry);
			}
			else 
				static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		OrientationOrFullVisibility);
}

bool ElementTimelineEx::IsEmpty() const
{
	return color_.list_.empty() && visibility_.list_.empty() && transform_.list_.empty()
		&& clippingPlane_.list_.empty();
}

void ElementTimelineEx::SetVisibilityAt(double const Time, std::optional<float> Alpha,
										ITwin::Timeline::Interpolation const Interp)
{
	if (Alpha)
	{
		visibility_.list_.insert(MakeEntry<PVisibility>(Time, *Alpha, Interp));
	}
	else
	{
		visibility_.list_.insert(MakeEntry<PVisibility>(Time, 1.f, Interp));
	}
}

bool ElementTimelineEx::HasFullyHidingCuttingPlaneKeyframes() const
{
	for (auto&& Keyframe : clippingPlane_.list_)
	{
		if (Keyframe.fullyHidden_)
		{
			return true;
		}
	}
	return false;
}

bool ElementTimelineEx::NeedsPartialVisibility() const
{
	for (auto&& Keyframe : visibility_.list_)
	{
		if (Keyframe.interpolation_ == ITwin::Timeline::Interpolation::Linear
			|| (Keyframe.value_ != 0.f && Keyframe.value_ != 1.f))
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<FJsonValue> ToJsonValue(PVisibility const& Prop)
{
	return MakeShared<FJsonValueNumber>(Prop.value_);
}

TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop)
{
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueBoolean>(Prop.hasColor_), MakeShared<FJsonValueNumber>(Prop.value_.X),
		  MakeShared<FJsonValueNumber>(Prop.value_.Y), MakeShared<FJsonValueNumber>(Prop.value_.Z) }));
}

TSharedPtr<FJsonValue> ToJsonValue(PClippingPlane const& Prop)
{
	if (Prop.fullyHidden_) return MakeShared<FJsonValueString>(TEXT("FullyHidden"));
	else if (Prop.fullyVisible_) return MakeShared<FJsonValueString>(TEXT("FullyVisible"));

	auto&& PlaneEq = Prop.deferredPlaneEquation_.planeEquation_;
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueString>(
			EGrowthBoundary::FullyGrown == Prop.deferredPlaneEquation_.growthBoundary_
				? TEXT("FullyGrownBoundary") : TEXT("FullyRemovedBoundary")),
		  MakeShared<FJsonValueNumber>(PlaneEq.X), MakeShared<FJsonValueNumber>(PlaneEq.Y),
		  MakeShared<FJsonValueNumber>(PlaneEq.Z), MakeShared<FJsonValueNumber>(PlaneEq.W) }));
}

TSharedPtr<FJsonValue> ToJsonValue(PTransform const& Prop)
{
	auto JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetArrayField(TEXT("translation"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Prop.translation_.X),
			  MakeShared<FJsonValueNumber>(Prop.translation_.Y),
			  MakeShared<FJsonValueNumber>(Prop.translation_.Z) }));
	JsonObj->SetArrayField(TEXT("orientation"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Prop.orientation_.X),
			  MakeShared<FJsonValueNumber>(Prop.orientation_.Y),
			  MakeShared<FJsonValueNumber>(Prop.orientation_.Z),
			  MakeShared<FJsonValueNumber>(Prop.orientation_.W) }));
	JsonObj->SetArrayField(TEXT("pivot"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Prop.pivot_.X),
			  MakeShared<FJsonValueNumber>(Prop.pivot_.Y),
			  MakeShared<FJsonValueNumber>(Prop.pivot_.Z) }));
	JsonObj->SetArrayField(TEXT("scale"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumber>(Prop.scale_.X),
			  MakeShared<FJsonValueNumber>(Prop.scale_.Y),
			  MakeShared<FJsonValueNumber>(Prop.scale_.Z) }));
	return MakeShared<FJsonValueObject>(std::move(JsonObj));
}

void ElementTimelineEx::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	// no uint64 json value :/
	JsonObj->SetStringField("elementId", FString::Printf(TEXT("0x%I64x"), IModelElementID.value()));
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
	// If hasColor_ is false in both, do not check value_.
	return x.hasColor_ == y.hasColor_ &&
		(!x.hasColor_ || x.value_ == y.value_);
}

bool operator ==(const PClippingPlane& x, const PClippingPlane& y)
{
	// Clipping planes are equal if one of these conditions is true:
	// - both are fully visible
	// - both are fully hidden
	// - all fields are equal
	return (x.fullyVisible_ && y.fullyVisible_) ||
		(x.fullyHidden_ && y.fullyHidden_) ||
		(x.deferredPlaneEquation_.planeEquation_ == y.deferredPlaneEquation_.planeEquation_ &&
			x.deferredPlaneEquation_.growthBoundary_ == y.deferredPlaneEquation_.growthBoundary_ &&
			x.fullyVisible_ == y.fullyVisible_ &&
			x.fullyHidden_ == y.fullyHidden_);
}

std::size_t hash_value(const FDeferredPlaneEquation& v) noexcept
{
	std::size_t seed = 0;
	boost::hash_combine(seed, v.planeEquation_);
	boost::hash_combine(seed, v.growthBoundary_);
	return seed;
}

FMatrix TransformToMatrix(const PTransform& t)
{
	// Apply pivot, then scale, then orientation, then translation (see iModel.js doc)
	// separately (as done in display-test-app) - shouldn't we interpolate separately, too
	// in order to benefit from the quaternion representation for orientation?)
	// <=> glm::mat4_cast(t.orientation_)*glm::scale(t.scale_)*glm::translate(t.pivot_)
	//   followed by mat[3] += position_.xyz
	FMatrix mat = FITwinMathExts::Conjugate(t.orientation_).ToMatrix()
				* FITwinMathExts::MakeScalingMatrix(t.scale_)
				* FITwinMathExts::MakeTranslationMatrix(t.pivot_);
	// Small optim over a full matrix multiplication (or even FMatrix::ConcatTranslation for that matter)
	mat.SetColumn(3, mat.GetColumn(3) + t.translation_);
	return mat;
}

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

void MainTimeline::FixColor()
{
	for (const auto& objectTimeline: GetContainer())
	{
		// The color animation must be fixed if:
		// - there is a color animation
		// - and the color is enabled at the end of the animation
		// - and the object is visible at the end of the animation
		//   (no need to fix the color if the object is no longer visible anyway).
		if (objectTimeline->color_.list_.empty() ||
			!objectTimeline->color_.list_.rbegin()->hasColor_ ||
			(!objectTimeline->visibility_.list_.empty() &&
				objectTimeline->visibility_.list_.rbegin()->value_ == 0))
			continue;
		// Create an entry at the end of the entire timeline animation,
		// with a disabled color overlay.
		ITwin::Timeline::PropertyEntry<PColor> entry;
		entry.time_ = GetTimeRange().second;
		entry.hasColor_ = false;
		objectTimeline->color_.list_.insert(entry);
	}
}

} // ns ITwin::Timeline