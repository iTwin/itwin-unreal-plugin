/*--------------------------------------------------------------------------------------+
|
|     $Source: ExtensionITwinMaterialID.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CesiumUtility/ExtensibleObject.h>

#include <cstdint>

namespace BeUtils
{
	/**
	 * @brief glTF extension to specify iTwin Material Identifier.
	 */
	struct ExtensionITwinMaterialID final : public CesiumUtility::ExtensibleObject
	{
		static inline constexpr const char* TypeName = "ExtensionITwinMaterialID";
		static inline constexpr const char* ExtensionName = "ITWIN_material_identifier";

		/**
		 * @brief The material identifier in the original model file.
		 */
		uint64_t materialId = 0;
	};
} // namespace BeUtils
