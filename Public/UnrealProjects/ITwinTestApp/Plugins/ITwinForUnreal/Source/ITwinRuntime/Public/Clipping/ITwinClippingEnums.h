/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEnums.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <UObject/ObjectMacros.h>
#include <Engine/Blueprint.h>
#include <Misc/EnumRange.h>

#include "ITwinClippingEnums.generated.h"

/// Supported primitive types for clipping (aka cutout).
UENUM(BlueprintType)
enum class EITwinClippingPrimitiveType : uint8
{
	Box,
	Plane,
	Polygon, /* stands for Cesium Cartographic Polygon (2.5D) */

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinClippingPrimitiveType, EITwinClippingPrimitiveType::Count);


/// Clipping effects usually work both at the tileset level (tile exclusion) and the
/// shader level (to clip more precisely inside a given tile).
UENUM(BlueprintType)
enum class EITwinClippingEffectLevel : uint8
{
	Shader,
	Tileset,
};
