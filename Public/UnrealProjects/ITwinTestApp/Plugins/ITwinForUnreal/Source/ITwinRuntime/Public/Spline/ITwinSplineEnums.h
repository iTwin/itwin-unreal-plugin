/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineEnums.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Engine/Blueprint.h>


//! WARNING!
//! The enums below must be synchronized with the equivalent enums defined in AdvViz::SDK
//!		-> see Public/SDK/Core/Visualization/Spline.h
//! They are duplicated here in order to be accessible in blueprints...

UENUM(BlueprintType)
enum class EITwinTangentMode : uint8
{
	Linear = 0,
	Smooth = 1,
	Custom = 2
};

UENUM(BlueprintType)
enum class EITwinSplineUsage : uint8
{
	Undefined = 0,
	MapCutout = 1 UMETA(DisplayName = "Cutout Polygon"),
	AnimPathTraffic = 2,
	PopulationZone = 3,
	PopulationPath = 4,
	AnimPathObject = 5,
	EdgeDisplayHelper = 6,
	AnimPathCrowd = 7,
	AnimPath = 8, // common type for all path animations (used only for selection by spline tool, should never be assigned to a spline itself)
	SplinePopulation = 9 // common type for all spline populations (used only for selection by spline tool, should never be assigned to a spline itself)
};
