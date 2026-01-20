/*--------------------------------------------------------------------------------------+
|
|     $Source: StringWithEncoding.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <optional>
#include <string>

namespace AdvViz::SDK::Tools
{
	/// Encoding used for string - by default, we use UTF-8.
	enum class EStringEncoding : uint8_t
	{
		Utf8 = 0,
		Ansi
	};

	/// Simple std::string wrapper, with the ability to store its encoding in an explicit way.
	/// When not provided, the encoding is supposed to be UTF-8
	class StringWithEncoding
	{
	public:
		StringWithEncoding() = default;

		StringWithEncoding(std::string const& inStr, std::optional<EStringEncoding> const& encoding = {})
			: str_(inStr)
			, encodingOpt_(encoding)
		{
		}

		std::string const& str() const { return str_; }
		bool empty() const { return str_.empty(); }

		EStringEncoding GetEncoding() const { return encodingOpt_.value_or(EStringEncoding::Utf8); }

	private:
		std::string str_;
		std::optional<EStringEncoding> encodingOpt_; // UTF-8 if not provided.
	};
}
