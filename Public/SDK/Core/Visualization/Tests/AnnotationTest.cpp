/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../AnnotationsManager.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;

bool CompareAnnotation(const AdvViz::SDK::AnnotationPtr& l1Ptr, const AdvViz::SDK::AnnotationPtr& l2Ptr)
{
	auto l1 = l1Ptr->GetRAutoLock();
	auto l2 = l2Ptr->GetRAutoLock();
	return l1->colorTheme == l2->colorTheme 
		&& l1->displayMode == l2->displayMode
		&& l1->position == l2->position
		&& l1->text == l2->text
		&& l1->name == l2->name
		&& l1->GetDBIdentifier() == l2->GetDBIdentifier();
}
bool CompareAnnotations(const std::vector<AdvViz::SDK::AnnotationPtr>& ll1, const std::vector<AdvViz::SDK::AnnotationPtr>& ll2)
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
			found = CompareAnnotation(l1, l2);
			if (found)
				break;
		}
		if (!found)
			return false;
	}
	return true;
}
bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);
void SetDefaultConfig();
TEST_CASE("Annotation"){

	SECTION("manager") {
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);
			REQUIRE(GetDefaultHttp().get() != nullptr);

			std::vector<std::string> objects;
			std::mutex objectsMutex;

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations/deid/annotations");
			mock->responseFctWithData_[respKeyPost] = [&objects, &objectsMutex](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(objectsMutex);

				std::string s = "{\"ids\" : [\"id1\"]}";
				std::string obj = "{ \"id\": \"id1\",";
				obj += data.substr(17, data.length() - 19);
				objects.push_back(obj);
				return HTTPMock::Response2(200, s);
			};

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/deid/annotations");
			mock->responseFct_[respKeyGet] = [&objects, &objectsMutex]
			{
				std::unique_lock<std::mutex> lock(objectsMutex);

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
			std::atomic_bool taskFinished = false;

			std::shared_ptr<IAnnotationsManager> annotationManager(IAnnotationsManager::New());
			auto ann1 = new Annotation();
			ann1->position = {-1.0,1.0,2.0};
			ann1->text = "1";
			ann1->name = "name1";
			ann1->colorTheme = "ct1";
			ann1->displayMode = "dp1";
			auto ann1Ptr = Tools::MakeSharedLockableDataPtr<Annotation>(ann1);
			annotationManager->AddAnnotation(ann1Ptr);
			annotationManager->AsyncSaveDataOnServer("deid", [&taskFinished](bool bSuccess)
			{
				REQUIRE(bSuccess);
				taskFinished = true;
			});
			REQUIRE(WaitForAsyncTask(taskFinished, 10));

			auto ann2 = new Annotation();
			ann2->position = { -1.0,1.0,2.0 };
			ann2->text = "2";
			ann2->name = "name2";
			ann2->colorTheme = "ct2";
			ann2->displayMode = "dp2";
			auto ann2Ptr = Tools::MakeSharedLockableDataPtr<Annotation>(ann2);
			annotationManager->AddAnnotation(ann2Ptr);
			taskFinished = false;
			annotationManager->AsyncSaveDataOnServer("deid", [&taskFinished](bool bSuccess)
			{
				REQUIRE(bSuccess);
				taskFinished = true;
			});
			REQUIRE(WaitForAsyncTask(taskFinished, 10));

			// create annotations copy by fetching previous annotations from server
			std::shared_ptr<IAnnotationsManager> annotationManager2(IAnnotationsManager::New());
			annotationManager2->LoadDataFromServer("deid");
			CHECK(CompareAnnotations(annotationManager->GetAnnotations(), annotationManager2->GetAnnotations()));

			// Test asynchronous Load as well.
			std::shared_ptr<IAnnotationsManager> annotationManager3(IAnnotationsManager::New());
			std::atomic_bool asyncLoadFinished = false;
			annotationManager3->AsyncLoadDataFromServer("deid",
				[](AnnotationPtr&) {},
				[&asyncLoadFinished, annotationManager, annotationManager3](AdvViz::expected<void, std::string> const& exp)
			{
				REQUIRE(exp);
				CHECK(CompareAnnotations(annotationManager->GetAnnotations(), annotationManager3->GetAnnotations()));
				asyncLoadFinished = true;
			});
			REQUIRE(WaitForAsyncTask(asyncLoadFinished, 10));

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}

