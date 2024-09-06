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
#include <Timeline/AnchorPoint.h>
#include <Timeline/Definition.h>

#include <Boost/BoostHash.h>
#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <BeHeaders/StrongTypes/TaggedValue.h>
#include <Compil/AfterNonUnrealIncludes.h>
#include <ITwinElementID.h>
#include "TimelineBase.h"
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <variant>

class AActor;
class FJsonValue;

class FIModelElementsKey
{
public:
	static FIModelElementsKey NOT_ANIMATED;
	std::variant<ITwinElementID, size_t/*GroupInVec*/> Key;
	explicit FIModelElementsKey(ITwinElementID const& ElementID) : Key(ElementID) {}
	explicit FIModelElementsKey(size_t const& GroupIndex) : Key(GroupIndex) {}
};

inline bool operator ==(FIModelElementsKey const& A, FIModelElementsKey const& B)
{
	return A.Key == B.Key;
}

template <>
struct std::hash<FIModelElementsKey>
{
public:
	size_t operator()(FIModelElementsKey const& ElementsKey) const
	{
		return std::hash<std::variant<ITwinElementID, size_t>>()(ElementsKey.Key);
	}
};

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
[[nodiscard]] bool operator ==(const PColor& x, const PColor& y);
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop);
inline bool NoEffect(PColor const& Prop) { return !Prop.bHasColor; }

/// Offset relative to an Elements group's bounding box center, to consider before applying the
/// transformation necessary to follow a 3D path.
/// Deferred because, like growth simulation, it is often defined as the center of a face of the BBox.
/// As opposed to growth simulation, it can also be defined explicitly. Shared among all keyframes of a given
/// animation3dPath assignment.
struct FDeferredAnchorPos
{
	static std::shared_ptr<FDeferredAnchorPos> MakeShared(EAnchorPoint const Anchor);
	static std::shared_ptr<FDeferredAnchorPos> MakeSharedCustom(FVector const& CustomAnchor);

	/// If it was not 'Custom' from the start, it is set to 'Custom' once the actual anchor pos has been
	/// computed.
	mutable EAnchorPoint AnchorPoint = EAnchorPoint::Center;
	mutable FVector Pos;

	bool IsDeferred() const { return AnchorPoint != EAnchorPoint::Custom; }
};

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PTransform,
	(ITwin::Flag::FPresence, bIsTransformed)
	(std::shared_ptr<FDeferredAnchorPos>, DefrdAnchor)
	(FTransform, Transform)
)
[[nodiscard]] bool operator ==(const PTransform& x, const PTransform& y);
[[nodiscard]] TSharedPtr<FJsonValue> ToJsonValue(PTransform const&);
inline bool NoEffect(PTransform const& Prop) { return !Prop.bIsTransformed; }
//[[nodiscard]] FMatrix TransformToMatrix(const PTransform& t);

namespace Detail::GrowthStatus
{
	namespace Bit { enum EBit { Removed, Grown, Deferred }; }
	namespace Mask {
		enum EMask { Removed = 1 << Bit::Removed, Grown = 1 << Bit::Grown, Deferred = 1 << Bit::Deferred };
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

/// Note: a separate structure is necessary in order to set the possibly deferred component (PlaneW) as well
/// as the growth status as mutable
struct FDeferredPlaneEquation
{
	/// Orientation of the cutting plane, to be used when
	FVector3f PlaneOrientation;
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
[[nodiscard]] std::size_t hash_value(const FDeferredPlaneEquation& v) noexcept;

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(PClippingPlane,
	(FDeferredPlaneEquation, DefrdPlaneEq)
)
[[nodiscard]] bool operator ==(const PClippingPlane& x, const PClippingPlane& y);
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
	std::set<ITwinElementID> IModelElements;
	/// Cache of offsets between each Element's BBox center and the center of the whole group's BBox
	/// (only in the case of a group of Elements - see FIModelElementsKey)
	std::unordered_map<ITwinElementID, FVector> IModelElementOffsets;
	FBox IModelElementsBoundingBox;
	bool bIModelElementsBBoxNeedsUpdate = true;
	bool bModified = true;

public:
	ElementTimelineEx(FIModelElementsKey const InIModelElementsKey,
					  std::set<ITwinElementID> const& InIModelElements)
		: IModelElementsKey(InIModelElementsKey), IModelElements(InIModelElements) {}

	void SetModified() { bModified = true; }
	bool IsModified() const { return bModified; }
	bool TestModifiedAndResetFlag() { bool const tmp = bModified; bModified = false; return tmp; }

	[[nodiscard]] FIModelElementsKey const& GetIModelElementsKey() const { return IModelElementsKey; }
	[[nodiscard]] std::set<ITwinElementID>& IModelElementsRef() { return IModelElements; }
	[[nodiscard]] std::set<ITwinElementID> const& GetIModelElements() const { return IModelElements; }
	void OnIModelElementsAdded() { bIModelElementsBBoxNeedsUpdate = true; }
	[[nodiscard]] FVector const& GetIModelElementOffsetInGroup(ITwinElementID const ElementID,
		std::function<FBox(std::set<ITwinElementID> const&)> const& GroupBBoxGetter,
		std::function<FBox const&(ITwinElementID const)> const& SingleBBoxGetter);
	[[nodiscard]] FBox const& GetIModelElementsBBox(
		std::function<FBox(std::set<ITwinElementID> const&)> ElementsBBoxGetter);
	/// SLOW!
	[[nodiscard]] bool AppliesToElement(ITwinElementID const& ElementID) const;
	/// Returns the total number of keyframes in the timeline
	[[nodiscard]] size_t NumKeyframes() const;

	void SetColorAt(double const Time, std::optional<FVector> Color, EInterpolation const Interp);
	/// Note: the fourth coordinate of the plane equation is not passed, because we can't generally expect to
	/// have the Element's bounding box when creating the timeline's keyframes.
	void SetCuttingPlaneAt(double const Time, std::optional<FVector> PlaneOrientation,
						   EGrowthStatus const GrowthStatus, EInterpolation const Interp);
	/// \return Whether the timeline has any CuttingPlane keyframe where fullyHidden_ is true
	[[nodiscard]] bool HasFullyHidingCuttingPlaneKeyframes() const;
	/// \return Whether the timeline has any Visibility keyframe where the transparency is neither 0 nor 1
	[[nodiscard]] bool HasPartialVisibility() const;
	void SetVisibilityAt(double const Time, std::optional<float> Alpha, EInterpolation const Interp);
	/// Sets a transformation at a given time, expressed in the UE world reference system
	void SetTransformationAt(double const Time, FTransform const& Transform,
		std::shared_ptr<FDeferredAnchorPos> SharedAnchor, EInterpolation const Interp);
	void SetTransformationDisabledAt(double const Time, EInterpolation const Interp);

	void ToJson(TSharedRef<FJsonObject>& JsonObj) const override;
	template<typename JsonPrintPolicy> [[nodiscard]] FString ToJsonString() const;
	[[nodiscard]] FString ToPrettyJsonString() const;
	[[nodiscard]] FString ToCondensedJsonString() const;
};
std::size_t hash_value(const ElementTimelineEx& Timeline) noexcept;
bool operator ==(const ElementTimelineEx& A, const ElementTimelineEx& B);

class MainTimeline : public MainTimelineBase<ElementTimelineEx>
{
	using Super = MainTimelineBase<ElementTimelineEx>;

	/// Maps each animated Element or group of Elements to a single timeline that applies only to it
	std::unordered_map<FIModelElementsKey, int/*timeline index*/> ElementsKeyToTimeline;
	/// See HideNonAnimatedDuplicates in ITwinSynchro4DSchedulesTimelineBuilder.cpp
	std::set<ITwinElementID> NonAnimatedDuplicates;
	bool bHasNewOrModifiedTimeline_ = false;

public:
	void OnElementsTimelineModified(ElementTimelineEx& ModifiedTimeline);
	bool TestNewOrModifiedAndResetFlag() {
		bool tmp = bHasNewOrModifiedTimeline_; bHasNewOrModifiedTimeline_ = false; return tmp;
	}
	std::set<ITwinElementID> const& GetNonAnimatedDuplicates() const { return NonAnimatedDuplicates; }
	void AddNonAnimatedDuplicate(ITwinElementID const Elem);
	void RemoveNonAnimatedDuplicate(ITwinElementID const Elem);
	// Removed: test FITwinElement::AnimationKeys instead
	//[[nodiscard]] bool HasTimelineForElement(ITwinElementID const& ElementID) const;
	/// Get or create and return a timeline for the Element or group of Elements.
	[[nodiscard]] ElementTimelineEx& ElementTimelineFor(FIModelElementsKey const IModelElementsKey,
														std::set<ITwinElementID> const& Elements);
	/// Get an existing timeline for the Element or group of Elements.
	/// \return An existing timeline, or nullptr if none was found.
	[[nodiscard]] ElementTimelineEx* GetElementTimelineFor(FIModelElementsKey const IModelElementsKey) const;

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
