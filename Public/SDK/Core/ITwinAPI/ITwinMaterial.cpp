/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterial.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinMaterial.h"

namespace SDK::Core
{

	bool ITwinChannelMap::operator == (ITwinChannelMap const& rhs) const
	{
		return texture == rhs.texture
			&& tilingH == rhs.tilingH
			&& tilingV == rhs.tilingH
			&& channel == rhs.channel;
	}

	bool ITwinChannel::operator == (ITwinChannel const& rhs) const
	{
		return color == rhs.color
			&& colorMap == rhs.colorMap
			&& intensity == rhs.intensity
			&& intensityMap == rhs.intensityMap;
	}

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
		if (chanIndex < channels.size() && channels[chanIndex].has_value())
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
		if (chanIndex < channels.size() && channels[chanIndex].has_value())
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
}
