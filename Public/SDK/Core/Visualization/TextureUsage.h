/*--------------------------------------------------------------------------------------+
|
|     $Source: TextureUsage.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include "TextureKey.h"

#ifndef SDK_CPPMODULES
#	include <map>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace AdvViz::SDK
{
	enum class EChannelType : uint8_t;

	/// Encodes the different channels using a given texture.
	struct TextureUsage
	{
		uint32_t flags_ = 0;

		void AddChannel(EChannelType chan)
		{
			flags_ |= (1 << (uint8_t)chan);
		}
		bool HasChannel(EChannelType chan) const
		{
			return (flags_ & (1 << (uint8_t)chan)) != 0;
		}
	};

	using TextureUsageMap = std::map<TextureKey, TextureUsage>;

	inline TextureUsage FindTextureUsage(TextureUsageMap const& usageMap, TextureKey const& textureKey)
	{
		auto usageIt = usageMap.find(textureKey);
		if (usageIt != usageMap.end())
			return usageIt->second;
		else
			return {};
	}
}
