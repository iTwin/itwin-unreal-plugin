/*--------------------------------------------------------------------------------------+
|
|     $Source: WebServicesTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS

#include <CoreMinimal.h>
#include <HAL/PlatformProcess.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <cpr/cpr.h>
	#include <httpmockserver/mock_server.h>
	#include <httpmockserver/port_searcher.h>
#include <Compil/AfterNonUnrealIncludes.h>


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinWebServicesTest, "Bentley.ITwinForUnreal.ITwinRuntime.WebServices", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinWebServicesTest::RunTest(const FString& /*Parameters*/)
{
	SECTION("TokenEncryption")
	{
		FString SrcToken;
		// Build a random token
		FString const ValidChars = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
		int32 const NbValidChars = ValidChars.Len() - 1;
		SrcToken.Reserve(512);
		for (uint32 i = 0; i < 512; ++i)
		{
			SrcToken.AppendChar(ValidChars[FMath::RandRange(0, NbValidChars)]);
		}
		// Since tokens are saved on a per-environment basis, we need to choose one, even though this has no
		// real impact on the test
		SDK::Core::EITwinEnvironment const TestEnv = SDK::Core::EITwinEnvironment::Prod;
		// Avoid conflicting with the true application (or another instance of the same test running in
		// parallel...), and provide with a default iTwin App ID if there is none currently.
		FString TokenFileSuffixForTest = FString::Printf(TEXT("_Test_%d"), FPlatformProcess::GetCurrentProcessId());
		FITwinAuthorizationManager::SetupTestMode(TestEnv, TokenFileSuffixForTest);

		UTEST_TRUE("SaveToken", FITwinAuthorizationManager::SaveToken(SrcToken, TestEnv));
		FString ReadToken;
		UTEST_TRUE("LoadToken", FITwinAuthorizationManager::LoadToken(ReadToken, TestEnv));
		UTEST_EQUAL("Unchanged Token", ReadToken, SrcToken);

		// Cleanup
		FITwinAuthorizationManager::DeleteTokenFile(TestEnv);
	}
	return true;
}




#define ITWINTEST_ACCESS_TOKEN "ThisIsATestITwinAccessToken"

#define ITWINID_CAYMUS_EAP "5e15184e-6d3c-43fd-ad04-e28b4b39485e"
#define IMODELID_BUILDING "cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49"
#define CHANGESETID_BUILDING "694305dbe2e5626267920f6a3f1e23db707674ba"
#define IMODELID_WIND_TURBINE "d66fcd8c-604a-41d6-964a-b9767d446c53"
#define EXPORTID_WIND_TURBINE_CESIUM "6e5a59b7-878b-4275-b960-8668dc11a04d"
#define SAVEDVIEWID_BUILDING_TEST "ARatNwH2bcJElcCIfZxfP69OGBVePG39Q60E4otLOUhewFJgy6DEw0GJFLG6fYuMSQ"

#define ITWINID_TESTS_ALEXW "e72496bd-03a5-4ad8-8a51-b14e827603b1"
#define IMODELID_PHOTO_REALISTIC_RENDERING "4dcf6dee-e7f1-4ed8-81f2-125402b9ac95"
#define SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02 "AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"

#define SAVEDVIEWID_BUILDING_ALEXVIEW2 "AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw"

#define ITWINID_STADIUM_RN_QA "a2a1ee72-7fb2-402d-a588-1d873aeaff3e"
#define IMODELID_STADIUM "e04bfa36-d4ce-4482-8057-cbd73ec80d23"
#define CHANGESETID_STADIUM "50c2eb88e48e7556635504cec91a6811b5939122"

#define REALITYDATAID_ORLANDO "21b5896b-5dbd-41a7-9f23-d988a1847c11"

#define ITWINID_NOT_EXISTING "toto"

/// Mock server implementation for iTwin services
class FITwinMockServer : public httpmock::MockServer
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FITwinMockServer(unsigned port) : MockServer(port) {}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		if (isUrl(url, "/arg_test"))
		{
			return ProcessArgTest(urlArguments);
		}
		if (isUrl(url, "/itwins"))
		{
			return ProcessItwinsTest(url, urlArguments, headers);
		}
		if (isUrl(url, "/imodels"))
		{
			return ProcessIModelsTest(url, urlArguments, headers);
		}
		if (isUrl(url, "/mesh-export"))
		{
			return ProcessMeshExportTest(url, method, data, urlArguments, headers);
		}
		if (isUrl(url, "/savedviews"))
		{
			return ProcessSavedViewsTest(url, method, data, urlArguments, headers);
		}
		if (isUrl(url, "/reality-management"))
		{
			return ProcessRealityDataTest(url, urlArguments, headers);
		}
		if (isUrl(url, "/imodel/rpc"))
		{
			return ProcessIModelRPCTest(url, method, data, urlArguments, headers);
		}
		return Response(cpr::status::HTTP_NOT_FOUND,
			std::string("Page not found: ") + url);
	}

private:
	bool isUrl(const std::string& url, const std::string& urlRequired) const {
		return url.compare(0, urlRequired.size(), urlRequired) == 0;
	}

	using StringMap = std::map<std::string, std::string>;

	template <typename KeyValueType>
	static StringMap ToArgMap(std::vector<KeyValueType> const& urlArguments)
	{
		StringMap res;
		for (auto const& arg : urlArguments)
		{
			res[arg.key] = arg.value;
		}
		return res;
	}

	/// Process /arg_test request
	Response ProcessArgTest(const std::vector<UrlArg>& urlArguments) const
	{
		static const StringMap expectedArgs = { { "b", "2" }, { "x", "0" } };
		check(ToArgMap(urlArguments) == expectedArgs);
		return Response();
	}

	/// Process /header_in request
	int CheckRequiredHeaders(const std::vector<Header>& headers,
		std::string const& expectedAccept,
		std::string const& expectedPrefer,
		std::string const& expectedAccessToken) const
	{
		bool validAccept = expectedAccept.empty();
		bool validPrefer = expectedPrefer.empty();
		bool validValidAuth = expectedAccessToken.empty();
		for (const Header& header : headers)
		{
			if (header.key == "Accept" && header.value == expectedAccept) {
				validAccept = true;
				continue;
			}
			if (header.key == "Prefer" && header.value == expectedPrefer) {
				validPrefer = true;
				continue;
			}
			if (header.key == "Authorization"
				&& header.value.starts_with("Bearer ")
				&& header.value.substr(7) == expectedAccessToken) {
				validValidAuth = true;
				continue;
			}
		}
		check(validAccept);
		check(validPrefer);
		check(validValidAuth);
		if (!validValidAuth)
			return cpr::status::HTTP_UNAUTHORIZED;
		if (!validAccept || !validPrefer)
			return cpr::status::HTTP_BAD_REQUEST;
		return cpr::status::HTTP_OK;
	}


#define CHECK_ITWIN_HEADERS( iTwin_VER )								\
{																		\
	const int HeaderStatus = CheckRequiredHeaders(headers,				\
		"application/vnd.bentley.itwin-platform." iTwin_VER "+json",	\
		"return=representation",										\
		ITWINTEST_ACCESS_TOKEN);										\
	if (HeaderStatus != cpr::status::HTTP_OK)							\
	{																	\
		return Response(HeaderStatus, "Error in headers.");				\
	}																	\
}

	/// Process /itwins/ requests
	Response ProcessItwinsTest(
		const std::string& url,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_ITWIN_HEADERS("v1");

		if (url.ends_with(ITWINID_CAYMUS_EAP)
			&& ensure(urlArguments.size() == 0))
		{
			//---------------------------------------------------------------------------
			// GetITwinInfo
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK,
				"{\"iTwin\":{\"id\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"class\":\"Endeavor\",\"subClass\":\"Project\"," \
				"\"type\":null,\"number\":\"Bentley Caymus EAP\",\"displayName\":\"Bentley Caymus EAP\",\"geographicLocation\":\"Exton, PA\"," \
				"\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\"," \
				"\"parentId\":\"78202ffd-272b-4207\",\"iTwinAccountId\":\"78202ffd-272b-4207\"," \
				"\"imageName\":null,\"image\":null,\"createdDateTime\":\"2021-09-28T19:16:06.183Z\",\"createdBy\":\"102f4511-1838\"}}"
			);
		}
		if (url.ends_with(ITWINID_NOT_EXISTING)
			&& ensure(urlArguments.size() == 0))
		{
			//---------------------------------------------------------------------------
			// GetITwinInfo with wrong ID
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_NOT_FOUND,
				"{\"error\":{\"code\":\"iTwinNotFound\",\"message\":\"Requested iTwin is not available.\"}}"
			);
		}
		if (url.ends_with("recents")
			&& ensure(urlArguments.size() == 3))
		{
			//---------------------------------------------------------------------------
			// GetiTwins
			//---------------------------------------------------------------------------
			StringMap argMap = ToArgMap(urlArguments);
			checkf(atoi(argMap["$top"].c_str()) >= 100 /* currently 1000, but we could change the limit... */
				&& argMap["subClass"] == "Project"
				&& argMap["status"] == "Active",
				TEXT("unexpected arguments"));
			return Response(cpr::status::HTTP_OK, "{\"iTwins\":[" \
				"{\"id\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\",\"class\":\"Endeavor\",\"subClass\":\"Project\"," \
				"\"type\":null,\"number\":\"Tests_AlexW\",\"displayName\":\"Tests_AlexW\",\"geographicLocation\":null,\"ianaTimeZone\":null," \
				"\"dataCenterLocation\":\"East US\",\"status\":\"Active\",\"parentId\":\"78202ffd-272b-4207\",\"iTwinAccountId\":\"78202ffd-272b-4207\"," \
				"\"imageName\":null,\"image\":null,\"createdDateTime\":\"2024-03-25T10:26:45.797Z\",\"createdBy\":\"aabbccdd-aaaa-bbbb-cccc-dddddddd\"}," \
				"{\"id\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"class\":\"Endeavor\",\"subClass\":\"Project\",\"type\":null,\"number\":\"Bentley Caymus EAP\"," \
				"\"displayName\":\"Bentley Caymus EAP\",\"geographicLocation\":\"Exton, PA\",\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\"," \
				"\"parentId\":\"78202ffd-272b-4207\",\"iTwinAccountId\":\"78202ffd-272b-4207\",\"imageName\":null,\"image\":null," \
				"\"createdDateTime\":\"2021-09-28T19:16:06.183Z\",\"createdBy\":\"102f4511-1838\"},{\"id\":\"257af6c2-b2fa-41fd-b85d-b90837f36934\",\"class\":\"Endeavor\"," \
				"\"subClass\":\"Project\",\"type\":null,\"number\":\"ConExpo 2023 - Civil\",\"displayName\":\"ConExpo 2023 - Civil\",\"geographicLocation\":\"Wilson, North Carolina I95 and Highway 97\"," \
				"\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\",\"parentId\":\"78202ffd-272b-4207\"," \
				"\"iTwinAccountId\":\"78202ffd-272b-4207\",\"imageName\":\"87a7d207-473d-4fed-abb9-999c555f70f0.jpg\",\"image\":\"https://image.net/context-thumbnails/999c555f70f0.jpg?sv=2018-03-28&sr=b&sig=Sbzx99oHmsVxjCX4J%2Fv5zpJP%3D&se=2024-06-16T00%3A00%3A00Z&sp=r\"," \
				"\"createdDateTime\":\"2023-02-06T18:33:42.283Z\",\"createdBy\":\"e955f160-7336-4395-b29f-08545764fc3d\"}],\"_links\":{\"self\":{\"href\":\"https://api.test.com/itwins/recents?$skip=0&$top=1000&subClass=Project&status=Active\"}}}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

	/// Process /imodels/ requests
	Response ProcessIModelsTest(
		const std::string& url,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_ITWIN_HEADERS("v2");

		StringMap argMap = ToArgMap(urlArguments);
		if (argMap["iTwinId"] == ITWINID_CAYMUS_EAP)
		{
			//---------------------------------------------------------------------------
			// GetiTwiniModels
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"iModels\":[" \
				"{\"id\":\"cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49\",\"displayName\":\"Building\",\"dataCenterLocation\":\"East US\",\"name\":\"Building\",\"description\":\"Bentley Building Project\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-10-05T16:31:18.1030000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49/namedversions\"}}}," \
				"{\"id\":\"e241cf6e-8d80-4cd8-bc67-2ad078a1a693\",\"displayName\":\"Hatch Terrain Model\",\"dataCenterLocation\":\"East US\",\"name\":\"Hatch Terrain Model\",\"description\":\"\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2023-03-18T06:33:58.3830000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/e241cf6e-8d80-4cd8-bc67-2ad078a1a693/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/e241cf6e-8d80-4cd8-bc67-2ad078a1a693/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/e241cf6e-8d80-4cd8-bc67-2ad078a1a693/namedversions\"}}}," \
				"{\"id\":\"d7f5dd60-08ea-46e1-8eec-3763f18c1c6a\",\"displayName\":\"Highway\",\"dataCenterLocation\":\"East US\",\"name\":\"Highway\",\"description\":\"Bentley Omniverse Testing\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-30T06:13:11.8070000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/d7f5dd60-08ea-46e1-8eec-3763f18c1c6a/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/d7f5dd60-08ea-46e1-8eec-3763f18c1c6a/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/d7f5dd60-08ea-46e1-8eec-3763f18c1c6a/namedversions\"}}}," \
				"{\"id\":\"ad358f03-5488-44e4-bc1f-42a610b99694\",\"displayName\":\"MetroStation\",\"dataCenterLocation\":\"East US\",\"name\":\"MetroStation\",\"description\":\"Test model for Bentley Omniverse\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:54:20.5130000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false," \
				"\"extent\":{\"southWest\":{\"latitude\":39.42986934243659,\"longitude\":-119.75930764897122},\"northEast\":{\"latitude\":39.4370289257737,\"longitude\":-119.74600389225735}},\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/ad358f03-5488-44e4-bc1f-42a610b99694/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/ad358f03-5488-44e4-bc1f-42a610b99694/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/ad358f03-5488-44e4-bc1f-42a610b99694/namedversions\"}}}," \
				"{\"id\":\"c2019b23-4501-41f3-b933-02e73ca5621b\",\"displayName\":\"OffshoreRig\",\"dataCenterLocation\":\"East US\",\"name\":\"OffshoreRig\",\"description\":\"Bentley Omniverse Test Model\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:55:30.6200000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/c2019b23-4501-41f3-b933-02e73ca5621b/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/c2019b23-4501-41f3-b933-02e73ca5621b/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/c2019b23-4501-41f3-b933-02e73ca5621b/namedversions\"}}}," \
				"{\"id\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"displayName\":\"WindTurbine\",\"dataCenterLocation\":\"East US\",\"name\":\"WindTurbine\",\"description\":\"Omniverse Test Model\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:19:44.8300000Z\",\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/namedversions\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/imodels?itwinId=5e15184e-6d3c-43fd-ad04-e28b4b39485e\u0026$skip=0\u0026$top=100\"},\"prev\":null,\"next\":null}}"
			);
		}
		else if (url.ends_with(IMODELID_WIND_TURBINE "/changesets" ))
		{
			//---------------------------------------------------------------------------
			// GetiModelChangesets
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"changesets\":[" \
				"{\"id\":\"943762e9afe5239d74623cf5081502df23c7816d\",\"displayName\":\"4\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - Initialization changes\",\"index\":4," \
				"\"parentId\":\"a579fa8c3a3dda5a04df9c3b87416de0df3a2d66\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:13.3530000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":0,\"fileSize\":599,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/943762e9afe5239d74623cf5081502df23c7816d.cs?sv=2019-07-07\u0026sr=b\u0026sig=TYtyeN3eMo0MfZ7dCWNkqA%2FSF4ZmyOiXaL3wZ5DOoYQ%3D\u0026st=2024-06-17T08%3A43%3A04.6502473Z\u0026se=2024-06-17T09%3A04%3A42.3118793Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/4\"}}}," \
				"{\"id\":\"a579fa8c3a3dda5a04df9c3b87416de0df3a2d66\",\"displayName\":\"3\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - BootstrapExternalSources\",\"index\":3," \
				"\"parentId\":\"db3c0e50fad288ad5af7ccfe53725de4c9876153\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:10.9100000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":0,\"fileSize\":229,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/a579fa8c3a3dda5a04df9c3b87416de0df3a2d66.cs?sv=2019-07-07\u0026sr=b\u0026sig=IZneO860eH1uYMqrNsaeTZ3SepPkardVBDc2NEdGsI0%3D\u0026st=2024-06-17T08%3A41%3A24.6846254Z\u0026se=2024-06-17T09%3A04%3A42.3118999Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/3\"}}}," \
				"{\"id\":\"db3c0e50fad288ad5af7ccfe53725de4c9876153\",\"displayName\":\"2\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - Domain schema upgrade\",\"index\":2," \
				"\"parentId\":\"4681a740b4d10e171d885a83bf3d507edada91cf\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:08.7300000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":1,\"fileSize\":3791,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/db3c0e50fad288ad5af7ccfe53725de4c9876153.cs?sv=2019-07-07\u0026sr=b\u0026sig=4OQNPY4%2BHVfRPdwi6sSrv20L5RYrawyhg2GT637f11s%3D\u0026st=2024-06-17T08%3A41%3A33.4453273Z\u0026se=2024-06-17T09%3A04%3A42.3119214Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/2\"}}}," \
				"{\"id\":\"4681a740b4d10e171d885a83bf3d507edada91cf\",\"displayName\":\"1\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - Domain schema upgrade\",\"index\":1," \
				"\"parentId\":\"\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:04.5700000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":1,\"fileSize\":6384,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/4681a740b4d10e171d885a83bf3d507edada91cf.cs?sv=2019-07-07\u0026sr=b\u0026sig=h3Fy8Kw9JHxCU6zBgeBAAOBiXUneLbFoT7C71z6B0WY%3D\u0026st=2024-06-17T08%3A42%3A16.1500756Z\u0026se=2024-06-17T09%3A04%3A42.3119433Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/1\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets?$skip=0\u0026$top=100\u0026$orderBy=index%20desc\"},\"prev\":null,\"next\":null}}"
			);
		}
		else if (argMap["iTwinId"] == ITWINID_NOT_EXISTING)
		{
			//---------------------------------------------------------------------------
			// GetiTwiniModels with wrong ID
			//---------------------------------------------------------------------------
			// Error 422
			return Response(cpr::status::HTTP_UNPROCESSABLE_ENTITY,
				"{\"error\":{\"code\":\"InvalidiModelsRequest\",\"message\":\"Cannot get iModels.\",\"details\":[{\"code\":\"InvalidValue\"," \
				"\"message\":\"\u0027toto\u0027 is not a valid \u0027iTwinId\u0027 value.\",\"target\":\"iTwinId\"}]}}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}


	/// Process /mesh-export/ requests
	Response ProcessMeshExportTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_ITWIN_HEADERS("v1");

		StringMap argMap = ToArgMap(urlArguments);
		StringMap headerMap = ToArgMap(headers);
		if (method == "POST"
			&& data == "{\"iModelId\":\"" IMODELID_STADIUM "\",\"changesetId\":\"" CHANGESETID_STADIUM "\",\"exportType\":\"CESIUM\"}")
		{
			//---------------------------------------------------------------------------
			// StartExport
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK,
				"{\"export\":{\"id\":\"70abfe04-f791-4cba-b7e6-d4d402dda844\",\"displayName\":\"SS_Stadium\",\"status\":\"NotStarted\"," \
				"\"lastModified\":\"2024-06-18T14:12:30.905Z\",\"request\":{\"iModelId\":\"e04bfa36-d4ce-4482-8057-cbd73ec80d23\",\"changesetId\":\"50c2eb88e48e7556635504cec91a6811b5939122\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\",\"contextId\":\"ea28fcd7-71d2-4313-951f-411639d9471e\"}}}"
			);
		}
		if (argMap["iModelId"] == IMODELID_WIND_TURBINE
			&& !argMap["changesetId"].empty()
			&& argMap["exportType"] == "CESIUM"
			&& argMap["cdn"] == "1"
			&& argMap["client"] == "Unreal")
		{
			//---------------------------------------------------------------------------
			// GetExports - WindTurbine
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"exports\":[" \
				"{\"id\":\"6e5a59b7-878b-4275-b960-8668dc11a04d\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-03-29T10:20:57.606Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/6e5a59b7-878b-4275-b960-8668dc11a04d?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D\"}}}," \
				"{\"id\":\"87316e15-3d1e-436f-bc7d-b22521f67aff\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-01-29T08:39:07.737Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"3DFT\",\"geometryOptions\":{},\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/87316e15-3d1e-436f-bc7d-b22521f67aff?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sO3bvUtBCrmQS1n8jvgcNNm5k8UOzKmP%2BGtOGBZ3DwM%3D\"}}}," \
				"{\"id\":\"a8d9806f-42e1-4523-aa25-0ba0b7f87e5c\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-05-02T13:00:11.999Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"IMODEL\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/a8d9806f-42e1-4523-aa25-0ba0b7f87e5c?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Va1c8owVNySpR7IFb4Q0A1%2FDqZn%2BD5B4T9%2F%2Fru8PFEM%3D\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/mesh-export/?$skip=0&$top=100&iModelId=d66fcd8c-604a-41d6-964a-b9767d446c53&changesetId=9641026f8e6370db8cc790fab8943255af57d38e\"}}}"
			);
		}
		if (argMap["iModelId"] == IMODELID_PHOTO_REALISTIC_RENDERING
			&& argMap["changesetId"].empty()
			&& argMap["exportType"] == "CESIUM"
			&& argMap["cdn"] == "1"
			&& argMap["client"] == "Unreal")
		{
			//---------------------------------------------------------------------------
			// GetExports - PhotoRealisticRendering
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"exports\":[" \
				"{\"id\":\"ed456436-ed0a-488c-a5f2-4115e7d8e311\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-20T15:06:47.548Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ed456436-ed0a-488c-a5f2-4115e7d8e311?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=1pievrXlFCSwmErxnSsIS4STny9y9oz%2B3P5j%2FsbPkgA%3D\"}}}," \
				"{\"id\":\"00af52a3-a416-4e37-99e9-6de56368bc37\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:37:17.574Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/00af52a3-a416-4e37-99e9-6de56368bc37?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sgi1%2F26Szx6zUezikckec3l0285RRw3A1k948KBAjsU%3D\"}}}," \
				"{\"id\":\"1485a12a-c4f6-416f-bb79-e1fe478a3220\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-18T15:00:19.179Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/1485a12a-c4f6-416f-bb79-e1fe478a3220?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=jAKM4lsaO0THXKe6Au9jOoqb4CUaAOVGy6hCf%2BGCO9s%3D\"}}}," \
				"{\"id\":\"1d7eb244-cec9-4f62-909b-fb4755c37d83\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:51:14.999Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"GLTF\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\",\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/1d7eb244-cec9-4f62-909b-fb4755c37d83?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=alK49gFhKRILyHFf%2FFRgVl3Lr1ARN%2Bkg8KFrLxomjqE%3D\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/mesh-export/?$skip=0&$top=100&iModelId=4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"}}}"
			);
		}
		else if (url.ends_with(EXPORTID_WIND_TURBINE_CESIUM))
		{
			//---------------------------------------------------------------------------
			// GetExportInfo
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"export\":" \
				"{\"id\":\"6e5a59b7-878b-4275-b960-8668dc11a04d\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-03-29T10:20:57.606Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/6e5a59b7-878b-4275-b960-8668dc11a04d?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D\"}}}}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

	/// Process /savedviews/ requests
	Response ProcessSavedViewsTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_ITWIN_HEADERS("v1");

		StringMap argMap = ToArgMap(urlArguments);
		StringMap headerMap = ToArgMap(headers);

#define SAVEDVIEW_02_DATA "\"savedViewData\":" \
			"{\"itwin3dView\":{\"origin\":[0.0,0.0,0.0],\"extents\":[0.0,0.0,0.0],\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":90.0,\"focusDist\":0.0,\"eye\":[-1.79,-0.69,1.59]}," \
			"\"displayStyle\":{\"viewflags\":{\"renderMode\":6,\"noConstructions\":false},\"environment\":{\"sky\":{\"display\":true,\"twoColor\":true,\"skyColor\":{\"red\":222,\"green\":242,\"blue\":255}," \
			"\"groundColor\":{\"red\":240,\"green\":236,\"blue\":232},\"zenithColor\":{\"red\":222,\"green\":242,\"blue\":255},\"nadirColor\":{\"red\":240,\"green\":236,\"blue\":232}}}}}},\"displayName\":\"view02\",\"shared\":true,\"tagIds\":[]"

#define ADD_SAVEDVIEW_02_DATA "{\"iTwinId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\",\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"," SAVEDVIEW_02_DATA "}"

		if (argMap["iTwinId"] == ITWINID_TESTS_ALEXW
			&& argMap["iModelId"] == IMODELID_PHOTO_REALISTIC_RENDERING)
		{
			//---------------------------------------------------------------------------
			// GetAllSavedViews
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"savedViews\":[" \
				"{\"id\":\"AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:07:29.897Z\",\"lastModified\":\"2024-06-13T12:25:19.239Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-3.12,7.39,2.2],\"extents\":[0,0,0],\"angles\":{\"yaw\":176.41,\"pitch\":-41.52,\"roll\":84.6},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-3.12,7.39,2.2]}}}," \
				"\"displayName\":\"view01\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:08:34.797Z\",\"lastModified\":\"2024-06-13T12:26:35.678Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0,0,0],\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.79,-0.69,1.59]}}}," \
				"\"displayName\":\"view02\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:43:36.006Z\",\"lastModified\":\"2024-06-18T07:27:58.423Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.6,6.77,10.89],\"extents\":[0,0,0],\"angles\":{\"yaw\":156.52,\"pitch\":-22.47,\"roll\":41.34},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.6,6.77,10.89]}}}," \
				"\"displayName\":\"view03 - top\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T12:16:16.765Z\",\"lastModified\":\"2024-06-13T12:17:04.237Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-2.67,3.17,1.9],\"extents\":[0,0,0],\"angles\":{\"yaw\":-170.55,\"pitch\":-86.22,\"roll\":99.47},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-2.67,3.17,1.9]}}}," \
				"\"displayName\":\"view04\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AG7BwHvOKrJJi-kRUac5AVa9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-18T07:33:29.596Z\",\"lastModified\":\"2024-06-18T07:33:29.596Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.23,-0.78,1.46],\"extents\":[0,0,0],\"angles\":{\"yaw\":0.04,\"pitch\":-0.53,\"roll\":-85.38},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.23,-0.78,1.46]}}}," \
				"\"displayName\":\"view05\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AG7BwHvOKrJJi-kRUac5AVa9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AG7BwHvOKrJJi-kRUac5AVa9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/savedviews?iTwinId=e72496bd-03a5-4ad8-8a51-b14e827603b1&iModelId=4dcf6dee-e7f1-4ed8-81f2-125402b9ac95&$top=100\"}}}"
			);
		}
		else if (method == "DELETE")
		{
			//---------------------------------------------------------------------------
			// DeleteSavedView
			//---------------------------------------------------------------------------
			if (url.ends_with(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02))
			{
				return Response(cpr::status::HTTP_OK, "");
			}
			if (url.ends_with(SAVEDVIEWID_BUILDING_TEST))
			{
				// Error 422
				return Response(cpr::status::HTTP_UNPROCESSABLE_ENTITY,
					"{\"error\":{\"code\":\"InvalidSavedviewsRequest\",\"message\":\"Cannot delete savedview.\",\"details\":[{\"code\":\"InvalidChange\",\"message\":\"Update operations not supported on legacy savedviews.\"}]}}"
				);
			}
		}
		else if (url.ends_with(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02)
			|| (method == "PATCH" && data == "{" SAVEDVIEW_02_DATA "}")
			|| (method == "POST" && data == ADD_SAVEDVIEW_02_DATA))
		{
			//---------------------------------------------------------------------------
			// GetSavedView / AddSavedView / EditSavedView
			// => same response structure for both
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"savedView\":" \
				"{\"id\":\"AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:08:34.797Z\",\"lastModified\":\"2024-06-13T12:26:35.678Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0,0,0]," \
				"\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.79,-0.69,1.59]}}}," \
				"\"displayName\":\"view02\",\"tags\":[],\"extensions\":[]," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"}," \
				"\"image\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}}"
			);
		}
		else if (url.ends_with(SAVEDVIEWID_BUILDING_ALEXVIEW2) && method == "GET")
		{
			// GetSavedView with only 'roll' angle
			return Response(cpr::status::HTTP_OK, "{\"savedView\":" \
				"{\"id\":\"AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw\",\"shared\":true,\"creationTime\":\"2024-08-21T08:31:17.000Z\",\"lastModified\":\"2024-08-21T08:31:17.000Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[61.18413816135583,-5.737108595657904,6.9723644948156185],\"extents\":[2.5791900968344437,1.8184076521127042,1.2895950484174423]," \
				"\"angles\":{\"roll\":-90},\"camera\":{\"lens\":90.00000000000115,\"focusDist\":1.2895950484171959,\"eye\":[62.47373320977305,-7.5267036440751,7.8815683208719705]}," \
				"\"categories\":{\"enabled\":[\"0x20000000057\",\"0x200000000d5\",\"0x200000000d7\",\"0x200000000d9\",\"0x200000000df\",\"0x200000000e1\",\"0x200000000e3\",\"0x200000000e5\"],\"disabled\":[]}," \
				"\"models\":{\"enabled\":[\"0x20000000144\",\"0x20000000146\",\"0x20000000148\",\"0x2000000014a\",\"0x2000000014c\"],\"disabled\":[]}," \
				"\"displayStyle\":{\"viewflags\":{\"renderMode\":6,\"noConstructions\":true,\"ambientOcclusion\":true},\"mapImagery\":{\"backgroundBase\":{\"visible\":true," \
				"\"name\":\"Bing Maps: Aerial Imagery with labels\",\"transparentBackground\":false,\"url\":\"https://dev.test.net/REST/v1/Imagery/Metadata/AerialWithLabels?o=json&incl=ImageryProviders&key={bingKey}\"," \
				"\"formatId\":\"BingMaps\",\"provider\":{\"name\":\"BingProvider\",\"type\":3}}},\"environment\":{\"ground\":{\"display\":false,\"elevation\":-0.01," \
				"\"aboveColor\":{\"red\":0,\"green\":100,\"blue\":0},\"belowColor\":{\"red\":101,\"green\":67,\"blue\":33}},\"sky\":{\"display\":true,\"twoColor\":true," \
				"\"skyColor\":{\"red\":222,\"green\":242,\"blue\":255},\"groundColor\":{\"red\":240,\"green\":236,\"blue\":232},\"zenithColor\":{\"red\":222,\"green\":242,\"blue\":255}," \
				"\"nadirColor\":{\"red\":240,\"green\":236,\"blue\":232}}},\"lights\":{\"portrait\":{\"intensity\":0.8},\"solar\":{\"intensity\":0},\"ambient\":{\"intensity\":0.55}," \
				"\"specularIntensity\":0}}}," \
				"\"legacyView\":{\"id\":\"8ce6a267-10a8-43f5-b6b8-0c1fb5d97973\",\"is2d\":false,\"groupId\":\"-1\",\"name\":\"AlexView2\",\"userId\":\"aabbccdd-aaaa-bbbb-cccc-dddddddd\"," \
				"\"shared\":true,\"categorySelectorProps\":{\"classFullName\":\"BisCore:CategorySelector\",\"code\":{\"spec\":\"0x1\",\"scope\":\"0x1\",\"value\":\"\"}," \
				"\"model\":\"0x10\",\"categories\":[\"0x20000000057\",\"0x200000000d5\",\"0x200000000d7\"]}," \
				"\"modelSelectorProps\":{\"classFullName\":\"BisCore:ModelSelector\",\"code\":{\"spec\":\"0x1\",\"scope\":\"0x1\",\"value\":\"\"},\"model\":\"0x10\"," \
				"\"models\":[\"0x2000000007f\",\"0x20000000134\",\"0x20000000136\",\"0x20000000138\",\"0x2000000013a\",\"0x2000000013c\",\"0x2000000013e\",\"0x20000000140\",\"0x20000000142\"]}," \
				"\"displayStyleProps\":{\"classFullName\":\"BisCore:DisplayStyle3d\"," \
				"\"jsonProperties\":{\"styles\":{\"viewflags\":{\"noConstruct\":true,\"ambientOcclusion\":true,\"renderMode\":6},\"environment\":{\"sky\":{\"skyColor\":16773854," \
				"\"groundColor\":15265008,\"nadirColor\":15265008,\"zenithColor\":16773854,\"twoColor\":true,\"display\":true},\"ground\":{\"elevation\":-0.01,\"aboveColor\":25600," \
				"\"belowColor\":2179941,\"display\":false},\"atmosphere\":{\"atmosphereHeightAboveEarth\":100000,\"exposure\":2,\"densityFalloff\":10,\"depthBelowEarthForMaxDensity\":0," \
				"\"numViewRaySamples\":10,\"numSunRaySamples\":5,\"scatteringStrength\":100,\"wavelengths\":{\"r\":700,\"g\":530,\"b\":440},\"display\":false}}," \
				"\"mapImagery\":{\"backgroundBase\":{\"name\":\"Bing Maps: Aerial Imagery with labels\",\"visible\":true,\"transparentBackground\":false," \
				"\"url\":\"https://dev.test.net/REST/v1/Imagery/Metadata/AerialWithLabels?o=json&incl=ImageryProviders&key={bingKey}\",\"formatId\":\"BingMaps\"," \
				"\"provider\":{\"name\":\"BingProvider\",\"type\":3}}},\"lights\":{\"solar\":{\"intensity\":0},\"ambient\":{\"intensity\":0.55},\"portrait\":{\"intensity\":0.8}," \
				"\"specularIntensity\":0}}},\"code\":{\"spec\":\"0x1\",\"scope\":\"0x1\",\"value\":\"\"},\"model\":\"0x10\"}," \
				"\"viewDefinitionProps\":{\"classFullName\":\"BisCore:SpatialViewDefinition\",\"jsonProperties\":{\"viewDetails\":{}},\"code\":{\"spec\":\"0x1\",\"scope\":\"0x1\"," \
				"\"value\":\"\"},\"model\":\"0x10\",\"categorySelectorId\":\"0\",\"displayStyleId\":\"0\",\"cameraOn\":true,\"origin\":[61.18413816135583,-5.737108595657904,6.9723644948156185]," \
				"\"extents\":[2.5791900968344437,1.8184076521127042,1.2895950484174423],\"angles\":{\"roll\":-90},\"camera\":{\"lens\":90.00000000000115,\"focusDist\":1.2895950484171959," \
				"\"eye\":[62.47373320977305,-7.5267036440751,7.8815683208719705]},\"modelSelectorId\":\"0\"},\"emphasizeElementsProps\":{},\"perModelCategoryVisibility\":[]," \
				"\"hiddenModels\":[],\"hiddenCategories\":[],\"lastModified\":1724229077000,\"extensions\":{\"EmphasizeElements\":{\"emphasizeElementsProps\":{}}," \
				"\"PerModelCategoryVisibility\":{\"perModelCategoryVisibilityProp\":[]}},\"thumbnailId\":\"f552fc81-fe71-49d4-bbcf-2872e2c0e579\"}}," \
				"\"displayName\":\"AlexView2\",\"tags\":[],\"extensions\":[{\"extensionName\":\"EmphasizeElements\"," \
				"\"markdownUrl\":\"https://www.test.com/\",\"schemaUrl\":\"https://www.test.com/\",\"data\":{\"emphasizeElementsProps\":{}}," \
				"\"_links\":{\"iTwin\":{\"href\":\"https://api.test.com/iTwins/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"savedView\":{\"href\":\"https://api.test.com/savedviews/AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw\"}}}," \
				"{\"extensionName\":\"PerModelCategoryVisibility\",\"markdownUrl\":\"https://www.test.com/\"," \
				"\"schemaUrl\":\"https://www.test.com/\",\"data\":{\"perModelCategoryVisibilityProps\":[]},\"_links\":{\"iTwin\":{" \
				"\"href\":\"https://api.test.com/iTwins/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"savedView\":{\"href\":\"https://api.test.com/savedviews/AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw\"}}}]," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/5e15184e-6d3c-43fd-ad04-e28b4b39485e/members/aabbccdd-aaaa-bbbb-cccc-dddddddd\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/5e15184e-6d3c-43fd-ad04-e28b4b39485e\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"image\":{\"href\":\"https://api.test.com/savedviews/AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AWei5oyoEPVDtrgMH7XZeXNOGBVePG39Q60E4otLOUheMCIwzguSSkan4OV67So_Nw/image\"}}}}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

	/// Process /reality-management requests
	Response ProcessRealityDataTest(
		const std::string& url,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		StringMap argMap = ToArgMap(urlArguments);
		StringMap headerMap = ToArgMap(headers);

		std::string const iTwinId = argMap["iTwinId"];

		if (url.ends_with("reality-data/")
			&& (iTwinId == ITWINID_CAYMUS_EAP || iTwinId == ITWINID_NOT_EXISTING)
			&& argMap["types"] == "Cesium3DTiles"
			&& atoi(argMap["$top"].c_str()) >= 100
			&& headerMap["types"] == "Cesium3DTiles"
			&& headerMap["Prefer"] == "return=minimal")
		{
			//---------------------------------------------------------------------------
			// GetRealityData
			//---------------------------------------------------------------------------
			// custom 'Prefer' header: return=minimal
			int const HeaderStatus = CheckRequiredHeaders(headers,
				"application/vnd.bentley.itwin-platform.v1+json",
				"return=minimal",
				ITWINTEST_ACCESS_TOKEN);
			if (HeaderStatus != cpr::status::HTTP_OK)
			{
				return Response(HeaderStatus, "Error in headers.");
			}
			if (iTwinId == ITWINID_CAYMUS_EAP)
			{
				return Response(cpr::status::HTTP_OK,
					"{\r\n  \"realityData\": [\r\n    {\r\n      \"id\": \"21b5896b-5dbd-41a7-9f23-d988a1847c11\",\r\n " \
					"      \"displayName\": \"Orlando_CesiumDraco_LAT\",\r\n      \"type\": \"Cesium3DTiles\"\r\n    }\r\n  ],\r\n  " \
					"    \"_links\": {\r\n      \"next\": null\r\n    }\r\n}"
				);
			}
			else
			{
				// with wrong ID => Error 422
				return Response(cpr::status::HTTP_UNPROCESSABLE_ENTITY,
					"{\"error\":{\"code\":\"InvalidRealityDataRequest\",\"message\":\"Invalid RealityData request.\",\"details\":[" \
					"{\"code\":\"InvalidParameter\",\"message\":\"The value 'toto' is not valid.\",\"target\":\"iTwinId\"}]," \
					"\"_seqUrl\":\"https://seq.test.com/#/events?filter=ActivityId%3D'dbdeb682-6b9d-4fc0-81f3-6db7621df5f8'&from=2024-06-19T12:59:05.3448458Z&to=2024-06-19T13:01:05.3468092Z\"," \
					"\"_applicationInsightsUrl\":\"https://portal.test.com/#blade/Test_Monitoring_Logs/LogsBlade/resourceId/%2Fsubscriptions%2F57b27da1-4c97-ababab" \
					"%2FresourceGroups%2Fprod-RealityDataServices-eus-rg%2Fproviders%2FTest.Insights%2Fcomponents%2Fprod-realitydataservicesapp-eus/source/AIExtension.DetailsV2/query/" \
					"%0D%0A%2F%2F%20All%20telemetry%20for%20Operation%20ID%3A%2041f7cd5fe24dc703abe6299aa7304b7f%0D%0A%2F%2F%20Entries%20can%20take%20several%20minutes%20to%20appear%0D%0A" \
					"union%20*%0D%0A%2F%2F%20Apply%20filters%0D%0A%7C%20where%20timestamp%20%3E%20datetime(%222024-06-19T13%3A00%3A05.3448458Z%22)%20and%20timestamp%3Cdatetime(%222024-06-19T13" \
					"%3A01%3A05.3468092Z%22)%0D%0A%7C%20where%20operation_Id%20%3D%3D%20%2241f7cd5fe24dc703abe6299aa7304b7f%22\"}}"
				);
			}
		}

		CHECK_ITWIN_HEADERS("v1");

		if (url.ends_with(REALITYDATAID_ORLANDO)
			&& iTwinId == ITWINID_CAYMUS_EAP)
		{
			//---------------------------------------------------------------------------
			// GetRealityData3DInfo - part 1
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK,
				"{\r\n  \"realityData\": {\r\n    \"id\": \"21b5896b-5dbd-41a7-9f23-d988a1847c11\",\r\n    \"displayName\": \"Orlando_CesiumDraco_LAT\"," \
				"\r\n    \"classification\": \"Model\",\r\n    \"type\": \"Cesium3DTiles\"," \
				"\r\n    \"rootDocument\": \"Orlando_CesiumDraco_LAT.json\"," \
				"\r\n    \"dataCenterLocation\" : \"East US\",\r\n    \"authoring\" : false,\r\n    \"size\" : 3164951," \
				"\r\n    \"extent\" : {\r\n      \"southWest\": {\r\n        \"latitude\": 28.496424905782874,\r\n        \"longitude\" : -81.42035061172474\r\n      }," \
				"\r\n      \"northEast\" : {\r\n        \"latitude\": 28.587753137096165,\r\n        \"longitude\" : -81.33756635398319\r\n      }\r\n    }," \
				"\r\n    \"accessControl\": \"ITwin\",\r\n    \"modifiedDateTime\" : \"2024-05-27T12:20:01Z\"," \
				"\r\n    \"lastAccessedDateTime\" : \"2024-06-18T08:07:48Z\"," \
				"\r\n    \"createdDateTime\" : \"2024-03-19T12:39:00Z\",\r\n    \"ownerId\" : \"aabbccdd-aaaa-bbbb-cccc-dddddddd\"}}"
			);
		}
		if (url.ends_with("readaccess")
			&& url.find(REALITYDATAID_ORLANDO) != std::string::npos
			&& iTwinId == ITWINID_CAYMUS_EAP)
		{
			//---------------------------------------------------------------------------
			// GetRealityData3DInfo - part 2
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK,
				"{\r\n  \"type\": \"AzureBlobSasUrl\",\r\n  \"access\": \"Read\",\r\n  \"_links\": {\r\n    \"containerUrl\":{\r\n      " \
				"\"href\": \"https://realityblob59.blob.core.net/21b5896b-5dbd-41a7-9f23-d988a1847c11?skoid=6db55139-0f1c-467a-95b4-5009c17c1bf0" \
				"\u0026sktid=067e9632-ea4c-4ed9-9e6d-e294956e284b\u0026skt=2024-06-18T17%3A42%3A00Z\u0026ske=2024-06-21T17%3A42%3A00Z\u0026sks=b\u0026skv=2024-05-04" \
				"\u0026sv=2024-05-04\u0026st=2024-06-18T20%3A11%3A05Z\u0026se=2024-06-19T23%3A59%3A59Z\u0026sr=c\u0026sp=rl\u0026sig=0qSqX3OF4qlyYeHUc8hT61NCI%3D\"}" \
				"\r\n    }\r\n}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}

	/// Process /imodel/rpc/ requests
	Response ProcessIModelRPCTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_ITWIN_HEADERS("v1");
		if (url.ends_with("/PresentationRpcInterface-4.1.0-getElementProperties")
			&& url.find("/" ITWINID_CAYMUS_EAP "/") != std::string::npos
			&& url.find("/" IMODELID_BUILDING "/") != std::string::npos
			&& url.find("/" CHANGESETID_BUILDING "/") != std::string::npos
			&& method == "POST"
			&& data == "[{\"key\":\"" IMODELID_BUILDING ":" CHANGESETID_BUILDING "\",\"iTwinId\":\"" ITWINID_CAYMUS_EAP "\",\"iModelId\":\"" IMODELID_BUILDING  "\",\"changeset\":{\"id\":\"" CHANGESETID_BUILDING "\"}},{\"elementId\":\"0x20000001baf\"}]")
		{
			//---------------------------------------------------------------------------
			// GetElementProperties
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK,
				"{\"statusCode\":0,\"result\":{\"class\":\"Physical Object\",\"id\":\"0x20000001baf\",\"label\":\"Shape [2-309]\"," \
				"\"items\":{\"@Presentation:selectedItems.categoryLabel@\":{\"type\":\"category\",\"items\":{\"Model\":{\"type\":\"primitive\",\"value\":\"West Wing, BSI300AE9-Shell.dgn, Composite\"}," \
				"\"Code\":{\"type\":\"primitive\",\"value\":\"\"},\"User Label\":{\"type\":\"primitive\",\"value\":\"Shape\"},\"Category\":{\"type\":\"primitive\",\"value\":\"A-G321-G3-Windw\"}," \
				"\"Physical Material\":{\"type\":\"primitive\",\"value\":\"\"},\"Source Information\":{\"type\":\"category\",\"items\":{\"Source Element ID\":{\"type\":\"array\",\"valueType\":\"primitive\"," \
				"\"values\":[\"45631\"]},\"Model Source\":{\"type\":\"category\",\"items\":{\"Repository Link\":{\"type\":\"array\",\"valueType\":\"struct\"," \
				"\"values\":[{\"Path\":{\"type\":\"primitive\",\"value\":\"F:/Bentley/BuildingProject/Workspace/Projects/Building Project/dgn/BSI300AE9-Shell.dgn\"}," \
				"\"Name\":{\"type\":\"primitive\",\"value\":\"BSI300AE9-Shell.dgn\"}}]}}},\"Document Link\":{\"type\":\"category\",\"items\":{\"Repository Link\":{\"type\":\"array\",\"valueType\":\"struct\"," \
				"\"values\":[{\"Code\":{\"type\":\"primitive\",\"value\":\"bsi300ae9-shell.dgn\"},\"Name\":{\"type\":\"primitive\",\"value\":\"BSI300AE9-Shell.dgn\"},\"Path\":{\"type\":\"primitive\"," \
				"\"value\":\"F:/Bentley/BuildingProject/Workspace/Projects/Building Project/dgn/BSI300AE9-Shell.dgn\"},\"Description\":{\"type\":\"primitive\",\"value\":\"\"}," \
				"\"Format\":{\"type\":\"primitive\",\"value\":\"\"}}]}}}}}}}}}}"
			);
		}
		if (url.ends_with("/IModelReadRpcInterface-3.6.0-getConnectionProps")
			&& method == "POST")
		{
			//---------------------------------------------------------------------------
			// GetIModelProperties
			//---------------------------------------------------------------------------
			if (url.find("/" ITWINID_CAYMUS_EAP "/") != std::string::npos
				&& url.find("/" IMODELID_BUILDING "/") != std::string::npos
				&& url.find("/" CHANGESETID_BUILDING "/") != std::string::npos
				&& data == "[{\"iTwinId\":\"" ITWINID_CAYMUS_EAP "\",\"iModelId\":\"" IMODELID_BUILDING  "\",\"changeset\":{\"id\":\"" CHANGESETID_BUILDING "\"}}]")
			{
				return Response(cpr::status::HTTP_OK,
					"{\"name\":\"Building\",\"rootSubject\":{\"name\":\"Building\"},\"projectExtents\":{\"low\":[-244.59492798331735,-303.66127815647087,-28.27051340710871]," \
					"\"high\":[409.678652192302,249.78031406156776,33.397180631459555]},\"globalOrigin\":[0,0,0],\"key\":\"cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49:694305dbe2e5626267920f6a3f1e23db707674ba\"," \
					"\"iTwinId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\",\"iModelId\":\"cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49\",\"changeset\":{\"id\":\"694305dbe2e5626267920f6a3f1e23db707674ba\",\"index\":12}}"
				);
			}
			if (url.find("/" ITWINID_STADIUM_RN_QA "/") != std::string::npos
				&& url.find("/" IMODELID_STADIUM "/") != std::string::npos
				&& url.find("/" CHANGESETID_STADIUM "/") != std::string::npos
				&& data == "[{\"iTwinId\":\"" ITWINID_STADIUM_RN_QA "\",\"iModelId\":\"" IMODELID_STADIUM  "\",\"changeset\":{\"id\":\"" CHANGESETID_STADIUM "\"}}]")
			{
				return Response(cpr::status::HTTP_OK,
					"{\"name\":\"Stadium QA 04 22\",\"rootSubject\":{\"name\":\"Stadium QA 04 22\"},\"projectExtents\":{\"low\":[32344.267871807926,31348.272780176438,-478.7556455931467]," \
					"\"high\":[33088.69387347796,32680.341868920772,144.21825526358407]},\"globalOrigin\":[0,0,0],\"ecefLocation\":{\"origin\":[-1497600.1543352203,6198968.877963936,112371.07286524471]," \
					"\"orientation\":{\"pitch\":-0.0009652883917540237,\"roll\":88.69419530866284,\"yaw\":-166.12431911119472}," \
					"\"transform\":[[-0.9707926309201866,0.005448576994240284,-0.2397540955179029,-1497600.1543352203],[-0.23980964627116919,-0.02212705835700035,0.9705661505307014,6198968.877963936]," \
					"[-0.000016847014194354415,0.9997136355086695,0.02278861835233834,112371.07286524471]],\"cartographicOrigin\":{\"latitude\":0.022790512521193126,\"longitude\":1.812972949468464," \
					"\"height\":-167.26869516478132},\"xVector\":[-0.9707926309201866,-0.23980964627116919,-0.000016847014194354415]," \
					"\"yVector\":[0.005448576994240284,-0.02212705835700035,0.9997136355086695]},\"geographicCoordinateSystem\":{\"horizontalCRS\":{\"id\":\"EPSG:3414\",\"description\":\"SVY21 / Singapore TM\"," \
					"\"source\":\"EPSG version 7.6\",\"epsg\":3414,\"datumId\":\"SVY21\",\"datum\":{\"id\":\"SVY21\",\"description\":\"Singapore SVY21\"," \
					"\"source\":\"Various including Singapore Land Authority\",\"ellipsoidId\":\"WGS84\",\"ellipsoid\":{\"equatorialRadius\":6378137," \
					"\"polarRadius\":6356752.3142,\"id\":\"WGS84\",\"description\":\"World Geodetic System of 1984, GEM 10C\",\"source\":\"US Defense Mapping Agency, TR-8350.2-B, December 1987\"," \
					"\"epsg\":7030},\"transforms\":[{\"method\":\"None\"}]},\"unit\":\"Meter\",\"projection\":{\"method\":\"TransverseMercator\",\"falseEasting\":28001.642,\"falseNorthing\":38744.572," \
					"\"centralMeridian\":103.83333333333331,\"latitudeOfOrigin\":1.3666666666666667,\"scaleFactor\":1},\"extent\":{\"southWest\":{\"latitude\":1.1166666666666667," \
					"\"longitude\":103.61666666666666},\"northEast\":{\"latitude\":1.45,\"longitude\":104.15}}},\"verticalCRS\":{\"id\":\"ELLIPSOID\"}}," \
					"\"key\":\"e04bfa36-d4ce-4482-8057-cbd73ec80d23:50c2eb88e48e7556635504cec91a6811b5939122\",\"iTwinId\":\"a2a1ee72-7fb2-402d-a588-1d873aeaff3e\"," \
					"\"iModelId\":\"e04bfa36-d4ce-4482-8057-cbd73ec80d23\",\"changeset\":{\"id\":\"50c2eb88e48e7556635504cec91a6811b5939122\",\"index\":63}}"
				);
			}
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}
};

/*static*/
std::unique_ptr<httpmock::MockServer> FITwinMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	return httpmock::getFirstRunningMockServer<FITwinMockServer>(startPort, tryCount);
}


class ITwinTestWebServicesObserver : public FITwinDefaultWebServicesObserver
{
public:
	virtual const TCHAR* GetObserverName() const override {
		return TEXT("TestObserver");
	}

	// Must be called *before* a new request is made, as the name says.
	void AddPendingRequest() { NumPendingRequests++; }
	void OnResponseReceived() {
		NumProcessedRequests++;
		ensureMsgf(NumProcessedRequests <= NumPendingRequests, TEXT("received more answers than expected!"));
	}
	bool IsWaitingForServerResponse() const { return NumProcessedRequests < NumPendingRequests; }


#define IMPLEMENT_OBS_CALLBACK( CallbackName, InfoClass )	\
	std::function<bool(bool, InfoClass const&)> CallbackName ## Func;			\
															\
	virtual void CallbackName(bool bSuccess, InfoClass const& Info) override	\
	{														\
		OnResponseReceived();								\
		if (CallbackName ## Func)							\
		{													\
			CallbackName ## Func(bSuccess, Info);			\
		}													\
	}

#define IMPLEMENT_OBS_CALLBACK_TWO_ARGS( CallbackName, InfoClass1, InfoClass2)	\
	std::function<bool(bool, InfoClass1 const&, InfoClass2 const&)> CallbackName ## Func;	\
															\
	virtual void CallbackName(bool bSuccess, InfoClass1 const& Info1, InfoClass2 const& Info2) override	\
	{														\
		OnResponseReceived();								\
		if (CallbackName ## Func)							\
		{													\
			CallbackName ## Func(bSuccess, Info1, Info2);	\
		}													\
	}

	IMPLEMENT_OBS_CALLBACK(OnITwinInfoRetrieved, FITwinInfo);
	IMPLEMENT_OBS_CALLBACK(OnITwinsRetrieved, FITwinInfos);
	IMPLEMENT_OBS_CALLBACK(OnIModelsRetrieved, FIModelInfos);
	IMPLEMENT_OBS_CALLBACK(OnChangesetsRetrieved, FChangesetInfos);

	IMPLEMENT_OBS_CALLBACK(OnExportInfosRetrieved, FITwinExportInfos);
	IMPLEMENT_OBS_CALLBACK(OnExportInfoRetrieved, FITwinExportInfo);
	IMPLEMENT_OBS_CALLBACK(OnExportStarted, FString);

	IMPLEMENT_OBS_CALLBACK(OnSavedViewInfosRetrieved, FSavedViewInfos);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewRetrieved, FSavedView, FSavedViewInfo);
	IMPLEMENT_OBS_CALLBACK(OnSavedViewAdded, FSavedViewInfo);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewDeleted, FString, FString);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewEdited, FSavedView, FSavedViewInfo);

	IMPLEMENT_OBS_CALLBACK(OnRealityDataRetrieved, FITwinRealityDataInfos);
	IMPLEMENT_OBS_CALLBACK(OnRealityData3DInfoRetrieved, FITwinRealityData3DInfo);

	IMPLEMENT_OBS_CALLBACK(OnElementPropertiesRetrieved, FElementProperties);

	std::function<bool(bool, bool, FProjectExtents const&, bool, FEcefLocation const&)> OnIModelPropertiesRetrievedFunc;

	virtual void OnIModelPropertiesRetrieved(bool bSuccess,
		bool bHasExtents, FProjectExtents const& Extents,
		bool bHasEcefLocation, FEcefLocation const& EcefLocation) override
	{
		OnResponseReceived();
		if (OnIModelPropertiesRetrievedFunc)
		{
			OnIModelPropertiesRetrievedFunc(bSuccess, bHasExtents, Extents, bHasEcefLocation, EcefLocation);
		}
	}

private:
	int NumPendingRequests = 0;
	int NumProcessedRequests = 0;
};

using TestObserverPtr = std::shared_ptr<ITwinTestWebServicesObserver>;

class FITwinAPITestHelper
{
public:
	static FITwinAPITestHelper& Instance();
	~FITwinAPITestHelper();
	bool Init();
	void Cleanup();

	/// Return URL server is listening at. E.g.: http://localhost:8080
	std::string GetServerUrl() const;

	TestObserverPtr const& GetObserver() const { return Observer; }
	TObjectPtr<UITwinWebServices> const& GetWebServices() const { return WebServices; }

private:
	FITwinAPITestHelper() {}

	std::unique_ptr<httpmock::MockServer> MockServer;
	TObjectPtr<UITwinWebServices> WebServices;
	TObjectPtr<AITwinServerConnection> ServerConnection;
	TestObserverPtr Observer;
	bool bInitDone = false;
};

/*static*/
FITwinAPITestHelper& FITwinAPITestHelper::Instance()
{
	static FITwinAPITestHelper instance;
	return instance;
}

bool FITwinAPITestHelper::Init()
{
	if (MockServer && WebServices && Observer)
	{
		// already initialized
		return true;
	}

	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8080;

	MockServer = FITwinMockServer::MakeServer(DEFAULT_SERVER_PORT);
	if (!ensureMsgf(MockServer && MockServer->isRunning(), TEXT("Mock Server not started!")))
	{
		return false;
	}

	// totally disable error logs (even though SuppressLogErrors avoids making the unit-test fail, the test
	// target still fails at the end because of the logs...)
	UITwinWebServices::SetLogErrors(false);

	const std::string url = GetServerUrl();

	WebServices = NewObject<UITwinWebServices>();
	ServerConnection = NewObject<AITwinServerConnection>();

	const SDK::Core::EITwinEnvironment Env = SDK::Core::EITwinEnvironment::Prod;
	ServerConnection->Environment = static_cast<EITwinEnvironment>(Env);
	FITwinAuthorizationManager::GetInstance(Env)->SetOverrideAccessToken(ITWINTEST_ACCESS_TOKEN);
	WebServices->SetServerConnection(ServerConnection);
	WebServices->SetTestServerURL(url.c_str());
	Observer = std::make_shared<ITwinTestWebServicesObserver>();
	WebServices->SetObserver(Observer.get());
	return true;
}

void FITwinAPITestHelper::Cleanup()
{
	MockServer.reset();
	if (WebServices)
	{
		WebServices->SetObserver(nullptr);
	}
	WebServices = nullptr;
	ServerConnection = nullptr;
	Observer.reset();
}

FITwinAPITestHelper::~FITwinAPITestHelper()
{
	Cleanup();
}

std::string FITwinAPITestHelper::GetServerUrl() const
{
	if (MockServer)
	{
		std::ostringstream url;
		url << "http://localhost:" << MockServer->getPort();
		return url.str();
	}
	return {};
}


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForMockServerResponse, TestObserverPtr, Observer);

bool FNUTWaitForMockServerResponse::Update()
{
	return !Observer || !Observer->IsWaitingForServerResponse();
}


// WebServices does log at Error level in case of errors, which by default would flag the test as failed
// => Use an intermediate class to change this behavior
//
class FAutomationTestBaseNoLogs : public FAutomationTestBase
{
public:
	FAutomationTestBaseNoLogs(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{

	}
	virtual bool SuppressLogErrors() override { return true; }
	virtual bool SuppressLogWarnings() override { return true; }
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinWebServicesRequestTest, FAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.WebServicesRequest", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinWebServicesRequestTest::RunTest(const FString& /*Parameters*/)
{
	FITwinAPITestHelper& Helper = FITwinAPITestHelper::Instance();
	if (!Helper.Init())
	{
		return false;
	}
	auto const& Observer = Helper.GetObserver();
	auto const& WebServices = Helper.GetWebServices();
	const std::string url = Helper.GetServerUrl();

	auto const TestErrorMessage = [this, &WebServices](FString const& expectedMessage)
	{
		FString LastError;
		UTEST_TRUE("Get Last Error", FITwinAPITestHelper::Instance().GetWebServices()
			->ConsumeLastError(LastError));
		UTEST_EQUAL("Compare Error", LastError, expectedMessage);
		return true;
	};

	SECTION("MockServer Validation")
	{
		// Most basic test, just to validate the mock server
		// This one is synchronous.
		cpr::Response r = cpr::Get(cpr::Url{ url + "/arg_test?x=0&b=2" });
		UTEST_EQUAL("status_code", 200, r.status_code);
	}

	SECTION("ITwin GetITwinInfo")
	{
		FString const ITwinId = TEXT(ITWINID_CAYMUS_EAP);

		Observer->OnITwinInfoRetrievedFunc = [this, ITwinId, TestErrorMessage](bool bSuccess, FITwinInfo const& ITwinIfo)
		{
			if (bSuccess)
			{
				UTEST_EQUAL("Id", ITwinIfo.Id, ITwinId);
				UTEST_EQUAL("DisplayName", ITwinIfo.DisplayName, TEXT("Bentley Caymus EAP"));
				UTEST_EQUAL("Status", ITwinIfo.Status, TEXT("Active"));
			}
			else
			{
				UTEST_TRUE("CheckError", TestErrorMessage(
					TEXT("[GetITwinInfo] code 404: Not Found\n\tError [iTwinNotFound]: Requested iTwin is not available.")));
			}
			return true;
		};
		// test success
		Observer->AddPendingRequest();
		WebServices->GetITwinInfo(ITwinId);

		// test failure
		Observer->AddPendingRequest();
		WebServices->GetITwinInfo(ITWINID_NOT_EXISTING);
	}

	SECTION("ITwin GetITwins")
	{
		Observer->AddPendingRequest();
		Observer->OnITwinsRetrievedFunc = [this](bool bSuccess, FITwinInfos const& ITwinArray)
		{
			UTEST_TRUE("Get ITwins request result", bSuccess);
			TArray<FITwinInfo> const& iTwins(ITwinArray.iTwins);
			UTEST_EQUAL("Num", iTwins.Num(), 3);

			UTEST_EQUAL("Id", iTwins[0].Id, TEXT("e72496bd-03a5-4ad8-8a51-b14e827603b1"));
			UTEST_EQUAL("DisplayName", iTwins[0].DisplayName, TEXT("Tests_AlexW"));
			UTEST_EQUAL("Status", iTwins[0].Status, TEXT("Active"));

			UTEST_EQUAL("Id", iTwins[1].Id, TEXT(ITWINID_CAYMUS_EAP));
			UTEST_EQUAL("DisplayName", iTwins[1].DisplayName, TEXT("Bentley Caymus EAP"));
			UTEST_EQUAL("Status", iTwins[1].Status, TEXT("Active"));

			UTEST_EQUAL("Id", iTwins[2].Id, TEXT("257af6c2-b2fa-41fd-b85d-b90837f36934"));
			UTEST_EQUAL("DisplayName", iTwins[2].DisplayName, TEXT("ConExpo 2023 - Civil"));
			UTEST_EQUAL("Status", iTwins[2].Status, TEXT("Active"));
			return true;
		};
		WebServices->GetiTwins();
	}

	SECTION("Get iTwin iModels")
	{
		FString const ITwinId = TEXT(ITWINID_CAYMUS_EAP);

		Observer->OnIModelsRetrievedFunc = [this, TestErrorMessage](bool bSuccess, FIModelInfos const& Infos)
		{
			TArray<FIModelInfo> const& iModels(Infos.iModels);
			if (bSuccess)
			{
				UTEST_EQUAL("Num", iModels.Num(), 6);

				UTEST_EQUAL("Id", iModels[0].Id, TEXT("cb6052c0-c4a0-41c3-8914-b1ba7d8b8c49"));
				UTEST_EQUAL("DisplayName", iModels[0].DisplayName, TEXT("Building"));

				UTEST_EQUAL("Id", iModels[1].Id, TEXT("e241cf6e-8d80-4cd8-bc67-2ad078a1a693"));
				UTEST_EQUAL("DisplayName", iModels[1].DisplayName, TEXT("Hatch Terrain Model"));

				UTEST_EQUAL("Id", iModels[2].Id, TEXT("d7f5dd60-08ea-46e1-8eec-3763f18c1c6a"));
				UTEST_EQUAL("DisplayName", iModels[2].DisplayName, TEXT("Highway"));

				UTEST_EQUAL("Id", iModels[3].Id, TEXT("ad358f03-5488-44e4-bc1f-42a610b99694"));
				UTEST_EQUAL("DisplayName", iModels[3].DisplayName, TEXT("MetroStation"));

				UTEST_EQUAL("Id", iModels[4].Id, TEXT("c2019b23-4501-41f3-b933-02e73ca5621b"));
				UTEST_EQUAL("DisplayName", iModels[4].DisplayName, TEXT("OffshoreRig"));

				UTEST_EQUAL("Id", iModels[5].Id, TEXT("d66fcd8c-604a-41d6-964a-b9767d446c53"));
				UTEST_EQUAL("DisplayName", iModels[5].DisplayName, TEXT("WindTurbine"));
			}
			else
			{
				UTEST_EQUAL("Num", iModels.Num(), 0);
				UTEST_TRUE("CheckError", TestErrorMessage(
					TEXT("[GetIModels] code 422: Unknown\n\tError [InvalidiModelsRequest]: Cannot get iModels.\n\tDetails: [InvalidValue] 'toto' is not a valid 'iTwinId' value. (target: iTwinId)")));
			}
			return true;
		};
		// test success
		Observer->AddPendingRequest();
		WebServices->GetiTwiniModels(ITwinId);

		// test failure
		Observer->AddPendingRequest();
		WebServices->GetiTwiniModels(ITWINID_NOT_EXISTING);
	}

	SECTION("Get iModel Changesets")
	{
		Observer->AddPendingRequest();
		Observer->OnChangesetsRetrievedFunc = [this](bool bSuccess, FChangesetInfos const& Infos)
		{
			UTEST_TRUE("Get iModel Changesets request result", bSuccess);
			TArray<FChangesetInfo> const& Changesets(Infos.Changesets);
			UTEST_EQUAL("Num", Changesets.Num(), 4);

			UTEST_EQUAL("Id", Changesets[0].Id, TEXT("943762e9afe5239d74623cf5081502df23c7816d"));
			UTEST_EQUAL("DisplayName", Changesets[0].DisplayName, TEXT("4"));
			UTEST_EQUAL("Description", Changesets[0].Description, TEXT("MicroStation Connector - initalLoad - Initialization changes"));
			UTEST_EQUAL("Index", Changesets[0].Index, 4);

			UTEST_EQUAL("Id", Changesets[1].Id, TEXT("a579fa8c3a3dda5a04df9c3b87416de0df3a2d66"));
			UTEST_EQUAL("DisplayName", Changesets[1].DisplayName, TEXT("3"));
			UTEST_EQUAL("Description", Changesets[1].Description, TEXT("MicroStation Connector - initalLoad - BootstrapExternalSources"));
			UTEST_EQUAL("Index", Changesets[1].Index, 3);

			UTEST_EQUAL("Id", Changesets[2].Id, TEXT("db3c0e50fad288ad5af7ccfe53725de4c9876153"));
			UTEST_EQUAL("DisplayName", Changesets[2].DisplayName, TEXT("2"));
			UTEST_EQUAL("Description", Changesets[2].Description, TEXT("MicroStation Connector - initalLoad - Domain schema upgrade"));
			UTEST_EQUAL("Index", Changesets[2].Index, 2);

			UTEST_EQUAL("Id", Changesets[3].Id, TEXT("4681a740b4d10e171d885a83bf3d507edada91cf"));
			UTEST_EQUAL("DisplayName", Changesets[3].DisplayName, TEXT("1"));
			UTEST_EQUAL("Description", Changesets[3].Description, TEXT("MicroStation Connector - Domain schema upgrade"));
			UTEST_EQUAL("Index", Changesets[3].Index, 1);
			return true;
		};
		// (WindTurbine)
		WebServices->GetiModelChangesets(TEXT(IMODELID_WIND_TURBINE));
	}

	FString const WindTurbine_CesiumExportId = TEXT(EXPORTID_WIND_TURBINE_CESIUM);
	FString const WindTurbine_ChangesetId = TEXT("9641026f8e6370db8cc790fab8943255af57d38e");
	FString const WindTurbine_MeshUrl =
		TEXT("https://gltf59.blob.net/6e5a59b7-878b-4275-b960-8668dc11a04d/tileset.json?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D");

	SECTION("GetExports")
	{
		Observer->OnExportInfosRetrievedFunc = [=,this](bool bSuccess, FITwinExportInfos const& Infos)
		{
			UTEST_TRUE("Get Exports request result", bSuccess);
			TArray<FITwinExportInfo> const& ExportInfos(Infos.ExportInfos);
			if (ExportInfos[0].Id == WindTurbine_CesiumExportId)
			{
				// WindTurbine
				UTEST_EQUAL("DisplayName", ExportInfos[0].DisplayName, TEXT("WindTurbine"));
				UTEST_EQUAL("Status", ExportInfos[0].Status, TEXT("Complete"));
				UTEST_EQUAL("iModelId", ExportInfos[0].iModelId, TEXT(IMODELID_WIND_TURBINE));
				UTEST_EQUAL("iTwinId", ExportInfos[0].iTwinId, TEXT(ITWINID_CAYMUS_EAP));
				UTEST_EQUAL("ChangesetId", ExportInfos[0].ChangesetId, WindTurbine_ChangesetId);
				UTEST_EQUAL("MeshUrl", ExportInfos[0].MeshUrl, WindTurbine_MeshUrl);
			}
			else
			{
				// PhotoRealisticRendering
				// we only keep one now in SDK::Core::ITwinWebServices::GetExports
				UTEST_EQUAL("NumExports", ExportInfos.Num(), 1/*Was: 3*/);
				for (FITwinExportInfo const& Info : ExportInfos)
				{
					UTEST_EQUAL("DisplayName", Info.DisplayName, TEXT("PhotoRealisticRendering"));
					UTEST_EQUAL("Status", Info.Status, TEXT("Complete"));
					UTEST_EQUAL("iModelId", Info.iModelId, TEXT(IMODELID_PHOTO_REALISTIC_RENDERING));
					UTEST_EQUAL("iTwinId", Info.iTwinId, TEXT(ITWINID_TESTS_ALEXW));
					UTEST_TRUE("No changesetId", Info.ChangesetId.IsEmpty());
				}
				// result is sorted by date, with only Cesium exports
				UTEST_EQUAL("Id 0", ExportInfos[0].Id, TEXT("ed456436-ed0a-488c-a5f2-4115e7d8e311"));
				//UTEST_EQUAL("Id 1", ExportInfos[1].Id, TEXT("1485a12a-c4f6-416f-bb79-e1fe478a3220"));
				// I left the old export version 0.2[.0] for this one, instead of 0.2.8.1, so that it was
				// filtered out by WebServices, but we can no longer (and should not) test versions
				//UTEST_EQUAL("Id 2", ExportInfos[2].Id, TEXT("00af52a3-a416-4e37-99e9-6de56368bc37"));
			}
			return true;
		};
		Observer->AddPendingRequest();
		WebServices->GetExports(TEXT(IMODELID_WIND_TURBINE), WindTurbine_ChangesetId);

		Observer->AddPendingRequest();
		WebServices->GetExports(TEXT(IMODELID_PHOTO_REALISTIC_RENDERING), {}); // This one has no changeset
	}

	SECTION("GetExportInfo")
	{
		Observer->AddPendingRequest();
		Observer->OnExportInfoRetrievedFunc = [=,this](bool bSuccess, FITwinExportInfo const& Info)
		{
			UTEST_TRUE("Get Export Info request result", bSuccess);

			UTEST_EQUAL("Id", Info.Id, WindTurbine_CesiumExportId);
			UTEST_EQUAL("DisplayName", Info.DisplayName, TEXT("WindTurbine"));
			UTEST_EQUAL("Status", Info.Status, TEXT("Complete"));
			UTEST_EQUAL("iModelId", Info.iModelId, TEXT(IMODELID_WIND_TURBINE));
			UTEST_EQUAL("iTwinId", Info.iTwinId, TEXT(ITWINID_CAYMUS_EAP));
			UTEST_EQUAL("ChangesetId", Info.ChangesetId, WindTurbine_ChangesetId);
			UTEST_EQUAL("MeshUrl", Info.MeshUrl, WindTurbine_MeshUrl);
			return true;
		};
		WebServices->GetExportInfo(WindTurbine_CesiumExportId);
	}

	SECTION("StartExport")
	{
		Observer->AddPendingRequest();
		Observer->OnExportStartedFunc = [this](bool bSuccess, FString const& InExportId)
		{
			UTEST_TRUE("Start Export request result", bSuccess);
			UTEST_EQUAL("ExportId", InExportId, TEXT("70abfe04-f791-4cba-b7e6-d4d402dda844"));
			return true;
		};
		WebServices->StartExport(IMODELID_STADIUM, CHANGESETID_STADIUM);
	}

	SECTION("GetAllSavedViews")
	{
		Observer->AddPendingRequest();
		Observer->OnSavedViewInfosRetrievedFunc = [this](bool bSuccess, FSavedViewInfos const& Infos)
		{
			UTEST_TRUE("Get All Saved Views request result", bSuccess);

			TArray<FSavedViewInfo> const& SavedViews(Infos.SavedViews);
			UTEST_EQUAL("Num", SavedViews.Num(), 5);

			UTEST_EQUAL("Id", SavedViews[0].Id, TEXT("AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"));
			UTEST_EQUAL("DisplayName", SavedViews[0].DisplayName, TEXT("view01"));
			UTEST_EQUAL("Shared", SavedViews[0].bShared, true);

			UTEST_EQUAL("Id", SavedViews[1].Id, TEXT("AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"));
			UTEST_EQUAL("DisplayName", SavedViews[1].DisplayName, TEXT("view02"));
			UTEST_EQUAL("Shared", SavedViews[1].bShared, true);

			UTEST_EQUAL("Id", SavedViews[2].Id, TEXT("AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"));
			UTEST_EQUAL("DisplayName", SavedViews[2].DisplayName, TEXT("view03 - top"));
			UTEST_EQUAL("Shared", SavedViews[2].bShared, true);

			UTEST_EQUAL("Id", SavedViews[3].Id, TEXT("AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"));
			UTEST_EQUAL("DisplayName", SavedViews[3].DisplayName, TEXT("view04"));
			UTEST_EQUAL("Shared", SavedViews[3].bShared, true);

			UTEST_EQUAL("Id", SavedViews[4].Id, TEXT("AG7BwHvOKrJJi-kRUac5AVa9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"));
			UTEST_EQUAL("DisplayName", SavedViews[4].DisplayName, TEXT("view05"));
			UTEST_EQUAL("Shared", SavedViews[4].bShared, true);

			return true;
		};
		WebServices->GetAllSavedViews(ITWINID_TESTS_ALEXW, IMODELID_PHOTO_REALISTIC_RENDERING);
	}

	// Get/Edit/Add SavedView both expect the same kind of response, so we share the same callback
	auto CheckSavedView = [this](FSavedView const& SV)
	{
		UTEST_TRUE("Origin", FVector::PointsAreNear(SV.Origin, FVector(-1.79, -0.69, 1.59), UE_SMALL_NUMBER));
		UTEST_TRUE("Extents", FVector::PointsAreNear(SV.Extents, FVector(0, 0, 0), UE_SMALL_NUMBER));
		UTEST_TRUE("Angles", FMath::IsNearlyEqual(SV.Angles.Yaw, -1.69, UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(SV.Angles.Pitch, -50.43, UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(SV.Angles.Roll, -92.19, UE_SMALL_NUMBER));
		return true;
	};
	auto CheckSavedViewInfo = [this](FSavedViewInfo const& Info)
	{
		UTEST_EQUAL("Id", Info.Id, TEXT(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02));
		UTEST_EQUAL("DisplayName", Info.DisplayName, TEXT("view02"));
		UTEST_EQUAL("Shared", Info.bShared, true);
		return true;
	};

	SECTION("GetSavedView")
	{
		Observer->OnSavedViewRetrievedFunc = [&,this]
		(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
		{
			UTEST_TRUE("Get Saved View request result", bSuccess);
			if (SavedViewInfo.Id == TEXT(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02))
			{
				UTEST_TRUE("CheckSavedView", CheckSavedView(SavedView));
				UTEST_TRUE("CheckSavedViewInfo", CheckSavedViewInfo(SavedViewInfo));
			}
			else
			{
				// AlexView2
				UTEST_EQUAL("Id", SavedViewInfo.Id, TEXT(SAVEDVIEWID_BUILDING_ALEXVIEW2));
				UTEST_EQUAL("DisplayName", SavedViewInfo.DisplayName, TEXT("AlexView2"));
				UTEST_EQUAL("Shared", SavedViewInfo.bShared, true);

				UTEST_TRUE("Origin", FVector::PointsAreNear(SavedView.Origin, FVector(62.47373320977305, -7.5267036440751, 7.8815683208719705), UE_SMALL_NUMBER));
				UTEST_TRUE("Extents", FVector::PointsAreNear(SavedView.Extents, FVector(2.5791900968344437, 1.8184076521127042, 1.2895950484174423), UE_SMALL_NUMBER));
				UTEST_TRUE("Angles", FMath::IsNearlyEqual(SavedView.Angles.Yaw, 0., UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Pitch, 0., UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Roll, -90., UE_SMALL_NUMBER));
			}
			return true;
		};
		Observer->AddPendingRequest();
		WebServices->GetSavedView(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02);

		Observer->AddPendingRequest();
		WebServices->GetSavedView(SAVEDVIEWID_BUILDING_ALEXVIEW2);
	}

	SECTION("EditSavedView")
	{
		Observer->AddPendingRequest();
		Observer->OnSavedViewEditedFunc = [&, this]
		(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
		{
			UTEST_TRUE("Edit Saved View request result", bSuccess);
			UTEST_TRUE("CheckSavedView", CheckSavedView(SavedView));
			UTEST_TRUE("CheckSavedViewInfo", CheckSavedViewInfo(SavedViewInfo));
			return true;
		};
		WebServices->EditSavedView(
			{
				FVector(-1.79, -0.69, 1.59),
				FVector(0.0, 0.0, 0.0),
				FRotator(-50.43, -1.69, -92.19)
			},
			{
				TEXT(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02),
				TEXT("view02"),
				true
			}
		);
	}

	SECTION("AddSavedView")
	{
		Observer->AddPendingRequest();
		Observer->OnSavedViewAddedFunc = [&, this]
		(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
		{
			UTEST_TRUE("Add Saved View request result", bSuccess);
			UTEST_TRUE("CheckSavedViewInfo", CheckSavedViewInfo(SavedViewInfo));
			return true;
		};
		WebServices->AddSavedView(
			TEXT(ITWINID_TESTS_ALEXW),
			TEXT(IMODELID_PHOTO_REALISTIC_RENDERING),
			{
				FVector(-1.79, -0.69, 1.59),
				FVector(0.0, 0.0, 0.0),
				FRotator(-50.43, -1.69, -92.19)
			},
			{
				TEXT(""),
				TEXT("view02"),
				true
			});
	}

	SECTION("DeleteSavedView")
	{
		// handle both a success and a failure
		Observer->OnSavedViewDeletedFunc = [this](bool bSuccess, FString const& SavedViewId, FString const& Response)
		{
			if (SavedViewId == TEXT(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02))
			{
				UTEST_TRUE("Delete Saved View request result", bSuccess);
				UTEST_TRUE("Empty Response", Response.IsEmpty());
			}
			else if (SavedViewId == TEXT(SAVEDVIEWID_BUILDING_TEST))
			{
				UTEST_EQUAL("Delete Saved View should fail", bSuccess, false);
				UTEST_EQUAL("ErrorMessage", Response,
					TEXT("[DeleteSavedView] code 422: Unknown\n\tError [InvalidSavedviewsRequest]: Cannot delete savedview.\n\tDetails: [InvalidChange] Update operations not supported on legacy savedviews."));
			}
			else
			{
				ensureMsgf(false, TEXT("Unexpected SavedView ID: %s"), *SavedViewId);
			}
			return true;
		};
		// this one will work
		Observer->AddPendingRequest();
		WebServices->DeleteSavedView(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02);

		// this one will fail
		Observer->AddPendingRequest();
		WebServices->DeleteSavedView(SAVEDVIEWID_BUILDING_TEST);
	}

	SECTION("GetRealityData")
	{
		Observer->OnRealityDataRetrievedFunc = [this, TestErrorMessage](bool bSuccess, FITwinRealityDataInfos const& Infos)
		{
			TArray<FITwinRealityDataInfo> const& realityDataArray(Infos.Infos);
			if (bSuccess)
			{
				UTEST_EQUAL("Num", realityDataArray.Num(), 1);

				UTEST_EQUAL("Id", realityDataArray[0].Id, TEXT(REALITYDATAID_ORLANDO));
				UTEST_EQUAL("DisplayName", realityDataArray[0].DisplayName, TEXT("Orlando_CesiumDraco_LAT"));
			}
			else
			{
				UTEST_EQUAL("Num", realityDataArray.Num(), 0);
				UTEST_TRUE("CheckError", TestErrorMessage(
					TEXT("[GetRealityData] code 422: Unknown\n\tError [InvalidRealityDataRequest]: Invalid RealityData request.\n\tDetails: [InvalidParameter] The value 'toto' is not valid. (target: iTwinId)")));
			}
			return true;
		};

		// test success
		Observer->AddPendingRequest();
		WebServices->GetRealityData(ITWINID_CAYMUS_EAP);

		// test failure
		Observer->AddPendingRequest();
		WebServices->GetRealityData(ITWINID_NOT_EXISTING);
	}

	SECTION("GetRealityData3DInfo")
	{
		Observer->AddPendingRequest();
		Observer->OnRealityData3DInfoRetrievedFunc = [this](bool bSuccess, FITwinRealityData3DInfo const& Info)
		{
			UTEST_TRUE("Get Reality Data 3D Info request result", bSuccess);
			UTEST_EQUAL("Id", Info.Id, TEXT(REALITYDATAID_ORLANDO));
			UTEST_EQUAL("DisplayName", Info.DisplayName, TEXT("Orlando_CesiumDraco_LAT"));
			UTEST_TRUE("GeoLocated", Info.bGeolocated);
			auto const& ExtSW = Info.ExtentSouthWest;
			auto const& ExtNE = Info.ExtentNorthEast;
			UTEST_TRUE("SouthWest latitude", FMath::IsNearlyEqual(ExtSW.Latitude, 28.496424905782874, UE_SMALL_NUMBER));
			UTEST_TRUE("SouthWest longitude", FMath::IsNearlyEqual(ExtSW.Longitude, -81.42035061172474, UE_SMALL_NUMBER));
			UTEST_TRUE("NorthEast latitude", FMath::IsNearlyEqual(ExtNE.Latitude, 28.587753137096165, UE_SMALL_NUMBER));
			UTEST_TRUE("NorthEast longitude", FMath::IsNearlyEqual(ExtNE.Longitude, -81.33756635398319, UE_SMALL_NUMBER));
			UTEST_EQUAL("MeshUrl", Info.MeshUrl,
				TEXT("https://realityblob59.blob.core.net/21b5896b-5dbd-41a7-9f23-d988a1847c11/Orlando_CesiumDraco_LAT.json?skoid=6db55139-0f1c-467a-95b4-5009c17c1bf0&sktid=067e9632-ea4c-4ed9-9e6d-e294956e284b&skt=2024-06-18T17%3A42%3A00Z&ske=2024-06-21T17%3A42%3A00Z&sks=b&skv=2024-05-04&sv=2024-05-04&st=2024-06-18T20%3A11%3A05Z&se=2024-06-19T23%3A59%3A59Z&sr=c&sp=rl&sig=0qSqX3OF4qlyYeHUc8hT61NCI%3D")
			);
			return true;
		};
		WebServices->GetRealityData3DInfo(ITWINID_CAYMUS_EAP, REALITYDATAID_ORLANDO);
	}

	SECTION("GetElementProperties")
	{
		Observer->AddPendingRequest();
		Observer->OnElementPropertiesRetrievedFunc = [this](bool bSuccess, FElementProperties const& InProps)
		{
			UTEST_TRUE("GetElementProperties request result", bSuccess);
			TArray<FElementProperty> const& bimProps(InProps.Properties);
			UTEST_EQUAL("NumProperties", bimProps.Num(), 4);

			UTEST_EQUAL("Property Name", bimProps[0].Name, TEXT("Selected Item"));
			UTEST_EQUAL("NumAttributes", bimProps[0].Attributes.Num(), 5);
			UTEST_EQUAL("Attr Name", bimProps[0].Attributes[0].Name, TEXT("Model"));
			UTEST_EQUAL("Attr Value", bimProps[0].Attributes[0].Value, TEXT("West Wing, BSI300AE9-Shell.dgn, Composite"));
			UTEST_EQUAL("Attr Name", bimProps[0].Attributes[3].Name, TEXT("Category"));
			UTEST_EQUAL("Attr Value", bimProps[0].Attributes[3].Value, TEXT("A-G321-G3-Windw"));
			UTEST_EQUAL("Attr Name", bimProps[0].Attributes[4].Name, TEXT("Physical Material"));
			UTEST_EQUAL("Attr Value", bimProps[0].Attributes[4].Value, TEXT(""));

			UTEST_EQUAL("Property Name", bimProps[1].Name, TEXT("Source Information"));
			UTEST_EQUAL("NumAttributes", bimProps[1].Attributes.Num(), 1);
			UTEST_EQUAL("Attr Name", bimProps[1].Attributes[0].Name, TEXT("Source Element ID"));
			UTEST_EQUAL("Attr Value", bimProps[1].Attributes[0].Value, TEXT("45631"));

			UTEST_EQUAL("Property Name", bimProps[2].Name, TEXT("Model Source"));
			UTEST_EQUAL("NumAttributes", bimProps[2].Attributes.Num(), 2);
			UTEST_EQUAL("Attr Name", bimProps[2].Attributes[0].Name, TEXT("Path"));
			UTEST_EQUAL("Attr Value", bimProps[2].Attributes[0].Value, TEXT("F:/Bentley/BuildingProject/Workspace/Projects/Building Project/dgn/BSI300AE9-Shell.dgn"));
			UTEST_EQUAL("Attr Name", bimProps[2].Attributes[1].Name, TEXT("Name"));
			UTEST_EQUAL("Attr Value", bimProps[2].Attributes[1].Value, TEXT("BSI300AE9-Shell.dgn"));

			UTEST_EQUAL("Property Name", bimProps[3].Name, TEXT("Document Link"));
			UTEST_EQUAL("NumAttributes", bimProps[3].Attributes.Num(), 5);
			UTEST_EQUAL("Attr Name", bimProps[3].Attributes[0].Name, TEXT("Code"));
			UTEST_EQUAL("Attr Value", bimProps[3].Attributes[0].Value, TEXT("bsi300ae9-shell.dgn"));
			UTEST_EQUAL("Attr Name", bimProps[3].Attributes[1].Name, TEXT("Name"));
			UTEST_EQUAL("Attr Value", bimProps[3].Attributes[1].Value, TEXT("BSI300AE9-Shell.dgn"));
			UTEST_EQUAL("Attr Name", bimProps[3].Attributes[4].Name, TEXT("Format"));
			UTEST_EQUAL("Attr Value", bimProps[3].Attributes[4].Value, TEXT(""));

			return true;
		};
		WebServices->GetElementProperties(ITWINID_CAYMUS_EAP, IMODELID_BUILDING, CHANGESETID_BUILDING, "0x20000001baf");
	}

	SECTION("GetIModelProperties")
	{
		Observer->OnIModelPropertiesRetrievedFunc = [this](bool bSuccess,
			bool bHasExtents, FProjectExtents const& Extents,
			bool bHasEcefLocation, FEcefLocation const& EcefLocation)
		{
			UTEST_TRUE("GetIModelProperties request result", bSuccess);
			UTEST_TRUE("bHasExtents", bHasExtents);

#define UTEST_VEC_EQUAL(VecName, CurrentVal, ExpectedVal) \
	UTEST_TRUE(VecName " X", FMath::IsNearlyEqual(CurrentVal.X, ExpectedVal.X, UE_SMALL_NUMBER)); \
	UTEST_TRUE(VecName " Y", FMath::IsNearlyEqual(CurrentVal.Y, ExpectedVal.Y, UE_SMALL_NUMBER)); \
	UTEST_TRUE(VecName " Z", FMath::IsNearlyEqual(CurrentVal.Z, ExpectedVal.Z, UE_SMALL_NUMBER));

#define UTEST_EXTENTS_EQUAL(ModelName, CurrentExt, ExpectedLow, ExpectedHigh) \
	UTEST_VEC_EQUAL(ModelName " Extents Low", CurrentExt.Low, ExpectedLow);		\
	UTEST_VEC_EQUAL(ModelName " Extents High", CurrentExt.High, ExpectedHigh);

			if (bHasEcefLocation)
			{
				const FVector StadiumLow = FVector(32344.267871807926, 31348.272780176438, -478.7556455931467);
				const FVector StadiumHigh = FVector(33088.69387347796, 32680.341868920772, 144.21825526358407);
				UTEST_EXTENTS_EQUAL("Stadium", Extents, StadiumLow, StadiumHigh);

				UTEST_TRUE("HasCartographicOrigin", EcefLocation.bHasCartographicOrigin);
				UTEST_TRUE("HasTransform", EcefLocation.bHasTransform);
				UTEST_TRUE("HasVectors", EcefLocation.bHasVectors);

				UTEST_TRUE("CartographicOrigin H", FMath::IsNearlyEqual(EcefLocation.CartographicOrigin.Height, -167.26869516478132, UE_SMALL_NUMBER));
				UTEST_TRUE("CartographicOrigin Lat.", FMath::IsNearlyEqual(EcefLocation.CartographicOrigin.Latitude, 0.022790512521193126, UE_SMALL_NUMBER));
				UTEST_TRUE("CartographicOrigin Long.", FMath::IsNearlyEqual(EcefLocation.CartographicOrigin.Longitude, 1.8129729494684641, UE_SMALL_NUMBER));

				UTEST_TRUE("Orientation P", FMath::IsNearlyEqual(EcefLocation.Orientation.Pitch, -0.00096528839175402366, UE_SMALL_NUMBER));
				UTEST_TRUE("Orientation Y", FMath::IsNearlyEqual(EcefLocation.Orientation.Yaw, -166.12431911119472, UE_SMALL_NUMBER));
				UTEST_TRUE("Orientation R", FMath::IsNearlyEqual(EcefLocation.Orientation.Roll, 88.694195308662842, UE_SMALL_NUMBER));

				UTEST_TRUE("Transform", EcefLocation.Transform.Equals(
					FMatrix(
						{ -0.97079263092018664, 0.0054485769942402840, -0.23975409551790289, -1497600.1543352203 },
						{ -0.23980964627116919, -0.022127058357000351, 0.97056615053070139, 6198968.8779639360 },
						{ -1.6847014194354415e-05, 0.99971363550866954, 0.022788618352338339, 112371.07286524471 },
						{ 0., 0., 0., 1. })));

				UTEST_VEC_EQUAL("xVector", EcefLocation.xVector, FVector(-0.97079263092018664, -0.23980964627116919, -1.6847014194354415e-05));
				UTEST_VEC_EQUAL("yVector", EcefLocation.yVector, FVector(0.005448576994240284, -0.02212705835700035, 0.9997136355086695));
			}
			else
			{
				const FVector BuildingLow = FVector(-244.59492798331735, -303.66127815647087, -28.27051340710871);
				const FVector BuildingHigh = FVector(409.678652192302, 249.78031406156776, 33.397180631459555);
				UTEST_EXTENTS_EQUAL("Building", Extents, BuildingLow, BuildingHigh);
			}
			return true;
		};
		// iModel without ECEF location
		Observer->AddPendingRequest();
		WebServices->GetIModelProperties(ITWINID_CAYMUS_EAP, IMODELID_BUILDING, CHANGESETID_BUILDING);

		// iModel with ECEF location
		Observer->AddPendingRequest();
		WebServices->GetIModelProperties(ITWINID_STADIUM_RN_QA, IMODELID_STADIUM, CHANGESETID_STADIUM);
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForMockServerResponse(Observer));

	return true;
}

#endif // WITH_TESTS
