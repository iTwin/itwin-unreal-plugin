/*--------------------------------------------------------------------------------------+
|
|     $Source: NetworkHttpTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

import SDK.Core.Network;

#include <gtest/gtest.h>
#include <rfl/json.hpp> //note: we should not include this but compiler error VS 2022 17.9.2: static function 'void yyjson_mut_doc_set_root(yyjson_mut_doc *,yyjson_mut_val *)' declared but not defined

//TEST(HttpTest, Put) {
//	auto http = SDK::Core::Http::New();
//	http->SetBaseUrl("http://127.0.0.1:5984");
//	http->SetBasicAuth("loloDb", "ydbOi589");
//	auto r = http->Put("my_database", "");
//	EXPECT_EQ(r.first, 200);
//	EXPECT_EQ(r.second, "{\"error\":\"file_exists\",\"reason\":\"The database could not be created, the file already exists.\"}\n");
//}


TEST(HttpTest, GetJsonStr) {
	auto http = SDK::Core::Http::New();
	http->SetBaseUrl("http://httpbin.org");
	auto r = http->GetJson("json", "");
	EXPECT_EQ(r.first, 200);
	EXPECT_EQ(r.second, "{\n  \"slideshow\": {\n    \"author\": \"Yours Truly\", \n    \"date\": \"date of publication\", \n    \"slides\": [\n      {\n        \"title\": \"Wake up to WonderWidgets!\", \n        \"type\": \"all\"\n      }, \n      {\n        \"items\": [\n          \"Why <em>WonderWidgets</em> are great\", \n          \"Who <em>buys</em> WonderWidgets\"\n        ], \n        \"title\": \"Overview\", \n        \"type\": \"all\"\n      }\n    ], \n    \"title\": \"Sample Slide Show\"\n  }\n}\n");
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
TEST(HttpTest, GetJsonOBJ) {
	auto http = SDK::Core::Http::New();
	http->SetBaseUrl("http://httpbin.org");
	S s;
	auto r = http->GetJson(s, "json", "");
	EXPECT_EQ(r, 200);
	EXPECT_EQ(s.slideshow.title, "Sample Slide Show");
	EXPECT_EQ(s.slideshow.author, "Yours Truly");
	EXPECT_EQ(s.slideshow.date, "date of publication");
	EXPECT_EQ(s.slideshow.slides.size(), 2);
	EXPECT_EQ(s.slideshow.slides[0].type, "all");
	EXPECT_EQ(s.slideshow.slides[0].title, "Wake up to WonderWidgets!");
	EXPECT_EQ(s.slideshow.slides[0].items.has_value(), false);
	EXPECT_EQ(s.slideshow.slides[1].type, "all");
	EXPECT_EQ(s.slideshow.slides[1].title, "Overview");
	EXPECT_EQ(s.slideshow.slides[1].items->size(), 2);
	EXPECT_EQ(s.slideshow.slides[1].items->at(0), "Why <em>WonderWidgets</em> are great");
	EXPECT_EQ(s.slideshow.slides[1].items->at(1), "Who <em>buys</em> WonderWidgets");
}

