/*--------------------------------------------------------------------------------------+
|
|     $Source: Config.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <filesystem>
#include <fstream>
#include <optional>
#include <rfl/json.hpp>

#include "Core/Network/Network.h"
#include "Config.h"
#include "AsyncHelpers.h"

namespace AdvViz::SDK
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
			// We can assume that Init is called by the main thread (aka 'game' thread in UE)
			InitMainThreadId();
			g_config.config_ = config;
			g_config.defaultHttp_.reset(Http::New());
			std::string baseUrl = config.server.server;
			if (config.server.port >= 0)
			{
				baseUrl += ":" + std::to_string(config.server.port);
			}
			baseUrl += config.server.urlapiprefix;
			g_config.defaultHttp_->SetBaseUrl(baseUrl.c_str());

			// Deactivate some asserts if the execution of callbacks in game/main thread is not supported.
			SetSupportAsyncCallbacksInMainThread(
				g_config.defaultHttp_->SupportsExecuteAsyncCallbackInMainThread());
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

	std::shared_ptr<Http> const& GetDefaultHttp()
	{
		std::shared_ptr<Http> const& defaultHttp = Config::g_config.defaultHttp_;
		if (!defaultHttp)
		{
			VIZ_SDK_WARN(std::string("Default Http not defined. Call Config::Init."));
		}
		return defaultHttp;
	}
}
