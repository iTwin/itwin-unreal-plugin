/*--------------------------------------------------------------------------------------+
|
|     $Source: VisualizationTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Visualization.h"

#include "MaterialPersistence.h"
#include <Core/ITwinAPI/ITwinMaterial.h>

#include <filesystem>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>

using namespace SDK::Core;

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
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers)
	{
		if (method == "POST" && matchesURL(url, "/advviz/v1/decorations")) {
			std::string s = "{\"data\":{\"gcs\":{\"center\":[0,0,0], \"wkt\":\"WGS84\"}, \"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\", \"name\":\"test auto\"}, \"id\":\"66c476ed1129763cf5485826\"}";
			return Response(200, s);
		}
		if (method == "GET" && matchesURL(url, "/advviz/v1/decorations")) {
			std::string s = "{\"name\":\"test auto\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"gcs\":{\"wkt\":\"WGS84\",\"center\":[0,0,0]},\"id\":\"66c476ed1129763cf5485826\"}";
			return Response(200, s);
		}
		if (method == "DELETE" && matchesURL(url, "/advviz/v1/decorations")) {
			return Response(200, "{\"id\":\"66c476ed1129763cf5485826\"");
		}
		// Return "URI not found" for the undefined methods
		return Response(404, "Not Found");
	}

	/// Return true if \p url starts with \p str.
	bool matchesURL(const std::string& url, const std::string& str) const {
		return url.substr(0, str.size()) == str;
	}

};

const HTTPMock* const GetHttpMock()
{
	static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	return static_cast<HTTPMock*>(httpMockM.get());
}


TEST_CASE("Visualization:Config")
{
	using namespace SDK::Core;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = SDK::Core::Config::LoadFromFile(filePath);
	REQUIRE(config.server.server== "plop");
	REQUIRE(config.server.port == 2345);
	REQUIRE(config.server.urlapiprefix == "api/v1");

	Config::Init(config);
	REQUIRE(GetDefaultHttp().get() != nullptr);
}

void SetDefaultConfig()
{
	Config::SConfig config;
	config.server.server = "http://localhost";
	config.server.port = GetHttpMock()->getPort();
	config.server.urlapiprefix = "/advviz/v1/decorations";
	Config::Init(config);

	Tools::InitLog("log_Test.txt");
	Tools::CreateLogChannel("ITwinDecoration", Tools::Level::info);
}

TEST_CASE("Visualization"){

	SECTION("Decoration") {
		try {
			SetDefaultConfig();
			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto decoration = IDecoration::New();
			decoration->Create("test auto", "", "");
			REQUIRE(decoration->GetId() != "");

			// create a decoration copy by fetching previous decoration from server
			auto decoration2 = IDecoration::New();
			decoration2->Get(decoration->GetId(), "");
			REQUIRE(decoration2->GetId() == decoration->GetId());

			// delete decoration on server
			decoration->Delete();
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}


class ExtendedDecoration : public Decoration
{
public:
	int Fct() { return 1234; }
	virtual const std::string& GetId() { 
		static std::string s("test"); 
		return s; 
	}
};

TEST_CASE("Visualization:ExtendedDecoration") {
	try {
		SetDefaultConfig();

		IDecoration::SetNewFct([]() {
			IDecoration* p(static_cast<IDecoration*>(new ExtendedDecoration));
			return p;
			});
		std::shared_ptr<IDecoration> pDecoration(IDecoration::New());
		CHECK(pDecoration.get() != nullptr);
		std::shared_ptr<ExtendedDecoration> pExt = std::dynamic_pointer_cast<ExtendedDecoration>(pDecoration);
		CHECK(pExt.get() != nullptr);
		CHECK(pExt->Fct() == 1234);
		CHECK(pExt->GetId() == "test");
	}
	catch (std::string& error)
	{
		FAIL("Error: " << error);
	}
}


TEST_CASE("Visualization:MaterialPersistence")
{
	MaterialPersistenceManager matIOMngr;
	const std::string iModelID("test_imodel_id");
	uint64_t matID = 0x1981;

	{
		KeyValueStringMap keyValueMap;
		bool const bGetNonExisting = matIOMngr.GetMaterialAsKeyValueMap(iModelID, matID, keyValueMap);
		CHECK(bGetNonExisting == false);
		CHECK(keyValueMap.empty());
	}

	// Set empty material definition

	auto const testSetAndGetMat = [&](ITwinMaterial const& material)
	{
		// Store test material in a new slot.
		++matID;
		matIOMngr.SetMaterialSettings(iModelID, matID, material);

		KeyValueStringMap keyValueMap;
		REQUIRE(matIOMngr.GetMaterialAsKeyValueMap(iModelID, matID, keyValueMap));
		CHECK(!keyValueMap.empty());

		// Duplicate slot using key-value map, and then check the value is identical.
		++matID;
		CHECK(matIOMngr.SetMaterialFromKeyValueMap(iModelID, matID, keyValueMap));
		ITwinMaterial materialClone;
		CHECK(matIOMngr.GetMaterialSettings(iModelID, matID, materialClone));
		CHECK(materialClone == material);
	};

	{
		ITwinMaterial const defaultMat;
		testSetAndGetMat(defaultMat);
	}
	{
		ITwinMaterial mat;
		mat.kind = EMaterialKind::Glass;
		testSetAndGetMat(mat);
	}
	{
		ITwinMaterial mat;
		mat.SetChannelIntensity(EChannelType::Roughness, 0.41);
		testSetAndGetMat(mat);
	}
	{
		ITwinMaterial mat;
		mat.SetChannelIntensityMap(EChannelType::Metallic,
			ITwinChannelMap{ .texture = "toto.png", .eSource = ETextureSource::Decoration });
		testSetAndGetMat(mat);
	}
	{
		ITwinMaterial mat;
		mat.SetChannelColor(EChannelType::Color, { 1., 0.5, 0.5, 1. });
		testSetAndGetMat(mat);
	}
	{
		ITwinMaterial mat;
		mat.uvTransform.offset = { 0.5, 0.5 };
		mat.uvTransform.scale = { 2., 3. };
		mat.uvTransform.rotation = 3.14;
		testSetAndGetMat(mat);
	}
	{
		ITwinMaterial mat;
		mat.SetChannelIntensity(EChannelType::AmbientOcclusion, 0.36);
		mat.SetChannelIntensityMap(EChannelType::AmbientOcclusion,
			ITwinChannelMap{ .texture = "ao.png", .eSource = ETextureSource::Decoration });
		mat.SetChannelIntensity(EChannelType::Normal, 0.75);
		mat.SetChannelColorMap(EChannelType::Normal,
			ITwinChannelMap{ .texture = "normal.png", .eSource = ETextureSource::Decoration });
		mat.SetChannelColorMap(EChannelType::Color,
			ITwinChannelMap{ .texture = "albedo.png", .eSource = ETextureSource::Decoration });
		mat.SetChannelColor(EChannelType::Color, { 1., 0.9, 0.8, 1. });
		mat.uvTransform.offset = { 0.4, 0.5 };
		mat.uvTransform.scale = { 2.1, 3.5 };
		mat.uvTransform.rotation = 3.1415;
		testSetAndGetMat(mat);
	}
}
