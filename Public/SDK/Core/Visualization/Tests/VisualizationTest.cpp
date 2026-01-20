/*--------------------------------------------------------------------------------------+
|
|     $Source: VisualizationTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include <filesystem>
#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

#include "Core/Tools/Tools.h"
#include "Core/Tools/Internal_mathConv.inl"
#include <numbers>

using namespace AdvViz::SDK;

HTTPMock* GetHttpMock()
{
	static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
	return static_cast<HTTPMock*>(httpMockM.get());
}


TEST_CASE("Visualization:Config")
{
	using namespace AdvViz::SDK;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath);

	{
		std::ofstream f(filePath);
		f << "{\"server\":{\"server\":\"plop\", \"port\":2345, \"urlapiprefix\":\"api/v1\"}}";
	}

	auto config = AdvViz::SDK::Config::LoadFromFile(filePath);
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
	config.server.urlapiprefix = "/advviz/v1";
	Config::Init(config);

	Tools::InitLog("log_Test.txt");
	CreateAdvVizLogChannels();
}

TEST_CASE("Visualization"){

	SECTION("Decoration") {
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations");
			mock->responseFct_[respKeyPost] = [] {
				std::string s = "{\"data\":{\"gcs\":{\"center\":[0,0,0], \"wkt\":\"WGS84\"}, \"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\", \"name\":\"test auto\"}, \"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->responseFct_[respKeyGet] = [] {
				std::string s = "{\"name\":\"test auto\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"gcs\":{\"wkt\":\"WGS84\",\"center\":[0,0,0]},\"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
				};

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->responseFct_[respKeyDelete] = [] {
				return HTTPMock::Response2(200, "{\"id\":\"66c476ed1129763cf5485826\"");
				};

			auto decoration = IDecoration::New();
			decoration->Create("test auto", "");
			REQUIRE(decoration->GetId() != "");

			// create a decoration copy by fetching previous decoration from server
			auto decoration2 = IDecoration::New();
			decoration2->Get(decoration->GetId());
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


TEST_CASE("GCSTransform::WGS84GeodeticToECEF") {
	SECTION("Eiffel Tower") {
		double3 latLonHeightRad = { 48.8584, 2.2945, 79.07 }; // Paris Eiffel Tower coordinates
		// convert degrees to radians
		latLonHeightRad[0] = latLonHeightRad[0] * std::numbers::pi / 180.0;
		latLonHeightRad[1] = latLonHeightRad[1] * std::numbers::pi / 180.0;
		double3 pos = GCSTransform::WGS84GeodeticToECEF(latLonHeightRad);
		CHECK((pos[0] - 4200987.789) < 1e-2);
		CHECK((pos[1] - 168325.184) < 1e-2);
		CHECK((pos[2] - 4780272.588) < 1e-2);
	}
	SECTION("Louvre") {
		double3 latLonHeightRad = { 48.86079461877862, 2.337627906746917 }; // Paris Louvre coordinates
		// convert degrees to radians
		latLonHeightRad[0] = latLonHeightRad[0] * std::numbers::pi / 180.0;
		latLonHeightRad[1] = latLonHeightRad[1] * std::numbers::pi / 180.0;
		double3 pos = GCSTransform::WGS84GeodeticToECEF(latLonHeightRad);
		CHECK((pos[0] - 4200607.545) < 1e-2);
		CHECK((pos[1] - 171477.019) < 1e-2);
		CHECK((pos[2] - 4780388.241) < 1e-2);
	}
}

TEST_CASE("GCSTransform::WGS84ECEFToENU") {
	double3 latLonHeightRad = { 48.8584, 2.2945, 79.07 }; // Paris Eiffel Tower coordinates
	// convert degrees to radians
	latLonHeightRad[0] = latLonHeightRad[0] * std::numbers::pi / 180.0;
	latLonHeightRad[1] = latLonHeightRad[1] * std::numbers::pi / 180.0;
	dmat4x4 matrix = GCSTransform::WGS84ECEFToENUMatrix(latLonHeightRad);
	const glm::dmat4x4& m = AdvViz::SDK::internal::toGlm(matrix);
	glm::dvec4 v = m * glm::dvec4(4200987.789, 168325.184, 4780272.588, 1.0); // Paris Eiffel Tower coordinates
	CHECK((v[0]) < 1e-2);
	CHECK((v[1]) < 1e-2);
	CHECK((v[2]) < 1e-2);

	glm::dvec4 v2 = m * glm::dvec4(4200607.545, 171477.019, 4780388.241, 1.0); // Paris Louvre coordinates, based on google map, distance is ~3.2kms from Eiffel Tower
	CHECK((v2[0] - 3164.530) < 1e-2);
	CHECK((v2[1] - 267.194) < 1e-2);
	CHECK((v2[2] + 79.85) < 1e-2);
}

TEST_CASE("GCSTransform::WGS84ENUToECEF") {
	double3 latLonHeightRad = { 48.8584, 2.2945, 79.07 }; // Paris Eiffel Tower coordinates
	// convert degrees to radians
	latLonHeightRad[0] = latLonHeightRad[0] * std::numbers::pi / 180.0;
	latLonHeightRad[1] = latLonHeightRad[1] * std::numbers::pi / 180.0;
	dmat4x4 matrix = GCSTransform::WGS84ENUToECEFMatrix(latLonHeightRad);
	const glm::dmat4x4& m = AdvViz::SDK::internal::toGlm(matrix);
	glm::dvec4 v = m * glm::dvec4(0., 0., 0., 1.0);
	CHECK((v[0]- 4200987.789) < 1e-2);
	CHECK((v[1]- 168325.184) < 1e-2);
	CHECK((v[2]- 4780272.588) < 1e-2);

	glm::dvec4 v2 = m * glm::dvec4(3164.530, 267.194, -79.85, 1.0);
	CHECK((v2[0] - 4200607.545) < 1e-2);
	CHECK((v2[1] - 171477.019) < 1e-2);
	CHECK((v2[2] - 4780388.241) < 1e-2);
}

