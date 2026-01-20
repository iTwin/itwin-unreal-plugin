/*--------------------------------------------------------------------------------------+
|
|     $Source: IDelayedCallHandler.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#ifndef SDK_CPPMODULES
#	include <functional>
#	include <memory>
#	include <string>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/DelayedCallEnums.h>
#include <Core/Tools/Tools.h>

MODULE_EXPORT namespace AdvViz::SDK
{
	using DelayedCallFunc = std::function<DelayedCall::EReturnedValue()>;

	/// Interface for a delayed call handler.
	class IDelayedCallHandler : public Tools::Factory<IDelayedCallHandler>
	{
	public:
		using SharedInstance = std::shared_ptr<IDelayedCallHandler>;

		static SharedInstance& GetInstance();

		IDelayedCallHandler() = default;
		virtual ~IDelayedCallHandler();

		//! Delay a function call to a certain delay.
		//! The function should return true if we need to repeat the call again (after the same delay).
		virtual void UniqueDelayedCall(std::string const& uniqueId, DelayedCallFunc&& func, float delayInSeconds) = 0;

	private:
		struct Globals
		{
			SharedInstance instance_;
		};
		static Globals& GetGlobals();
	};

	template<>
	Tools::Factory<IDelayedCallHandler>::Globals::Globals();

}
