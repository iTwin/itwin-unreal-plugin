/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistenceTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS

#include "WebTestHelpers.h"

#include <HAL/PlatformProcess.h>
#include <HAL/PlatformFile.h>
#include <HAL/PlatformFileManager.h>

#include <Misc/LowLevelTestAdapter.h>
#include <Misc/Paths.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <Core/ITwinAPI/ITwinAuthManager.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#	include <Core/Network/http.h>
#	include <Core/Visualization/MaterialPersistence.h>
#include <Compil/AfterNonUnrealIncludes.h>


#define TEST_DECO_ID "679d2cc2ba6b5b82ce6e1ec5"


/// Mock server implementation for material persistence
class FMaterialPersistenceMockServer : public FITwinMockServerBase
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FMaterialPersistenceMockServer(int port) : FITwinMockServerBase(port) {}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		if (url.find("/arg_test") != std::string::npos)
		{
			return ProcessArgTest(urlArguments);
		}
		if (url.ends_with("/" TEST_DECO_ID "/materials"))
		{
			return ProcessMaterialsTest(url, method, data, urlArguments, headers);
		}
		if (url.ends_with("/" TEST_DECO_ID "/files"))
		{
			return ProcessFilesTest(url, method, data, urlArguments, headers);
		}
		return Response(cpr::status::HTTP_NOT_FOUND,
			std::string("Page not found: ") + url);
	}

	virtual bool PostCondition() const override
	{
		return bHasReceivedFile && bHasModifiedMaterial && bHasAddedMaterial;
	}

private:
	mutable bool bHasReceivedFile = false;
	mutable bool bHasModifiedMaterial = false;
	mutable bool bHasAddedMaterial = false;


#define CHECK_DECORATION_HEADERS()										\
{																		\
	const int HeaderStatus = CheckRequiredHeaders(headers,				\
		{{ "accept", "application/json" },								\
		 { "Content-Type", "application/json; charset=UTF-8" },			\
		 { "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN } });		\
	if (HeaderStatus != cpr::status::HTTP_OK)							\
	{																	\
		return Response(HeaderStatus, "Error in headers.");				\
	}																	\
}

	/// Process /materials requests
	Response ProcessMaterialsTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_DECORATION_HEADERS();

		if (method == "GET")
		{
			return Response(cpr::status::HTTP_OK,
				"{\"total_rows\":4,\"rows\":[" \
				"{\"roughness\":0.884214,\"roughnessMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/roughness.png\",\"metallic\":0,\"opacity\":1,\"opacityMap\":\"0xbd64766217cf1f3f_hlfoot.png\",\"normal\":1,\"normalMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/normal.png\",\"ao\":1,\"aoMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/AO.png\",\"albedoMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/color.png\",\"color\":\"#FFFFFF\",\"type\":\"PBR\",\"id\":\"83_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
				"{\"roughness\":0.8988662958145142,\"metallic\":0,\"opacity\":1,\"normal\":0.2551162838935852,\"normalMap\":\"0xd4919cc328654b18_Normal.png\",\"albedoMap\":\"0x23a8b53872b49b9a_grid.jpeg\",\"color\":\"#CCC3B3\",\"type\":\"Glass\",\"id\":\"328_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
				"{\"roughness\":0.13949279487133026,\"metallic\":0.8144928216934204,\"metallicMap\":\"0xade57343837ffcc8_road_4ln2w_height.jpg\",\"opacity\":1,\"normal\":0.7250000238418579,\"normalMap\":\"0xd4919cc328654b18_Normal.png\",\"ao\":1,\"aoMap\":\"0xbceda97d82965da6_road_4ln2w_ambientOcclusion.jpg\",\"albedoMap\":\"0\",\"color\":\"#296E1D\",\"uvScaling\":[0.5,0.5],\"uvOffset\":[0,0],\"uvRotationAngle\":0,\"type\":\"PBR\",\"id\":\"56_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
				"{\"roughness\":0.7457427978515625,\"metallic\":0,\"opacity\":1,\"color\":\"#211A0F\",\"type\":\"PBR\",\"id\":\"53_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}],\"_links\":{\"self\":\"https://itwindecoration-eus.bentley.com/advviz/v1/decorations/679d2cc2ba6b5b82ce6e1ec5/materials?$skip=0\u0026$top=1000\"}}"
			);
		}
		if (method == "POST")
		{
			if (data != "{\"materials\":[{\"id\":\"357_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\",\"displayName\":\"Glass #357\",\"type\":\"Glass\",\"color\":\"#33FF33\",\"albedoMapFactor\":0.0}]}")
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Unexpected new material");
			}
			bHasAddedMaterial = true;
			return Response(cpr::status::HTTP_CREATED,
				"{\"materials\":[{\"roughness\":0.85,\"metallic\":0.75,\"opacity\":1,\"opacityMap\":\"0x34efa79259bb8be0_Vector 1.png\",\"color\":\"#FFFFFF\",\"uvScaling\":[0.5,0.5],\"uvOffset\":[0,0],\"uvRotationAngle\":0,\"type\":\"Glass\",\"id\":\"357_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}]}"
			);
		}
		if (method == "PUT")
		{
			if (data.find("328_8eb0fcc5-712b-48b6-a74d-5c80e50008b1") == std::string::npos)
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Bad material id");
			}
			if (data.find("#CCC3B3") == std::string::npos)
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Bad material color");
			}
			if (data.find("0.1234") == std::string::npos)
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Bad material roughness");
			}
			if (data.find("_UT_TextureToUpload.png") == std::string::npos)
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Bad material opacity map");
			}
			bHasModifiedMaterial = true;
			return Response(cpr::status::HTTP_OK,
				"{\"numUpdated\":1}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

	/// Process /files requests
	Response ProcessFilesTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		if (method == "POST")
		{
			const size_t expectedFileSize = 16029; // Depends on the file, of course (UT_TextureToUpload.png here).
			// Add a tolerance for content-length, as some additional data depending on upload implementation
			// can be necessary...
			const int HeaderStatus = CheckRequiredHeaders(headers, {
				{ "Content-Length", std::to_string(expectedFileSize / 1000) + "*" },
				{ "Content-Type", "multipart/form-data; boundary=*" },
				{ "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN }
			});
			if (HeaderStatus != cpr::status::HTTP_OK)
			{
				return Response(HeaderStatus, "Error in headers.");
			}
			if (data.size() < expectedFileSize)
			{
				BE_LOGE("ITwinDecoration", "[File Upload] not the expected data size: " << data.size());
				return Response(cpr::status::HTTP_PRECONDITION_FAILED, "Not the expected data size");
			}
			if (data.find("Content-Disposition: form-data; name=\"filename\"") == std::string::npos)
			{
				BE_LOGE("ITwinDecoration", "[File Upload] missing \"filename\" in content");
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Missing form data");
			}
			if (data.find("Content-Disposition: form-data; name=\"file\"") == std::string::npos)
			{
				BE_LOGE("ITwinDecoration", "[File Upload] missing \"file\" in content");
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Missing form data");
			}
			//if (data.find("Content-Type: image/png") == std::string::npos)
			if (data.find("Content-Type: application/octet-stream") == std::string::npos)
			{
				BE_LOGE("ITwinDecoration", "[File Upload] wrong Content-Type");
				return Response(cpr::status::HTTP_UNSUPPORTED_MEDIA_TYPE, "Wrong Content-Type");
			}
			bHasReceivedFile = true;
			return Response(cpr::status::HTTP_CREATED,
				std::string("{\"filename\": \"0xa6a83333a6a95d69_UT_TextureToUpload.png\", \"length\": ") + std::to_string(expectedFileSize) + " }");
		}
		// We could test the download as well, but the latter is performed by cesium asset accessor, which
		// is probably heavily tested by Cesium team...
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

};

/*static*/
std::unique_ptr<httpmock::MockServer> FMaterialPersistenceMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	return httpmock::getFirstRunningMockServer<FMaterialPersistenceMockServer>(startPort, tryCount);
}



class FITwinMatPersistenceTestHelper : public FITwinAPITestHelperBase
{
public:
	using MaterialPersistenceManager = AdvViz::SDK::MaterialPersistenceManager;

	static FITwinMatPersistenceTestHelper& Instance();
	~FITwinMatPersistenceTestHelper();

	std::shared_ptr<MaterialPersistenceManager> GetMatIOMngr() { return MatIO; }

protected:
	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment) override;
	virtual void DoCleanup() override;

private:
	FITwinMatPersistenceTestHelper() {}

	std::shared_ptr<MaterialPersistenceManager> MatIO;
};

/*static*/
FITwinMatPersistenceTestHelper& FITwinMatPersistenceTestHelper::Instance()
{
	static FITwinMatPersistenceTestHelper instance;
	return instance;
}

bool FITwinMatPersistenceTestHelper::DoInit(AdvViz::SDK::EITwinEnvironment Env)
{
	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8090;

	if (!InitServer(FMaterialPersistenceMockServer::MakeServer(DEFAULT_SERVER_PORT)))
	{
		return false;
	}

	MatIO = std::make_shared<MaterialPersistenceManager>();

	// Use our local mock server's URL
	std::shared_ptr<AdvViz::SDK::Http> Http;
	Http.reset(AdvViz::SDK::Http::New());
	Http->SetBaseUrl(GetServerUrl().c_str());
	Http->SetAccessToken(AdvViz::SDK::ITwinAuthManager::GetInstance(Env)->GetAccessToken());

	MatIO->SetHttp(Http);

	return true;
}

void FITwinMatPersistenceTestHelper::DoCleanup()
{
	
}

FITwinMatPersistenceTestHelper::~FITwinMatPersistenceTestHelper()
{
	Cleanup();
}

//
//
//DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForMockServerResponse, TestObserverPtr, Observer);
//
//bool FNUTWaitForMockServerResponse::Update()
//{
//	return !Observer || !Observer->IsWaitingForServerResponse();
//}



IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinMaterialPersistenceTest, FAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.MaterialPersistence", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinMaterialPersistenceTest::RunTest(const FString& /*Parameters*/)
{
	FITwinMatPersistenceTestHelper& Helper = FITwinMatPersistenceTestHelper::Instance();
	if (!Helper.Init())
	{
		return false;
	}

	auto pMatIOMngr = Helper.GetMatIOMngr();
	auto& MatIOMngr = *pMatIOMngr;

	const std::string url = Helper.GetServerUrl();

	const std::string imodelId = "8eb0fcc5-712b-48b6-a74d-5c80e50008b1";

	SECTION("MockServer Validation")
	{
		// Most basic test, just to validate the mock server
		// This one is synchronous.
		cpr::Response r = cpr::Get(cpr::Url{ url + "/arg_test?x=0&b=2" });
		UTEST_EQUAL("status_code", 200, r.status_code);
	}

	SECTION("Load Materials")
	{
		// this call is synchronous (for now) 
		MatIOMngr.LoadDataFromServer(TEST_DECO_ID);

		AdvViz::SDK::PerIModelTextureSet const& perModelTextures =
			MatIOMngr.GetDecorationTexturesByIModel();
		UTEST_EQUAL("imodels in decoration", perModelTextures.size(), 1);
		UTEST_EQUAL("decoration textures", perModelTextures.begin()->second.size(), 9);

		// Each texture should have an usage.
		for (auto const& texKey : perModelTextures.begin()->second)
		{
			UTEST_TRUE("has usage", MatIOMngr.GetTextureUsage(texKey).flags_ != 0);
		}

		// Check a few loaded settings
		AdvViz::SDK::ITwinMaterial matDefinition;

		UTEST_TRUE("mat #83", MatIOMngr.GetMaterialSettings(imodelId, 83, matDefinition));
		{
			auto const colorMap = matDefinition.GetChannelMapOpt(AdvViz::SDK::EChannelType::Color);
			UTEST_TRUE("mat #83 color map", colorMap
				&& colorMap->eSource == AdvViz::SDK::ETextureSource::Library
				&& colorMap->texture == "Roof_Tiles/color.png");
			UTEST_TRUE("color map usage", MatIOMngr.GetTextureUsage({ colorMap->texture, colorMap->eSource })
				.HasChannel(AdvViz::SDK::EChannelType::Color));
			auto const color = matDefinition.GetChannelColorOpt(AdvViz::SDK::EChannelType::Color);
			bool const bIsExpectedColor = color && *color == AdvViz::SDK::ITwinColor{ 1., 1., 1., 1. };
			UTEST_TRUE("mat #83 color", bIsExpectedColor);
		}

		UTEST_TRUE("mat #56", MatIOMngr.GetMaterialSettings(imodelId, 56, matDefinition));
		{
			auto const metallicMap = matDefinition.GetChannelMapOpt(AdvViz::SDK::EChannelType::Metallic);
			UTEST_TRUE("mat #56 metallic map", metallicMap
				&& metallicMap->eSource == AdvViz::SDK::ETextureSource::Decoration
				&& metallicMap->texture == "0xade57343837ffcc8_road_4ln2w_height.jpg");
			UTEST_TRUE("metallic map usage", MatIOMngr.GetTextureUsage({ metallicMap->texture, metallicMap->eSource })
				.HasChannel(AdvViz::SDK::EChannelType::Metallic));
			auto const normalValue = matDefinition.GetChannelIntensityOpt(AdvViz::SDK::EChannelType::Normal);
			UTEST_TRUE("mat #56 normal amplitude", normalValue && std::fabs(*normalValue - 0.725) < 1e-5);
			auto const normalMap = matDefinition.GetChannelMapOpt(AdvViz::SDK::EChannelType::Normal);
			UTEST_TRUE("mat #56 normal map", normalMap
				&& normalMap->eSource == AdvViz::SDK::ETextureSource::Decoration
				&& normalMap->texture == "0xd4919cc328654b18_Normal.png");
			UTEST_TRUE("normal map usage", MatIOMngr.GetTextureUsage({ normalMap->texture, normalMap->eSource })
				.HasChannel(AdvViz::SDK::EChannelType::Normal));
			auto const color = matDefinition.GetChannelColorOpt(AdvViz::SDK::EChannelType::Color);
			bool const bIsExpectedColor = color && (std::fabs((*color)[1] - 0.4313725) < 1e-5);
			UTEST_TRUE("mat #56 color", bIsExpectedColor);
		}

		UTEST_FALSE(TEXT("DB up-to-date"), MatIOMngr.NeedUpdateDB());
	}

	SECTION("Modify Material Definition and Save")
	{
		// Modify an existing one

		BeUtils::GltfMaterialHelper MatHelper;
		MatHelper.SetPersistenceInfo(imodelId, pMatIOMngr);

		{
			BeUtils::WLock Lock(MatHelper.GetMutex());
			auto const MatInfo = MatHelper.CreateITwinMaterialSlot(328, "", Lock);
			UTEST_TRUE(TEXT("existing material"),
				MatInfo.second && MatInfo.second->kind == AdvViz::SDK::EMaterialKind::Glass);
		}

		bool bValueModified = false;
		MatHelper.SetChannelIntensity(328, AdvViz::SDK::EChannelType::Roughness, 0.1234, bValueModified);
		UTEST_TRUE(TEXT("value modified"), bValueModified);

		bValueModified = false;
		// Add a texture (taken from source code).
		// Ensure we do not use a texture which is a symbolic link, as it gives some platform-dependent
		// behavior...
		// This works for both ITwinTestApp and Carrot, as we build in-source.
		const FString TexturePath = FPaths::ProjectDir() / "../../../Public/SDK/Core/Visualization/Tests/UT_TextureToUpload.png";
		UTEST_TRUE(TEXT("texture exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*TexturePath));

		MatHelper.SetChannelIntensityMap(328, AdvViz::SDK::EChannelType::Opacity,
			AdvViz::SDK::ITwinChannelMap{ .texture = TCHAR_TO_UTF8(*TexturePath) },
			bValueModified);
		UTEST_TRUE(TEXT("value modified"), bValueModified);

		UTEST_TRUE(TEXT("DB Invalidation"), MatIOMngr.NeedUpdateDB());

		// The texture will be uploaded now.
		MatIOMngr.SaveDataOnServer(TEST_DECO_ID);
		UTEST_FALSE(TEXT("DB up-to-date"), MatIOMngr.NeedUpdateDB());
	}

	SECTION("Add Material Definition and Save")
	{
		// Add a new material definition
		AdvViz::SDK::ITwinMaterial matDefinition;
		matDefinition.displayName = "Glass #357";
		matDefinition.kind = AdvViz::SDK::EMaterialKind::Glass;
		matDefinition.SetChannelColor(AdvViz::SDK::EChannelType::Color,
			AdvViz::SDK::ITwinColor{ 0.2, 1.0, 0.2, 1.0 });
		MatIOMngr.SetMaterialSettings(imodelId, 357, matDefinition);

		UTEST_TRUE(TEXT("DB Invalidation"), MatIOMngr.NeedUpdateDB());

		MatIOMngr.SaveDataOnServer(TEST_DECO_ID);
		UTEST_FALSE(TEXT("DB up-to-date"), MatIOMngr.NeedUpdateDB());
	}

	UTEST_TRUE("Post-Condition", Helper.PostCondition());

	return true;
}

#endif // WITH_TESTS
