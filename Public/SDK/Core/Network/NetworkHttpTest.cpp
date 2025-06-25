/*--------------------------------------------------------------------------------------+
|
|     $Source: NetworkHttpTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Network.h"
#include <catch2/catch_all.hpp>

// For now, unit tests regarding ITwinAPI are done in the Unreal plugin (see WebServicesTest.cpp)
// We could o them in the SDK as well, but it would require to transfer/share the same mock server
// as in the plugin, and it has few interest until we actually use the SDK for another platform
// such as Unity...
#define TEST_ITWINAPI_REQUESTS_IN_SDK() 0


#if TEST_ITWINAPI_REQUESTS_IN_SDK()
#include <Core/ITwinAPI/ITwinTypes.h>
#include <Core/ITwinAPI/ITwinWebServices.h>
#include <Core/ITwinAPI/ITwinWebServicesObserver.h>
#endif

//TEST_CASE(HttpTest, Put) {
//	auto http = AdvViz::SDK::Http::New();
//	http->SetBaseUrl("http://127.0.0.1:5984");
//	http->SetBasicAuth("loloDb", "ydbOi589");
//	auto r = http->Put("my_database", "");
//	REQUIRE(r.first, 200);
//	REQUIRE(r.second, "{\"error\":\"file_exists\",\"reason\":\"The database could not be created, the file already exists.\"}\n");
//}

#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>

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
		const std::string& /*data*/,
		const std::vector<UrlArg>& /*urlArguments*/,
		const std::vector<Header>& /*headers*/)
	{
		if (method == "GET" && matchesPrefix(url, "/json")) {
			// Do something and return response
			return Response(200, "{\n  \"slideshow\": {\n    \"author\": \"Yours Truly\", \n    \"date\": \"date of publication\", \n    \"slides\": [\n      {\n        \"title\": \"Wake up to WonderWidgets!\", \n        \"type\": \"all\"\n      }, \n      {\n        \"items\": [\n          \"Why <em>WonderWidgets</em> are great\", \n          \"Who <em>buys</em> WonderWidgets\"\n        ], \n        \"title\": \"Overview\", \n        \"type\": \"all\"\n      }\n    ], \n    \"title\": \"Sample Slide Show\"\n  }\n}\n");
		}
		// Return "URI not found" for the undefined methods
		return Response(404, "Not Found");
	}

	/// Return true if \p url starts with \p str.
	bool matchesPrefix(const std::string& url, const std::string& str) const {
		return url.substr(0, str.size()) == str;
	}
};


TEST_CASE("HttpTest:GetJsonStr") {
	std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	HTTPMock* httpMock = static_cast<HTTPMock*>(httpMockM.get());
	auto http = AdvViz::SDK::Http::New();
	http->SetBaseUrl(httpMock->GetUrl().c_str());
	AdvViz::SDK::Http::Response r = http->GetJsonStr("json");
	REQUIRE(r.first == 200);
	REQUIRE(r.second == "{\n  \"slideshow\": {\n    \"author\": \"Yours Truly\", \n    \"date\": \"date of publication\", \n    \"slides\": [\n      {\n        \"title\": \"Wake up to WonderWidgets!\", \n        \"type\": \"all\"\n      }, \n      {\n        \"items\": [\n          \"Why <em>WonderWidgets</em> are great\", \n          \"Who <em>buys</em> WonderWidgets\"\n        ], \n        \"title\": \"Overview\", \n        \"type\": \"all\"\n      }\n    ], \n    \"title\": \"Sample Slide Show\"\n  }\n}\n");
}

struct Slide {
	std::optional<std::vector<std::string>> items;
	std::string type;
	std::string title;
};

struct Slideshow {
	std::string author;
	std::string date;
	std::string title;
	std::vector<Slide> slides;
};

struct S {
	Slideshow slideshow;
};
TEST_CASE("HttpTest:GetJsonOBJ") {
	std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	HTTPMock* httpMock = static_cast<HTTPMock*>(httpMockM.get());

	auto http = AdvViz::SDK::Http::New();
	http->SetBaseUrl(httpMock->GetUrl().c_str());
	S s;
	auto r = http->GetJson(s, "json");
	REQUIRE(r== 200);
	REQUIRE(s.slideshow.title == "Sample Slide Show");
	REQUIRE(s.slideshow.author== "Yours Truly");
	REQUIRE(s.slideshow.date== "date of publication");
	REQUIRE(s.slideshow.slides.size()== 2);
	REQUIRE(s.slideshow.slides[0].type== "all");
	REQUIRE(s.slideshow.slides[0].title== "Wake up to WonderWidgets!");
	REQUIRE(s.slideshow.slides[0].items.has_value()== false);
	REQUIRE(s.slideshow.slides[1].type== "all");
	REQUIRE(s.slideshow.slides[1].title== "Overview");
	REQUIRE(s.slideshow.slides[1].items->size()== 2);
	REQUIRE(s.slideshow.slides[1].items->at(0)== "Why <em>WonderWidgets</em> are great");
	REQUIRE(s.slideshow.slides[1].items->at(1)== "Who <em>buys</em> WonderWidgets");
}

struct SlidePartial {
	std::optional<std::vector<std::string>> items;
	//std::string type;
	std::string title;
};

struct SlideshowPartial {
	std::string author;
	//std::string date;
	std::string title;
	std::vector<SlidePartial> slides;
};

struct SPartial {
	SlideshowPartial slideshow;
};

TEST_CASE("HttpTest:GetJsonPartialOBJ") {
	std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	HTTPMock* httpMock = static_cast<HTTPMock*>(httpMockM.get());
	auto http = AdvViz::SDK::Http::New();
	http->SetBaseUrl(httpMock->GetUrl().c_str());
	SPartial s;
	auto r = http->GetJson(s, "json");
	REQUIRE(r == 200);
	REQUIRE(s.slideshow.title == "Sample Slide Show");
	REQUIRE(s.slideshow.author == "Yours Truly");
	//REQUIRE(s.slideshow.date == "date of publication");
	REQUIRE(s.slideshow.slides.size() == 2);
	//REQUIRE(s.slideshow.slides[0].type == "all");
	REQUIRE(s.slideshow.slides[0].title == "Wake up to WonderWidgets!");
	REQUIRE(s.slideshow.slides[0].items.has_value() == false);
	//REQUIRE(s.slideshow.slides[1].type == "all");
	REQUIRE(s.slideshow.slides[1].title == "Overview");
	REQUIRE(s.slideshow.slides[1].items->size() == 2);
	REQUIRE(s.slideshow.slides[1].items->at(0) == "Why <em>WonderWidgets</em> are great");
	REQUIRE(s.slideshow.slides[1].items->at(1) == "Who <em>buys</em> WonderWidgets");
}


#if TEST_ITWINAPI_REQUESTS_IN_SDK()

struct ITwinInfoHolder
{
	AdvViz::SDK::ITwinInfo iTwin;
};

#define ITWIN_ACCESS_TOKEN "abcdefg"

TEST_CASE("HttpTest:GetITwinInfo")
{
	struct TestITwinInfoObserver : public AdvViz::SDK::ITwinDefaultWebServicesObserver
	{
		void OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& info) override
		{
			REQUIRE(bSuccess);
			CHECK(info.id == "e72496bd-03a5-4ad8-8a51-b14e827603b1");
			CHECK(info.displayName == "Tests_AlexW");
		}
		std::string GetObserverName() const override
		{
			return "Test";
		}
	};
	TestITwinInfoObserver obs;
	AdvViz::SDK::ITwinWebServices webServices;
	webServices.SetObserver(&obs);
	webServices.SetAuthToken(ITWIN_ACCESS_TOKEN);
	webServices.GetITwinInfo("e72496bd-03a5-4ad8-8a51-b14e827603b1");
}

TEST_CASE("HttpTest:GetITwins")
{
	struct TestITwinObserver : public AdvViz::SDK::ITwinDefaultWebServicesObserver
	{
		void OnITwinsRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfos const& infos) override
		{
			REQUIRE(bSuccess);
			REQUIRE(infos.iTwins.size() == 4);

			CHECK(infos.iTwins[0].id == "5e15184e-6d3c-43fd-ad04-e28b4b39485e");
			CHECK(infos.iTwins[0].displayName == "Bentley Caymus EAP");
			CHECK(infos.iTwins[0].status == "Active");

			CHECK(infos.iTwins[1].id == "e72496bd-03a5-4ad8-8a51-b14e827603b1");
			CHECK(infos.iTwins[1].displayName == "Tests_AlexW");
			CHECK(infos.iTwins[1].status == "Active");

			CHECK(infos.iTwins[2].id == "ea28fcd7-71d2-4313-951f-411639d9471e");
			CHECK(infos.iTwins[2].displayName == "Omniverse iModel Viz Test Hub");
			CHECK(infos.iTwins[2].status == "Active");

			CHECK(infos.iTwins[3].id == "257af6c2-b2fa-41fd-b85d-b90837f36934");
			CHECK(infos.iTwins[3].displayName == "ConExpo 2023 - Civil");
			CHECK(infos.iTwins[3].status == "Active");
		}
		std::string GetObserverName() const override
		{
			return "Test";
		}
	};
	TestITwinObserver obs;
	AdvViz::SDK::ITwinWebServices webServices;
	webServices.SetObserver(&obs);
	webServices.SetAuthToken(ITWIN_ACCESS_TOKEN);
	webServices.GetITwins();
}

TEST_CASE("HttpTest:GetIModelChangesets")
{
	auto http = AdvViz::SDK::Http::New();
	http->SetBaseUrl("https://api.bentley.com");
	AdvViz::SDK::ChangesetInfos infos;
	auto r = http->GetJson(infos,
		"imodels/d66fcd8c-604a-41d6-964a-b9767d446c53/changesets?$orderBy=index+desc",
		"",
		{
			{ "Accept", "application/vnd.bentley.itwin-platform.v2+json" },
			{ "Prefer", "return=representation" },
			{ "Authorization", "Bearer " ITWIN_ACCESS_TOKEN },
		});
	REQUIRE(r == 200);
	REQUIRE(infos.changesets.size() == 10);

	CHECK(infos.changesets[0].id == "9641026f8e6370db8cc790fab8943255af57d38e");
	CHECK(infos.changesets[0].index == 10);
	CHECK(infos.changesets[0].displayName == "10");
	CHECK(infos.changesets[0].description == "MicroStation Connector - initalLoad - Finalization changes");
	
	CHECK(infos.changesets[1].id == "57486e5a422f5fc59933ebca5e7f4975afb1a496");
	CHECK(infos.changesets[1].index == 9);
	CHECK(infos.changesets[1].displayName == "9");
	CHECK(infos.changesets[1].description == "MicroStation Connector - initalLoad - Extent Changes");

	CHECK(infos.changesets[9].id == "4681a740b4d10e171d885a83bf3d507edada91cf");
	CHECK(infos.changesets[9].index == 1);
	CHECK(infos.changesets[9].displayName == "1");
	CHECK(infos.changesets[9].description == "MicroStation Connector - Domain schema upgrade");
}

#endif // TEST_ITWINAPI_REQUESTS_IN_SDK
