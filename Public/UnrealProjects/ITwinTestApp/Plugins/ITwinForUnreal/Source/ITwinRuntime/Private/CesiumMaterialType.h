/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumMaterialType.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once


/// Enum listing the different master material types handled in Cesium
enum class ECesiumMaterialType : uint8
{
	Opaque,
	Translucent,
	Water,
};
