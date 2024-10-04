/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterial.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <array>
	#include <optional>
	#include <string>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core
{

	///==================================================================================
	/// SIMPLIFIED VERSION
	///==================================================================================

	using ITwinColor = std::array<double, 4>; // R,G,B,A

	enum class ETilingMode
	{
		Repeat,
		Mirror,
		Once,
		ClampToEdge
	};

	struct ITwinChannelMap
	{
		// placeholder for real image
		std::string texture;

		ETilingMode tilingH = ETilingMode::Once;
		ETilingMode tilingV = ETilingMode::Once;

		// for intensity Maps we can store several intensity in the same image on different colors
		std::array<uint8_t, 4> channel = { 1, 1, 1, 1 }; // R,G,B,A
	};

	struct ITwinChannel
	{
		ITwinColor color = { 0., 0., 0., 1. };
		ITwinChannelMap colorMap;

		double intensity = 0.;
		ITwinChannelMap intensityMap; // always grayscale
	};

	enum class EMaterialKind
	{
		PBR,
		Glass,
		// Direct (mapping),
	};

	enum class EChannelType : uint8_t
	{
		Color,
		Normal,
		Metallic,
		Roughness,
		AmbientOcclusion,

		Alpha,
		Transparency,

		Bump,
		Displacement,

		Backlight,
		Luminous,
		Reflection,
		Refraction,
		Specular,

		ENUM_END
	};

	struct ITwinMaterial
	{
		EMaterialKind kind;
		
		std::array< std::optional<ITwinChannel>, (size_t)EChannelType::ENUM_END > channels;

		/// Return true if this material holds a definition for the given channel.
		bool DefinesChannel(EChannelType channel) const;

		std::optional<double> GetChannelIntensityOpt(EChannelType channel) const;

		/// Defines the intensity of the given channel.
		void SetChannelIntensity(EChannelType channel, double intensity);
	};

}
