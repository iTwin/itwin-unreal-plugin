/*--------------------------------------------------------------------------------------+
|
|     $Source: DelayedCall.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#ifndef SDK_CPPMODULES
#	include <functional>
#	include <string>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/DelayedCallEnums.h>

MODULE_EXPORT namespace AdvViz::SDK
{
	//! Delay a function to a certain delay. Returns false if no support for delayed calls currently exists.
	//! \param uniqueId unique identifier for the callback: for a given ID, only one call will be stacked.
	//! \param func callback function to call after delay. The function should return Repeat if the call has
	//!		to be repeated.
	//! \param delayInSeconds minimum delay (in seconds) before the call is actually processed.
	bool UniqueDelayedCall(std::string const& uniqueId,
		std::function<DelayedCall::EReturnedValue()>&& func,
		float delayInSeconds);
}
