/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#ifndef SDK_CPPMODULES
#	include <chrono>
#	include <string>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif


MODULE_EXPORT namespace AdvViz::SDK
{
	struct ITwinAuthInfo
	{
		std::string AuthorizationCode;
		std::string CodeVerifier;
		std::string RefreshToken;
		int ExpiresIn = 0; // expressed in seconds
		std::chrono::system_clock::time_point CreationTime = std::chrono::system_clock::now();

		std::chrono::system_clock::time_point GetExpirationTime() const
		{
			return CreationTime + std::chrono::seconds(ExpiresIn);
		}
	};
}
