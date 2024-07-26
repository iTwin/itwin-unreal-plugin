/*--------------------------------------------------------------------------------------+
|
|     $Source: fnv1ahash.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


namespace SDK::Core::Tools::Internal
{
	// FNV1a c++11 constexpr compile time hash functions, 64 bit
	// str should be a null terminated string literal, value should be left out
	// e.g hash_32_fnv1a_const("example")
	// code license: public domain or equivalent
	// post: https://notes.underscorediscovery.com/constexpr-fnv1a/
	// http://www.isthe.com/chongo/tech/comp/fnv/#FNV-1a

	template<class _Char>
	inline constexpr std::uint64_t hash_64_fnv1a_const(
		const _Char* const str,
		const std::uint64_t value = 0xcbf29ce484222325)
	{
		std::uint64_t crc = value;
		for (const _Char* p = str; *p != _Char(0); ++p)
			crc = (crc ^ std::uint64_t(*p)) * 0x100000001b3;
		return crc;
	}


}
