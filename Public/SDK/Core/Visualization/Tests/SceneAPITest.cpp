/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneAPITest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../ScenePersistenceAPI.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;


static HTTPMock* GetHttpMock2()
{
	static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	return static_cast<HTTPMock*>(httpMockM.get());
}
bool CompareLink(const AdvViz::SDK::ILink& l1, const AdvViz::SDK::ILink& l2)
{
	if (l1.GetType() != l2.GetType())
	{
		return false;
	}
	if (l1.GetRef() != l2.GetRef())
	{
		return false;
	}
	if ( l1.GetName() != l2.GetName())
	{
		return false;
	}
	if (l1.HasVisibility() != l2.HasVisibility()  || l1.GetVisibility() != l2.GetVisibility())
	{
		return false;
	}
	if (l1.HasQuality() != l2.HasQuality() || l1.GetQuality() != l2.GetQuality())
	{
		return false;
	}
	if (l1.HasTransform() != l2.HasTransform() || l1.GetTransform() != l2.GetTransform())
	{
		return false;
	}
	return true;
}

bool CompareLinks(const std::vector<std::shared_ptr<AdvViz::SDK::ILink>> & ll1, const std::vector<std::shared_ptr<AdvViz::SDK::ILink>>& ll2)
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
			found = CompareLink(*l1, *l2);
			if (found)
				break;
		}
		if (!found)
			return false;
	}
	return true;
}

static void SetDefaultConfig()
{
	Config::SConfig config;
	config.server.server = "http://localhost";
	config.server.port = GetHttpMock2()->getPort();
	config.server.urlapiprefix = "";
	Config::Init(config);
	SetSceneAPIConfig(config);
	Tools::InitLog("log_Test.txt");
	CreateAdvVizLogChannels();
}

bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);

TEST_CASE("SceneAPI"){

	SECTION("ScenePersistence") {
		try {
			static const std::string itwinID = "eaa1a1d1-0e60-4894-92be-c393fba76ca6";
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock2();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);
			std::vector<std::string> objects;
			std::mutex objectsMutex;
			bool bTestMultiPage = false;
			auto respKeyPost = std::pair("POST", "/scenes");
			mock->responseFct_[respKeyPost] = [] {
				std::string s = "\
				{\
					\"scene\" : { \
						\"id\": \"995970f2-bdfb-4d6b-8224-a40e890859fb\", \
						\"displayName\" : \"test auto\",\
						\"iTwinId\" : \"eaa1a1d1-0e60-4894-92be-c393fba76ca6\",\
						\"createdById\" : \"703f290c-58f2-4f61-b2a8-5d62a4c81386\",\
						\"creationTime\" :\"2025-03-04T22:42:37.203Z\",\
						\"lastModified\" : \"2025-03-04T22:42:37.213Z\"\
					}\
				}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyGet = std::pair("GET", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->responseFct_[respKeyGet] = [] {
				std::string s = "\
				{\
					\"scene\" : { \
						\"id\": \"995970f2-bdfb-4d6b-8224-a40e890859fb\", \
						\"displayName\" : \"test auto\",\
						\"iTwinId\" : \"eaa1a1d1-0e60-4894-92be-c393fba76ca6\",\
						\"createdById\" : \"703f290c-58f2-4f61-b2a8-5d62a4c81386\",\
						\"creationTime\" :\"2025-03-04T22:42:37.203Z\",\
						\"lastModified\" : \"2025-03-04T22:42:37.213Z\"\
					}\
				}";
				return HTTPMock::Response2(200, s);
				};
			auto respKeyPATCH = std::pair("PATCH", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->responseFct_[respKeyPATCH] = [] {
				std::string s = "\
				{\
					\"scene\" : { \
						\"id\": \"995970f2-bdfb-4d6b-8224-a40e890859fb\", \
						\"displayName\" : \"test auto\",\
						\"iTwinId\" : \"eaa1a1d1-0e60-4894-92be-c393fba76ca6\",\
						\"createdById\" : \"703f290c-58f2-4f61-b2a8-5d62a4c81386\",\
						\"creationTime\" :\"2025-03-04T22:42:37.203Z\",\
						\"lastModified\" : \"2025-03-04T22:42:37.213Z\"\
					}\
				}";
				return HTTPMock::Response2(200, s);
				};
			auto respKeyGetObj = std::pair("GET", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->responseFctWithArgs_[respKeyGetObj] = [&objects, &objectsMutex, &bTestMultiPage]
				(const std::vector<HTTPMock::UrlArg>& urlArguments)
			{
				std::unique_lock<std::mutex> lock(objectsMutex);
				REQUIRE(!objects.empty());
				std::string objectss;

				size_t toSkip = 0;
				for (auto const& arg : urlArguments)
				{
					if (arg.hasValue && arg.key == "$skip")
					{
						toSkip = std::stoull(arg.value);
						break;
					}
				}

				if (bTestMultiPage)
				{
					REQUIRE(objects.size() > 6);
					// Split the result in 2 pages
					size_t iEnd = std::min(toSkip + 6, objects.size());
					for (size_t i(toSkip); i < iEnd; ++i)
					{
						objectss += objects[i];
						objectss += ",";
					}
				}
				else
				{
					REQUIRE(toSkip == 0);
					for (auto a : objects)
					{
						objectss += a;
						objectss += ",";
					}
				}
				// replace last coma
				objectss[objectss.size() - 1] = ' ';

				auto const buildLinks = [=]() -> std::string
				{
					REQUIRE(GetDefaultHttp().get() != nullptr);
					auto const baseUrl = GetDefaultHttp()->GetBaseUrlStr();
					if (bTestMultiPage)
					{
						if (toSkip == 0)
						{
							// 1st page
							return "\"self\": { \"href\": \"" + baseUrl + "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects?iTwinId=eaa1a1d1-0e60-4894-92be-c393fba76ca6&$skip=0&$top=6\" }, " \
								   "\"next\": { \"href\": \"" + baseUrl + "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects?iTwinId=eaa1a1d1-0e60-4894-92be-c393fba76ca6&$skip=6&$top=6\" }";
						}
						else
						{
							// 2nd page
							return "\"prev\": { \"href\": \"" + baseUrl + "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects?iTwinId=eaa1a1d1-0e60-4894-92be-c393fba76ca6&$skip=0&$top=6\" }, " \
								   "\"self\": { \"href\": \"" + baseUrl + "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects?iTwinId=eaa1a1d1-0e60-4894-92be-c393fba76ca6&$skip=6&$top=6\" }";
						}
					}
					else
					{
						return "\"self\": { \"href\": \"" + baseUrl + "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects?iTwinId=eaa1a1d1-0e60-4894-92be-c393fba76ca6&$skip=0&$top=100\" }";
					}
				};

				const std::string s = "{" \
					"\"objects\" : [" +objectss +"], " \
					"\"_links\" : {" + buildLinks() + "}" \
				" }";
				return HTTPMock::Response2(200, s);
			};

			auto respKeyPostObj = std::pair("POST", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->responseFctWithData_[respKeyPostObj] = [&objects, &objectsMutex]  (const std::string& data){
				std::unique_lock<std::mutex> lock(objectsMutex);
				size_t ss = objects.size();
				auto id = "995970f2-bdfb-4d6b-8224-" + std::to_string(ss);
				std::string obj = "{ \"id\": \"" + id + "\",";
				if (data.starts_with("{\"objects\":[{"))
				{
					obj += data.substr(13,data.length()- 15);
				}
				else
				{ 
					obj += data.substr(1);
				}
				objects.push_back(obj);
				std::string s = "\
				{\
					\"objects\" :[ { \
						\"id\": \""+ id +"\"\
					}\
				]}";
				return HTTPMock::Response2(200, s);
				};
			auto respKeyDelete = std::pair("DELETE", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->responseFct_[respKeyDelete] = [] {
				return HTTPMock::Response2(204, "");
				};

			std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
			std::atomic_bool asyncCreateSceneDone = false;
			scene->AsyncCreate("test auto", itwinID,
				[scene, &asyncCreateSceneDone](bool bSuccess) {
					REQUIRE(bSuccess);
					asyncCreateSceneDone = true;
				}
			);

			// Make sure the scene is actually created before editing it.
			REQUIRE(WaitForAsyncTask(asyncCreateSceneDone, 10));
			REQUIRE(scene->GetId() == "995970f2-bdfb-4d6b-8224-a40e890859fb");

			{
				auto nulink = scene->MakeLink();
				nulink->SetType("decoration");
				nulink->SetRef("TestDecorationIDofsize24");
				scene->AddLink(nulink);
			}
			{
				auto nulink = scene->MakeLink();
				nulink->SetType("DecorationScene");
				nulink->SetRef("Tes2DecorationIDofsize24");
				scene->AddLink(nulink);
			}
			{
				auto nulink = scene->MakeLink();
				nulink->SetType("iModel");
				nulink->SetRef("096001d3-3d4b-4f9d-a530-995016cbfc97");
				nulink->SetQuality(0.02);
				nulink->SetVisibility(false);
				dmat4x3 mat = { 0.1,1,0,0,1.0,2.3,1.2,0,1.0,74,5,12 };
				nulink->SetTransform(mat);
				scene->AddLink(nulink);
			}
			{
				auto nulink = scene->MakeLink();
				nulink->SetType("camera");
				nulink->SetRef("Main Camera");
				//nulink->SetQuality(0.02);
				//nulink->SetVisibility(false);
				dmat4x3 mat = { 0.1,1,1.56,0,1.0,2.3,1.2,0.13,1.0,74,5,12.52 };
				nulink->SetTransform(mat);
				scene->AddLink(nulink);
			}
			{
				auto nulink = scene->MakeLink();
				nulink->SetType("timeline");
				scene->AddLink(nulink);
			}

			std::atomic_bool asyncSaveSceneDone = false;
			scene->AsyncSave([&asyncSaveSceneDone](bool bSuccess)
			{
				REQUIRE(bSuccess);
				asyncSaveSceneDone = true;
			});

			// Make sure the scene is saved before getting it.
			REQUIRE(WaitForAsyncTask(asyncSaveSceneDone, 10));

			// Run the tests in multi-page and single page mode:
			for (int pageMode = 0; pageMode <2 ; ++pageMode)
			{
				// Reload scene from server
				std::shared_ptr<ScenePersistenceAPI> scene2(ScenePersistenceAPI::New());
				scene2->Get(itwinID, scene->GetId());
				REQUIRE(scene2->GetId() == scene->GetId());
				CHECK(CompareLinks(scene->GetLinks(), scene2->GetLinks()));
				CHECK(scene->GetAtmosphere() == scene2->GetAtmosphere());
				CHECK(scene->GetSceneSettings() == scene2->GetSceneSettings());

				// Test asynchronous load as well
				std::shared_ptr<ScenePersistenceAPI> scene3(ScenePersistenceAPI::New());
				std::atomic_bool asyncGetSceneDone = false;
				scene3->AsyncGet(itwinID, scene->GetId(),
					[&asyncGetSceneDone, scene3, scene](AdvViz::expected<void, std::string> const& exp)
				{
					REQUIRE(exp);
					REQUIRE(scene3->GetId() == scene->GetId());
					CHECK(CompareLinks(scene->GetLinks(), scene3->GetLinks()));
					CHECK(scene->GetAtmosphere() == scene3->GetAtmosphere());
					CHECK(scene->GetSceneSettings() == scene3->GetSceneSettings());
					asyncGetSceneDone = true;
				});
				REQUIRE(WaitForAsyncTask(asyncGetSceneDone, 10));

				// Toggle multi-page mode
				{
					std::unique_lock<std::mutex> lock(objectsMutex);
					bTestMultiPage = !bTestMultiPage;
				}
			}

			// delete decoration on server
			scene->Delete();
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}
