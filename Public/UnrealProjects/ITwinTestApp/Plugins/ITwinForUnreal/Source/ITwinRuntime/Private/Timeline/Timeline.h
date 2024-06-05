/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Caymus/IModelUsdNodeAddonUtils/Timeline.h
// (semantically the same as vue.git/viewer/Code/IModelJs/IModelModule/RenderSchedule.h
//  but without the V1/V2/V3 versions)

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Schedule/Definition.h"
#include "Schedule/Schedule.h"
#include <Boost/BoostHash.h>
#include <ITwinElementID.h>

#include <optional>
#include <unordered_map>

class AActor;
class FJsonValue;

namespace ITwin::Timeline {

LRT_SCHEDULE_DEFINE_PROPERTY_VALUES(PVisibility,
	(float, value_)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PVisibility const& Prop);

LRT_SCHEDULE_DEFINE_PROPERTY_VALUES(PColor,
	(bool, hasColor_, ITwin::Schedule::Interpolators::BoolOr)
	(FVector, value_)
)
[[nodiscard]] bool operator ==(const PColor& x, const PColor& y);
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop);

LRT_SCHEDULE_DEFINE_PROPERTY_VALUES(PTransform,
	(FVector, translation_)
	(FQuat, orientation_)
	(FVector, pivot_)
	(FVector, scale_)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PTransform const&);
[[nodiscard]] FMatrix TransformToMatrix(const PTransform& t);

enum class EGrowthBoundary : uint8_t
{
	/// The growth animation has reached a point where the Element is fully hidden
	/// (ie. construction has not started, or removal has finished)
	FullyRemoved,
	/// The growth animation has reached a point where the Element is fully grown (visible)
	/// (ie. construction has finished, or removal has not started)
	FullyGrown,
	/// TODO_GCO: this enum is partly redundant with fullyVisible_/fullyHidden_, let's add another value NotABoundary and remove them.
};

struct FDeferredPlaneEquation
{
	mutable FVector4 planeEquation_;
	EGrowthBoundary growthBoundary_;
};
[[nodiscard]] std::size_t hash_value(const FDeferredPlaneEquation& v) noexcept;

LRT_SCHEDULE_DEFINE_PROPERTY_VALUES(PClippingPlane,
	(FDeferredPlaneEquation, deferredPlaneEquation_)
	(bool, fullyVisible_, ITwin::Schedule::Interpolators::BoolAnd)
	(bool, fullyHidden_, ITwin::Schedule::Interpolators::BoolAnd)
)
[[nodiscard]] bool operator ==(const PClippingPlane& x, const PClippingPlane& y);
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PClippingPlane const&);

/// Defines class ElementTimeline, which is an ObjectTimeline that associates the SYNCHRO schedule animated
/// properties (visibility, color, transform and cutting plane) to an element and is used to query the state
/// at a given time (see GetStateAtTime)
LRT_SCHEDULE_DEFINE_OBJECT_PROPERTIES(Element,
	(PVisibility, visibility_)
	(PColor, color_)
	(PTransform, transform_)
	(PClippingPlane, clippingPlane_)
)

/// ElementTimeline stores the individual property timelines for a given Synchro4D Task's
/// animatedElementId. This class adds a mapping to the scene entities belonging to the
/// Element, and their initial transformation matrix, in case it can be animated.
class ElementTimelineEx : public ElementTimeline
{
public:
	using Super = ElementTimeline;
	static const double DeferredPlaneEquationW;
	ITwinElementID IModelElementID;

	ElementTimelineEx(ITwinElementID _IModelElementID) : IModelElementID(_IModelElementID) {}

	/// Returns whether the timeline is truly empty (no keyframe at all), not just whether its time range is
	/// reduced to a single time point.
	[[nodiscard]] bool IsEmpty() const;
	void SetColorAt(double const Time, std::optional<FVector> Color,
					ITwin::Schedule::InterpolationMode const Interp);
	/// Note: the fourth coordinate of the plane equation is missing, because we can't generally expect to
	/// have the Element's bounding box when creating the timeline's keyframes.
	/// The constant DeferredPlaneEquationW is used instead, until it can be properly initialized.
	void SetCuttingPlaneAt(double const Time,
		std::variant<FDeferredPlaneEquation, bool> const& OrientationOrFullVisibility,
		ITwin::Schedule::InterpolationMode const Interp);
	/// \return Whether the timeline has any CuttingPlane keyframe where fullyHidden_ is true
	[[nodiscard]] bool HasFullyHidingCuttingPlaneKeyframes() const;
	/// \return Whether the timeline has any Visibility keyframe where the transparency is neither 0 nor 1
	[[nodiscard]] bool NeedsPartialVisibility() const;
	void SetVisibilityAt(double const Time, std::optional<float> Alpha,
						 ITwin::Schedule::InterpolationMode const Interp);
	//TODO_GCO: SetTransformAt(..)

	void ToJson(TSharedRef<FJsonObject>& JsonObj) const override;
	template<typename JsonPrintPolicy> [[nodiscard]] FString ToJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	[[nodiscard]] FString ToCondensedJsonString() const;
};

class MainTimeline : public ITwin::Schedule::MainTimeline<ElementTimelineEx>
{
	using Super = ITwin::Schedule::MainTimeline<ElementTimelineEx>;

	/// Maps each animated element to a single timeline that references it
	std::unordered_map<ITwinElementID, int /*elementTimelineIndex*/> IModelElementToTimeline;

public:
	ObjectTimelinePtr Add(const ObjectTimelinePtr& object) override;

	/// \return -1 if none was found
	[[nodiscard]] int GetElementTimelineIndex(ITwinElementID const IModelElementID) const;
	[[nodiscard]] ElementTimelineEx& ElementTimelineFor(ITwinElementID const IModelElementID);
	[[nodiscard]] ElementTimelineEx const& GetElementTimelineByIndex(int TimelineIndex) const;

	template<typename JsonPrintPolicy> [[nodiscard]] FString ToJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	[[nodiscard]] FString ToCondensedJsonString() const;

	/// TODO_GCO: Not sure I'll need that?
	void FixColor();

	// In case we need the elementTimelineId_ inside ElementTimelineEx, we'd have to override this:
	//std::shared_ptr<ElementTimelineEx> Add(const typename Super::ObjectTimelinePtr& object) override;
	// with the code from vue.git/viewer/Code/RealTimeBuilder/IModel/RenderSchedule.inl
};

} // ns ITwin::Timeline

#include "TimelineFwd.h" // fwd and aliases
