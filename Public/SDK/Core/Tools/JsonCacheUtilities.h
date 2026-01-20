/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonCacheUtilities.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <filesystem>
#include "../AdvVizLinkType.h"

namespace AdvViz::SDK::Tools
{
	enum class EFileBOM
	{
		None,
		UTF8,
		UTF16_BE,
		UTF16_LE,
		UTF32_BE,
		UTF32_LE,
		Unknown
	};

	ADVVIZ_LINK EFileBOM HasBOM(std::filesystem::path const& filepath, uint64_t& fileSize);

	/// Load the full content of a file in a string, dealing with different encoding formats, while
	/// discarding the `"reply":` prefix the file may start with.
	/// (Very specific to the way we cache Json replies in the iTwin schedule cache mechanism...)
	ADVVIZ_LINK std::string LoadCacheFileToStringWithoutReply(std::filesystem::path const& Filepath);
}
