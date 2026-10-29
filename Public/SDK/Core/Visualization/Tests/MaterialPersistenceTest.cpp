/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistenceTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../MaterialPersistence.h"
#include "../TextureUsage.h"
#include "MaterialPersistencePayloads.h"
#include <Core/ITwinAPI/ITwinMaterial.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <string>

#include <catch2/catch_all.hpp>
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/MockServerHeaderCheck.h>
#include <Core/Network/Tests/HttpMock.h>

using namespace AdvViz::SDK;

// Defined in VisualizationTest.cpp
void SetDefaultConfig();

namespace
{
	const std::string MaterialsUrl =
		"/advviz/v1/decorations/" TEST_DECO_ID "/materials";
	const std::string FilesUrl =
		"/advviz/v1/decorations/" TEST_DECO_ID "/files";

	// Depends on the file (UT_TextureToUpload.png).
	constexpr size_t ExpectedFileSize = 16029;

	const std::map<std::string, std::string> JsonHeaders = {
		{ "accept", "application/json" },
		{ "Content-Type", "application/json; charset=UTF-8" },
		{ "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN }
	};
}

TEST_CASE("MaterialPersistence")
{
	SECTION("materials")
	{
		SetDefaultConfig();
		HTTPMock* mock = GetHttpMock();
		REQUIRE(mock != nullptr);
		REQUIRE(GetDefaultHttp().get() != nullptr);

		std::atomic_bool hasReceivedFile = false;
		std::atomic_bool hasModifiedMaterial = false;
		std::atomic_bool hasAddedMaterial = false;

		const std::string imodelId = TEST_IMODEL_ID;

		// ---- mock endpoints -------------------------------------------------

		mock->SetResponseFunction(
			std::pair("GET", MaterialsUrl),
			[] {
			return HTTPMock::Response2(
				200, std::string(MatPersistenceTestData::GetMaterialsRows));
		});

		mock->SetResponseFunctionWithData(
			std::pair("POST", MaterialsUrl),
			[&hasAddedMaterial](const std::string& data) {
			if (data != MatPersistenceTestData::ExpectedPostBody)
				return HTTPMock::Response2(417, "Unexpected new material");
			hasAddedMaterial = true;
			return HTTPMock::Response2(
				201, std::string(MatPersistenceTestData::PostResponse));
		});

		mock->SetResponseFunctionWithRequest(
			std::pair("PUT", MaterialsUrl),
			[&hasModifiedMaterial](const std::string& data,
				const std::vector<HTTPMock::UrlArg>&,
				const HTTPMock::Headers& headers)
			{
				const int st = MockServerUtils::CheckRequiredHeaders(headers, JsonHeaders);
				if (st != MockServerUtils::HttpOk)
					return HTTPMock::Response2(st, "Error in headers.");
				if (data.find("328_" TEST_IMODEL_ID) == std::string::npos)
					return HTTPMock::Response2(417, "Bad material id");
				if (data.find("#CCC3B3") == std::string::npos)
					return HTTPMock::Response2(417, "Bad material color");
				if (data.find("0.1234") == std::string::npos)
					return HTTPMock::Response2(417, "Bad material roughness");
				if (data.find("_UT_TextureToUpload.png") == std::string::npos)
					return HTTPMock::Response2(417, "Bad material opacity map");
				hasModifiedMaterial = true;
				return HTTPMock::Response2(200, "{\"numUpdated\":1}");
			});

		mock->SetResponseFunctionWithRequest(
			std::pair("POST", FilesUrl),
			[&hasReceivedFile](const std::string& data,
				const std::vector<HTTPMock::UrlArg>&,
				const HTTPMock::Headers& headers)
			{
				// Tolerance on Content-Length: the multipart envelope adds
				// implementation-dependent bytes, so only match the magnitude.
				const int st = MockServerUtils::CheckRequiredHeaders(headers, {
					{ "Content-Length", std::to_string(ExpectedFileSize / 1000) + "*" },
					{ "Content-Type", "multipart/form-data; boundary=*" },
					{ "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN }
				});
				if (st != MockServerUtils::HttpOk)
					return HTTPMock::Response2(st, "Error in headers.");
				if (data.size() < ExpectedFileSize)
					return HTTPMock::Response2(412, "Not the expected data size");
				if (data.find("name=\"filename\"") == std::string::npos)
					return HTTPMock::Response2(417, "Missing form data");
				if (data.find("name=\"file\"") == std::string::npos)
					return HTTPMock::Response2(417, "Missing form data");
				if (data.find("Content-Type: application/octet-stream")	== std::string::npos
					&& data.find("Content-Type: image/png")	== std::string::npos)
					return HTTPMock::Response2(415, "Wrong Content-Type");
				hasReceivedFile = true;
				std::string s =
					"{\"filename\": \"0xa6a83333a6a95d69_UT_TextureToUpload.png\","
					" \"length\": " + std::to_string(ExpectedFileSize) + " }";
				return HTTPMock::Response2(201, s);
			});

		// ---- manager --------------------------------------------------------

		auto http = GetDefaultHttp();
		std::shared_ptr<ThreadSafeAccessToken> sharedToken = std::make_shared<ThreadSafeAccessToken>();
		sharedToken->Set(ITWINTEST_ACCESS_TOKEN);
		http->SetAccessToken(sharedToken);
		auto matIO = std::make_shared<MaterialPersistenceManager>();
		matIO->SetHttp(http);
		auto& MatIOMngr = *matIO;

		// ---- Load Materials -------------------------------------------------
		{
			std::atomic_bool loadFinished = false;
			TextureUsageMap usageMap;

			MatIOMngr.AsyncLoadDataFromServer(TEST_DECO_ID,
				[&loadFinished, &usageMap](
					AdvViz::expected<void, std::string> const& exp,
					TextureUsageMap const& map)
			{
				REQUIRE(exp);
				usageMap = map;
				loadFinished = true;
			});
			REQUIRE(WaitForAsyncTask(loadFinished, 20));

			PerIModelTextureSet const& perModelTextures =
				MatIOMngr.GetDecorationTexturesByIModel();
			REQUIRE(perModelTextures.size() == 1);
			CHECK(perModelTextures.begin()->second.size() == 9);

			for (auto const& texKey : perModelTextures.begin()->second)
				CHECK(FindTextureUsage(usageMap, texKey).flags_ != 0);

			ITwinMaterial mat;

			REQUIRE(MatIOMngr.GetMaterialSettings(imodelId, 83, mat));
			{
				auto const colorMap = mat.GetChannelMapOpt(EChannelType::Color);
				REQUIRE(colorMap);
				CHECK(colorMap->eSource == ETextureSource::Library);
				CHECK(colorMap->texture == "Roof_Tiles/color.png");
				auto const usage = FindTextureUsage(usageMap,
					{ colorMap->texture, colorMap->eSource });
				CHECK(usage.HasChannel(EChannelType::Color));
				auto const color = mat.GetChannelColorOpt(EChannelType::Color);
				REQUIRE(color);
				CHECK(*color == ITwinColor{ 1., 1., 1., 1. });
			}

			REQUIRE(MatIOMngr.GetMaterialSettings(imodelId, 56, mat));
			{
				auto const metallicMap =
					mat.GetChannelMapOpt(EChannelType::Metallic);
				REQUIRE(metallicMap);
				CHECK(metallicMap->eSource == ETextureSource::Decoration);
				CHECK(metallicMap->texture
					== "0xade57343837ffcc8_road_4ln2w_height.jpg");
				CHECK(FindTextureUsage(usageMap,
					{ metallicMap->texture, metallicMap->eSource })
					.HasChannel(EChannelType::Metallic));

				auto const normalValue =
					mat.GetChannelIntensityOpt(EChannelType::Normal);
				REQUIRE(normalValue);
				CHECK(std::fabs(*normalValue - 0.725) < 1e-5);

				auto const normalMap =
					mat.GetChannelMapOpt(EChannelType::Normal);
				REQUIRE(normalMap);
				CHECK(normalMap->eSource == ETextureSource::Decoration);
				CHECK(normalMap->texture == "0xd4919cc328654b18_Normal.png");
				CHECK(FindTextureUsage(usageMap,
					{ normalMap->texture, normalMap->eSource })
					.HasChannel(EChannelType::Normal));

				auto const color = mat.GetChannelColorOpt(EChannelType::Color);
				REQUIRE(color);
				CHECK(std::fabs((*color)[1] - 0.4313725) < 1e-5);
				CHECK_FALSE(mat.useSrgbColorSpace);
			}

			CHECK_FALSE(MatIOMngr.NeedUpdateDB());
		}

		// ---- Modify Material Definition and Save ----------------------------
		{
			const std::filesystem::path texturePath =
				std::filesystem::path(VIZ_TEST_DATA_DIR)
				/ "UT_TextureToUpload.png";
			std::error_code ec;
			REQUIRE(std::filesystem::exists(texturePath, ec));

			ITwinMaterial mat;
			REQUIRE(MatIOMngr.GetMaterialSettings(imodelId, 328, mat));
			REQUIRE(mat.kind == EMaterialKind::Glass);

			mat.SetChannelIntensity(EChannelType::Roughness, 0.1234);
			mat.SetChannelMap(EChannelType::Opacity,
				ITwinChannelMap{
					.texture = texturePath.generic_string(),
					.eSource = ETextureSource::LocalDisk });
			MatIOMngr.SetMaterialSettings(imodelId, 328, mat);

			REQUIRE(MatIOMngr.NeedUpdateDB());

			std::atomic_bool saveFinished = false;
			MatIOMngr.AsyncSaveDataOnServer(TEST_DECO_ID,
				[&saveFinished](bool) { saveFinished = true; });
			CHECK_FALSE(MatIOMngr.NeedUpdateDB());
			REQUIRE(WaitForAsyncTask(saveFinished, 20));

			CHECK(hasReceivedFile);
			CHECK(hasModifiedMaterial);
		}

		// ---- Add Material Definition and Save -------------------------------
		{
			ITwinMaterial mat;
			mat.displayName = "Glass #357";
			mat.kind = EMaterialKind::Glass;
			mat.SetChannelColor(EChannelType::Color,
				ITwinColor{ 0.2, 1.0, 0.2, 1.0 });
			MatIOMngr.SetMaterialSettings(imodelId, 357, mat);

			REQUIRE(MatIOMngr.NeedUpdateDB());

			std::atomic_bool saveFinished = false;
			MatIOMngr.AsyncSaveDataOnServer(TEST_DECO_ID,
				[&saveFinished](bool) { saveFinished = true; });
			CHECK_FALSE(MatIOMngr.NeedUpdateDB());
			REQUIRE(WaitForAsyncTask(saveFinished, 20));

			CHECK(hasAddedMaterial);
		}

		mock->ResetResponseFunction(std::pair("GET", MaterialsUrl));
		mock->ResetResponseFunction(std::pair("POST", MaterialsUrl));
		mock->ResetResponseFunction(std::pair("PUT", MaterialsUrl));
		mock->ResetResponseFunction(std::pair("POST", FilesUrl));
	}
}
