/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterial.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

#define ITWIN_MAT_LIBRARY_TAG "<MatLibrary>"

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
		R = 0,
		G,
		B,
		A
	};

	enum class ETextureSource : uint8_t
	{
		LocalDisk = 0,
		ITwin,
		Decoration,
		Library,
	};

	// Special tag which can be used to nullify a texture (typically if the original model exported by the
	// Mesh Export Service provides with an albedo map but the user wants to discard it).
	static constexpr auto NONE_TEXTURE = "0";

	struct ITwinChannelMap
	{
		/// Placeholder for real image.
		std::string texture;
		/// Identifies the source repository.
		ETextureSource eSource = ETextureSource::LocalDisk;

		ETilingMode tilingH = ETilingMode::Once;
		ETilingMode tilingV = ETilingMode::Once;

		// for intensity Maps we can store several intensities in the same image on different channels.
		// Note that some configurations are imposed if we use the Cesium GLTF shaders (see documentation for
		// #MaterialPBRMetallicRoughness)
		std::optional<ETextureChannel> channel = std::nullopt;

		bool operator == (ITwinChannelMap const& rhs) const;
		bool IsEmpty() const { return texture.empty(); }
		bool IsDiscarded() const { return texture == NONE_TEXTURE; }
		bool HasTexture() const { return !IsEmpty() && !IsDiscarded(); }
	};

	struct ITwinChannel
	{
		ITwinColor color = { 0., 0., 0., 1. };
		ITwinChannelMap colorMap;

		double intensity = 0.;
		ITwinChannelMap intensityMap; // always grayscale

		bool operator == (ITwinChannel const& rhs) const;
		bool HasTextureMap() const {
			return colorMap.HasTexture() || intensityMap.HasTexture();
		}
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
		Opacity = Alpha,
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

	std::string GetChannelName(EChannelType chan);

	/**
	 * Per material UV transformation (analog to ExtensionKhrTextureTransform, but applied to all textures
	 * in the material...)
	 */
	struct ITwinUVTransform
	{
		/**
		 * @brief The offset of the UV coordinate origin as a factor of the texture
		 * dimensions.
		 */
		std::array<double, 2> offset = { 0., 0. };

		/**
		 * @brief The scale factor applied to the components of the UV coordinates.
		 */
		std::array<double, 2> scale = { 1., 1. };

		/**
		 * @brief Rotate the UVs by this many radians counter-clockwise around the
		 * origin.
		 */
		double rotation = 0.;


		static ITwinUVTransform NullTransform();

		/// Return whether an actual transformation is defined.
		bool HasTransform() const;

		bool operator == (ITwinUVTransform const& rhs) const;
		bool operator != (ITwinUVTransform const& rhs) const {
			return !(*this == rhs);
		}
	};


	struct ITwinMaterial
	{
		EMaterialKind kind = EMaterialKind::PBR;
		std::array< std::optional<ITwinChannel>, (size_t)EChannelType::ENUM_END > channels;
		ITwinUVTransform uvTransform;
		std::string displayName;


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

		/// Simplified texture access (as for a given channel, we support either an intensity map or a color
		/// map, and never both...
		std::optional<ITwinChannelMap> GetChannelMapOpt(EChannelType channel) const;
		void SetChannelMap(EChannelType channel, ITwinChannelMap const& texMap);
		ITwinChannelMap& GetMutableChannelMap(EChannelType channel);

		bool HasTextureMap() const;
		bool HasUVTransform() const { return uvTransform.HasTransform(); }
	};

}
