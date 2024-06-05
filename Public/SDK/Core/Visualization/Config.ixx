export module SDK.Core.Visualization:Config;
import <string>;
import<memory>;
import<filesystem>;

import SDK.Core.Network;

export namespace SDK::Core
{
	namespace Config {
		struct SServer
		{
			std::string server;
			int port;
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