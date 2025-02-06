/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterial.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include "ITwinMaterial.h"

#include <algorithm>

namespace SDK::Core
{
	inline bool HasDefinedChannels(ITwinMaterial const& mat)
	{
		return std::find_if(mat.channels.begin(), mat.channels.end(),
			[](auto const& chan) { return chan.has_value(); }) != mat.channels.end();
	}

	inline bool HasCustomSettings(ITwinMaterial const& mat)
	{
		if (HasDefinedChannels(mat))
			return true;
		if (mat.HasUVTransform())
			return true;
		return false;
	}

}
