/*--------------------------------------------------------------------------------------+
|
|     $Source: TypeId.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "Hash.h"
#include <memory>
namespace AdvViz::SDK::Tools
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
		virtual std::uint64_t GetDynTypeId() const = 0;
		virtual bool IsTypeOf(std::uint64_t i) const = 0;
	};

	template<typename T, typename Parent>
	class DynType : public TypeId<T>, public IDynType {
	public:
		// for type check
		using TypeId<T>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || Parent::IsTypeOf(i); }
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

	template<typename T1, typename T2>
	T1* DynamicCast(T2* pObj)
	{
		if (pObj->IsTypeOf(T1::GetTypeId()) == true)
		{
			T1* ext = static_cast<T1*>(pObj);
			return ext;
		}
		return {};
	}

}