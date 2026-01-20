/*--------------------------------------------------------------------------------------+
|
|     $Source: ExtensionITwinMaterial.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CesiumUtility/ExtensibleObject.h>

namespace BeUtils
{
	/**
	 * @brief glTF extension for materials, introduced to support a specular factor in iTwin Materials.
	 */
	struct ExtensionITwinMaterial final : public CesiumUtility::ExtensibleObject
	{
		static inline constexpr const char* TypeName = "ExtensionITwinMaterial";
		static inline constexpr const char* ExtensionName = "EXT_ITWIN_material";

		/**
		 * @brief The material specular value.
		 */
		double specularFactor = 0.0;

		/**
		 * @brief The material base color texture intensity value.
		 */
		double baseColorTextureFactor = 1.0;
	};
} // namespace BeUtils

