
#pragma once

#include "CesiumGltf/Library.h"

#include <CesiumUtility/ExtensibleObject.h>

#include <cstdint>

namespace CesiumGltf {
	/**
	 * @brief glTF extension for materials, introduced to support a specular factor in iTwin Materials.
	 */
	struct CESIUMGLTF_API ExtensionITwinMaterial final : public CesiumUtility::ExtensibleObject
	{
		static inline constexpr const char* TypeName = "ExtensionITwinMaterial";
		static inline constexpr const char* ExtensionName = "EXT_ITWIN_material";

		/**
		 * @brief The material specular value.
		 */
     double specularFactor = 0.0;
	};
} // namespace CesiumGltf
