/*--------------------------------------------------------------------------------------+
|
|     $Source: VisualizationTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

import SDK.Core.Visualization;
import<filesystem>;

#include <gtest/gtest.h>
#include <fstream>
#include <thread>

#include <process.hpp>
using namespace SDK::Core;

static std::string serverURL("localhost");
static int serverPort = 8080;

static const bool g_startNewServer = true;

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

// define test environment : start local http server
class Environment : public ::testing::Environment
{
public:
	~Environment(){}

	// Override this to define how to set up the environment.
	void SetUp() override {
		if (g_startNewServer)
			StartServer();
	}

	// Override this to define how to tear down the environment.
	void TearDown() override {
		if (g_serverProcess)
		{
			std::cout << "Stop server process" << std::endl;
			g_serverProcess->kill();
		}
	}
};


testing::Environment* const dummy_env = testing::AddGlobalTestEnvironment(new Environment);

TEST(Visualization, Config) {
	using namespace SDK::Core;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = SDK::Core::Config::LoadFromFile(filePath);
	EXPECT_EQ(config.server.server, "plop");
	EXPECT_EQ(config.server.port, 2345);
	EXPECT_EQ(config.server.urlapiprefix, "api/v1");

	Config::Init(config);
	EXPECT_TRUE(GetDefaultHttp().get() != nullptr);
}

TEST(Visualization, Scene) {
	try {

		Config::SConfig config;
		config.server.server = serverURL;
		config.server.port = serverPort;
		config.server.urlapiprefix = "/advviz/v1";
		Config::Init(config);
		EXPECT_TRUE(GetDefaultHttp().get() != nullptr);

		auto scene = IScene::New();
		scene->Create("test auto");
		auto decEnv = scene->GetDecorationEnvironment();
		EXPECT_NE(scene->GetId(), "");
		ASSERT_NE(decEnv.get(), nullptr);
		EXPECT_NE(decEnv->GetId(), "");
		auto decLayers = scene->GetDecorationLayers();
		ASSERT_EQ(decLayers.size(), 1);
		ASSERT_NE(decLayers[0].get(), nullptr);
		ASSERT_NE(decLayers[0]->GetId(), "");

		auto scene2 = IScene::New();
		scene2->Get(scene->GetId());
		EXPECT_EQ(scene2->GetId(), scene->GetId());
		auto decEnv2 = scene2->GetDecorationEnvironment();
		ASSERT_NE(decEnv2.get(), nullptr);
		EXPECT_EQ(decEnv2->GetId(), decEnv->GetId());
		auto decLayers2 = scene2->GetDecorationLayers();
		ASSERT_EQ(decLayers2.size(), decLayers.size());
		for (size_t i = 0; i < decLayers.size(); ++i)
			EXPECT_EQ(decLayers2[i]->GetId(), decLayers[i]->GetId());

		scene->Delete(true /*delete layers*/);

		auto scene3 = IScene::New();
		EXPECT_THROW(scene2->Get(scene->GetId()), std::string);

	}
	catch (std::string& error)
	{
		FAIL() << "Error: " << error << std::endl;
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

TEST(Visualization, ExtendedScene) {
	try {
		IScene::SetNewFct([]() {
			std::shared_ptr<IScene> p(static_cast<IScene*>(new ExtendedScene));
			return p;
			});
		std::shared_ptr<IScene> pScene = IScene::New();
		ASSERT_NE(pScene.get(), nullptr);
		std::shared_ptr<ExtendedScene> pExt = std::dynamic_pointer_cast<ExtendedScene>(pScene);
		ASSERT_NE(pExt.get(), nullptr);
		ASSERT_EQ(pExt->Fct(), 1234);
		ASSERT_EQ(pExt->GetId(), "test");
	}
	catch (std::string& error)
	{
		FAIL() << "Error: " << error << std::endl;
	}
}