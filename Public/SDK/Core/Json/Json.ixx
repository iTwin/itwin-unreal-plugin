module;

// note: should be imported but generates internal error with VS 2022 17.9.2
#include <rfl/json.hpp>
#include <rfl.hpp>

export module SDK.Core.Json;

import<string>;
//import generates Internal compiler error on VS 2022 17.9.2
//import <rfl/json.hpp>;
//import <rfl.hpp>;

export namespace SDK::Core::Json {

	template<typename Type> 
	inline std::string ToString(const Type& t) {
		return rfl::json::write(t);
	}

	template<typename Type>
	inline void FromString(Type &t, const std::string &s) {
		t = rfl::json::read<Type>(s).value();
	}
}