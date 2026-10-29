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
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>

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
			mock->SetResponseFunctionWithData(respKeyPost, [&objects, &objectsMutex](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(objectsMutex);

				std::string s = "{\"ids\" : [\"id1\"]}";
				std::string obj = "{ \"id\": \"id1\",";
				obj += data.substr(17, data.length() - 19);
				objects.push_back(obj);
				return HTTPMock::Response2(200, s);
			});

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/deid/annotations");
			mock->SetResponseFunction(respKeyGet, [&objects, &objectsMutex]
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
			});

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/deid/annotations");
			mock->SetResponseFunction(respKeyDelete, [] {
				return HTTPMock::Response2(200, "{\"id\":\"id1\"");
				});
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

// ---------------------------------------------------------------------------------------------
// Additional coverage: RemoveAnnotation / RestoreAnnotation / HasAnnotationToSave.
// ---------------------------------------------------------------------------------------------

namespace
{
	const std::string kAnnotDecoId = "deid2";
	const std::string kAnnotUrlPath = "/advviz/v1/decorations/" + kAnnotDecoId + "/annotations";

	AdvViz::SDK::AnnotationPtr MakeAnnotation(std::string const& name, std::string const& text)
	{
		auto* annot = new Annotation();
		annot->position = { 1.0, 2.0, 3.0 };
		annot->text = text;
		annot->name = name;
		annot->colorTheme = "ct";
		annot->displayMode = "dp";
		return Tools::MakeSharedLockableDataPtr<Annotation>(annot);
	}

	bool AnnotationShouldSave(AdvViz::SDK::AnnotationPtr const& annotPtr)
	{
		auto annot = annotPtr->GetRAutoLock();
		return annot->ShouldSave();
	}

	bool AnnotationHasDBId(AdvViz::SDK::AnnotationPtr const& annotPtr)
	{
		auto annot = annotPtr->GetRAutoLock();
		return annot->HasDBIdentifier();
	}

	bool ContainsAnnotation(std::vector<AdvViz::SDK::AnnotationPtr> const& annots,
		AdvViz::SDK::AnnotationPtr const& annot)
	{
		return std::find(annots.begin(), annots.end(), annot) != annots.end();
	}

	//! Saves the manager content and waits for completion.
	void SaveAnnotations(std::shared_ptr<IAnnotationsManager> const& manager)
	{
		std::atomic_bool taskFinished = false;
		manager->AsyncSaveDataOnServer(kAnnotDecoId, [&taskFinished](bool bSuccess)
		{
			REQUIRE(bSuccess);
			taskFinished = true;
		});
		REQUIRE(WaitForAsyncTask(taskFinished, 10));
	}
}

TEST_CASE("Annotation:RemoveRestoreAndSaveState")
{
	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);
	REQUIRE(GetDefaultHttp().get() != nullptr);

	// One id is returned per POSTed annotation (the POST body contains the whole vector).
	std::mutex serverMutex;
	size_t nextServerId = 0;
	std::vector<std::string> deletedIds;

	mock->SetResponseFunctionWithData(std::pair("POST", kAnnotUrlPath),
		[&serverMutex, &nextServerId](const std::string& data)
	{
		std::unique_lock<std::mutex> lock(serverMutex);
		// Count the posted annotations to return as many ids (TAsyncSaveItems requires an
		// exact match between the number of new items and the number of returned ids).
		size_t nbPosted = 0;
		for (size_t pos = data.find("\"text\""); pos != std::string::npos;
			 pos = data.find("\"text\"", pos + 1))
		{
			++nbPosted;
		}
		if (nbPosted == 0)
			nbPosted = 1;
		std::string ids;
		for (size_t i = 0; i < nbPosted; ++i)
		{
			if (i > 0)
				ids += ",";
			ids += "\"annotId" + std::to_string(nextServerId++) + "\"";
		}
		return HTTPMock::Response2(200, "{\"ids\":[" + ids + "]}");
	});

	mock->SetResponseFunctionWithData(std::pair("PUT", kAnnotUrlPath),
		[](const std::string& data)
	{
		size_t nbUpdated = 0;
		for (size_t pos = data.find("\"text\""); pos != std::string::npos;
			 pos = data.find("\"text\"", pos + 1))
		{
			++nbUpdated;
		}
		return HTTPMock::Response2(200, "{\"numUpdated\":" + std::to_string(nbUpdated) + "}");
	});

	mock->SetResponseFunctionWithData(std::pair("DELETE", kAnnotUrlPath),
		[&serverMutex, &deletedIds](const std::string& data)
	{
		std::unique_lock<std::mutex> lock(serverMutex);
		deletedIds.push_back(data);
		return HTTPMock::Response2(204, "");
	});

	std::shared_ptr<IAnnotationsManager> manager(IAnnotationsManager::New());

	SECTION("HasAnnotationToSave on a new annotation")
	{
		// Nothing to save on an empty manager.
		CHECK(!manager->HasAnnotationToSave());

		auto annot = MakeAnnotation("name1", "text1");
		// AddAnnotation marks the annotation as to-be-saved (it has no DB identifier yet).
		manager->AddAnnotation(annot);
		CHECK(AnnotationShouldSave(annot));
		CHECK(manager->HasAnnotationToSave());

		SaveAnnotations(manager);

		// Once saved, the annotation has a DB identifier and is up-to-date.
		CHECK(AnnotationHasDBId(annot));
		CHECK(!AnnotationShouldSave(annot));
		CHECK(!manager->HasAnnotationToSave());

		// Modifying it makes it dirty again.
		{
			auto a = annot->GetAutoLock();
			a->text = "modified";
			a->SetShouldSave(true);
		}
		CHECK(manager->HasAnnotationToSave());

		SaveAnnotations(manager);
		CHECK(!manager->HasAnnotationToSave());
	}

	SECTION("RemoveAnnotation of an annotation saved on the server")
	{
		auto annot = MakeAnnotation("name1", "text1");
		manager->AddAnnotation(annot);
		SaveAnnotations(manager);
		REQUIRE(AnnotationHasDBId(annot));
		REQUIRE(manager->GetAnnotations().size() == 1);

		manager->RemoveAnnotation(annot);

		// The annotation is no longer part of the live annotations...
		CHECK(manager->GetAnnotations().empty());
		CHECK(!ContainsAnnotation(manager->GetAnnotations(), annot));
		// ... but it must be deleted on the server, hence there is something to save.
		CHECK(manager->HasAnnotationToSave());

		SaveAnnotations(manager);

		// The deletion request has been sent, and the removed list is now empty.
		{
			std::unique_lock<std::mutex> lock(serverMutex);
			REQUIRE(deletedIds.size() == 1);
			CHECK(deletedIds[0].find("\"ids\"") != std::string::npos);
		}
		CHECK(!manager->HasAnnotationToSave());
		CHECK(manager->GetAnnotations().empty());
	}

	SECTION("RemoveAnnotation of a never-saved annotation")
	{
		auto annot = MakeAnnotation("name1", "text1");
		manager->AddAnnotation(annot);
		REQUIRE(manager->HasAnnotationToSave());

		// The annotation was never saved on the server: removing it leaves nothing to do.
		manager->RemoveAnnotation(annot);
		CHECK(manager->GetAnnotations().empty());
		CHECK(!AnnotationHasDBId(annot));
		CHECK(!manager->HasAnnotationToSave());

		SaveAnnotations(manager);

		// No DELETE request was needed for an annotation that never reached the server.
		{
			std::unique_lock<std::mutex> lock(serverMutex);
			CHECK(deletedIds.empty());
		}
	}

	SECTION("RemoveAnnotation keeps the other annotations")
	{
		auto annot1 = MakeAnnotation("name1", "text1");
		auto annot2 = MakeAnnotation("name2", "text2");
		manager->AddAnnotation(annot1);
		manager->AddAnnotation(annot2);
		SaveAnnotations(manager);
		REQUIRE(manager->GetAnnotations().size() == 2);

		manager->RemoveAnnotation(annot1);

		auto const annots = manager->GetAnnotations();
		REQUIRE(annots.size() == 1);
		CHECK(!ContainsAnnotation(annots, annot1));
		CHECK(ContainsAnnotation(annots, annot2));
	}

	SECTION("RemoveAnnotation ignores a null annotation")
	{
		auto annot = MakeAnnotation("name1", "text1");
		manager->AddAnnotation(annot);
		REQUIRE(manager->GetAnnotations().size() == 1);

		manager->RemoveAnnotation({});
		CHECK(manager->GetAnnotations().size() == 1);
	}

	SECTION("RestoreAnnotation")
	{
		auto annot = MakeAnnotation("name1", "text1");
		manager->AddAnnotation(annot);
		SaveAnnotations(manager);
		REQUIRE(AnnotationHasDBId(annot));

		manager->RemoveAnnotation(annot);
		REQUIRE(manager->GetAnnotations().empty());
		REQUIRE(manager->HasAnnotationToSave());

		manager->RestoreAnnotation(annot);

		// The annotation is back in the live list, and no longer pending deletion.
		auto const annots = manager->GetAnnotations();
		REQUIRE(annots.size() == 1);
		CHECK(ContainsAnnotation(annots, annot));
		// It is already saved on the server and was not modified: nothing to save.
		CHECK(!AnnotationShouldSave(annot));
		CHECK(!manager->HasAnnotationToSave());

		SaveAnnotations(manager);
		{
			std::unique_lock<std::mutex> lock(serverMutex);
			CHECK(deletedIds.empty()); // the restored annotation was not deleted
		}
		CHECK(manager->GetAnnotations().size() == 1);
	}

	SECTION("RestoreAnnotation of a never-saved annotation")
	{
		auto annot = MakeAnnotation("name1", "text1");
		manager->AddAnnotation(annot);
		REQUIRE(AnnotationShouldSave(annot));

		manager->RemoveAnnotation(annot);
		REQUIRE(manager->GetAnnotations().empty());

		manager->RestoreAnnotation(annot);
		REQUIRE(manager->GetAnnotations().size() == 1);
		// It still needs to be created on the server.
		CHECK(AnnotationShouldSave(annot));
		CHECK(manager->HasAnnotationToSave());

		SaveAnnotations(manager);
		CHECK(AnnotationHasDBId(annot));
		CHECK(!manager->HasAnnotationToSave());
	}

	SECTION("RestoreAnnotation ignores a null annotation")
	{
		manager->RestoreAnnotation({});
		CHECK(manager->GetAnnotations().empty());
		CHECK(!manager->HasAnnotationToSave());
	}
}
