/*--------------------------------------------------------------------------------------+
|
|     $Source: FactoryClass.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
		static std::shared_ptr<Type> New(Args...args) { return newFct_(args...); }
		static void SetNewFct(std::function<std::shared_ptr<Type>(Args...)> fct) { newFct_ = fct; }
		static std::function<std::shared_ptr<Type>(Args...)> GetNewFct() { return newFct_; }
		virtual ~Factory() {} //class derivate from Factory needs virtual destructor
	private:
		static std::function<std::shared_ptr<Type>(Args...)> newFct_;
	};

}