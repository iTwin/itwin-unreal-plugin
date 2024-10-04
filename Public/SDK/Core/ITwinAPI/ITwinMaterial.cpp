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
}
