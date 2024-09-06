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
#include <Core/Json/Json.h>

namespace SDK::Core::Json {

	template<typename Type> 
	inline std::string ToString(const Type& t) {
		return rfl::json::write(t);
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s, std::string& parseError) {
		bool bHasParseError = false;
		try {
			t = rfl::json::read<Type>(s).value();
		}
		catch (std::exception const& e) {
			parseError = e.what();
			bHasParseError = true;
		}
		return !bHasParseError;
	}

	template<typename Type>
	inline bool FromString(Type& t, const std::string& s) {
		std::string parseJsonError;
		return FromString(t, s, parseJsonError);
	}
}
