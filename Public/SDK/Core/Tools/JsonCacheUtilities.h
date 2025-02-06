/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonCacheUtilities.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <filesystem>

namespace SDK::Core::Tools
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

	EFileBOM HasBOM(std::filesystem::path const& filepath, uint64_t& fileSize);

	/// Load the full content of a file in a string, dealing with different encoding formats, while
	/// discarding the `"reply":` prefix the file may start with.
	/// (Very specific to the way we cache Json replies in the iTwin schedule cache mechanism...)
	std::string LoadCacheFileToStringWithoutReply(std::filesystem::path const& Filepath);
}
