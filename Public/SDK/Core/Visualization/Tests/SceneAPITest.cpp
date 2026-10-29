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
#include <fstream>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>
#include <Core/Tools/StringParsing.h>
#include <Core/Json/Json.h>
#include <map>
#include <rfl/Generic.hpp>

using namespace AdvViz::SDK;

TEST_CASE("SceneAPI camera metadata")
{
	class CameraScene : public ScenePersistenceAPI
	{
	public:
		using ScenePersistenceAPI::GenerateBody;
	};
	struct CameraObject
	{
		std::optional<std::string> kind;
		std::string displayName;
		std::map<std::string, rfl::Generic> data;
	};
	struct CameraObjects
	{
		std::vector<CameraObject> objects;
	};
	auto checkCamera = [](CameraObject const& camera, std::string const& ref)
	{
		CHECK(camera.displayName == ref);
		REQUIRE(camera.data.size() == 8);
		for (auto const& key : { "up", "direction", "position", "isOrthographic",
			"aspectRatio", "far", "near", "ecefTransform" })
		{
			REQUIRE(camera.data.find(key) != camera.data.end());
		}
		CHECK(camera.data.find("name") == camera.data.end());
		CHECK(camera.data.find("displayOrder") == camera.data.end());
		auto transform = rfl::json::read<std::array<double, 16>>(
			rfl::json::write(camera.data.at("ecefTransform")));
		REQUIRE(transform);
		CHECK(*transform == std::array<double, 16>{
			1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 30, 0, 0, 0, 1 });
	};
	CameraScene scene;
	auto link = std::dynamic_pointer_cast<LinkAPI>(scene.MakeLink());
	REQUIRE(link);
	link->SetType("camera");
	link->SetTransform({ 1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 30 });
	SECTION("Encoded camera reference preserves order, identity and label on POST and PATCH")
	{
		std::string const createdRef = "Viewpoint;2;8e748cef-4443-41c3-9a58-8bf04d780756;North side";
		link->SetRef(createdRef);
		auto const postBody = scene.GenerateBody(link, false);
		CHECK(postBody.find("\"displayOrder\"") == std::string::npos);
		auto created = rfl::json::read<CameraObjects>(postBody);
		REQUIRE(created);
		REQUIRE(created->objects.size() == 1);
		CHECK(created->objects[0].kind == "View3d");
		checkCamera(created->objects[0], createdRef);
		link->OnSaved();
		link->SetShouldSave(false);
		std::string const patchedRef = "Viewpoint;1;8e748cef-4443-41c3-9a58-8bf04d780756;South side";
		link->SetRef(patchedRef);
		CHECK(link->ShouldSave());
		auto const patchBody = scene.GenerateBody(link, true);
		CHECK(patchBody.find("\"displayOrder\"") == std::string::npos);
		auto patched = rfl::json::read<CameraObject>(patchBody);
		REQUIRE(patched);
		checkCamera(*patched, patchedRef);
	}
	SECTION("Reserved camera names remain unchanged")
	{
		for (auto const& name : { "Home Camera", "Main Camera" })
		{
			link->SetRef(name);
			auto const postBody = scene.GenerateBody(link, false);
			CHECK(postBody.find("\"displayOrder\"") == std::string::npos);
			auto created = rfl::json::read<CameraObjects>(postBody);
			REQUIRE(created);
			REQUIRE(created->objects.size() == 1);
			CHECK(created->objects[0].kind == "View3d");
			checkCamera(created->objects[0], name);
			auto const patchBody = scene.GenerateBody(link, true);
			CHECK(patchBody.find("\"displayOrder\"") == std::string::npos);
			auto patched = rfl::json::read<CameraObject>(patchBody);
			REQUIRE(patched);
			checkCamera(*patched, name);
		}
	}
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

	auto const linkAPI1 = AdvViz::SDK::Tools::DynamicCast<LinkAPI const>(&l1);
	auto const linkAPI2 = AdvViz::SDK::Tools::DynamicCast<LinkAPI const>(&l2);
	if (linkAPI1 && linkAPI2)
	{
		if (linkAPI1->IsRepositoryResource() != linkAPI2->IsRepositoryResource())
		{
			return false;
		}
		if (linkAPI1->IsRepositoryResource() && linkAPI2->IsRepositoryResource())
		{
			auto repoInfo1 = linkAPI1->GetRepositoryResourceInfo();
			auto repoInfo2 = linkAPI2->GetRepositoryResourceInfo();
			BE_ASSERT(repoInfo1 && repoInfo2);
			if (repoInfo1->repositoryId != repoInfo2->repositoryId
				|| repoInfo1->strClass != repoInfo2->strClass)
			{
				return false;
			}
		}
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

static bool CompareDouble3(double3 const& v1, double3 const& v2)
{
	for (size_t i = 0; i < 3; ++i)
	{
		if (v1[i] != Catch::Approx(v2[i]))
			return false;
	}
	return true;
}

static bool CompareDouble4(double4 const& v1, double4 const& v2)
{
	for (size_t i = 0; i < 4; ++i)
	{
		if (v1[i] != Catch::Approx(v2[i]))
			return false;
	}
	return true;
}

static bool CompareTransform(std::optional<std::array<double, 16>> const& t1,
	std::optional<std::array<double, 16>> const& t2)
{
	if (t1.has_value() != t2.has_value())
		return false;
	if (!t1)
		return true;
	for (size_t i = 0; i < 16; ++i)
	{
		if ((*t1)[i] != Catch::Approx((*t2)[i]))
			return false;
	}
	return true;
}

static bool CompareCutout(Cutout const& c1, Cutout const& c2)
{
	if (c1.cutoutType != c2.cutoutType
		|| c1.enabled != c2.enabled
		|| c1.inverse != c2.inverse
		|| c1.appliesTo != c2.appliesTo)
	{
		return false;
	}
	if (c1.cutoutData.index() != c2.cutoutData.index())
		return false;

	if (std::holds_alternative<CutoutBox>(c1.cutoutData))
	{
		auto const& b1 = std::get<CutoutBox>(c1.cutoutData);
		auto const& b2 = std::get<CutoutBox>(c2.cutoutData);
		return CompareDouble3(b1.center, b2.center)
			&& CompareDouble3(b1.halfExtents, b2.halfExtents)
			&& CompareDouble4(b1.rotation, b2.rotation)
			&& CompareTransform(b1.transformFromClip, b2.transformFromClip);
	}
	if (std::holds_alternative<CutoutPlane>(c1.cutoutData))
	{
		auto const& p1 = std::get<CutoutPlane>(c1.cutoutData);
		auto const& p2 = std::get<CutoutPlane>(c2.cutoutData);
		return CompareDouble3(p1.normal, p2.normal)
			&& p1.distance == Catch::Approx(p2.distance);
	}
	if (std::holds_alternative<CutoutPolygonSet>(c1.cutoutData))
	{
		auto const& s1 = std::get<CutoutPolygonSet>(c1.cutoutData);
		auto const& s2 = std::get<CutoutPolygonSet>(c2.cutoutData);
		if (s1.polygons.size() != s2.polygons.size()
			|| !CompareTransform(s1.transformFromClip, s2.transformFromClip))
		{
			return false;
		}
		for (size_t i = 0; i < s1.polygons.size(); ++i)
		{
			auto const& pos1 = s1.polygons[i].positions;
			auto const& pos2 = s2.polygons[i].positions;
			if (pos1.size() != pos2.size())
				return false;
			for (size_t j = 0; j < pos1.size(); ++j)
			{
				if (!CompareDouble3(pos1[j], pos2[j]))
					return false;
			}
		}
		return true;
	}
	return false;
}

//! Compares cutouts by content: after a reload, the RefIDs are regenerated and thus differ from
//! those of the initial session (see RefID::FromDBIdentifier).
static bool CompareCutouts(std::map<RefID, Cutout> const& m1, std::map<RefID, Cutout> const& m2)
{
	if (m1.size() != m2.size())
		return false;
	for (auto const& [refId1, cutout1] : m1)
	{
		bool found = false;
		for (auto const& [refId2, cutout2] : m2)
		{
			found = CompareCutout(cutout1, cutout2);
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
	config.server.port = GetHttpMock()->getPort();
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
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);
			std::vector<std::string> objects;
			std::mutex objectsMutex;
			bool bTestMultiPage = false;
			auto respKeyPost = std::pair("POST", "/scenes");
			mock->SetResponseFunction(respKeyPost, [] {
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
			});

			auto respKeyGet = std::pair("GET", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->SetResponseFunction(respKeyGet, [] {
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
			});
			auto respKeyPATCH = std::pair("PATCH", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->SetResponseFunction(respKeyPATCH, [] {
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
			});
			auto respKeyGetObj = std::pair("GET", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->SetResponseFunctionWithArgs(respKeyGetObj, [&objects, &objectsMutex, &bTestMultiPage]
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
						toSkip = static_cast<size_t>(
							AdvViz::SDK::Tools::ParseUInt64Or(arg.value, 0 /*fallback*/));
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
			});

			auto respKeyPostObj = std::pair("POST", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb/objects");
			mock->SetResponseFunctionWithData(respKeyPostObj, [&objects, &objectsMutex]  (const std::string& data){
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
			});
			auto respKeyDelete = std::pair("DELETE", "/scenes/995970f2-bdfb-4d6b-8224-a40e890859fb");
			mock->SetResponseFunction(respKeyDelete, [] {
				return HTTPMock::Response2(204, "");
			});

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
			{
				auto viewpoint = scene->MakeLink();
				viewpoint->SetType("camera");
					viewpoint->SetRef("Viewpoint;3;8e748cef-4443-41c3-9a58-8bf04d780756;North side");
				viewpoint->SetTransform({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
				scene->AddLink(viewpoint);
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

TEST_CASE("SceneAPI:Cutouts")
{
	static const std::string itwinID = "eaa1a1d1-0e60-4894-92be-c393fba76ca6";
	static const std::string sceneID = "995970f2-bdfb-4d6b-8224-a40e890859fb";

	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);
	REQUIRE(GetDefaultHttp().get() != nullptr);

	// --- mocks (same scheme as the SceneAPI test: posted objects are echoed back on GET) ---
	std::vector<std::string> objects;
	std::mutex objectsMutex;

	const std::string sceneJson = "\
	{\
		\"scene\" : { \
			\"id\": \"" + sceneID + "\", \
			\"displayName\" : \"test cutouts\",\
			\"iTwinId\" : \"" + itwinID + "\",\
			\"createdById\" : \"703f290c-58f2-4f61-b2a8-5d62a4c81386\",\
			\"creationTime\" :\"2025-03-04T22:42:37.203Z\",\
			\"lastModified\" : \"2025-03-04T22:42:37.213Z\"\
		}\
	}";

	auto respKeyPost = std::pair("POST", "/scenes");
	mock->SetResponseFunction(respKeyPost, [sceneJson] {
		return HTTPMock::Response2(200, std::string(sceneJson));
	});
	auto respKeyGet = std::pair("GET", "/scenes/" + sceneID);
	mock->SetResponseFunction(respKeyGet, [sceneJson] {
		return HTTPMock::Response2(200, std::string(sceneJson));
	});
	auto respKeyPatch = std::pair("PATCH", "/scenes/" + sceneID);
	mock->SetResponseFunction(respKeyPatch, [sceneJson] {
		return HTTPMock::Response2(200, std::string(sceneJson));
	});

	auto respKeyGetObj = std::pair("GET", "/scenes/" + sceneID + "/objects");
	mock->SetResponseFunctionWithArgs(respKeyGetObj, [&objects, &objectsMutex]
		(const std::vector<HTTPMock::UrlArg>& /*urlArguments*/)
	{
		std::unique_lock<std::mutex> lock(objectsMutex);
		REQUIRE(!objects.empty());
		std::string objectss;
		for (auto const& a : objects)
		{
			objectss += a;
			objectss += ",";
		}
		objectss[objectss.size() - 1] = ' ';
		REQUIRE(GetDefaultHttp().get() != nullptr);
		auto const baseUrl = GetDefaultHttp()->GetBaseUrlStr();
		const std::string s = "{"
			"\"objects\" : [" + objectss + "], "
			"\"_links\" : { \"self\": { \"href\": \"" + baseUrl + "/scenes/" + sceneID +
				"/objects?iTwinId=" + itwinID + "&$skip=0&$top=100\" } }"
			" }";
		return HTTPMock::Response2(200, s);
	});

	auto respKeyPostObj = std::pair("POST", "/scenes/" + sceneID + "/objects");
	mock->SetResponseFunctionWithData(respKeyPostObj, [&objects, &objectsMutex](const std::string& data) {
		std::unique_lock<std::mutex> lock(objectsMutex);
		size_t ss = objects.size();
		auto id = "995970f2-bdfb-4d6b-8224-" + std::to_string(ss);
		std::string obj = "{ \"id\": \"" + id + "\",";
		if (data.starts_with("{\"objects\":[{"))
			obj += data.substr(13, data.length() - 15);
		else
			obj += data.substr(1);
		objects.push_back(obj);
		return HTTPMock::Response2(200, "{ \"objects\" :[ { \"id\": \"" + id + "\" } ]}");
	});

	// The save of a removed link sends DELETE scenes/<id>/objects/<objectId>, and the save of a
	// modified link sends PATCH on the same url. HTTPMock matches urls exactly, so one handler must
	// be registered per object id the POST handler above can generate
	// (ids are "995970f2-bdfb-4d6b-8224-<index>").
	const size_t kMaxObjects = 16;
	for (size_t i = 0; i < kMaxObjects; ++i)
	{
		const std::string objId = "995970f2-bdfb-4d6b-8224-" + std::to_string(i);
		const std::string objUrl = "/scenes/" + sceneID + "/objects/" + objId;

		mock->SetResponseFunction(std::pair("DELETE", objUrl), [&objects, &objectsMutex, objId] {
			std::unique_lock<std::mutex> lock(objectsMutex);
			const std::string idTag = "\"id\": \"" + objId + "\"";
			std::erase_if(objects, [&idTag](std::string const& obj) {
				return obj.find(idTag) != std::string::npos;
			});
			return HTTPMock::Response2(204, "");
		});

		mock->SetResponseFunctionWithData(std::pair("PATCH", objUrl),
			[&objects, &objectsMutex, objId](const std::string& data) {
			std::unique_lock<std::mutex> lock(objectsMutex);
			const std::string idTag = "\"id\": \"" + objId + "\"";
			// Body of the patch, without the enclosing "objects" array if any.
			const std::string patched = data.starts_with("{\"objects\":[{")
				? data.substr(13, data.length() - 15)
				: data.substr(1);
			for (auto& stored : objects)
			{
				if (stored.find(idTag) == std::string::npos)
					continue;
				// A PATCH is a *partial* update: the server keeps the fields which are not
				// re-sent (in particular "version" and "kind", only sent on creation).
				// Those are needed to identify the object kind on reload (eg. "Cutout").
				std::string preserved;
				for (const char* field : { "\"version\"", "\"kind\"" })
				{
					const size_t pos = stored.find(field);
					if (pos == std::string::npos)
						continue;
					const size_t end = stored.find(',', pos);
					if (end == std::string::npos)
						continue;
					preserved += stored.substr(pos, end - pos + 1);
				}
				stored = "{ \"id\": \"" + objId + "\"," + preserved + patched;
				break;
			}
			return HTTPMock::Response2(200, "{ \"object\" : { \"id\": \"" + objId + "\" } }");
		});
	}

	auto respKeyDelete = std::pair("DELETE", "/scenes/" + sceneID);
	mock->SetResponseFunction(respKeyDelete, [] {
		return HTTPMock::Response2(204, "");
	});

	// --- scene creation ---
	std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
	std::atomic_bool asyncCreateSceneDone = false;
	scene->AsyncCreate("test cutouts", itwinID, [&asyncCreateSceneDone](bool bSuccess) {
		REQUIRE(bSuccess);
		asyncCreateSceneDone = true;
	});
	REQUIRE(WaitForAsyncTask(asyncCreateSceneDone, 10));
	REQUIRE(scene->GetId() == sceneID);

	CHECK(!scene->HasCutouts());
	CHECK(scene->GetCutouts().empty());

	// --- cutout definitions ---
	RefID const boxRef, planeRef, polyRef;

	Cutout boxCutout;
	boxCutout.cutoutType = ECutoutType::Box;
	boxCutout.appliesTo = { "096001d3-3d4b-4f9d-a530-995016cbfc97" };
	boxCutout.enabled = true;
	boxCutout.inverse = true;
	{
		CutoutBox box;
		box.center = { 1., 2., 3. };
		box.halfExtents = { 10., 20., 30. };
		box.rotation = { 0., 0., 0.7071067811865476, 0.7071067811865476 };
		boxCutout.cutoutData = box;
	}

	Cutout planeCutout;
	planeCutout.cutoutType = ECutoutType::Plane;
	planeCutout.enabled = false;
	planeCutout.inverse = false; // see note below: inversion is baked into the plane data
	{
		CutoutPlane plane;
		plane.normal = { 0., 0., 1. };
		plane.distance = 12.5;
		planeCutout.cutoutData = plane;
	}

	Cutout polyCutout;
	polyCutout.cutoutType = ECutoutType::Polygons;
	polyCutout.appliesTo = { "TestDecorationIDofsize24" };
	{
		CutoutPolygonSet polygonSet;
		CutoutPolygon polygon;
		polygon.positions = { { 0., 0., 0. }, { 1., 0., 0. }, { 1., 1., 0. } };
		polygonSet.polygons.push_back(polygon);
		polyCutout.cutoutData = polygonSet;
	}

	SECTION("SetCutout / FindCutoutType / GetCutouts")
	{
		scene->SetCutout(boxRef, boxCutout);
		CHECK(scene->HasCutouts());
		CHECK(scene->FindCutoutType(boxRef) == ECutoutType::Box);

		scene->SetCutout(planeRef, planeCutout);
		scene->SetCutout(polyRef, polyCutout);
		CHECK(scene->FindCutoutType(planeRef) == ECutoutType::Plane);
		CHECK(scene->FindCutoutType(polyRef) == ECutoutType::Polygons);
		// unknown reference
		CHECK(scene->FindCutoutType(RefID()) == ECutoutType::ENUM_END);

		auto const cutouts = scene->GetCutouts();
		REQUIRE(cutouts.size() == 3);
		REQUIRE(cutouts.contains(boxRef));
		CHECK(CompareCutout(cutouts.at(boxRef), boxCutout));
		CHECK(CompareCutout(cutouts.at(planeRef), planeCutout));
		CHECK(CompareCutout(cutouts.at(polyRef), polyCutout));

		SECTION("type filtering")
		{
			auto const boxes = scene->GetCutouts({ ECutoutType::Box });
			REQUIRE(boxes.size() == 1);
			CHECK(boxes.contains(boxRef));

			auto const planesAndPolys = scene->GetCutouts({ ECutoutType::Plane, ECutoutType::Polygons });
			REQUIRE(planesAndPolys.size() == 2);
			CHECK(planesAndPolys.contains(planeRef));
			CHECK(planesAndPolys.contains(polyRef));

			// an empty filter means "no filtering"
			CHECK(scene->GetCutouts({}).size() == 3);
		}
	}

	SECTION("Update methods")
	{
		scene->SetCutout(boxRef, boxCutout);
		scene->SetCutout(planeRef, planeCutout);
		scene->SetCutout(polyRef, polyCutout);

		SECTION("UpdateCutoutBaseInfo")
		{
			CutoutBase newBase;
			newBase.cutoutType = ECutoutType::Box;
			newBase.appliesTo = { "Tes2DecorationIDofsize24" };
			newBase.enabled = false;
			newBase.inverse = false;
			scene->UpdateCutoutBaseInfo(boxRef, newBase);

			auto const cutouts = scene->GetCutouts();
			auto const& updated = cutouts.at(boxRef);
			CHECK(updated.appliesTo == newBase.appliesTo);
			CHECK(updated.enabled == false);
			CHECK(updated.inverse == false);
			// the geometry is untouched
			CHECK(CompareDouble3(std::get<CutoutBox>(updated.cutoutData).center,
				std::get<CutoutBox>(boxCutout.cutoutData).center));
		}

		SECTION("UpdateCutoutBox")
		{
			CutoutBox newBox;
			newBox.center = { -5., -6., -7. };
			newBox.halfExtents = { 2., 2., 2. };
			newBox.rotation = { 0., 0., 0., 1. }; // identity
			newBox.transformFromClip = std::array<double, 16>{
				1., 0., 0., 0.,  0., 1., 0., 0.,  0., 0., 1., 0.,  4., 5., 6., 1. };
			scene->UpdateCutoutBox(boxRef, newBox);

			auto const cutouts = scene->GetCutouts();
			auto const& box = std::get<CutoutBox>(cutouts.at(boxRef).cutoutData);
			CHECK(CompareDouble3(box.center, newBox.center));
			CHECK(CompareDouble3(box.halfExtents, newBox.halfExtents));
			CHECK(CompareDouble4(box.rotation, newBox.rotation));
			CHECK(CompareTransform(box.transformFromClip, newBox.transformFromClip));
		}

		SECTION("UpdateCutoutPlane")
		{
			CutoutPlane newPlane;
			newPlane.normal = { 0., 1., 0. };
			newPlane.distance = 4.;
			scene->UpdateCutoutPlane(planeRef, newPlane, false /*inverse*/);

			auto cutouts = scene->GetCutouts();
			auto plane = std::get<CutoutPlane>(cutouts.at(planeRef).cutoutData);
			CHECK(CompareDouble3(plane.normal, newPlane.normal));
			CHECK(plane.distance == Catch::Approx(newPlane.distance));

			// inversion is baked into the plane data (no 'inverse' flag in the scene API)
			scene->UpdateCutoutPlane(planeRef, newPlane, true /*inverse*/);
			cutouts = scene->GetCutouts();
			plane = std::get<CutoutPlane>(cutouts.at(planeRef).cutoutData);
			CHECK(CompareDouble3(plane.normal, double3{ 0., -1., 0. }));
			CHECK(plane.distance == Catch::Approx(-newPlane.distance));
			CHECK(cutouts.at(planeRef).inverse == false);
		}

		SECTION("UpdateCutoutPolygonPoint / UpdateCutoutPolygonTransform")
		{
			double3 const newPoint = { 9., 9., 9. };
			scene->UpdateCutoutPolygonPoint(polyRef, 0 /*polygonIndex*/, 1 /*pointIndex*/, newPoint);
			std::array<double, 16> const transform = {
				2., 0., 0., 0.,  0., 2., 0., 0.,  0., 0., 2., 0.,  0., 0., 0., 1. };
			scene->UpdateCutoutPolygonTransform(polyRef, transform);

			auto const cutouts = scene->GetCutouts();
			auto const& polygonSet = std::get<CutoutPolygonSet>(cutouts.at(polyRef).cutoutData);
			REQUIRE(polygonSet.polygons.size() == 1);
			REQUIRE(polygonSet.polygons[0].positions.size() == 3);
			CHECK(CompareDouble3(polygonSet.polygons[0].positions[1], newPoint));
			// other points are untouched
			CHECK(CompareDouble3(polygonSet.polygons[0].positions[0], double3{ 0., 0., 0. }));
			CHECK(CompareTransform(polygonSet.transformFromClip, transform));
		}
	}

	SECTION("RemoveCutout")
	{
		scene->SetCutout(boxRef, boxCutout);
		scene->SetCutout(planeRef, planeCutout);
		REQUIRE(scene->GetCutouts().size() == 2);

		scene->RemoveCutout(boxRef);

		auto const cutouts = scene->GetCutouts();
		CHECK(cutouts.size() == 1);
		CHECK(!cutouts.contains(boxRef));
		CHECK(cutouts.contains(planeRef));
	}

	SECTION("Save and reload")
	{
		scene->SetCutout(boxRef, boxCutout);
		scene->SetCutout(planeRef, planeCutout);
		scene->SetCutout(polyRef, polyCutout);
		REQUIRE(scene->ShouldSave());

		std::atomic_bool asyncSaveSceneDone = false;
		scene->AsyncSave([&asyncSaveSceneDone](bool bSuccess) {
			REQUIRE(bSuccess);
			asyncSaveSceneDone = true;
		});
		REQUIRE(WaitForAsyncTask(asyncSaveSceneDone, 10));

		auto const savedCutouts = scene->GetCutouts();
		REQUIRE(savedCutouts.size() == 3);

		SECTION("synchronous reload")
		{
			std::shared_ptr<ScenePersistenceAPI> scene2(ScenePersistenceAPI::New());
			REQUIRE(scene2->Get(itwinID, scene->GetId()));
			REQUIRE(scene2->GetId() == scene->GetId());

			CHECK(scene2->HasCutouts());
			auto const reloaded = scene2->GetCutouts();
			CHECK(reloaded.size() == 3);
			CHECK(CompareCutouts(savedCutouts, reloaded));
			// per-type reload
			CHECK(scene2->GetCutouts({ ECutoutType::Box }).size() == 1);
			CHECK(scene2->GetCutouts({ ECutoutType::Plane }).size() == 1);
			CHECK(scene2->GetCutouts({ ECutoutType::Polygons }).size() == 1);
		}

		SECTION("asynchronous reload")
		{
			std::shared_ptr<ScenePersistenceAPI> scene3(ScenePersistenceAPI::New());
			std::atomic_bool asyncGetSceneDone = false;
			scene3->AsyncGet(itwinID, scene->GetId(),
				[&asyncGetSceneDone, scene3, &savedCutouts](AdvViz::expected<void, std::string> const& exp)
			{
				REQUIRE(exp);
				CHECK(scene3->HasCutouts());
				CHECK(CompareCutouts(savedCutouts, scene3->GetCutouts()));
				asyncGetSceneDone = true;
			});
			REQUIRE(WaitForAsyncTask(asyncGetSceneDone, 10));
		}

		SECTION("removal is persisted")
		{
			scene->RemoveCutout(boxRef);
			std::atomic_bool asyncSave2Done = false;
			scene->AsyncSave([&asyncSave2Done](bool bSuccess) {
				REQUIRE(bSuccess);
				asyncSave2Done = true;
			});
			REQUIRE(WaitForAsyncTask(asyncSave2Done, 10));

			std::shared_ptr<ScenePersistenceAPI> scene2(ScenePersistenceAPI::New());
			REQUIRE(scene2->Get(itwinID, scene->GetId()));
			CHECK(scene2->GetCutouts().size() == 2);
			CHECK(scene2->GetCutouts({ ECutoutType::Box }).empty());
		}
	}

	scene->Delete();
}

// ---------------------------------------------------------------------------------------------
// Additional coverage: link name/GCS, scene listing, links refresh, HDRI helpers,
// timeline (pre-links + "clip" bodies) and PATCH bodies (GenerateBody with forPatch == true).
// ---------------------------------------------------------------------------------------------

namespace
{
	const std::string kITwinId = "eaa1a1d1-0e60-4894-92be-c393fba76ca6";
	const std::string kSceneId = "995970f2-bdfb-4d6b-8224-a40e890859fb";

	std::string MakeSceneJson(std::string const& displayName)
	{
		return "{ \"scene\": {"
			"\"id\": \"" + kSceneId + "\","
			"\"displayName\": \"" + displayName + "\","
			"\"iTwinId\": \"" + kITwinId + "\","
			"\"createdById\": \"703f290c-58f2-4f61-b2a8-5d62a4c81386\","
			"\"creationTime\": \"2025-03-04T22:42:37.203Z\","
			"\"lastModified\": \"2025-03-04T22:42:37.213Z\" } }";
	}

	//! Mock state shared by the tests below: objects POSTed on the server are echoed back on GET,
	//! and the raw POST/PATCH bodies are recorded so that GenerateBody() can be checked.
	struct SceneObjectsMock
	{
		std::mutex mutex;
		std::vector<std::string> objects;
		std::vector<std::string> postedBodies;
		std::vector<std::string> patchedBodies;

		std::vector<std::string> GetPostedBodies()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return postedBodies;
		}
		std::vector<std::string> GetPatchedBodies()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return patchedBodies;
		}
	};

	//! Registers the whole set of mocks for one scene (creation, load, save, objects CRUD).
	void InitSceneMocks(HTTPMock* mock, SceneObjectsMock& state, std::string const& sceneName)
	{
		const std::string sceneJson = MakeSceneJson(sceneName);

		mock->SetResponseFunction(std::pair("POST", "/scenes"), [sceneJson] {
			return HTTPMock::Response2(200, std::string(sceneJson));
		});
		mock->SetResponseFunction(std::pair("GET", "/scenes/" + kSceneId), [sceneJson] {
			return HTTPMock::Response2(200, std::string(sceneJson));
		});
		mock->SetResponseFunction(std::pair("PATCH", "/scenes/" + kSceneId), [sceneJson] {
			return HTTPMock::Response2(200, std::string(sceneJson));
		});
		mock->SetResponseFunction(std::pair("DELETE", "/scenes/" + kSceneId), [] {
			return HTTPMock::Response2(204, "");
		});

		mock->SetResponseFunctionWithArgs(std::pair("GET", "/scenes/" + kSceneId + "/objects"),
			[&state](const std::vector<HTTPMock::UrlArg>& /*urlArguments*/)
		{
			std::unique_lock<std::mutex> lock(state.mutex);
			std::string objectss;
			for (auto const& a : state.objects)
			{
				objectss += a;
				objectss += ",";
			}
			if (!objectss.empty())
				objectss[objectss.size() - 1] = ' ';
			REQUIRE(GetDefaultHttp().get() != nullptr);
			auto const baseUrl = GetDefaultHttp()->GetBaseUrlStr();
			const std::string s = "{"
				"\"objects\" : [" + objectss + "], "
				"\"_links\" : { \"self\": { \"href\": \"" + baseUrl + "/scenes/" + kSceneId +
					"/objects?iTwinId=" + kITwinId + "&$skip=0&$top=100\" } }"
				" }";
			return HTTPMock::Response2(200, s);
		});

		mock->SetResponseFunctionWithData(std::pair("POST", "/scenes/" + kSceneId + "/objects"),
			[&state](const std::string& data)
		{
			std::unique_lock<std::mutex> lock(state.mutex);
			state.postedBodies.push_back(data);
			const auto id = "995970f2-bdfb-4d6b-8224-" + std::to_string(state.objects.size());
			std::string obj = "{ \"id\": \"" + id + "\",";
			if (data.starts_with("{\"objects\":[{"))
				obj += data.substr(13, data.length() - 15);
			else
				obj += data.substr(1);
			state.objects.push_back(obj);
			return HTTPMock::Response2(200, "{ \"objects\" :[ { \"id\": \"" + id + "\" } ]}");
		});

		// One DELETE/PATCH handler per object id the POST handler above can generate.
		const size_t kMaxObjects = 16;
		for (size_t i = 0; i < kMaxObjects; ++i)
		{
			const std::string objId = "995970f2-bdfb-4d6b-8224-" + std::to_string(i);
			const std::string objUrl = "/scenes/" + kSceneId + "/objects/" + objId;

			mock->SetResponseFunction(std::pair("DELETE", objUrl), [&state, objId] {
				std::unique_lock<std::mutex> lock(state.mutex);
				const std::string idTag = "\"id\": \"" + objId + "\"";
				std::erase_if(state.objects, [&idTag](std::string const& obj) {
					return obj.find(idTag) != std::string::npos;
				});
				return HTTPMock::Response2(204, "");
			});

			mock->SetResponseFunctionWithData(std::pair("PATCH", objUrl),
				[&state, objId](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(state.mutex);
				state.patchedBodies.push_back(data);
				return HTTPMock::Response2(200, "{ \"object\" : { \"id\": \"" + objId + "\" } }");
			});
		}
	}

	std::shared_ptr<ScenePersistenceAPI> CreateMockedScene(std::string const& sceneName)
	{
		std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
		std::atomic_bool done = false;
		scene->AsyncCreate(sceneName, kITwinId, [&done](bool bSuccess) {
			REQUIRE(bSuccess);
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		REQUIRE(scene->GetId() == kSceneId);
		return scene;
	}

	void SaveScene(std::shared_ptr<ScenePersistenceAPI> const& scene)
	{
		// AsyncSave is a no-op (callback never invoked) when there is nothing to save.
		REQUIRE(scene->ShouldSave());
		std::atomic_bool done = false;
		scene->AsyncSave([&done](bool bSuccess) {
			REQUIRE(bSuccess);
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
	}

	bool AnyBodyContains(std::vector<std::string> const& bodies, std::string const& needle)
	{
		for (auto const& b : bodies)
		{
			if (b.find(needle) != std::string::npos)
				return true;
		}
		return false;
	}
}

TEST_CASE("SceneAPI:LinkProperties")
{
	SetDefaultConfig();
	REQUIRE(GetHttpMock() != nullptr);

	std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
	auto link = scene->MakeLink();
	REQUIRE(link);

	SECTION("SetName / HasName")
	{
		CHECK(!link->HasName());
		CHECK(link->GetName().empty());

		link->SetName("My camera clip");
		CHECK(link->HasName());
		CHECK(link->GetName() == "My camera clip");

		// setting the same value again is a no-op, setting a new one updates it
		link->SetName("My camera clip");
		CHECK(link->GetName() == "My camera clip");
		link->SetName("Renamed");
		CHECK(link->GetName() == "Renamed");
	}

	SECTION("SetGCS / HasGCS / GetGCS")
	{
		CHECK(!link->HasGCS());
		// default value when no GCS was set
		auto const defaultGCS = link->GetGCS();
		CHECK(defaultGCS.first.empty());
		CHECK(defaultGCS.second == std::array<float, 3>{ 0.f, 0.f, 0.f });

		const std::string wkt = "PROJCS[\"WGS 84 / UTM zone 31N\"]";
		const std::array<float, 3> center = { 1.5f, -2.5f, 3.f };
		link->SetGCS(wkt, center);

		CHECK(link->HasGCS());
		auto const gcs = link->GetGCS();
		CHECK(gcs.first == wkt);
		CHECK(gcs.second == center);

		// a different center is taken into account as well
		const std::array<float, 3> center2 = { 10.f, 20.f, 30.f };
		link->SetGCS(wkt, center2);
		CHECK(link->GetGCS().second == center2);
	}
}

TEST_CASE("SceneAPI:HDRI")
{
	SetDefaultConfig();
	std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());

	ITwinHDRISettings hdri;
	hdri.hdriName = "studio_small_08";
	hdri.sunPitch = -12.5;
	hdri.sunYaw = 42.25;
	hdri.sunIntensity = 3.75;
	hdri.rotation = 180.;

	const std::string json = scene->ExportHDRIAsJson(hdri);
	CHECK(json.find("\"hdriName\"") != std::string::npos);
	CHECK(json.find("studio_small_08") != std::string::npos);
	CHECK(json.find("\"sunPitch\"") != std::string::npos);
	CHECK(json.find("\"sunYaw\"") != std::string::npos);
	CHECK(json.find("\"sunIntensity\"") != std::string::npos);
	CHECK(json.find("\"rotation\"") != std::string::npos);

	SECTION("ConvertHDRIJsonFileToKeyValueMap")
	{
		std::error_code ec;
		const std::filesystem::path jsonPath =
			std::filesystem::temp_directory_path(ec) / "SceneAPITest_hdri.json";
		{
			std::ofstream ofs(jsonPath);
			REQUIRE(ofs.good());
			ofs << json;
		}

		KeyValueStringMap outMap;
		REQUIRE(scene->ConvertHDRIJsonFileToKeyValueMap(jsonPath, outMap));
		CHECK(outMap["hdriName"] == hdri.hdriName);
		CHECK(outMap["sunPitch"] == std::to_string(hdri.sunPitch));
		CHECK(outMap["sunYaw"] == std::to_string(hdri.sunYaw));
		CHECK(outMap["sunIntensity"] == std::to_string(hdri.sunIntensity));
		CHECK(outMap["rotation"] == std::to_string(hdri.rotation));

		std::filesystem::remove(jsonPath, ec);
	}

	SECTION("ConvertHDRIJsonFileToKeyValueMap fails on an invalid file")
	{
		std::error_code ec;
		const std::filesystem::path badPath =
			std::filesystem::temp_directory_path(ec) / "SceneAPITest_hdri_bad.json";
		{
			std::ofstream ofs(badPath);
			REQUIRE(ofs.good());
			ofs << "this is not json";
		}
		KeyValueStringMap outMap;
		CHECK(!scene->ConvertHDRIJsonFileToKeyValueMap(badPath, outMap));
		CHECK(outMap.empty());

		std::filesystem::remove(badPath, ec);
	}
}

TEST_CASE("SceneAPI:SceneListing")
{
	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);
	REQUIRE(GetDefaultHttp().get() != nullptr);

	// GetITwinScenesAPI / AsyncGetITwinSceneInfosAPI use the Http owned by the Scene API
	// credentials: make sure it points to the mock server.
	ScenePersistenceAPI::SetDefaultHttp(GetDefaultHttp());

	SceneObjectsMock state;
	InitSceneMocks(mock, state, "listed scene");

	// At least one object must exist for the objects endpoint (GetITwinScenesAPI calls Get(),
	// which loads the links).
	state.objects.push_back(
		"{ \"id\": \"995970f2-bdfb-4d6b-8224-0\", \"kind\": \"UnrealAtmosphericStyling\","
		" \"data\": { \"atmosphere\": { \"sunAzimuth\": 0.0, \"sunPitch\": -10.0,"
		" \"heliodonLongitude\": 0.0, \"heliodonLatitude\": 0.0, \"heliodonDate\": \"\","
		" \"weather\": 0.0, \"windOrientation\": 0.0, \"windForce\": 0.0, \"fog\": 0.0,"
		" \"exposure\": 0.0, \"useHeliodon\": true } } }");

	const std::string scenesPayload =
		"{ \"scenes\": [ { \"displayName\": \"listed scene\", \"iTwinId\": \"" + kITwinId + "\","
		" \"id\": \"" + kSceneId + "\" } ],"
		" \"_links\": { \"self\": { \"href\": \"" + GetDefaultHttp()->GetBaseUrlStr() +
			"/scenes?iTwinId=" + kITwinId + "&$skip=0&$top=100\" } } }";

	mock->SetResponseFunction(std::pair("GET", "/scenes"), [scenesPayload] {
		return HTTPMock::Response2(200, std::string(scenesPayload));
	});

	SECTION("GetITwinScenesAPI")
	{
		auto const exp = GetITwinScenesAPI(kITwinId);
		REQUIRE(exp);
		ScenePtrVector const& scenes = *exp;
		REQUIRE(scenes.size() == 1);
		CHECK(scenes[0]->GetId() == kSceneId);
		CHECK(scenes[0]->GetITwinId() == kITwinId);
		CHECK(scenes[0]->GetName() == "listed scene");
		// links have been loaded as well (Get() calls LoadLinks)
		CHECK(!scenes[0]->GetLinks().empty());
	}

	SECTION("GetITwinScenesAPI reports server errors")
	{
		mock->SetResponseFunction(std::pair("GET", "/scenes"), [] {
			return HTTPMock::Response2(500, "{ \"error\": \"server failure\" }");
		});
		auto const exp = GetITwinScenesAPI(kITwinId);
		REQUIRE(!exp);
		CHECK(exp.error().httpCode == 500);
	}

	SECTION("AsyncGetITwinSceneInfosAPI")
	{
		std::atomic_bool done = false;
		SceneInfoVec infos;
		AsyncGetITwinSceneInfosAPI(kITwinId,
			[&done, &infos](AdvViz::expected<SceneInfoVec, HttpError> exp)
		{
			REQUIRE(exp);
			infos = *exp;
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		REQUIRE(infos.size() == 1);
		CHECK(infos[0].id == kSceneId);
		CHECK(infos[0].iTwinId == kITwinId);
		CHECK(infos[0].displayName == "listed scene");
	}

	SECTION("AsyncGetITwinSceneInfosAPI reports server errors")
	{
		mock->SetResponseFunction(std::pair("GET", "/scenes"), [] {
			return HTTPMock::Response2(404, "{ \"error\": \"not found\" }");
		});
		std::atomic_bool done = false;
		std::atomic_bool bErrorReported = false;
		AsyncGetITwinSceneInfosAPI(kITwinId,
			[&done, &bErrorReported](AdvViz::expected<SceneInfoVec, HttpError> exp)
		{
			if (!exp)
				bErrorReported = true;
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(bErrorReported);
	}
}

TEST_CASE("SceneAPI:AsyncRefreshLinks")
{
	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	SceneObjectsMock state;
	InitSceneMocks(mock, state, "refreshed scene");

	SECTION("no ID set")
	{
		std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
		std::atomic_bool done = false;
		std::atomic_bool bErrorReported = false;
		scene->AsyncRefreshLinks([&done, &bErrorReported](AdvViz::expected<void, std::string> const& exp)
		{
			if (!exp)
				bErrorReported = true;
			done = true;
		});
		// this path is synchronous, but we wait anyway to keep the test robust
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(bErrorReported);
	}

	SECTION("reload of the links")
	{
		auto scene = CreateMockedScene("refreshed scene");
		{
			auto nulink = scene->MakeLink();
			nulink->SetType("decoration");
			nulink->SetRef("TestDecorationIDofsize24");
			scene->AddLink(nulink);
		}
		SaveScene(scene);
		const size_t nbLinksAfterSave = scene->GetLinks().size();
		REQUIRE(nbLinksAfterSave > 0);

		auto CountLinksOfType = [&scene](std::string const& type)
		{
			size_t count = 0;
			for (auto const& l : scene->GetLinks())
			{
				if (l->GetType() == type)
					++count;
			}
			return count;
		};
		REQUIRE(CountLinksOfType("timeline") == 0);

		std::atomic_bool done = false;
		std::atomic_bool bSucceeded = false;
		scene->AsyncRefreshLinks([&done, &bSucceeded](AdvViz::expected<void, std::string> const& exp)
		{
			if (exp)
				bSucceeded = true;
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(bSucceeded);

		// Links already known are matched by DB identifier: no duplicate is created for them.
		// But OnLoadLinksFinished() creates the missing "default" links: since the server
		// returned no "Movie" object, a timeline link is added.
		CHECK(CountLinksOfType("timeline") == 1);
		CHECK(scene->GetLinks().size() == nbLinksAfterSave + 1);
		// atmosphere and SceneSettings were already there => not duplicated.
		CHECK(CountLinksOfType("atmosphere") == 1);
		CHECK(CountLinksOfType("SceneSettings") == 1);

		// A second refresh does not add another one (the existing link is detected by type).
		done = false;
		scene->AsyncRefreshLinks([&done](AdvViz::expected<void, std::string> const&) { done = true; });
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(CountLinksOfType("timeline") == 1);
		CHECK(scene->GetLinks().size() == nbLinksAfterSave + 1);

		scene->Delete();
	}

	SECTION("server error is reported")
	{
		auto scene = CreateMockedScene("refreshed scene");
		REQUIRE(!scene->GetId().empty());

		mock->SetResponseFunctionWithArgs(std::pair("GET", "/scenes/" + kSceneId + "/objects"),
			[](const std::vector<HTTPMock::UrlArg>&)
		{
			return HTTPMock::Response2(500, "{ \"error\": \"server failure\" }");
		});

		std::atomic_bool done = false;
		std::atomic_bool bErrorReported = false;
		scene->AsyncRefreshLinks([&done, &bErrorReported](AdvViz::expected<void, std::string> const& exp)
		{
			// N.B. the callback may be invoked more than once by the paged-data helper:
			// latch the error instead of keeping the last result.
			if (!exp)
				bErrorReported = true;
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(bErrorReported);
	}
}

TEST_CASE("SceneAPI:Timeline")
{
	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	SceneObjectsMock state;
	InitSceneMocks(mock, state, "timeline scene");

	auto scene = CreateMockedScene("timeline scene");

	// --- SetTimeline / GetTimeline ---
	std::shared_ptr<ITimeline> timeline(ITimeline::New());
	scene->SetTimeline(timeline);
	REQUIRE(scene->GetTimeline() == timeline);

	// A clip with two camera key-frames: this is what makes GenerateBody() emit a non-empty
	// "CameraAnimation" body (clipp->GetKeyframeCount() > 0).
	auto clip = timeline->AddClip("My clip");
	REQUIRE(clip);
	for (int i = 0; i < 2; ++i)
	{
		ITimelineKeyframe::KeyframeData kd;
		kd.time = 1.0 * i;
		kd.camera = ITimelineKeyframe::CameraData();
		kd.camera->transform = { 1., 0., 0., 10. * i,
								 0., 1., 0., 20. * i,
								 0., 0., 1., 30. * i };
		kd.atmo = ITimelineKeyframe::AtmoData();
		kd.atmo->time = "2025-03-04T12:00:00Z";
		kd.atmo->fog = 0.25f;
		kd.atmo->cloudCoverage = 0.5f;
		auto kfExp = clip->AddKeyframe(kd);
		REQUIRE(kfExp);
	}
	REQUIRE(clip->GetKeyframeCount() == 2);

	// The timeline link is needed as well, so that the clip ids are referenced ("Movie" object).
	{
		auto nulink = scene->MakeLink();
		nulink->SetType("timeline");
		scene->AddLink(nulink);
	}

	SECTION("GeneratePreLinks and GenerateBody for clips (POST)")
	{
		SaveScene(scene);

		auto const posted = state.GetPostedBodies();
		REQUIRE(!posted.empty());
		// POST bodies are wrapped and carry the object kind.
		CHECK(AnyBodyContains(posted, "\"objects\":["));
		CHECK(AnyBodyContains(posted, "\"kind\":\"CameraAnimation\""));
		// GeneratePreLinks names the clip link after the clip itself.
		CHECK(AnyBodyContains(posted, "My clip"));
		// The key-frames have been exported (input/output arrays of the camera animation).
		CHECK(AnyBodyContains(posted, "\"input\":"));
		CHECK(AnyBodyContains(posted, "\"output\":"));
		CHECK(AnyBodyContains(posted, "\"ecefTransform\":"));
		// The "Movie" object references the saved clip.
		CHECK(AnyBodyContains(posted, "\"kind\":\"Movie\""));

		// Once saved, the clip knows its server id and is up-to-date.
		CHECK(clip->HasDBIdentifier());
		CHECK(!clip->ShouldSave());
	}

	SECTION("GenerateBody with forPatch == true")
	{
		// 1st save: all links are new => POST.
		SaveScene(scene);
		REQUIRE(!state.GetPostedBodies().empty());

		// 2nd save: the links now have a DB identifier => PATCH (GenerateBody(link, true)).
		clip->SetName("Renamed clip");
		// The clip should now be marked dirty, wich should invalidate the timeline and then the scene.
		CHECK(clip->ShouldSave());

		SaveScene(scene);

		auto const patched = state.GetPatchedBodies();
		REQUIRE(!patched.empty());
		for (auto const& body : patched)
		{
			// A PATCH body is a partial update: no wrapping array, no version/kind.
			CHECK(body.find("\"objects\":[") == std::string::npos);
			CHECK(body.find("\"version\"") == std::string::npos);
			CHECK(body.find("\"kind\"") == std::string::npos);
			CHECK(body.find("\"data\"") != std::string::npos);
		}
		// The renamed clip is sent through the "clip" PATCH body.
		CHECK(AnyBodyContains(patched, "Renamed clip"));
	}

	SECTION("obsolete clips are deleted")
	{
		SaveScene(scene);
		REQUIRE(clip->HasDBIdentifier());

		REQUIRE(timeline->RemoveClip(0));
		CHECK(timeline->GetClipCount() == 0);
		// Note that removing a clip invalidates the timeline, which itself invalidates the scene.
		CHECK(timeline->ShouldSave());

		SaveScene(scene);

		// The obsolete clip generated a deletion pre-link and was removed from the timeline.
		CHECK(timeline->GetObsoleteClips().empty());
	}

	scene->Delete();
}
