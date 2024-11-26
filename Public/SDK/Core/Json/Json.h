/*--------------------------------------------------------------------------------------+
|
|     $Source: Json.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <rfl/json.hpp>
#include <rfl.hpp>
#include <string>
#include <stdexcept>

namespace SDK::Core::Json {

	template<typename Type> 
	inline std::string ToString(const Type& t) {
		return rfl::json::write(t);
	}

	template<typename Type, typename From>
	inline bool FromStream(Type& t, From& s, std::string& parseError) {
		auto result = rfl::json::read<Type>(s);
		auto const optErr = result.error();
		if (optErr)
		{
			parseError = optErr->what();
			return false;
		}
		else
		{
			t = result.value();
			return true;
		}
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s, std::string& parseError) {
		return FromStream(t, s, parseError);
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s) {
		std::string parseJsonError;
		return FromString(t, s, parseJsonError);
	}
}
