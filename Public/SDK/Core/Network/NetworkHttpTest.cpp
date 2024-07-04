/*--------------------------------------------------------------------------------------+
|
|     $Source: NetworkHttpTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Network.h"
#include <catch2/catch_all.hpp>

//TEST_CASE(HttpTest, Put) {
//	auto http = SDK::Core::Http::New();
//	http->SetBaseUrl("http://127.0.0.1:5984");
//	http->SetBasicAuth("loloDb", "ydbOi589");
//	auto r = http->Put("my_database", "");
//	REQUIRE(r.first, 200);
//	REQUIRE(r.second, "{\"error\":\"file_exists\",\"reason\":\"The database could not be created, the file already exists.\"}\n");
//}


TEST_CASE("HttpTest:GetJsonStr") {
	auto http = SDK::Core::Http::New();
	http->SetBaseUrl("http://httpbin.org");
	auto r = http->GetJson("json", "");
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
	auto http = SDK::Core::Http::New();
	http->SetBaseUrl("http://httpbin.org");
	S s;
	auto r = http->GetJson(s, "json", "");
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

