/*--------------------------------------------------------------------------------------+
|
|     $Source: FactoryClass.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <memory>
#include <functional>

namespace SDK::Core::Tools
{
	/// <summary>
	/// Allows to override the creation of objects
	/// </summary>
	/// <typeparam name="Type"> is base type</typeparam>
	/// <typeparam name="...Args"> are constructor arguments</typeparam>
	template<typename Type, typename... Args>
	class Factory
	{
	public:
		typedef std::function<Type* (Args...)> NewFctT;
		static Type* New(Args...args) { return GetGlobals().newFct_(args...); }
		static void SetNewFct(NewFctT fct) { GetGlobals().newFct_ = fct; }
		static NewFctT GetNewFct() { return GetGlobals().newFct_; }
		virtual ~Factory() {} //class derivate from Factory needs virtual destructor
	private:
		struct Globals {
			NewFctT newFct_;
			Globals();
		};
		static Globals& GetGlobals();
	};

}