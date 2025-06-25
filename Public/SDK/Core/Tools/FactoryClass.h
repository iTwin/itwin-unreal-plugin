/*--------------------------------------------------------------------------------------+
|
|     $Source: FactoryClass.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "../AdvVizLinkType.h"
#include "TypeId.h"
#include <memory>
#include <functional>

namespace AdvViz::SDK::Tools
{
	namespace Internal
	{
		ADVVIZ_LINK void DefaultDelete(void* p);
	}
	/// <summary>
	/// Allows to override the creation of objects
	/// </summary>
	/// <typeparam name="Type"> is base type</typeparam>
	/// <typeparam name="...Args"> are constructor arguments</typeparam>
	template<typename Type, typename... Args>
	class ADVVIZ_LINK Factory : public Tools::TypeId<Type>, public IDynType
	{
	public:
		typedef std::function<Type* (Args...)> NewFctT;
		typedef std::function<void(void* ptr)> DeleteFctT;
		inline static Type* New(Args...args) { return GetGlobals().newFct_(args...); }
		inline static void SetNewFct(NewFctT newFct) { 
			GetGlobals().newFct_ = newFct;
		}
		inline static void SetDeleteFct(DeleteFctT deleteFct) {
			GetGlobals().deleteFct_ = deleteFct;
		}
		static NewFctT GetNewFct() { return GetGlobals().newFct_; }
		inline static DeleteFctT GetDeleteFct() { return GetGlobals().deleteFct_; }
		virtual ~Factory() {} //class derivate from Factory needs virtual destructor

		inline static void operator delete(void* ptr, std::size_t /*sz*/) {
			if (GetGlobals().deleteFct_)
				GetGlobals().deleteFct_(ptr);
			else
				Internal::DefaultDelete(ptr);
		}

		using Tools::TypeId<Type>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }

	private:
		struct Globals {
			NewFctT newFct_;
			DeleteFctT deleteFct_;
			Globals();
		};
		static Globals& GetGlobals();
	};

}