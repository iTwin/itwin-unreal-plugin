/*--------------------------------------------------------------------------------------+
|
|     $Source: Json.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <rfl/json.hpp>
#include <rfl.hpp>
#include <string>
#include <stdexcept>
#include <filesystem>
#include "../Tools/Assert.h"
#include "../Tools/Log.h"

namespace rfl {
	// additionnal class for std::u8string
	template <>
	struct Reflector<std::u8string> {
		using ReflType = std::string;
		static constexpr std::u8string to(const ReflType& v) noexcept {
			return std::u8string(reinterpret_cast<const char8_t*>(v.data()));
		}
		static constexpr ReflType from(const std::u8string& v)
		{
			return std::string(reinterpret_cast<const char*>(v.c_str()));
		}
	};
}

namespace AdvViz::SDK::Json {

	template<typename Type> 
	inline std::string ToString(const Type& t) {
		return rfl::json::write(t);
	}

	template<typename Type, typename From>
	inline bool FromStream(Type& t, From& s, std::string& parseError, bool bLogParseError = true) {
		auto result = rfl::json::read<Type>(s);
		auto const optErr = result.error();
		if (optErr)
		{
			parseError = optErr->what();
			// We may not want to log the error directly, in case the client code handles the error itself.
			// Typically, some iTwin responses can be very different in specific cases, and thus it is easier
			// to parse a different structure if the expected one is not provided.
			// (See for example ITwinWebServices::QueryIModel).
			if (bLogParseError)
			{
				BE_LOGE("json", "json parse error:" << parseError );
			}
			// Asserting here is quite annoying because it blocks the debugging, and with the automatic retry
			// system, it can become a nightmare very soon, and lead several developers to investigate the
			// same issue...
			//
			// BE_ISSUE("json parse error", parseError);
			return false;
		}
		else
		{
			t = result.value();
			return true;
		}
	}
	template<typename Type>
	inline bool FromStream(Type& t, const std::string& s, std::string& parseError, bool bLogParseError = true) {
		auto result = rfl::json::read<Type>(s);
		auto const optErr = result.error();
		if (optErr)
		{
			parseError = optErr->what();
			// We may not want to log the error directly, in case the client code handles the error itself.
			// Typically, some iTwin responses can be very different in specific cases, and thus it is easier
			// to parse a different structure if the expected one is not provided.
			// (See for example ITwinWebServices::QueryIModel).
			if (bLogParseError)
			{
				BE_LOGE("json", "json parse error:" << parseError << " from json :" << std::endl << s);
			}
			// Asserting here is quite annoying because it blocks the debugging, and with the automatic retry
			// system, it can become a nightmare very soon, and lead several developers to investigate the
			// same issue...
			//
			// BE_ISSUE("json parse error", parseError);
			return false;
		}
		else
		{
			t = result.value();
			return true;
		}
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s,
						   std::string& parseError, bool bLogParseError = true) {
		return FromStream(t, s, parseError, bLogParseError);
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s) {
		std::string parseJsonError;
		return FromString(t, s, parseJsonError, true);
	}

	template<typename Type>
	inline bool LoadFile(Type& t, const std::filesystem::path& jsonPath, std::string &parseError) {
		if (!std::filesystem::exists(jsonPath))
		{
			BE_LOGE("json", "Content JSON file does not exist:" << jsonPath.string());
			return false;
		}
		std::ifstream fstream;
		fstream.open(jsonPath);
		if (!fstream.is_open())
		{
			BE_LOGE("json", "Failed to open content JSON file: " << jsonPath.string());
			return false;
		}
		bool bret =  AdvViz::SDK::Json::FromStream(t, fstream, parseError);
		fstream.close();
		return bret;
	}

}
