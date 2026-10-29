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
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>

#include "Core/Tools/Tools.h"
#include "Core/Tools/Internal_mathConv.inl"
#include <numbers>

using namespace AdvViz::SDK;


TEST_CASE("Visualization:Config")
{
	using namespace AdvViz::SDK;
	std::error_code ec;
	std::filesystem::path filePath("test.conf");
	std::filesystem::remove(filePath, ec);

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
			mock->SetResponseFunction(respKeyPost, [] {
				std::string s = "{\"data\":{\"gcs\":{\"center\":[0,0,0], \"wkt\":\"WGS84\"}, \"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\", \"name\":\"test auto\"}, \"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
			});

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->SetResponseFunction(respKeyGet, [] {
				std::string s = "{\"name\":\"test auto\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"gcs\":{\"wkt\":\"WGS84\",\"center\":[0,0,0]},\"id\":\"66c476ed1129763cf5485826\"}";
				return HTTPMock::Response2(200, s);
			});

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/66c476ed1129763cf5485826");
			mock->SetResponseFunction(respKeyDelete, [] {
				return HTTPMock::Response2(200, "{\"id\":\"66c476ed1129763cf5485826\"");
			});

			std::shared_ptr<IDecoration> decoration(IDecoration::New());
			std::atomic_bool asyncCreateDone = false;
			decoration->AsyncCreate("test auto", "",
				[decoration, &asyncCreateDone](bool bSuccess)
			{
				REQUIRE(bSuccess);
				REQUIRE(decoration->GetId() != "");
				asyncCreateDone = true;
			});

			// Make sure we get the response before continuing.
			REQUIRE(WaitForAsyncTask(asyncCreateDone, 10));

			// create a decoration copy by fetching previous decoration from server
			std::shared_ptr<IDecoration> decoration2(IDecoration::New());
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

namespace
{
	//! RAII helper restoring the default IDecoration factory on scope exit, so that
	//! overriding it in one test case cannot leak into the others.
	class [[nodiscard]] ScopedDecorationFactory
	{
	public:
		explicit ScopedDecorationFactory(std::function<IDecoration* ()> newFct)
		{
			IDecoration::SetNewFct(std::move(newFct));
		}
		~ScopedDecorationFactory()
		{
			IDecoration::SetNewFct([]() {
				return static_cast<IDecoration*>(new Decoration());
			});
		}
	};
}

class ExtendedDecoration : public Decoration
{
public:
	int Fct() { return 1234; }

	virtual const std::string& GetId() const override
	{
		static std::string s("test"); 
		return s; 
	}
};

TEST_CASE("Visualization:ExtendedDecoration") {
	try {
		SetDefaultConfig();

		ScopedDecorationFactory const scopedFactory([]() {
			return static_cast<IDecoration*>(new ExtendedDecoration);
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

// ---------------------------------------------------------------------------------------------
// Additional coverage for IDecoration: GetId, GCS / GCS transform accessors, GetITwinDecorations.
// ---------------------------------------------------------------------------------------------

namespace
{
	const std::string kDecoITwinId = "904a89f7-b63c-4ae1-a223-88517bd4bb08";
	const std::string kDecoId1 = "66c476ed1129763cf5485826";
	const std::string kDecoId2 = "66c476ed1129763cf5485827";

	std::string MakeDecoJson(std::string const& id, std::string const& name, std::string const& wkt)
	{
		return "{\"name\":\"" + name + "\","
			"\"itwinid\":\"" + kDecoITwinId + "\","
			"\"gcs\":{\"wkt\":\"" + wkt + "\",\"center\":[1.5,2.5,3.5]},"
			"\"id\":\"" + id + "\"}";
	}

	//! Registers GET decorations/<id> for the two decorations used below.
	void SetDecorationGetMocks(HTTPMock* mock)
	{
		mock->SetResponseFunction(std::pair("GET", "/advviz/v1/decorations/" + kDecoId1), [] {
			return HTTPMock::Response2(200, MakeDecoJson(kDecoId1, "deco 1", "WGS84"));
		});
		mock->SetResponseFunction(std::pair("GET", "/advviz/v1/decorations/" + kDecoId2), [] {
			return HTTPMock::Response2(200, MakeDecoJson(kDecoId2, "deco 2", "EPSG:4326"));
		});
	}
}

TEST_CASE("Visualization:DecorationProperties")
{
	SetDefaultConfig();
	REQUIRE(GetHttpMock() != nullptr);
	REQUIRE(GetDefaultHttp().get() != nullptr);

	std::shared_ptr<IDecoration> decoration(IDecoration::New());
	REQUIRE(decoration);

	SECTION("GetId is empty until the decoration exists on the server")
	{
		CHECK(decoration->GetId().empty());

		HTTPMock* mock = GetHttpMock();
		mock->SetResponseFunction(std::pair("POST", "/advviz/v1/decorations"), [] {
			return HTTPMock::Response2(200,
				"{\"data\":{\"gcs\":{\"center\":[0,0,0],\"wkt\":\"WGS84\"},"
				"\"itwinid\":\"" + kDecoITwinId + "\",\"name\":\"test auto\"},"
				"\"id\":\"" + kDecoId1 + "\"}");
		});
		mock->SetResponseFunction(std::pair("DELETE", "/advviz/v1/decorations/" + kDecoId1), [] {
			return HTTPMock::Response2(204, "");
		});

		std::atomic_bool done = false;
		decoration->AsyncCreate("test auto", kDecoITwinId, [&done](bool bSuccess) {
			REQUIRE(bSuccess);
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(decoration->GetId() == kDecoId1);

		// Delete() resets the identifier (and the cached data).
		decoration->Delete();
		CHECK(decoration->GetId().empty());
		CHECK(!decoration->GetGCS().has_value());
	}

	SECTION("GetId stays empty when the server fails")
	{
		GetHttpMock()->SetResponseFunction(std::pair("POST", "/advviz/v1/decorations"), [] {
			return HTTPMock::Response2(500, "{\"error\":\"server failure\"}");
		});
		std::atomic_bool done = false;
		std::atomic_bool bFailureReported = false;
		decoration->AsyncCreate("test auto", kDecoITwinId, [&done, &bFailureReported](bool bSuccess) {
			if (!bSuccess)
				bFailureReported = true;
			done = true;
		});
		REQUIRE(WaitForAsyncTask(done, 10));
		CHECK(bFailureReported);
		CHECK(decoration->GetId().empty());
	}

	SECTION("SetGCS / GetGCS")
	{
		// No GCS by default.
		CHECK(!decoration->GetGCS().has_value());

		GCS gcs;
		gcs.wkt = "PROJCS[\"WGS 84 / UTM zone 31N\"]";
		gcs.center = { 48.85, 2.35, 35.0 };
		decoration->SetGCS(gcs);

		REQUIRE(decoration->GetGCS().has_value());
		CHECK(decoration->GetGCS()->wkt == gcs.wkt);
		CHECK(decoration->GetGCS()->center == gcs.center);

		// Overwriting replaces the whole value.
		GCS gcs2;
		gcs2.wkt = "EPSG:4326";
		gcs2.center = { 0.0, 0.0, 0.0 };
		decoration->SetGCS(gcs2);
		REQUIRE(decoration->GetGCS().has_value());
		CHECK(decoration->GetGCS()->wkt == "EPSG:4326");
		CHECK(decoration->GetGCS()->center == gcs2.center);
	}

	SECTION("GetGCS is refreshed by Get")
	{
		SetDecorationGetMocks(GetHttpMock());

		GCS gcs;
		gcs.wkt = "local wkt";
		gcs.center = { 1.0, 1.0, 1.0 };
		decoration->SetGCS(gcs);

		// Loading from the server overwrites the whole cached data, including the GCS.
		decoration->Get(kDecoId1);
		CHECK(decoration->GetId() == kDecoId1);
		REQUIRE(decoration->GetGCS().has_value());
		CHECK(decoration->GetGCS()->wkt == "WGS84");
		CHECK(decoration->GetGCS()->center == double3{ 1.5, 2.5, 3.5 });
	}

	SECTION("SetGCSTransform / GetGCSTransform")
	{
		// No transform by default.
		CHECK(!decoration->GetGCSTransform());

		Tools::IGCSTransformPtr transform(Tools::IGCSTransform::New());
		REQUIRE(transform);
		decoration->SetGCSTransform(transform);
		CHECK(decoration->GetGCSTransform() == transform);

		// The transform is independent from the GCS data.
		CHECK(!decoration->GetGCS().has_value());

		// It can be replaced, and reset.
		Tools::IGCSTransformPtr transform2(Tools::IGCSTransform::New());
		decoration->SetGCSTransform(transform2);
		CHECK(decoration->GetGCSTransform() == transform2);

		decoration->SetGCSTransform({});
		CHECK(!decoration->GetGCSTransform());
	}
}

TEST_CASE("Visualization:GetITwinDecorations")
{
	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);
	REQUIRE(GetDefaultHttp().get() != nullptr);

	SetDecorationGetMocks(mock);

	SECTION("list of decorations")
	{
		mock->SetResponseFunction(std::pair("GET", "/advviz/v1/decorations"), [] {
			const std::string s =
				"{\"total_rows\":2,"
				"\"rows\":[" + MakeDecoJson(kDecoId1, "deco 1", "WGS84") + ","
						   + MakeDecoJson(kDecoId2, "deco 2", "EPSG:4326") + "],"
				"\"_links\":{\"self\":\"\"}}";
			return HTTPMock::Response2(200, s);
		});

		auto const decorations = GetITwinDecorations(kDecoITwinId);
		REQUIRE(decorations.size() == 2);
		// Each row is fetched individually (IDecoration::Get).
		CHECK(decorations[0]->GetId() == kDecoId1);
		CHECK(decorations[1]->GetId() == kDecoId2);
		REQUIRE(decorations[0]->GetGCS().has_value());
		CHECK(decorations[0]->GetGCS()->wkt == "WGS84");
		REQUIRE(decorations[1]->GetGCS().has_value());
		CHECK(decorations[1]->GetGCS()->wkt == "EPSG:4326");
	}

	SECTION("empty list")
	{
		mock->SetResponseFunction(std::pair("GET", "/advviz/v1/decorations"), [] {
			return HTTPMock::Response2(200, "{\"total_rows\":0,\"rows\":[],\"_links\":{\"self\":\"\"}}");
		});
		CHECK(GetITwinDecorations(kDecoITwinId).empty());
	}

	SECTION("server error returns no decoration")
	{
		mock->SetResponseFunction(std::pair("GET", "/advviz/v1/decorations"), [] {
			return HTTPMock::Response2(500, "{\"error\":\"server failure\"}");
		});
		CHECK(GetITwinDecorations(kDecoITwinId).empty());
	}
}
