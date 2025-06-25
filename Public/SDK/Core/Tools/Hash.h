/*--------------------------------------------------------------------------------------+
|
|     $Source: Hash.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "fnv1ahash.h"

namespace AdvViz::SDK::Tools
{
	inline constexpr std::uint64_t GenHash(const char* txt)
	{
		return Internal::hash_64_fnv1a_const(txt);
	}

	inline constexpr std::uint64_t GenHash(const wchar_t* txt)
	{
		return Internal::hash_64_fnv1a_const(txt);
	}

	// Template used to force the compiler to generate the hash at compile time because it's not guarantee that constexpr are always compile time
	template<uint64_t n>
	struct constN
	{
		enum value : std::uint64_t {
			crc = n
		};
	};

	// compile time Hash
	#define GenHashCT(txt) (AdvViz::SDK::Tools::constN<AdvViz::SDK::Tools::GenHash(txt)>::value::crc)

	//
	//inline constexpr std::uint64_t GenHashCT(const char* txt) {
	//	return constN<GenHash(txt)>::value::crc;
	//}

	//// compile time Hash
	//inline constexpr std::uint64_t GenHashCT(const wchar_t* txt) {
	//	return constN<GenHash(txt)>::value::crc;
	//}

}