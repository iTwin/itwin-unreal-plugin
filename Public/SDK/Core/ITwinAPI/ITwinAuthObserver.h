/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthObserver.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	include <string>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core
{
	/// Used as callback for iTwin authorization process.
	class ITwinAuthObserver
	{
	public:
		virtual ~ITwinAuthObserver() = default;

		virtual void OnAuthorizationDone(bool bSuccess, std::string const& strError) = 0;
	};
}
