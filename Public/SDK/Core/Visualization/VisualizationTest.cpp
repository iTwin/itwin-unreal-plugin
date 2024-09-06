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
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>

using namespace SDK::Core;

class HTTPMock : public httpmock::MockServer {
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer()
	{
		return httpmock::getFirstRunningMockServer<HTTPMock>();
	}

	explicit HTTPMock(int port = 9200) : MockServer(port) {}

	std::string GetUrl()
	{
		return "http://localhost:" + std::to_string(getPort());
	}

private:

	/// Handler called by MockServer on HTTP request.
	Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers)
	{
		if (method == "POST" && matchesURL(url, "/advviz/v1/decorations")) {
			std::string s = "{\"data\":{\"gcs\":{\"center\":[0,0,0], \"wkt\":\"WGS84\"}, \"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\", \"name\":\"test auto\"}, \"id\":\"66c476ed1129763cf5485826\"}";
			return Response(200, s);
		}
		if (method == "GET" && matchesURL(url, "/advviz/v1/decorations")) {
			std::string s = "{\"name\":\"test auto\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"gcs\":{\"wkt\":\"WGS84\",\"center\":[0,0,0]},\"id\":\"66c476ed1129763cf5485826\"}";
			return Response(200, s);
		}
		if (method == "DELETE" && matchesURL(url, "/advviz/v1/decorations")) {
			return Response(200, "{\"id\":\"66c476ed1129763cf5485826\"");
		}
		// Return "URI not found" for the undefined methods
		return Response(404, "Not Found");
	}

	/// Return true if \p url starts with \p str.
	bool matchesURL(const std::string& url, const std::string& str) const {
		return url.substr(0, str.size()) == str;
	}

};

const HTTPMock* const GetHttpMock()
{
	static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	return static_cast<HTTPMock*>(httpMockM.get());
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
	config.server.server = "http://localhost";
	config.server.port = GetHttpMock()->getPort();
	config.server.urlapiprefix = "/advviz/v1/decorations";
	Config::Init(config);
}

TEST_CASE("Visualization"){

	SECTION("Decoration") {
		try {
			SetDefaultConfig();
			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto decoration = IDecoration::New();
			decoration->Create("test auto", "", "");
			REQUIRE(decoration->GetId() != "");

			// create a decoration copy by fetching previous decoration from server
			auto decoration2 = IDecoration::New();
			decoration2->Get(decoration->GetId(), "");
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



