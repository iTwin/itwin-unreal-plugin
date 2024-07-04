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

#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
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
		EITwinEnvironment const TestEnv = EITwinEnvironment::Prod;
		// Avoid conflicting with the true application (or another instance of the same test running in
		// parallel...), and provide with a default iTwin App ID if there is none currently.
		FString TokenFileSuffix = FString::Printf(TEXT("_Test_%d"), FPlatformProcess::GetCurrentProcessId());
		UITwinWebServices::SetupTestMode(TestEnv, TokenFileSuffix);

		UTEST_TRUE("SaveToken", UITwinWebServices::SaveToken(SrcToken, TestEnv));
		FString ReadToken;
		UTEST_TRUE("LoadToken", UITwinWebServices::LoadToken(ReadToken, TestEnv));
		UTEST_EQUAL("Unchanged Token", ReadToken, SrcToken);

		// Cleanup
		UITwinWebServices::DeleteTokenFile(TestEnv);
	}
	return true;
}




#define ITWINTEST_ACCESS_TOKEN "ThisIsATestITwinAccessToken"

#define ITWINID_CAYMUS_EAP "5e15184e-6d3c-43fd-ad04-e28b4b39485e"
#define IMODELID_WIND_TURBINE "d66fcd8c-604a-41d6-964a-b9767d446c53"
#define EXPORTID_WIND_TURBINE_CESIUM "6e5a59b7-878b-4275-b960-8668dc11a04d"
#define SAVEDVIEWID_BUILDING_TEST "ARatNwH2bcJElcCIfZxfP69OGBVePG39Q60E4otLOUhewFJgy6DEw0GJFLG6fYuMSQ"

#define ITWINID_TESTS_ALEXW "e72496bd-03a5-4ad8-8a51-b14e827603b1"
#define IMODELID_PHOTO_REALISTIC_RENDERING "4dcf6dee-e7f1-4ed8-81f2-125402b9ac95"
#define SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02 "AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ"

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
				"\"imageName\":null,\"image\":null,\"createdDateTime\":\"2024-03-25T10:26:45.797Z\",\"createdBy\":\"0a483d73-ffce-4d52-9af4-a9927d07aa82\"}," \
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
			&& headerMap["exportType"] == "CESIUM"
			&& headerMap["cdn"] == "1"
			&& headerMap["client"] == "Unreal")
		{
			//---------------------------------------------------------------------------
			// GetExports - WindTurbine
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"exports\":[" \
				"{\"id\":\"6e5a59b7-878b-4275-b960-8668dc11a04d\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-03-29T10:20:57.606Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"},\"stats\":{\"useNewExporter\":\"false\",\"iModelSize\":16777216,\"compressedSize\":4072655,\"rawSize\":26028172," \
				"\"startTime\":\"2024-03-29T10:20:53.495Z\",\"duration\":4,\"metrics\":{\"iModel\":\"0.bim\",\"format\":\"cesium\",\"memoryThreshold\":16000000," \
				"\"totalTime\":1,\"avgTotalCpu\":0.91,\"avgFacetterCpu\":0,\"avgPublisherCpu\":1.82,\"maxTotalMem\":334,\"maxTSMem\":196,\"maxFacetterMem\":110,\"max1FacetterMem\":110," \
				"\"maxPublisherMem\":125,\"facetterTime\":0,\"numStoreEntries\":264,\"numStoreEntryPrimitivesMax\":240536,\"numStoreLeafEntries\":127,\"numStoreLeafPolyfacesEntries\":4," \
				"\"numStoreLeafPolyfacesPrimitives\":4,\"numStoreLeafPrimitives\":300759,\"numStoreLeafStrokesEntries\":123,\"numStoreLeafStrokesPrimitives\":300755,\"numStoreLevels\":6," \
				"\"numStorePolyfacesEntries\":256,\"numStorePolyfacesPrimitives\":673886,\"numStorePrimitives\":673894,\"numStoreStrokesEntries\":8,\"numStoreStrokesPrimitives\":8," \
				"\"storeSize\":28,\"leafTileSizeMax\":17,\"leafTolerance\":0,\"modelBox\":256,\"modelSize\":16,\"numLeafTilePrimitives\":293566,\"numLeafTilePrimitivesMax\":214374," \
				"\"numLeafTiles\":7,\"numTileBatches\":10,\"numTileBatchesMax\":3,\"numTilePrimitives\":301360,\"numTilePrimitivesMax\":214374,\"numTiles\":8,\"publisherTime\":0," \
				"\"tileSizeMax\":17,\"tileSizeMaxLevel\":7,\"version\":\"0.2.0\",\"tilesetSize\":24,\"tilesetStructureTime\":0}}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/6e5a59b7-878b-4275-b960-8668dc11a04d?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D\"}}}," \
				"{\"id\":\"87316e15-3d1e-436f-bc7d-b22521f67aff\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-01-29T08:39:07.737Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"3DFT\",\"geometryOptions\":{},\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"},\"stats\":{\"useNewExporter\":\"false\",\"iModelSize\":0,\"compressedSize\":2239874,\"rawSize\":5453417," \
				"\"startTime\":\"2024-01-29T08:38:54.158Z\",\"duration\":13}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/87316e15-3d1e-436f-bc7d-b22521f67aff?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sO3bvUtBCrmQS1n8jvgcNNm5k8UOzKmP%2BGtOGBZ3DwM%3D\"}}}," \
				"{\"id\":\"a8d9806f-42e1-4523-aa25-0ba0b7f87e5c\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-05-02T13:00:11.999Z\"," \
				"\"request\":{\"iModelId\":\"d66fcd8c-604a-41d6-964a-b9767d446c53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"IMODEL\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"},\"stats\":{\"useNewExporter\":\"3\",\"iModelSize\":16777216,\"compressedSize\":4367044,\"rawSize\":16975637," \
				"\"startTime\":\"2024-05-02T13:00:08.022Z\",\"duration\":3,\"metrics\":{\"iModel\":\"0.bim\",\"format\":\"imdl\",\"memoryThreshold\":16000000," \
				"\"totalTime\":1,\"avgTotalCpu\":0.88,\"avgFacetterCpu\":0,\"avgPublisherCpu\":1.76,\"maxTotalMem\":339,\"maxTSMem\":190,\"maxFacetterMem\":110,\"max1FacetterMem\":110," \
				"\"maxPublisherMem\":149,\"facetterTime\":0,\"numStoreEntries\":264,\"numStoreEntryPrimitivesMax\":240536,\"numStoreLeafEntries\":127,\"numStoreLeafPolyfacesEntries\":4," \
				"\"numStoreLeafPolyfacesPrimitives\":4,\"numStoreLeafPrimitives\":300759,\"numStoreLeafStrokesEntries\":123,\"numStoreLeafStrokesPrimitives\":300755,\"numStoreLevels\":6," \
				"\"numStorePolyfacesEntries\":256,\"numStorePolyfacesPrimitives\":673886,\"numStorePrimitives\":673894,\"numStoreStrokesEntries\":8,\"numStoreStrokesPrimitives\":8," \
				"\"storeSize\":28,\"leafTileSizeMax\":11,\"leafTolerance\":0,\"modelBox\":256,\"modelSize\":16,\"numLeafTilePrimitives\":293566,\"numLeafTilePrimitivesMax\":214374," \
				"\"numLeafTiles\":7,\"numTileBatches\":10,\"numTileBatchesMax\":3,\"numTilePrimitives\":301360,\"numTilePrimitivesMax\":214374,\"numTiles\":8,\"publisherTime\":0," \
				"\"tileSizeMax\":11,\"tileSizeMaxLevel\":7,\"version\":\"0.2.0\",\"tilesetSize\":16,\"tilesetStructureTime\":0}}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/a8d9806f-42e1-4523-aa25-0ba0b7f87e5c?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Va1c8owVNySpR7IFb4Q0A1%2FDqZn%2BD5B4T9%2F%2Fru8PFEM%3D\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/mesh-export/?$skip=0&$top=100&iModelId=d66fcd8c-604a-41d6-964a-b9767d446c53&changesetId=9641026f8e6370db8cc790fab8943255af57d38e\"}}}"
			);
		}
		if (argMap["iModelId"] == IMODELID_PHOTO_REALISTIC_RENDERING
			&& argMap["changesetId"].empty()
			&& headerMap["exportType"] == "CESIUM"
			&& headerMap["cdn"] == "1"
			&& headerMap["client"] == "Unreal")
		{
			//---------------------------------------------------------------------------
			// GetExports - PhotoRealisticRendering
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"exports\":[" \
				"{\"id\":\"00af52a3-a416-4e37-99e9-6de56368bc37\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:37:17.574Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"stats\":{\"useNewExporter\":\"false\",\"iModelSize\":16777216,\"compressedSize\":2340836,\"rawSize\":3706175," \
				"\"startTime\":\"2024-06-05T13:37:15.301Z\",\"duration\":2,\"metrics\":{\"iModel\":\"0.bim\",\"format\":\"cesium\",\"memoryThreshold\":16000000," \
				"\"totalTime\":0,\"avgTotalCpu\":0,\"avgFacetterCpu\":0,\"avgPublisherCpu\":0,\"maxTotalMem\":0,\"maxTSMem\":0,\"maxFacetterMem\":0,\"max1FacetterMem\":0," \
				"\"maxPublisherMem\":0,\"facetterTime\":0,\"numStoreEntries\":2272,\"numStoreEntryPrimitivesMax\":4053,\"numStoreLeafEntries\":1136,\"numStoreLeafPolyfacesEntries\":385," \
				"\"numStoreLeafPolyfacesPrimitives\":1956,\"numStoreLeafPrimitives\":59872,\"numStoreLeafStrokesEntries\":751,\"numStoreLeafStrokesPrimitives\":57916," \
				"\"numStoreLevels\":2,\"numStorePolyfacesEntries\":1502,\"numStorePolyfacesPrimitives\":76874,\"numStorePrimitives\":80745,\"numStoreStrokesEntries\":770," \
				"\"numStoreStrokesPrimitives\":3871,\"storeSize\":5,\"leafTileSizeMax\":1,\"leafTolerance\":0,\"modelBox\":16,\"modelSize\":16,\"numLeafTilePrimitives\":57076," \
				"\"numLeafTilePrimitivesMax\":57076,\"numLeafTiles\":1,\"numTileBatches\":39,\"numTileBatchesMax\":39,\"numTilePrimitives\":57076,\"numTilePrimitivesMax\":57076," \
				"\"numTiles\":1,\"publisherTime\":0,\"tileSizeMax\":1,\"tileSizeMaxLevel\":4,\"version\":\"0.2.0\",\"tilesetSize\":1,\"tilesetStructureTime\":0}}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/00af52a3-a416-4e37-99e9-6de56368bc37?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sgi1%2F26Szx6zUezikckec3l0285RRw3A1k948KBAjsU%3D\"}}}," \
				"{\"id\":\"1485a12a-c4f6-416f-bb79-e1fe478a3220\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-18T15:00:19.179Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"stats\":{\"useNewExporter\":\"false\",\"iModelSize\":16777216,\"compressedSize\":2340836,\"rawSize\":3706175," \
				"\"startTime\":\"2024-06-18T15:00:16.411Z\",\"duration\":2,\"metrics\":{\"iModel\":\"0.bim\",\"memoryThreshold\":16000000," \
				"\"totalTime\":0,\"avgTotalCpu\":0,\"avgFacetterCpu\":0,\"avgPublisherCpu\":0,\"maxTotalMem\":0,\"maxTSMem\":0,\"maxFacetterMem\":0,\"max1FacetterMem\":0," \
				"\"maxPublisherMem\":0,\"facetterTime\":0,\"numStoreEntries\":2272,\"numStoreEntryPrimitivesMax\":4053,\"numStoreLeafEntries\":1136,\"numStoreLeafPolyfacesEntries\":385," \
				"\"numStoreLeafPolyfacesPrimitives\":1956,\"numStoreLeafPrimitives\":59872,\"numStoreLeafStrokesEntries\":751,\"numStoreLeafStrokesPrimitives\":57916," \
				"\"numStoreLevels\":2,\"numStorePolyfacesEntries\":1502,\"numStorePolyfacesPrimitives\":76874,\"numStorePrimitives\":80745,\"numStoreStrokesEntries\":770," \
				"\"numStoreStrokesPrimitives\":3871,\"storeSize\":5,\"leafTileSizeMax\":1,\"leafTolerance\":0,\"modelBox\":16,\"modelSize\":16,\"numLeafTilePrimitives\":57076," \
				"\"numLeafTilePrimitivesMax\":57076,\"numLeafTiles\":1,\"numTileBatches\":39,\"numTileBatchesMax\":39,\"numTilePrimitives\":57076,\"numTilePrimitivesMax\":57076," \
				"\"numTiles\":1,\"publisherTime\":0,\"tileSizeMax\":1,\"tileSizeMaxLevel\":4,\"version\":\"0.2.0\",\"tilesetSize\":1,\"tilesetStructureTime\":0}}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/1485a12a-c4f6-416f-bb79-e1fe478a3220?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=jAKM4lsaO0THXKe6Au9jOoqb4CUaAOVGy6hCf%2BGCO9s%3D\"}}}," \
				"{\"id\":\"1d7eb244-cec9-4f62-909b-fb4755c37d83\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:51:14.999Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"GLTF\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\",\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/1d7eb244-cec9-4f62-909b-fb4755c37d83?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=alK49gFhKRILyHFf%2FFRgVl3Lr1ARN%2Bkg8KFrLxomjqE%3D\"}}}," \
				"{\"id\":\"ed456436-ed0a-488c-a5f2-4115e7d8e311\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-20T15:06:47.548Z\"," \
				"\"request\":{\"iModelId\":\"4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"stats\":{\"useNewExporter\":\"false\",\"iModelSize\":16777216,\"compressedSize\":2340836,\"rawSize\":3706175," \
				"\"startTime\":\"2024-06-20T15:06:44.389Z\",\"duration\":2,\"metrics\":{\"iModel\":\"0.bim\",\"memoryThreshold\":16000000," \
				"\"totalTime\":0,\"avgTotalCpu\":0,\"avgFacetterCpu\":0,\"avgPublisherCpu\":0,\"maxTotalMem\":0,\"maxTSMem\":0,\"maxFacetterMem\":0,\"max1FacetterMem\":0," \
				"\"maxPublisherMem\":0,\"facetterTime\":0,\"numStoreEntries\":2272,\"numStoreEntryPrimitivesMax\":4053,\"numStoreLeafEntries\":1136,\"numStoreLeafPolyfacesEntries\":385," \
				"\"numStoreLeafPolyfacesPrimitives\":1956,\"numStoreLeafPrimitives\":59872,\"numStoreLeafStrokesEntries\":751,\"numStoreLeafStrokesPrimitives\":57916," \
				"\"numStoreLevels\":2,\"numStorePolyfacesEntries\":1502,\"numStorePolyfacesPrimitives\":76874,\"numStorePrimitives\":80745,\"numStoreStrokesEntries\":770," \
				"\"numStoreStrokesPrimitives\":3871,\"storeSize\":5,\"leafTileSizeMax\":1,\"leafTolerance\":0,\"modelBox\":16,\"modelSize\":16,\"numLeafTilePrimitives\":57076," \
				"\"numLeafTilePrimitivesMax\":57076,\"numLeafTiles\":1,\"numTileBatches\":39,\"numTileBatchesMax\":39,\"numTilePrimitives\":57076,\"numTilePrimitivesMax\":57076," \
				"\"numTiles\":1,\"publisherTime\":0,\"tileSizeMax\":1,\"tileSizeMaxLevel\":4,\"version\":\"0.2.0\",\"tilesetSize\":1,\"tilesetStructureTime\":0}}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ed456436-ed0a-488c-a5f2-4115e7d8e311?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=1pievrXlFCSwmErxnSsIS4STny9y9oz%2B3P5j%2FsbPkgA%3D\"}}}]," \
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
				"\"contextId\":\"5e15184e-6d3c-43fd-ad04-e28b4b39485e\"},\"stats\":{\"iModelSize\":16777216,\"compressedSize\":4072655,\"rawSize\":26028172," \
				"\"startTime\":\"2024-03-29T10:20:53.495Z\",\"duration\":4,\"useNewExporter\":\"false\",\"metrics\":{\"iModel\":\"0.bim\",\"format\":\"cesium\",\"memoryThreshold\":16000000," \
				"\"totalTime\":1,\"avgTotalCpu\":0.91,\"avgFacetterCpu\":0,\"avgPublisherCpu\":1.82,\"maxTotalMem\":334,\"maxTSMem\":196,\"maxFacetterMem\":110,\"max1FacetterMem\":110," \
				"\"maxPublisherMem\":125,\"facetterTime\":0,\"numStoreEntries\":264,\"numStoreEntryPrimitivesMax\":240536,\"numStoreLeafEntries\":127,\"numStoreLeafPolyfacesEntries\":4," \
				"\"numStoreLeafPolyfacesPrimitives\":4,\"numStoreLeafPrimitives\":300759,\"numStoreLeafStrokesEntries\":123,\"numStoreLeafStrokesPrimitives\":300755,\"numStoreLevels\":6," \
				"\"numStorePolyfacesEntries\":256,\"numStorePolyfacesPrimitives\":673886,\"numStorePrimitives\":673894,\"numStoreStrokesEntries\":8,\"numStoreStrokesPrimitives\":8," \
				"\"storeSize\":28,\"leafTileSizeMax\":17,\"leafTolerance\":0,\"modelBox\":256,\"modelSize\":16,\"numLeafTilePrimitives\":293566,\"numLeafTilePrimitivesMax\":214374," \
				"\"numLeafTiles\":7,\"numTileBatches\":10,\"numTileBatchesMax\":3,\"numTilePrimitives\":301360,\"numTilePrimitivesMax\":214374,\"numTiles\":8,\"publisherTime\":0," \
				"\"tileSizeMax\":17,\"tileSizeMaxLevel\":7,\"version\":\"0.2.0\",\"tilesetSize\":24,\"tilesetStructureTime\":0}}," \
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

#define SAVEDVIEW_02_DATA "\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0.00,0.00,0.00],\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0.0,\"focusDist\":0.0,\"eye\":[-1.79,-0.69,1.59]}}},\"displayName\":\"view02\",\"shared\":true,\"tagIds\":[]"

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
				"\"displayName\":\"view01\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AI2zKB-8DhFGnKK6h32qexm9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:08:34.797Z\",\"lastModified\":\"2024-06-13T12:26:35.678Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0,0,0],\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.79,-0.69,1.59]}}}," \
				"\"displayName\":\"view02\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T10:43:36.006Z\",\"lastModified\":\"2024-06-18T07:27:58.423Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.6,6.77,10.89],\"extents\":[0,0,0],\"angles\":{\"yaw\":156.52,\"pitch\":-22.47,\"roll\":41.34},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.6,6.77,10.89]}}}," \
				"\"displayName\":\"view03 - top\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AOZH6-V9SGJPgQ25caQq6cK9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-13T12:16:16.765Z\",\"lastModified\":\"2024-06-13T12:17:04.237Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-2.67,3.17,1.9],\"extents\":[0,0,0],\"angles\":{\"yaw\":-170.55,\"pitch\":-86.22,\"roll\":99.47},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-2.67,3.17,1.9]}}}," \
				"\"displayName\":\"view04\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"},\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"},\"image\":{\"href\":\"https://api.test.com/savedviews/AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AP_q2cM-UHxAlm2OcWIk7Fu9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}," \
				"{\"id\":\"AG7BwHvOKrJJi-kRUac5AVa9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ\",\"shared\":true,\"creationTime\":\"2024-06-18T07:33:29.596Z\",\"lastModified\":\"2024-06-18T07:33:29.596Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.23,-0.78,1.46],\"extents\":[0,0,0],\"angles\":{\"yaw\":0.04,\"pitch\":-0.53,\"roll\":-85.38},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.23,-0.78,1.46]}}}," \
				"\"displayName\":\"view05\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
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
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1/members/fab7496a-f871-4381-b243-623d0f897e09\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/e72496bd-03a5-4ad8-8a51-b14e827603b1\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/4dcf6dee-e7f1-4ed8-81f2-125402b9ac95\"}," \
				"\"image\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/AB36h6dwg89Cg4SMOWg6cKy9liTnpQPYSopRsU6CdgOx7m3PTfHn2E6B8hJUArmslQ/image\"}}}}"
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
				"\r\n    \"createdDateTime\" : \"2024-03-19T12:39:00Z\",\r\n    \"ownerId\" : \"0a483d73-ffce-4d52-9af4-a9927d07aa82\"}}"
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

};

/*static*/
std::unique_ptr<httpmock::MockServer> FITwinMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	try {
		return httpmock::getFirstRunningMockServer<FITwinMockServer>(startPort, tryCount);
	}
	catch (std::runtime_error&) {
		return {};
	}
	checkNoEntry();
	return {};
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

	ServerConnection->Environment = EITwinEnvironment::Prod;
	ServerConnection->AccessToken = TEXT(ITWINTEST_ACCESS_TOKEN);
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
					TEXT("code 404: Not Found\n\tError [iTwinNotFound]: Requested iTwin is not available.")));
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
					TEXT("code 422: Unknown\n\tError [InvalidiModelsRequest]: Cannot get iModels.\n\tDetails: [InvalidValue] 'toto' is not a valid 'iTwinId' value. (target: iTwinId)")));
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
			if (ExportInfos.Num() == 1)
			{
				// WindTurbine
				UTEST_EQUAL("Id", ExportInfos[0].Id, WindTurbine_CesiumExportId);
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
				UTEST_EQUAL("NumExports", ExportInfos.Num(), 3);
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
				UTEST_EQUAL("Id 1", ExportInfos[1].Id, TEXT("1485a12a-c4f6-416f-bb79-e1fe478a3220"));
				UTEST_EQUAL("Id 2", ExportInfos[2].Id, TEXT("00af52a3-a416-4e37-99e9-6de56368bc37"));
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
		Observer->AddPendingRequest();
		Observer->OnSavedViewRetrievedFunc = [&,this]
		(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
		{
			UTEST_TRUE("Get Saved View request result", bSuccess);
			UTEST_TRUE("CheckSavedView", CheckSavedView(SavedView));
			UTEST_TRUE("CheckSavedViewInfo", CheckSavedViewInfo(SavedViewInfo));
			return true;
		};
		WebServices->GetSavedView(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02);
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
					TEXT("code 422: Unknown\n\tError [InvalidSavedviewsRequest]: Cannot delete savedview.\n\tDetails: [InvalidChange] Update operations not supported on legacy savedviews."));
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
					TEXT("code 422: Unknown\n\tError [InvalidRealityDataRequest]: Invalid RealityData request.\n\tDetails: [InvalidParameter] The value 'toto' is not valid. (target: iTwinId)")));
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

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForMockServerResponse(Observer));

	return true;
}

#endif // WITH_TESTS
