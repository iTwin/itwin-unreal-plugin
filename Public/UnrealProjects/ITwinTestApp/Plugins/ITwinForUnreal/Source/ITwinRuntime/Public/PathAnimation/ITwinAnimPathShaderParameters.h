/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathShaderParameters.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <UObject/ObjectMacros.h>
#include <Engine/Blueprint.h>
#include <Misc/EnumRange.h>

//! Enumerates the different CustomPrimitiveData parameters used in M_PathAnimation.uasset
//! Obviously, if we add/remove those parameters, or change their index in the material graph,
//! we must report the modifications here...
UENUM(BlueprintType)
enum class EITwinAnimPathShaderScalarParam : uint8
{
	Selection = 0, /* not used for now, but keep index 0 for compatibility with other spline materials! */
	NumLanes = 1,
	LaneWidth = 2,
	CentralSpacing = 3,
	TwoWay = 4,
	LeftHandDrive = 5,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinAnimPathShaderScalarParam, EITwinAnimPathShaderScalarParam::Count);
