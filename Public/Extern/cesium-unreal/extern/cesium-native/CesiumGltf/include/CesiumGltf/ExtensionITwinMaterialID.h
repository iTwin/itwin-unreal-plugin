
#pragma once

#include "CesiumGltf/Library.h"

#include <CesiumUtility/ExtensibleObject.h>

#include <cstdint>

namespace CesiumGltf {
	/**
	 * @brief glTF extension to specify iTwin Material Identifier.
	 */
	struct CESIUMGLTF_API ExtensionITwinMaterialID final : public CesiumUtility::ExtensibleObject
	{
		static inline constexpr const char* TypeName = "ExtensionITwinMaterialID";
		static inline constexpr const char* ExtensionName = "ITWIN_material_identifier";

		/**
		 * @brief The material identifier in the original model file.
		 */
		uint64_t materialId = 0;
	};
} // namespace CesiumGltf
