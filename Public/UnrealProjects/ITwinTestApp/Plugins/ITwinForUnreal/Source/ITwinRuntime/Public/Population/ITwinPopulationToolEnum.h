/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationToolEnum.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <UObject/ObjectMacros.h>
#include <Engine/Blueprint.h>

UENUM(BlueprintType)
enum class EPopulationToolMode : uint8 {
	Select = 0,
	Instantiate = 1,
	InstantiateN = 2,
	RemoveInstances = 3,
};

UENUM(BlueprintType)
enum class ETransformationMode : uint8 {
	Move = 0,
	Rotate = 1,
	Scale = 2
};