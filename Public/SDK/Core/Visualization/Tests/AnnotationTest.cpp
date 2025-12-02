/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../AnnotationsManager.h"
#include <filesystem>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;

bool CompareAnnotation(const AdvViz::SDK::Annotation& l1, const AdvViz::SDK::Annotation& l2)
{
	return l1.colorTheme == l2.colorTheme 
		&& l1.displayMode == l2.displayMode
		&& l1.position == l2.position
		&& l1.text == l2.text
		&& l1.name == l2.name
		&& l1.id.GetDBIdentifier() == l2.id.GetDBIdentifier();
}
bool CompareAnnotations(const std::vector<std::shared_ptr<AdvViz::SDK::Annotation>>& ll1, const std::vector<std::shared_ptr<AdvViz::SDK::Annotation>>& ll2)
{
	if (ll1.size() != ll2.size())
	{
		return false;
	}
	for (auto l1 : ll1)
	{
		bool found = false;
		for (auto l2 : ll2)
		{
			found = CompareAnnotation(*l1, *l2);
			if (found)
				break;
		}
		if (!found)
			return false;
	}
	return true;
}
void SetDefaultConfig();
TEST_CASE("Annotation"){

	SECTION("manager") {
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);
			std::vector<std::string> objects;

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations/deid/annotations");
			mock->responseFctWithData_[respKeyPost] = [&objects](const std::string& data) {

				std::string s = "{\"ids\" : [\"id1\"]}";
				std::string obj = "{ \"id\": \"id1\",";
				obj += data.substr(17, data.length() - 19);
				objects.push_back(obj);
				return HTTPMock::Response2(200, s);
				};

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/deid/annotations");
			mock->responseFct_[respKeyGet] = [&objects] {

				std::string s = "{\"total_rows\":" + std::to_string(objects.size()) + ",\"rows\":[";
				for (auto obj : objects)
				{
					s += obj;
					s += ",";
				}
				if (objects.size() > 0)
					s[s.size() - 1] = ' ';
				s += "],\"_links\":{}}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/deid/annotations");
			mock->responseFct_[respKeyDelete] = [] {
				return HTTPMock::Response2(200, "{\"id\":\"id1\"");
				};

			auto annotationManager = IAnnotationsManager::New();
			auto ann1 = std::make_shared<Annotation>();
			ann1->position = {-1.0,1.0,2.0};
			ann1->text = "1";
			ann1->name = "name1";
			ann1->colorTheme = "ct1";
			ann1->displayMode = "dp1";
			annotationManager->AddAnnotation(ann1);
			annotationManager->SaveDataOnServerDS("deid");

			auto ann2 = std::make_shared<Annotation>();
			ann2->position = { -1.0,1.0,2.0 };
			ann2->text = "2";
			ann2->name = "name2";
			ann2->colorTheme = "ct2";
			ann2->displayMode = "dp2";
			annotationManager->AddAnnotation(ann2);
			annotationManager->SaveDataOnServerDS("deid");

			// create annotations copy by fetching previous annotations from server
			auto annotationManager2 = IAnnotationsManager::New();
			annotationManager2->LoadDataFromServerDS("deid");

			REQUIRE(CompareAnnotations(annotationManager->GetAnnotations(), annotationManager2->GetAnnotations()));

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}

