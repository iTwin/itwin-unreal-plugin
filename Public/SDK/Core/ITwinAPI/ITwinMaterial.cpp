/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterial.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinMaterial.h"

#include <Core/Tools/Assert.h>

namespace AdvViz::SDK
{
	std::string GetChannelName(EChannelType chan)
	{
		switch (chan)
		{
		case EChannelType::Color: return "color";
		case EChannelType::Normal: return "normal";
		case EChannelType::Metallic: return "metallic";
		case EChannelType::Roughness: return "roughness";
		case EChannelType::AmbientOcclusion: return "AO";

		case EChannelType::Alpha: return "opacity";
		case EChannelType::Transparency: return "transparency";

		case EChannelType::Bump: return "bump";
		case EChannelType::Displacement: return "displacement";

		case EChannelType::Backlight: return "backlight";
		case EChannelType::Luminous: return "luminous";
		case EChannelType::Reflection: return "reflection";
		case EChannelType::Refraction: return "refraction";
		case EChannelType::Specular: return "specular";

		case EChannelType::ENUM_END:
		default:
			BE_ISSUE("unhandled channel", (uint8_t)chan);
			return "";
		}
	}

	//-----------------------------------------------------------------------------------
	// ITwinChannelMap
	//-----------------------------------------------------------------------------------

	bool ITwinChannelMap::operator == (ITwinChannelMap const& rhs) const
	{
		return texture == rhs.texture
			&& eSource == rhs.eSource
			&& tilingH == rhs.tilingH
			&& tilingV == rhs.tilingH
			&& channel == rhs.channel;
	}

	// Due to conversion to 8-bit for each color component, the comparison should use this tolerance.
	static constexpr double COLOR_COMPONENT_INCR = 1. / 255.;

	inline bool IdenticalColor(ITwinColor const& col1, ITwinColor const& col2)
	{
		for (size_t i(0); i < col1.size(); ++i)
		{
			if (std::fabs(col1[i] - col2[i]) > COLOR_COMPONENT_INCR)
				return false;
		}
		return true;
	}

	bool ITwinChannel::operator == (ITwinChannel const& rhs) const
	{
		return IdenticalColor(color, rhs.color)
			&& colorMap == rhs.colorMap
			&& intensity == rhs.intensity
			&& intensityMap == rhs.intensityMap;
	}


	//-----------------------------------------------------------------------------------
	// ITwinUVTransform
	//-----------------------------------------------------------------------------------

	/*static*/ ITwinUVTransform ITwinUVTransform::NullTransform()
	{
		// The default values set a null transformation...
		return {};
	}

	bool ITwinUVTransform::HasTransform() const
	{
		if (std::fabs(offset[0]) > 1e-4) return true;
		if (std::fabs(offset[1]) > 1e-4) return true;
		if (std::fabs(scale[0] - 1.) > 1e-4) return true;
		if (std::fabs(scale[1] - 1.) > 1e-4) return true;
		if (std::fabs(rotation) > 1e-4) return true;
		return false;
	}

	bool ITwinUVTransform::operator == (ITwinUVTransform const& rhs) const
	{
		return offset == rhs.offset
			&& scale == rhs.scale
			&& rotation == rhs.rotation;
	}

	//-----------------------------------------------------------------------------------
	// ITwinMaterial
	//-----------------------------------------------------------------------------------

	bool ITwinMaterial::DefinesChannel(EChannelType channel) const
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size())
		{
			return channels[chanIndex].has_value();
		}
		else
		{
			return false;
		}
	}

	bool ITwinMaterial::operator == (ITwinMaterial const& rhs) const
	{
		if (kind != rhs.kind)
			return false;
		if (channels != rhs.channels)
			return false;
		if (uvTransform != rhs.uvTransform)
			return false;
		if (displayName != rhs.displayName)
			return false;
		return true;
	}

	std::optional<double> ITwinMaterial::GetChannelIntensityOpt(EChannelType channel) const
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size() && channels[chanIndex].has_value())
		{
			return channels[chanIndex]->intensity;
		}
		else
		{
			return std::nullopt;
		}
	}

	std::optional<ITwinChannelMap> ITwinMaterial::GetChannelIntensityMapOpt(EChannelType channel) const
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size()
			&& channels[chanIndex].has_value()
			&& !channels[chanIndex]->intensityMap.IsEmpty())
		{
			return channels[chanIndex]->intensityMap;
		}
		else
		{
			return std::nullopt;
		}
	}

	void ITwinMaterial::SetChannelIntensity(EChannelType channel, double intensity)
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size())
		{
			std::optional<ITwinChannel>& chanOpt = channels[chanIndex];
			if (!chanOpt)
			{
				chanOpt.emplace();
			}
			chanOpt->intensity = intensity;
		}
	}

	void ITwinMaterial::SetChannelIntensityMap(EChannelType channel, ITwinChannelMap const& intensityMap)
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size())
		{
			std::optional<ITwinChannel>& chanOpt = channels[chanIndex];
			if (!chanOpt)
			{
				chanOpt.emplace();
			}
			chanOpt->intensityMap = intensityMap;
		}
	}

	std::optional<ITwinColor> ITwinMaterial::GetChannelColorOpt(EChannelType channel) const
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size() && channels[chanIndex].has_value())
		{
			return channels[chanIndex]->color;
		}
		else
		{
			return std::nullopt;
		}
	}

	std::optional<ITwinChannelMap> ITwinMaterial::GetChannelColorMapOpt(EChannelType channel) const
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size()
			&& channels[chanIndex].has_value()
			&& !channels[chanIndex]->colorMap.IsEmpty())
		{
			return channels[chanIndex]->colorMap;
		}
		else
		{
			return std::nullopt;
		}
	}

	void ITwinMaterial::SetChannelColor(EChannelType channel, ITwinColor const& color)
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size())
		{
			std::optional<ITwinChannel>& chanOpt = channels[chanIndex];
			if (!chanOpt)
			{
				chanOpt.emplace();
			}
			chanOpt->color = color;
		}
	}

	void ITwinMaterial::SetChannelColorMap(EChannelType channel, ITwinChannelMap const& colorMap)
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		if (chanIndex < channels.size())
		{
			std::optional<ITwinChannel>& chanOpt = channels[chanIndex];
			if (!chanOpt)
			{
				chanOpt.emplace();
			}
			chanOpt->colorMap = colorMap;
		}
	}

	bool ITwinMaterial::HasTextureMap() const
	{
		return std::find_if(channels.begin(), channels.end(),
			[](auto const& chan) { return chan && chan->HasTextureMap(); }) != channels.end();
	}


	std::optional<ITwinChannelMap> ITwinMaterial::GetChannelMapOpt(EChannelType channel) const
	{
		if (channel == EChannelType::Color
			|| channel == EChannelType::Normal)
		{
			return GetChannelColorMapOpt(channel);
		}
		else
		{
			return GetChannelIntensityMapOpt(channel);
		}
	}

	ITwinChannelMap& ITwinMaterial::GetMutableChannelMap(EChannelType channel)
	{
		size_t const chanIndex = static_cast<size_t>(channel);
		std::optional<ITwinChannel>& chanOpt = channels[chanIndex];
		if (!chanOpt)
		{
			chanOpt.emplace();
		}
		if (channel == EChannelType::Color
			|| channel == EChannelType::Normal)
		{
			return chanOpt->colorMap;
		}
		else
		{
			return chanOpt->intensityMap;
		}
	}

	void ITwinMaterial::SetChannelMap(EChannelType channel, ITwinChannelMap const& texMap)
	{
		if (channel == EChannelType::Color
			|| channel == EChannelType::Normal)
		{
			SetChannelColorMap(channel, texMap);
		}
		else
		{
			SetChannelIntensityMap(channel, texMap);
		}
	}

}
