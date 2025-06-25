/*--------------------------------------------------------------------------------------+
|
|     $Source: VisualizationTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include <filesystem>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;


HTTPMock* GetHttpMock()
{
	static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	return static_cast<HTTPMock*>(httpMockM.get());
}


TEST_CASE("Visualization:Config")
{
	using namespace AdvViz::SDK;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = AdvViz::SDK::Config::LoadFromFile(filePath);
	REQUIRE(config.server.server== "plop");
	REQUIRE(config.server.port == 2345);
	REQUIRE(config.server.urlapiprefix == "api/v1");

	Config::Init(config);
	REQUIRE(GetDefaultHttp().get() != nullptr);
}
 
void SetDefaultConfig()
{
	Config::SConfig config;
	config.server.server = "http://localhost";
	config.server.port = GetHttpMock()->getPort();
	config.server.urlapiprefix = "/advviz/v1";
	Config::Init(config);

	Tools::InitLog("log_Test.txt");
	CreateAdvVizLogChannels();
}

TEST_CASE("Visualization"){

	SECTION("Decoration") {
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations");
			mock->responseFct_[respKeyPost] = [] {
				std::string s = "{\"data\":{\"gcs\":{\"center\":[0,0,0], \"wkt\":\"WGS84\"}, \"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\", \"name\":\"test auto\"}, \"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->responseFct_[respKeyGet] = [] {
				std::string s = "{\"name\":\"test auto\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"gcs\":{\"wkt\":\"WGS84\",\"center\":[0,0,0]},\"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->responseFct_[respKeyDelete] = [] {
				return HTTPMock::Response2(200, "{\"id\":\"66c476ed1129763cf5485826\"");
				};

			auto decoration = IDecoration::New();
			decoration->Create("test auto", "");
			REQUIRE(decoration->GetId() != "");

			// create a decoration copy by fetching previous decoration from server
			auto decoration2 = IDecoration::New();
			decoration2->Get(decoration->GetId());
			REQUIRE(decoration2->GetId() == decoration->GetId());

			// delete decoration on server
			decoration->Delete();
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}


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
			IDecoration* p(static_cast<IDecoration*>(new ExtendedDecoration));
			return p;
			});
		std::shared_ptr<IDecoration> pDecoration(IDecoration::New());
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



