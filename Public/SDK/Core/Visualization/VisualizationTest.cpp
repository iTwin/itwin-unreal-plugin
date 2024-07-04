/*--------------------------------------------------------------------------------------+
|
|     $Source: VisualizationTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Visualization.h"
#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fstream>
#include <thread>

#include <process.hpp>
using namespace SDK::Core;

static std::string serverURL("localhost");
static int serverPort = 8080;

static bool g_startNewServer = true;

std::unique_ptr<TinyProcessLib::Process> g_serverProcess;
void StartServer()
{
	//using namespace subprocess;
	using namespace TinyProcessLib;
	std::filesystem::path serverexePath(CMAKE_SOURCE_DIR);
	serverexePath /= "../../Private/Server";

	bool serverStarted = false;
	auto fctOut = [&serverStarted](const char* bytes, size_t n) {
		std::string s(bytes, n);
		std::cout << "[server] " << s;
		if (s.find("router started") != std::string::npos)
			serverStarted = true;
		};

	g_serverProcess.reset(new Process("go run server.go -server "+ serverURL + ":" + std::to_string(serverPort), serverexePath.string().c_str(),
		fctOut,
		fctOut));

	std::cout << "Waiting server to start:" << std::endl;
	int exit_status;
	while (!g_serverProcess->try_get_exit_status(exit_status) && !serverStarted) {
		std::cout << ".";
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

TEST_CASE("Visualization:Config")
{
	using namespace SDK::Core;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = SDK::Core::Config::LoadFromFile(filePath);
	REQUIRE(config.server.server== "plop");
	REQUIRE(config.server.port == 2345);
	REQUIRE(config.server.urlapiprefix == "api/v1");

	Config::Init(config);
	REQUIRE(GetDefaultHttp().get() != nullptr);
}

void SetDefaultConfig()
{
	Config::SConfig config;
	config.server.server = serverURL;
	config.server.port = serverPort;
	config.server.urlapiprefix = "/advviz/v1";
	Config::Init(config);
}



TEST_CASE("Visualization"){

	if (g_startNewServer)
	{
		g_startNewServer = false;
		StartServer();
	}

	SECTION("Scene") {
		try {
			SetDefaultConfig();
			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto scene = IScene::New();
			scene->Create("test auto");
			auto decEnv = scene->GetDecorationEnvironment();
			REQUIRE(scene->GetId() != "");
			CHECK(decEnv.get() != nullptr);
			REQUIRE(decEnv->GetId() != "");
			auto decLayers = scene->GetDecorationLayers();
			CHECK(decLayers.size() == 1);
			REQUIRE(decLayers[0].get() != nullptr);
			REQUIRE(decLayers[0]->GetId() != "");

			auto scene2 = IScene::New();
			scene2->Get(scene->GetId());
			REQUIRE(scene2->GetId() == scene->GetId());
			auto decEnv2 = scene2->GetDecorationEnvironment();
			CHECK(decEnv2.get() != nullptr);
			REQUIRE(decEnv2->GetId() == decEnv->GetId());
			auto decLayers2 = scene2->GetDecorationLayers();
			CHECK(decLayers2.size() == decLayers.size());
			for (size_t i = 0; i < decLayers.size(); ++i)
				REQUIRE(decLayers2[i]->GetId() == decLayers[i]->GetId());

			scene->Delete(true /*delete layers*/);

			auto scene3 = IScene::New();
			REQUIRE_THROWS(scene2->Get(scene->GetId()));

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
	
	if (g_serverProcess)
	{
		std::cout << "Stop server process" << std::endl;
		g_serverProcess->kill();
	}
}

class ExtendedScene : public Scene
{
public:
	int Fct() { return 1234; }
	virtual const std::string& GetId() { 
		static std::string s("test"); 
		return s; 
	}
};

TEST_CASE("Visualization:ExtendedScene") {
	try {
		SetDefaultConfig();

		IScene::SetNewFct([]() {
			std::shared_ptr<IScene> p(static_cast<IScene*>(new ExtendedScene));
			return p;
			});
		std::shared_ptr<IScene> pScene = IScene::New();
		CHECK(pScene.get() != nullptr);
		std::shared_ptr<ExtendedScene> pExt = std::dynamic_pointer_cast<ExtendedScene>(pScene);
		CHECK(pExt.get() != nullptr);
		CHECK(pExt->Fct() == 1234);
		CHECK(pExt->GetId() == "test");
	}
	catch (std::string& error)
	{
		FAIL("Error: " << error);
	}
}
