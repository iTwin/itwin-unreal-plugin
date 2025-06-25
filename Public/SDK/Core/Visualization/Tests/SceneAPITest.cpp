/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneAPITest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../ScenePersistenceAPI.h"
#include <filesystem>

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

TEST_CASE("SceneAPI"){

	SECTION("ScenePersistence") {
		try {
			static const std::string itwinID = "eaa1a1d1-0e60-4894-92be-c393fba76ca6";
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock2();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);
			std::vector<std::string> objects;
			auto respKeyPost = std::pair("POST", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes");
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

			auto respKeyGet = std::pair("GET", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
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
			auto respKeyPATCH = std::pair("PATCH", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
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
			auto respKeyGetObj = std::pair("GET", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->responseFct_[respKeyGetObj] = [&objects] {
				std::string objectss;
				for (auto a : objects)
				{
					objectss += a;
					objectss += ",";
				}
				objectss[objectss.size() - 1] = ' ';
				std::string s = "\
				{\
					\"objects\" : ["+objectss+"] \
				}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyPostObj = std::pair("POST", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->responseFctWithData_[respKeyPostObj] = [&objects]  (const std::string& data){

				size_t ss = objects.size();
				auto id = "995970f2-bdfb-4d6b-8224-" + std::to_string(ss);
				std::string obj = "{ \"id\": \"" + id + "\",";
				obj += data.substr(1);
				objects.push_back(obj);
				std::string s = "\
				{\
					\"object\" : { \
						\"id\": \""+ id +"\"\
					}\
				}";
				return HTTPMock::Response2(200, s);
				};
			auto respKeyDelete = std::pair("DELETE", "/iTwins/eaa1a1d1-0e60-4894-92be-c393fba76ca6/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->responseFct_[respKeyDelete] = [] {
				return HTTPMock::Response2(204, "");
				};

			auto scene = ScenePersistenceAPI::New();
			scene->Create("test auto", itwinID);
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
			scene->Save();

			// create a decoration copy by fetching previous decoration from server
			auto scene2 = ScenePersistenceAPI::New();
			scene2->Get(itwinID,scene->GetId());
			REQUIRE(scene2->GetId() == scene->GetId());
			REQUIRE(CompareLinks(scene->GetLinks(), scene2->GetLinks()));
			REQUIRE(scene->GetAtmosphere() == scene2->GetAtmosphere());
			REQUIRE(scene->GetSceneSettings() == scene2->GetSceneSettings());


			// delete decoration on server
			scene->Delete();
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}
