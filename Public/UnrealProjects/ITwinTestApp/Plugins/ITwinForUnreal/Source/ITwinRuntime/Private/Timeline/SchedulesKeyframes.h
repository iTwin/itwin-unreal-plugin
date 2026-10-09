/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesKeyframes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "AnchorPoint.h"
#include "Timeline.h"
#include "TimeInSeconds.h"

#include <Math/Quat.h>
#include <Math/Transform.h>
#include <Math/Vector.h>

#include <variant>
#include <vector>

class FTransformKey;
class FAnimation3DPath;
class FAppearanceProfile;
class FActiveAppearance;
class FPathTransformAssignment;
class FSimpleAppearance;
class FStaticTransformAssignment;
class FPathTransformAssignment;
struct FITwinCoordConversions;

namespace ITwin::Timeline {

struct FTaskDependenciesData
{
	bool bHasOnlyNeutralTasks = false;
	std::optional<bool> ProfileForcedVisibilityBefore;
	std::optional<bool> ProfileForcedVisibilityAfter;
	/// Appearance forced before a Maintain or Temp task when preceded by an Install
	FSimpleAppearance const* ProfileForcedAppearanceBefore = nullptr;
	/// Appearance forced after a Maintain or Temp task when preceded by an Install
	FSimpleAppearance const* ProfileForcedAppearanceAfter = nullptr;
	/// See ProfileForcedTransfoAssignOutside below
	bool bForceDisablingTransformationOutside = false;
	/// Transformation to fallback to outside of a task can be "none" (nullptr), "something" (either one or both
	/// ProfileForcedStaticTransfoAssignOutside and ProfileForcedPathTransfoAssignOutside are non-null),
	/// or "reset to original" (both nullptr but bForceDisablingTransformationOutside == true)
	FStaticTransformAssignment const* ProfileForcedStaticTransfoAssignOutside = nullptr;
	FPathTransformAssignment const* ProfileForcedPathTransfoAssignOutside = nullptr;
	/// Only set if ProfileForcedTransfoAssignOutside is not null and assignment is to a 3D path
	FAnimation3DPath const* ProfileForced3DPathOutside = nullptr;
	/// Only set if ProfileForcedTransfoAssignOutside is not null and assignment is to a 3D path, tells whether to use
	/// the start or end keyframe position of the path
	bool bProfileForced3DPathOutsideIsAtPathStart = false;
};

struct F3DPathKFData
{
	FVector ConvertedPosition;
	FQuat NormalizedRotation;
	FDeferredAnchor BaseAnchor;
	bool bFirstOrLastKeyframe = false;
};

void AddColorToTimeline(FITwinElementTimeline& ElementTimeline,
	FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time, FTaskDependenciesData const& TaskDeps);
FVector GetCuttingPlaneOrientation(FActiveAppearance const& Appearance);

void AddCuttingPlaneToTimeline(FITwinElementTimeline& ElementTimeline, FAppearanceProfile const& Profile,
	FTimeRangeInSeconds const& Time, FITwinCoordConversions const& CoordConv,
	PTransform const* const TransformKeyframe = nullptr);

void AddVisibilityToTimeline(FITwinElementTimeline& ElementTimeline,
	FAppearanceProfile const& Profile, FTimeRangeInSeconds const& Time, FTaskDependenciesData const& TaskDeps);

PTransform const& AddStaticTransformToTimeline(FITwinElementTimeline& ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, FTransform const& Transform, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps);

/// \param StaticTransformUnused Ignored as #2070419 concluded that it was in fact not possible to have both
///		actually applied at the same time: in that case, the static transform is ignored
bool GetLast3DPathTransformKeyframeToApply(FTimeRangeInSeconds const& TaskTimes,
	FTransform const* StaticTransformUnused, FPathTransformAssignment const& PathAssignment,
	std::vector<FTransformKey> const& Keyframes, FITwinCoordConversions const& CoordConv,
	F3DPathKFData& KeyframeToApply);

/// \param StaticTransformUnused Ignored as #2070419 concluded that it was in fact not possible to have both
///		actually applied at the same time: in that case, the static transform is ignored
/// \return Whether pOnlyGetSingleKeyframe was non-null AND data for a keyframe was indeed extracted into it
bool Add3DPathTransformToTimeline(
	FITwinElementTimeline* ElementTimeline, FTimeRangeInSeconds const& TaskTimes,
	FTransform const* StaticTransformUnused, FPathTransformAssignment const& PathAssignment,
	std::vector<FTransformKey> const& Keyframes, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps, F3DPathKFData* pOnlyGetSingleKeyframe = nullptr);

void HandleFallbackTransfoOutsideTaskIfNeeded(FITwinElementTimeline& ElementTimeline,
	FTimeRangeInSeconds const& TaskTimes, FITwinCoordConversions const& CoordConv,
	FTaskDependenciesData const& TaskDeps);

} // ns Synchro4DKeyframes
