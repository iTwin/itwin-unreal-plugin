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



/*
TEST_CASE("Visualization"){

	if (g_startNewServer)
	{
		g_startNewServer = false;
		StartServer();
	}

	SECTION("Decoration") {
		try {
			SetDefaultConfig();
			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto decoration = IDecoration::New();
			decoration->Create("test auto");
			REQUIRE(decoration->GetId() != "");

			auto decoration2 = IDecoration::New();
			decoration2->Get(decoration->GetId());
			REQUIRE(decoration2->GetId() == decoration->GetId());

			decoration->Delete();

			auto decoration3 = IDecoration::New();
			REQUIRE_THROWS(decoration3->Get(decoration->GetId()));

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
*/

class ExtendedDecoration : public Decoration
{
public:
	int Fct() { return 1234; }
	virtual const std::string& GetId() { 
		static std::string s("test"); 
		return s; 
	}
};

TEST_CASE("Visualization:ExtendedDecoration") {
	try {
		SetDefaultConfig();

		IDecoration::SetNewFct([]() {
			std::shared_ptr<IDecoration> p(static_cast<IDecoration*>(new ExtendedDecoration));
			return p;
			});
		std::shared_ptr<IDecoration> pDecoration = IDecoration::New();
		CHECK(pDecoration.get() != nullptr);
		std::shared_ptr<ExtendedDecoration> pExt = std::dynamic_pointer_cast<ExtendedDecoration>(pDecoration);
		CHECK(pExt.get() != nullptr);
		CHECK(pExt->Fct() == 1234);
		CHECK(pExt->GetId() == "test");
	}
	catch (std::string& error)
	{
		FAIL("Error: " << error);
	}
}
