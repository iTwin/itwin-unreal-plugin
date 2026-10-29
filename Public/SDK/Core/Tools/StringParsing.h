/*--------------------------------------------------------------------------------------+
|
|     $Source: StringParsing.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>

namespace AdvViz::SDK::Tools
{
	/// Strict decimal parsing of an unsigned 64-bit integer.
	///
	/// Preferred over std::stoull at any boundary where the string comes from outside the
	/// process (cross-language calls, json payloads, urls): stoull throws on malformed input
	/// and on overflow, silently wraps negative values, and accepts trailing garbage.
	/// This one never throws, never allocates, and requires the whole string to be consumed.
	///
	/// \return false, leaving outValue untouched, if str is not exactly a decimal number.
	inline bool TryParseUInt64(std::string_view str, uint64_t& outValue)
	{
		if (str.empty())
			return false;
		// from_chars rejects leading '+', '-', whitespace and any base prefix by itself.
		uint64_t parsed = 0;
		auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), parsed);
		if (ec != std::errc() || ptr != str.data() + str.size())
			return false;
		outValue = parsed;
		return true;
	}

	/// Same, returning the fallback instead of a success flag, for call sites where a
	/// malformed value has an obvious neutral meaning.
	inline uint64_t ParseUInt64Or(std::string_view str, uint64_t fallback)
	{
		uint64_t value = 0;
		return TryParseUInt64(str, value) ? value : fallback;
	}
}
