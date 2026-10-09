/*--------------------------------------------------------------------------------------+
|
|     $Source: NetworkHttpTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Network.h"
#include <catch2/catch_all.hpp>

#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>

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

namespace
{
	static const auto slideshowJson = "{\n  \"slideshow\": {\n" \
		"  \"author\": \"Yours Truly\", \n" \
		"  \"date\": \"date of publication\", \n" \
		"  \"slides\": [\n" \
		"    {\n" \
		"      \"title\": \"Wake up to WonderWidgets!\", \n" \
		"      \"type\": \"all\"\n" \
		"    }, \n" \
		"    {\n" \
		"      \"items\": [\n  \"Why <em>WonderWidgets</em> are great\",\n  \"Who <em>buys</em> WonderWidgets\"\n ], \n" \
		"      \"title\": \"Overview\", \n" \
		"      \"type\": \"all\"\n" \
		"    }\n " \
		"  ], \n" \
		"  \"title\": \"Sample Slide Show\"\n" \
		"}\n}\n";
}


TEST_CASE("HttpTest:GetJsonStr") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("GET", "/json");
	httpMock->SetResponseFunction(requestKey, [] {
		return HTTPMock::Response2(200, slideshowJson);
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());
	Http::Response r = http->GetJsonStr("json");
	REQUIRE(r.first == 200);
	REQUIRE(r.second == slideshowJson);
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
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);

	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("GET", "/json");
	httpMock->SetResponseFunction(requestKey, [] {
		return HTTPMock::Response2(200, slideshowJson);
	});

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
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("GET", "/json");
	httpMock->SetResponseFunction(requestKey, [] {
		return HTTPMock::Response2(200, slideshowJson);
	});

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

TEST_CASE("HttpTest:AsyncGetJson") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("GET", "/scene");
	httpMock->SetResponseFunction(requestKey, [] {
		return HTTPMock::Response2(200, "{ \"scene\": { \"id\": \"abc\", \"displayName\": \"Scene001\" } }");
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	struct SJsonOutData
	{
		std::string displayName;
		std::string id;
	};
	struct SJsonOut
	{
		SJsonOutData scene;
	};
	typedef Tools::TSharedLockableDataPtr<SJsonOut> TJsonOutPtr;
	TJsonOutPtr jOut = Tools::MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
	std::atomic_bool taskFinished = false;
	http->AsyncGetJson(jOut,
		[&taskFinished](const Http::Response& r, AdvViz::expected<TJsonOutPtr, std::string>& exp) {
		REQUIRE(exp);
		CHECK(r.first == 200);
		auto jOut = (*exp)->GetRAutoLock();
		CHECK(jOut->scene.id == "abc");
		CHECK(jOut->scene.displayName == "Scene001");
		taskFinished = true;
	},
		"scene");
	REQUIRE(WaitForAsyncTask(taskFinished, 10));
}

TEST_CASE("HttpTest:PutJsonJBody") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);

	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("PUT", "/animation");
	httpMock->SetResponseFunction(requestKey, [] {
		return HTTPMock::Response2(200, "{ \"numUpdated\": 1 }");
	});

	struct SJIn
	{
		std::string animationId;
		double time = 0.0;
		double duration = 120.0;
	};
	SJIn jin { .animationId = "toto" };

	struct SJout_Put
	{
		int numUpdated = 0;
	};
	SJout_Put jout;
	auto r = http->PutJsonJBody(jout, "animation", jin);
	REQUIRE(r == 200);
	CHECK(jout.numUpdated == 1);
}

TEST_CASE("HttpTest:AsyncPutJson") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("PUT", "/scene");
	httpMock->SetResponseFunctionWithData(requestKey, [] (const std::string& data) {
		if (data.find("displayName") != std::string::npos
			&& data.find("SceneABC") != std::string::npos
			&& data.find("id") != std::string::npos
			&& data.find("abc") != std::string::npos)
			return HTTPMock::Response2(200, "{ \"numUpdated\": 1 }");
		else
			return HTTPMock::Response2(505, "unexpected data");
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	struct SJIn
	{
		std::string displayName;
		std::string id;
	};
	SJIn jin{ .displayName = "SceneABC", .id = "abc" };

	struct SJout_Put
	{
		int numUpdated = 0;
	};

	std::atomic_bool taskFinished = false;

	auto dataOut = Tools::MakeSharedLockableData<SJout_Put>();
	http->AsyncPutJson<SJout_Put>(dataOut,
		[&taskFinished](long httpCode, Tools::TSharedLockableData<SJout_Put> const& joutPtr)
	{
		REQUIRE(joutPtr);
		CHECK(httpCode == 200);
		auto jOut = joutPtr->GetRAutoLock();
		CHECK(jOut->numUpdated == 1);
		taskFinished = true;
	},
		"scene", Json::ToString(jin));
	REQUIRE(WaitForAsyncTask(taskFinished, 10));
}

TEST_CASE("HttpTest:PostJsonJBody") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);

	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("POST", "/points");
	httpMock->SetResponseFunctionWithData(requestKey, [] (const std::string& data) {
		if (data == "{\"positions\":[51.0,52.0,53.0]}")
			return HTTPMock::Response2(200, "{ \"ids\": [\"1\", \"2\", \"3\"] }");
		else
			return HTTPMock::Response2(505, "unexpected points");
	});

	struct SJIn
	{
		std::vector<double> positions;
	};
	SJIn jin{ .positions = {51.0, 52.0, 53.0} };

	struct SJOut {
		std::vector<std::string> ids;
	};
	SJOut jout;
	auto r = http->PostJsonJBody(jout, "points", jin);
	REQUIRE(r == 200);
	CHECK(jout.ids == std::vector<std::string>{ "1", "2", "3" });
}

TEST_CASE("HttpTest:AsyncPostJson") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("POST", "/scene");
	httpMock->SetResponseFunctionWithData(requestKey, [](const std::string& data) {
		if (data.find("displayName") != std::string::npos
			&& data.find("A new dawn") != std::string::npos)
			return HTTPMock::Response2(200, "{ \"id\": \"def\" }");
		else
			return HTTPMock::Response2(505, "unexpected data");
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	struct SJIn
	{
		std::string displayName;
	};
	SJIn jin{ .displayName = "A new dawn" };

	struct SJout_Post
	{
		std::string id;
	};

	std::atomic_bool taskFinished = false;

	auto dataOut = Tools::MakeSharedLockableData<SJout_Post>();
	http->AsyncPostJson<SJout_Post>(dataOut,
		[&taskFinished](long httpCode, Tools::TSharedLockableData<SJout_Post> const& joutPtr)
	{
		REQUIRE(joutPtr);
		CHECK(httpCode == 200);
		auto jOut = joutPtr->GetRAutoLock();
		CHECK(jOut->id == "def");
		taskFinished = true;
	},
		"scene", Json::ToString(jin));
	REQUIRE(WaitForAsyncTask(taskFinished, 10));
}

TEST_CASE("HttpTest:PatchJsonJBody") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);

	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("PATCH", "/points");
	httpMock->SetResponseFunctionWithData(requestKey, [](const std::string& data) {
		if (data == "{\"id\":\"1\",\"position\":61.0}")
			return HTTPMock::Response2(200, "{ \"ids\": [\"1\"] }");
		else
			return HTTPMock::Response2(505, "unexpected points");
	});

	struct SJIn
	{
		std::string id;
		double position = 0.0;
	};
	SJIn jin{ .id = "1", .position = 61.0 };

	struct SJOut {
		std::vector<std::string> ids;
	};
	SJOut jout;
	auto r = http->PatchJsonJBody(jout, "points", jin);
	REQUIRE(r == 200);
	CHECK(jout.ids == std::vector<std::string>{ "1" });
}

TEST_CASE("HttpTest:AsyncPatchJson") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("PATCH", "/scene");
	httpMock->SetResponseFunctionWithData(requestKey, [](const std::string& data) {
		if (data.find("displayName") != std::string::npos
			&& data.find("Displacement") != std::string::npos
			&& data.find("id") != std::string::npos
			&& data.find("def") != std::string::npos)
			return HTTPMock::Response2(200, "{ \"id\": \"def\" }");
		else
			return HTTPMock::Response2(505, "unexpected data");
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	struct SJIn
	{
		std::string id;
		std::string displayName;
	};
	SJIn jin{ .id = "def", .displayName = "Displacement" };

	struct SJout_Patch
	{
		std::string id;
	};

	std::atomic_bool taskFinished = false;

	auto dataOut = Tools::MakeSharedLockableData<SJout_Patch>();
	http->AsyncPatchJson<SJout_Patch>(dataOut,
		[&taskFinished](long httpCode, Tools::TSharedLockableData<SJout_Patch> const& joutPtr)
	{
		REQUIRE(joutPtr);
		CHECK(httpCode == 200);
		auto jOut = joutPtr->GetRAutoLock();
		CHECK(jOut->id == "def");
		taskFinished = true;
	},
		"scene", Json::ToString(jin));
	REQUIRE(WaitForAsyncTask(taskFinished, 10));
}

TEST_CASE("HttpTest:DeleteJsonJBody") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);

	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	auto requestKey = std::pair("DELETE", "/points");
	httpMock->SetResponseFunctionWithData(requestKey, [](const std::string& data) {
		if (data == "{\"id\":\"2\"}")
			return HTTPMock::Response2(204, "");
		else
			return HTTPMock::Response2(505, "unexpected points");
	});

	struct SJIn
	{
		std::string id;
	};
	SJIn jin{ .id = "2" };

	struct SJOut {
	};
	SJOut jout;
	auto r = http->DeleteJsonJBody(jout, "points", jin, {}, false /*bIsExpectingOutput*/);
	REQUIRE(r == 204);
}

TEST_CASE("HttpTest:AsyncDeleteJson") {
	using namespace AdvViz::SDK;
	HTTPMock* httpMock = GetHttpMock();
	REQUIRE(httpMock != nullptr);
	auto requestKey = std::pair("DELETE", "/scene");
	httpMock->SetResponseFunctionWithData(requestKey, [](const std::string& data) {
		if (data.find("id") != std::string::npos
			&& data.find("def") != std::string::npos)
			return HTTPMock::Response2(204, "{ \"id\": \"def\" }");
		else
			return HTTPMock::Response2(505, "unexpected data");
	});
	std::shared_ptr<Http> http(Http::New());
	http->SetBaseUrl(httpMock->GetUrl().c_str());

	struct SJIn
	{
		std::string id;
	};
	SJIn jin{ .id = "def" };

	struct SJOut_Del {};

	std::atomic_bool taskFinished = false;

	auto dataOut = Tools::MakeSharedLockableData<SJOut_Del>();
	http->AsyncDeleteJson<SJOut_Del>(dataOut,
		[&taskFinished](long httpCode, Tools::TSharedLockableData<SJOut_Del> const& joutPtr)
	{
		REQUIRE(joutPtr);
		CHECK(httpCode == 204);
		taskFinished = true;
	},
		"scene",
		Json::ToString(jin),
		{},
		Http::EAsyncCallbackExecutionMode::Default,
		false /*bIsExpectingOutput*/);
	REQUIRE(WaitForAsyncTask(taskFinished, 10));
}


TEST_CASE("HttpTest:DecodeBase64")
{
	using namespace AdvViz::SDK;
	std::shared_ptr<Http> http(Http::New());
	Http::RawData buffer;
	REQUIRE(http->DecodeBase64("TGUgTE9TQyBlc3QgZ3JhbmQh", buffer));
	std::string const bufferStr(buffer.begin(), buffer.end());
	CHECK(bufferStr == "Le LOSC est grand!");
}

TEST_CASE("HttpTest:EncodeForUrl")
{
	using namespace AdvViz::SDK;
	std::shared_ptr<Http> http(Http::New());
	std::string const urlCompliant = http->EncodeForUrl("hello world #2");
	CHECK(urlCompliant == "hello%20world%20%232");
}


// TODO_JDE - move the tests below to where they should go: in a separate ITwinAPITest!
// we could reuse the same tests as in the Unreal plugin, which would require to share the same mock server,
// (typically, we could extract the generic, file based mock server we use for iModel and iTwin manager tests).
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
	using namespace AdvViz::SDK;
	std::shared_ptr<Http> http(Http::New());
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

