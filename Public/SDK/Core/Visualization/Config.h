/*--------------------------------------------------------------------------------------+
|
|     $Source: Config.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#ifndef SDK_CPPMODULES
	#include <string>
	#include <memory>
	#include <filesystem>
	#include "Core/Network/Network.h"
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

// The macro below is left empty for now, until there is an handling of errors
// in the Visualization SDK
#define VIZ_SDK_WARN(FORMAT, ...)

MODULE_EXPORT namespace SDK::Core
{
	namespace Config {
		struct SServer
		{
			std::string server;
			int port = -1;
			std::string urlapiprefix;
		};

		struct SConfig
		{
			SServer server;
		};
		void Init(const SConfig& config);

		SConfig LoadFromFile(std::filesystem::path& path);
	}


	std::shared_ptr<Http>& GetDefaultHttp();
}