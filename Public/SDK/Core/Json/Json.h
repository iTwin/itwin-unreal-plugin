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
#include <Core/Json/Json.h>

namespace SDK::Core::Json {

	template<typename Type> 
	inline std::string ToString(const Type& t) {
		return rfl::json::write(t);
	}

	template<typename Type>
	inline void FromString(Type &t, const std::string &s) {
		t = rfl::json::read<Type>(s).value();
	}
}
