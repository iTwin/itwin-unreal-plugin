/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Caymus/IModelUsdNodeAddonUtils/Timeline.h
// (semantically the same as vue.git/viewer/Code/RealTimeBuilder/IModel/RenderSchedule.h
//  but without the V1/V2/V3 versions)

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"

#include <Hashing/UnrealMath.h>
#include <ITwinElementID.h>
#include <Timeline/AnchorPoint.h>
#include <Timeline/Definition.h>
#include <Timeline/TimelineBase.h>
#include <Timeline/TimelineTypes.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <BeHeaders/StrongTypes/TaggedValue.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>
#include <unordered_map>

class AActor;
class FJsonValue;

namespace ITwin::Flag {

DEFINE_STRONG_BOOL(FPresence);
constexpr FPresence Present(true);
constexpr FPresence Absent(false);

}

namespace ITwin::Timeline {

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PVisibility,
	(float, Value)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PVisibility const& Prop);
inline bool NoEffect(PVisibility const& Prop) { return Prop.Value == 1.f; }

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PColor,
	(ITwin::Flag::FPresence, bHasColor)
	(FVector, Value)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop);
inline bool NoEffect(PColor const& Prop) { return !Prop.bHasColor; }

/// Offset from the Elements group's axis-aligned bounding box center, to apply after the rotation at a given
/// keyframe. Keep in mind that anchor is a property of a path assignment, ie to use for the whole
/// path for a given task. But we cannot precompute it once for the whole path, because interpolation
/// wouldn't work: interpolation of the rotated offsets != offset after interpolated rotation! At each
/// animation tick, we'll have to interpolate the rotation, then only rotate the local offset.
/// That's why rotation has to be in there, because interpolation works at the timeline property field level.
struct FDeferredAnchor
{
	EAnchorPoint AnchorPoint = EAnchorPoint::Original;
	mutable bool bDeferred = false;///< No offset to compute when using 'Original [Position]'
	/// When 'AnchorPoint' is not 'Original', this is the offset between the BBox center and the anchor point,
	/// WITHOUT keyframe rotation applied, expressed as fully transformed Unreal-space coords (ie. including
	/// a possible iModel transformation aka "offset").
	/// When 'bDeferred' is true, this member is irrelevant /unless/ it's the 'Custom' offset (converted from 
	/// its original definition in iModel coordinates). After 'bDeferred' has been toggled off, the offset has
	/// been computed here from the Element (group)'s BBox
	mutable FVector Offset = FVector::ZeroVector;

	bool IsDeferred() const { return bDeferred; }
};

// 'Position' is the absolute UE world coordinate of the keyframe, /except/ for 'Original' anchor point, in
// which case it is a relative translation from the initial (non-animated) position.
// 'Rotation' is the relative rotation of the Element at the given keyframe, in UE convention, around
// the anchor point.
ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PTransform,
	(ITwin::Flag::FPresence, bIsTransformed)
	(FVector, Position)
	(FQuat, Rotation)
	(FDeferredAnchor, DefrdAnchor)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PTransform const&);
inline bool NoEffect(PTransform const& Prop) { return !Prop.bIsTransformed; }
//[[nodiscard]] FMatrix TransformToMatrix(const PTransform& t);

namespace Detail::GrowthStatus
{
	namespace Bit { enum EBit { Removed, Grown, Deferred }; }
	namespace Mask {
		enum EMask { Removed = (1 << Bit::Removed), Grown = (1 << Bit::Grown),
					 Deferred = (1 << Bit::Deferred) };
	}
	constexpr int IgnoreDeferred = ~Mask::Deferred; ///< to be ANDed with
}

enum class EGrowthStatus : uint8_t
{
	/// Neither of the other states, ie the growth is probably somewhere in the middle of the Element(s) BBox
	Partial = 0,
	FullyRemoved = Detail::GrowthStatus::Mask::Removed,
	/// The growth animation has reached a point where the Element(s) are fully hidden (ie. construction has
	/// not started, or removal has finished). This is a deferred state, in that it will have to be converted
	/// to the first or last(*) cutting plane equation of the growth simulation ((*) depending on the task
	/// action = install/remove/etc.)
	DeferredFullyRemoved = (FullyRemoved | Detail::GrowthStatus::Mask::Deferred),
	/// The Element(s) are simply full visible ('static' state, as opposed to DeferredFullyRemoved).
	FullyGrown = Detail::GrowthStatus::Mask::Grown,
	/// The Element(s) are simply fully hidden ('static' state, as opposed to DeferredFullyRemoved).
	/// The growth animation has reached a point where the Element(s) are fully visible (ie. construction has
	/// finished, or removal has not started). This is a deferred state, in that it will have to be converted
	/// to the first or last(*) cutting plane equation of the growth simulation ((*) depending on the task
	/// action = install/remove/etc.)
	DeferredFullyGrown = (FullyGrown | Detail::GrowthStatus::Mask::Deferred),
};

struct FDeferredPlaneEquation
{
	/// Orientation of the cutting plane, first stored in Unreal world coordinates AS IF iModel were
	/// untransformed (it's much easier to compute Position in FinalizeCuttingPlaneEquation using an AABB),
	/// then when "finalized", the plane orientation is also transformed by the iModel's transform/offset.
	mutable FVector3f PlaneOrientation;
	PTransform const* TransformKeyframe = nullptr;
	/// The necessarily deferred (until BBoxes are known for the Elements) translation (W) component of the
	/// plane equation, ie. actually set only when !IsDeferred() (ie depending on GrowthStatus)
	mutable float PlaneW;
	/// When set to one of the 'Deferred' states, it means PlaneW is not yet known. When it becomes known,
	/// GrowthStatus switches to the corresponding non-deferred state.
	mutable EGrowthStatus GrowthStatus;

	bool IsDeferred() const
	{
		switch (GrowthStatus)
		{
		case EGrowthStatus::DeferredFullyRemoved:
		case EGrowthStatus::DeferredFullyGrown:
			return true;
		default:
		case EGrowthStatus::FullyRemoved:
		case EGrowthStatus::FullyGrown:
		case EGrowthStatus::Partial:
			return false;
		}
	}
};

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PClippingPlane,
	(FDeferredPlaneEquation, DefrdPlaneEq)
)
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PClippingPlane const&);
inline bool NoEffect(PClippingPlane const& Prop) {
	return EGrowthStatus::DeferredFullyGrown == Prop.DefrdPlaneEq.GrowthStatus
		|| EGrowthStatus::FullyGrown == Prop.DefrdPlaneEq.GrowthStatus;
}

/// Defines class ElementTimeline, which is an ObjectTimeline that associates the SYNCHRO schedule animated
/// properties (visibility, color, transform and cutting plane) to an element and is used to query the state
/// at a given time (see GetStateAtTime)
ITWIN_TIMELINE_DEFINE_OBJECT_PROPERTIES(Element,
	(PVisibility, Visibility)
	(PColor, Color)
	(PTransform, Transform)
	(PClippingPlane, ClippingPlane)
)

/// ElementTimeline stores the individual property timelines for a given Synchro4D Task's
/// animatedElementId. This class adds a mapping to the scene entities belonging to the
/// Element, and their initial transformation matrix, in case it can be animated.
class ElementTimelineEx : public ElementTimeline
{
public:
	using Super = ElementTimeline;

private:
	FIModelElementsKey IModelElementsKey;
	FElementsGroup IModelElements;
	/// Cache of offsets between each Element's BBox center and the center of the whole group's BBox
	/// (only in the case of a group of Elements - see FIModelElementsKey)
	std::unordered_map<ITwinElementID, FVector> IModelElementOffsets;
	FBox IModelElementsBoundingBox;
	bool bIModelElementsBBoxNeedsUpdate = true;
	bool bModified = true;

public:
	ElementTimelineEx(FIModelElementsKey const InIModelElementsKey,
					  FElementsGroup const& InIModelElements)
		: IModelElementsKey(InIModelElementsKey), IModelElements(InIModelElements) {}

	void* ExtraData = nullptr; ///< Pointer to opaque structure, currently for optimization data

	void SetModified() { bModified = true; }
	bool IsModified() const { return bModified; }
	bool TestModifiedAndResetFlag() { bool const tmp = bModified; bModified = false; return tmp; }

	[[nodiscard]] FIModelElementsKey const& GetIModelElementsKey() const { return IModelElementsKey; }
	[[nodiscard]] FElementsGroup& IModelElementsRef() { return IModelElements; }
	[[nodiscard]] FElementsGroup const& GetIModelElements() const { return IModelElements; }
	void OnIModelElementsAdded() { bIModelElementsBBoxNeedsUpdate = true; }
	[[nodiscard]] FVector const& GetIModelElementOffsetInGroup(ITwinElementID const ElementID,
		std::function<FBox(FElementsGroup const&)> const& GroupBBoxGetter,
		std::function<FBox const&(ITwinElementID const)> const& SingleBBoxGetter);
	[[nodiscard]] FBox const& GetIModelElementsBBox(
		std::function<FBox(FElementsGroup const&)> ElementsBBoxGetter);
	/// SLOW!
	[[nodiscard]] bool AppliesToElement(ITwinElementID const& ElementID) const;
	/// Returns the total number of keyframes in the timeline
	[[nodiscard]] size_t NumKeyframes() const;

	void SetColorAt(double const Time, std::optional<FVector> Color, EInterpolation const Interp);
	/// Note: the fourth coordinate of the plane equation is not passed, because we can't generally expect to
	/// have the Element's bounding box when creating the timeline's keyframes.
	/// \param InTransformKeyframe Optional transformation of the Element(s) to account for when finalizing 
	///		the plane equation based on their bounding box.
	void SetCuttingPlaneAt(double const Time, std::optional<FVector> PlaneOrientation,
		EGrowthStatus const GrowthStatus, EInterpolation const Interp,
		PTransform const* const InTransformKeyframe = nullptr);
	/// \return Whether the timeline has any CuttingPlane keyframe where fullyHidden_ is true
	[[nodiscard]] bool HasFullyHidingCuttingPlaneKeyframes() const;
	/// \return Whether the timeline has any Visibility keyframe where the transparency is neither 0 nor 1
	[[nodiscard]] bool HasPartialVisibility() const;
	void SetVisibilityAt(double const Time, std::optional<float> Alpha, EInterpolation const Interp);
	/// Sets a transformation at a given time, expressed in the UE world reference system
	PTransform const& SetTransformationAt(double const Time, FVector const& Position, FQuat const& Rotation,
		FDeferredAnchor const& DefrdAnchor, EInterpolation const Interp);
	void SetTransformationDisabledAt(double const Time, EInterpolation const Interp);

	void ToJson(TSharedRef<FJsonObject>& JsonObj) const override;
	template<typename JsonPrintPolicy> [[nodiscard]] FString ToJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	[[nodiscard]] FString ToCondensedJsonString() const;
};

class MainTimeline : public MainTimelineBase<ElementTimelineEx>
{
	using Super = MainTimelineBase<ElementTimelineEx>;

	/// Maps each animated Element or group of Elements to a single timeline that applies only to it
	std::unordered_map<FIModelElementsKey, int/*timeline index*/> ElementsKeyToTimeline;
	/// See HideNonAnimatedDuplicates in ITwinSynchro4DSchedulesTimelineBuilder.cpp
	FElementsGroup NonAnimatedDuplicates;
	bool bHasNewOrModifiedTimeline_ = false;

public:
	void OnElementsTimelineModified(ElementTimelineEx& ModifiedTimeline);
	bool TestNewOrModifiedAndResetFlag() {
		bool tmp = bHasNewOrModifiedTimeline_; bHasNewOrModifiedTimeline_ = false; return tmp;
	}
	FElementsGroup const& GetNonAnimatedDuplicates() const { return NonAnimatedDuplicates; }
	void AddNonAnimatedDuplicate(ITwinElementID const Elem);
	void RemoveNonAnimatedDuplicate(ITwinElementID const Elem);
	// Removed: test FITwinElement::AnimationKeys instead
	//[[nodiscard]] bool HasTimelineForElement(ITwinElementID const& ElementID) const;
	/// Get or create and return a timeline for the Element or group of Elements.
	[[nodiscard]] ElementTimelineEx& ElementTimelineFor(FIModelElementsKey const IModelElementsKey,
														FElementsGroup const& Elements);
	/// Get an existing timeline for the Element or group of Elements.
	/// \return An existing timeline, or nullptr if none was found.
	[[nodiscard]] ElementTimelineEx* GetElementTimelineFor(FIModelElementsKey const& IModelElementsKey,
														   int* Index = nullptr) const;

	/// Dumps the timelines as an array of individual FElementsGroup timelines: since this is used for
	/// unit testing, the array is ordered with respect to FElementsGroup's Elements, not to any
	/// possibly non-deterministic internal index (timeline index, Elements group index...), which can
	/// make this function much slower than would be necessary for other use cases.
	template<typename JsonPrintPolicy> [[nodiscard]] FString ToJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	[[nodiscard]] FString ToCondensedJsonString() const;

	void FixColor();

	// In case we need the elementTimelineId_ inside ElementTimelineEx, we'd have to override this:
	//std::shared_ptr<ElementTimelineEx> Add(const typename Super::ObjectTimelinePtr& object) override;
	// with the code from vue.git/viewer/Code/RealTimeBuilder/IModel/RenderSchedule.inl
};

} // ns ITwin::Timeline

#include "TimelineFwd.h" // fwd and aliases
