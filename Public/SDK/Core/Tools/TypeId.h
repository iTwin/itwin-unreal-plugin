/*--------------------------------------------------------------------------------------+
|
|     $Source: TypeId.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "Hash.h"

namespace SDK::Core::Tools
{
	namespace Internal {
		// return string that depend on template Type
		// from https ://stackoverflow.com/questions/56292104/hashing-types-at-compile-time-in-c17-c2a
		template <typename T> constexpr const char* TypedFctName()
		{
#ifdef _MSC_VER
			return __FUNCSIG__;
#else
			return __PRETTY_FUNCTION__;
#endif
		}
	}

	template<typename T>
	inline constexpr std::uint64_t GenHash()
	{
		return GenHash(Internal::TypedFctName<T>());
	}

	template<typename T>
	class TypeId 
	{
	public:
		enum ETypeid : std::uint64_t {
			value = Tools::GenHash<T>()
		};
		static constexpr inline std::uint64_t GetTypeId() { return ETypeid::value; }
	};

	class IDynType {
	public:
		// for type check
		virtual std::uint64_t GetDynTypeId() = 0;
		virtual bool IsTypeOf(std::uint64_t i) = 0;
	};


	template<typename T1, typename T2>
	std::shared_ptr<T1> DynamicCast(std::shared_ptr<T2> pObj)
	{
		if (pObj->IsTypeOf(T1::GetTypeId()) == true)
		{
			std::shared_ptr<T1> ext = std::static_pointer_cast<T1>(pObj);
			return ext;
		}
		return {};
	}


}