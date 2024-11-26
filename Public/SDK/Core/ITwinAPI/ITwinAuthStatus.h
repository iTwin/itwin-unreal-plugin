/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthStatus.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif


MODULE_EXPORT namespace SDK::Core
{
	enum class EITwinAuthStatus : uint8_t
	{
		None,
		InProgress,
		Success,
		Failed
	};
}
