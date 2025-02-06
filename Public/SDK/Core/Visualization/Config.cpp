/*--------------------------------------------------------------------------------------+
|
|     $Source: Config.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <filesystem>
#include <fstream>
#include <rfl/json.hpp>

#include "Core/Network/Network.h"
#include "Config.h"
namespace SDK::Core
{
	namespace Config {

		class ConfigImpl {
		public:
			SConfig config_;
			std::shared_ptr<Http> defaultHttp_;
		};

		static ConfigImpl g_config;
		void Init(const SConfig& config)
		{
			g_config.config_ = config;
			g_config.defaultHttp_.reset(Http::New());
			std::string baseUrl = config.server.server;
			if (config.server.port >= 0)
			{
				baseUrl += ":" + std::to_string(config.server.port);
			}
			baseUrl += config.server.urlapiprefix;
			g_config.defaultHttp_->SetBaseUrl(baseUrl);
		}

		SConfig LoadFromFile(std::filesystem::path& path)
		{
			std::ifstream ifs(path);
			std::stringstream buffer;
			buffer << ifs.rdbuf();
			SConfig config;
			Json::FromString<SConfig>(config, buffer.str());
			return std::move(config);
		}
	}

	std::shared_ptr<Http>& GetDefaultHttp()
	{
		if (!Config::g_config.defaultHttp_)
		{
			VIZ_SDK_WARN(std::string("Default Http not defined. Call Config::Init."));
		}
		return Config::g_config.defaultHttp_;
	}
}

