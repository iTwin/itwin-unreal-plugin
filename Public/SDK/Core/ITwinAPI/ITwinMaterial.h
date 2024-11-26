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

	enum class ETilingMode : uint8_t
	{
		Repeat,
		Mirror,
		Once,
		ClampToEdge
	};

	enum class ETextureChannel : uint8_t
	{
		R,
		G,
		B,
		A
	};

	struct ITwinChannelMap
	{
		// placeholder for real image
		std::string texture;

		ETilingMode tilingH = ETilingMode::Once;
		ETilingMode tilingV = ETilingMode::Once;

		// for intensity Maps we can store several intensities in the same image on different channels.
		// Note that some configurations are imposed if we use the Cesium GLTF shaders (see documentation for
		// #MaterialPBRMetallicRoughness)
		std::optional<ETextureChannel> channel = std::nullopt;

		bool operator == (ITwinChannelMap const& rhs) const;
		bool IsEmpty() const { return texture.empty(); }
	};

	struct ITwinChannel
	{
		ITwinColor color = { 0., 0., 0., 1. };
		ITwinChannelMap colorMap;

		double intensity = 0.;
		ITwinChannelMap intensityMap; // always grayscale

		bool operator == (ITwinChannel const& rhs) const;
	};

	enum class EMaterialKind : uint8_t
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
		EMaterialKind kind = EMaterialKind::PBR;
		
		std::array< std::optional<ITwinChannel>, (size_t)EChannelType::ENUM_END > channels;


		/// Return true if this material holds a definition for the given channel.
		bool DefinesChannel(EChannelType channel) const;

		bool operator == (ITwinMaterial const& rhs) const;
		bool operator != (ITwinMaterial const& rhs) const {
			return !(*this == rhs);
		}

		std::optional<double> GetChannelIntensityOpt(EChannelType channel) const;
		std::optional<ITwinChannelMap> GetChannelIntensityMapOpt(EChannelType channel) const;

		/// Defines the intensity of the given channel.
		void SetChannelIntensity(EChannelType channel, double intensity);
		void SetChannelIntensityMap(EChannelType channel, ITwinChannelMap const& intensityMap);

		std::optional<ITwinColor> GetChannelColorOpt(EChannelType channel) const;
		std::optional<ITwinChannelMap> GetChannelColorMapOpt(EChannelType channel) const;

		void SetChannelColor(EChannelType channel, ITwinColor const& color);
		void SetChannelColorMap(EChannelType channel, ITwinChannelMap const& colorMap);
	};

}
