/*--------------------------------------------------------------------------------------+
|
|     $Source: WebServicesTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS 

#include "WebTestHelpers.h"

#include <HAL/PlatformProcess.h>
#include <Misc/LowLevelTestAdapter.h>

#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>

#include <ImageUtils.h>

#include <set>

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
		AdvViz::SDK::EITwinEnvironment const TestEnv = AdvViz::SDK::EITwinEnvironment::Prod;
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

#define ITWINID_CAYMUS_EAP "itwinId-Cay-EA"
#define IMODELID_BUILDING "imodelId-Building"
#define CHANGESETID_BUILDING "changesetidbuilding59"
#define IMODELID_WIND_TURBINE "imodelId-Turb-53"
#define EXPORTID_WIND_TURBINE_CESIUM "expId-Turb-53"
#define SAVEDVIEWID_BUILDING_TEST "SVIdBuilding81"

#define ITWINID_TESTS_ALEXW "itwinId-Tests-Plop"
#define IMODELID_PHOTO_REALISTIC_RENDERING "imodelId-PhotoReal-Render-97"
#define SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02 "SVIdPhotoRealisticView02"
#define SAVEDVIEWGROUPID_GROUP02 "SVGroupId-Group02"
#define SAVEDVIEWGROUPID_TESTRENAMEGROUP "SVGroupIdRenameTest"

#define SMALLPNG_BASE64 "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC"
#define SAVEDVIEW_THUMBNAILURL "data:image/png;base64," SMALLPNG_BASE64
#define SAVEDVIEW_THUMBNAILURL_ERROR "data:image/png;base64,testwrongurl"

#define SAVEDVIEWID_BUILDING_ALEXVIEW2 "SVIdBuildingView_Nm02"
#define SAVEDVIEWID_BUILDING_CONSTRUCTION "SVIdBuildingConstruction"

#define ITWINID_STADIUM_RN_QA "itwinId-Stadium-Ouh-QA"
#define IMODELID_STADIUM "imodelId-Stadium-023"
#define CHANGESETID_STADIUM "changesetIdStadium"

#define REALITYDATAID_ORLANDO "realityData-Id-Orlando-Magic"

#define ITWINID_NOT_EXISTING "toto"

/// Mock server implementation for iTwin services

FITwinMockServerBase::FITwinMockServerBase(int port)
	: httpmock::MockServer(port)
{

}
/// Process /header_in request

int FITwinMockServerBase::CheckRequiredHeaders(const std::vector<Header>& headers,
	std::map<std::string, std::string> const& requiredHeaders) const
{
	std::set<std::string> matchedHeaders;
	std::optional<int> headerError;
	std::string errorInfo;
	for (const Header& header : headers)
	{
		auto itReq = requiredHeaders.find(header.key);
		if (itReq != requiredHeaders.end())
		{
			std::string_view const requiredValue(itReq->second);
			bool const bMatchingValue = (header.value == requiredValue)
				|| (requiredValue.ends_with("*")
					&& header.value.starts_with(requiredValue.substr(0, requiredValue.length() - 1)));
			if (bMatchingValue)
			{
				matchedHeaders.insert(header.key);
			}
			else
			{
				// Not the expected value!
				errorInfo = std::string(" - value differs for ") + header.key
					+ ": was expecting '" + itReq->second + "' and found '" + header.value + "'";
				if (header.key == "Authorization")
					headerError = cpr::status::HTTP_UNAUTHORIZED;
				else
					headerError = cpr::status::HTTP_BAD_REQUEST;
				break;
			}
		}
	}

	if (!headerError
		&& matchedHeaders.size() != requiredHeaders.size())
	{
		errorInfo = " - missing header(s)";
		headerError = cpr::status::HTTP_BAD_REQUEST;
	}
	if (headerError)
	{
		BE_LOGE("ITwinAPI", "Not the expected headers (" << *headerError << errorInfo << ") -> " << ToString(headers));
		return *headerError;
	}

	return cpr::status::HTTP_OK;
}

std::string FITwinMockServerBase::ToString(const std::vector<Header>& headers) const
{
	std::string Str("{ ");
	for (const Header& header : headers)
	{
		Str += "{";
		Str += header.key + " : " + header.value + "}, ";
	}
	Str += "}";
	return Str;
}

/// Process /arg_test request (basic test to check that the mock server is answering)
FITwinMockServerBase::Response FITwinMockServerBase::ProcessArgTest(const std::vector<UrlArg>& urlArguments) const
{
	static const StringMap expectedArgs = { { "b", "2" }, { "x", "0" } };
	check(ToArgMap(urlArguments) == expectedArgs);
	return Response();
}

class FITwinMockServer : public FITwinMockServerBase
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FITwinMockServer(int port) : FITwinMockServerBase(port) {}

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


#define CHECK_ITWIN_HEADERS( iTwin_VER )											\
{																					\
	const int HeaderStatus = CheckRequiredHeaders(headers,							\
		{{ "Accept", "application/vnd.bentley.itwin-platform." iTwin_VER "+json" },	\
		 { "Prefer", "return=representation" },										\
		 { "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN }});					\
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
				"{\"iTwin\":{\"id\":\"itwinId-Cay-EA\",\"class\":\"Endeavor\",\"subClass\":\"Project\"," \
				"\"type\":null,\"number\":\"Bentley Caymus EAP\",\"displayName\":\"Bentley Caymus EAP\",\"geographicLocation\":\"Exton, PA\"," \
				"\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\"," \
				"\"parentId\":\"ITW-ACCOUNT-ID-59\",\"iTwinAccountId\":\"ITW-ACCOUNT-ID-59\"," \
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
				"{\"id\":\"itwinId-Tests-Plop\",\"class\":\"Endeavor\",\"subClass\":\"Project\"," \
				"\"type\":null,\"number\":\"Tests_AlexW\",\"displayName\":\"Tests_AlexW\",\"geographicLocation\":null,\"ianaTimeZone\":null," \
				"\"dataCenterLocation\":\"East US\",\"status\":\"Active\",\"parentId\":\"ITW-ACCOUNT-ID-59\",\"iTwinAccountId\":\"ITW-ACCOUNT-ID-59\"," \
				"\"imageName\":null,\"image\":null,\"createdDateTime\":\"2024-03-25T10:26:45.797Z\",\"createdBy\":\"owner-identifier-059\"}," \
				"{\"id\":\"itwinId-Cay-EA\",\"class\":\"Endeavor\",\"subClass\":\"Project\",\"type\":null,\"number\":\"Bentley Caymus EAP\"," \
				"\"displayName\":\"Bentley Caymus EAP\",\"geographicLocation\":\"Exton, PA\",\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\"," \
				"\"parentId\":\"ITW-ACCOUNT-ID-59\",\"iTwinAccountId\":\"ITW-ACCOUNT-ID-59\",\"imageName\":null,\"image\":null," \
				"\"createdDateTime\":\"2021-09-28T19:16:06.183Z\",\"createdBy\":\"102f4511-1838\"},{\"id\":\"itwinId-Another-Project\",\"class\":\"Endeavor\"," \
				"\"subClass\":\"Project\",\"type\":null,\"number\":\"ConExpo 2023 - Civil\",\"displayName\":\"ConExpo 2023 - Civil\",\"geographicLocation\":\"Wilson, North Carolina I95 and Highway 97\"," \
				"\"ianaTimeZone\":\"America/New_York\",\"dataCenterLocation\":\"East US\",\"status\":\"Active\",\"parentId\":\"ITW-ACCOUNT-ID-59\"," \
				"\"iTwinAccountId\":\"ITW-ACCOUNT-ID-59\",\"imageName\":\"some-name.jpg\",\"image\":\"https://image.net/context-thumbnails/999c.jpg?sv=2018-03-28&sr=b&sig=99o%2Fv5zpJP%3D&se=2024-06-16T00%3A00%3A00Z&sp=r\"," \
				"\"createdDateTime\":\"2023-02-06T18:33:42.283Z\",\"createdBy\":\"creator-Id-01\"}],\"_links\":{\"self\":{\"href\":\"https://api.test.com/itwins/recents?$skip=0&$top=1000&subClass=Project&status=Active\"}}}"
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
				"{\"id\":\"imodelId-Building\",\"displayName\":\"Building\",\"dataCenterLocation\":\"East US\",\"name\":\"Building\",\"description\":\"Bentley Building Project\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-10-05T16:31:18.1030000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/imodelId-Building/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/imodelId-Building/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/imodelId-Building/namedversions\"}}}," \
				"{\"id\":\"imodelId-Another-World\",\"displayName\":\"Hatch Terrain Model\",\"dataCenterLocation\":\"East US\",\"name\":\"Hatch Terrain Model\",\"description\":\"\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2023-03-18T06:33:58.3830000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/imodelId-Another-World/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/imodelId-Another-World/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/imodelId-Another-World/namedversions\"}}}," \
				"{\"id\":\"imodelId-Highway-66\",\"displayName\":\"Highway\",\"dataCenterLocation\":\"East US\",\"name\":\"Highway\",\"description\":\"Bentley Omniverse Testing\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-30T06:13:11.8070000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/imodelId-Highway-66/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/imodelId-Highway-66/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/imodelId-Highway-66/namedversions\"}}}," \
				"{\"id\":\"imodelId-Metro-Boulot\",\"displayName\":\"MetroStation\",\"dataCenterLocation\":\"East US\",\"name\":\"MetroStation\",\"description\":\"Test model for Bentley Omniverse\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:54:20.5130000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false," \
				"\"extent\":{\"southWest\":{\"latitude\":39.42986934243659,\"longitude\":-119.75930764897122},\"northEast\":{\"latitude\":39.4370289257737,\"longitude\":-119.74600389225735}},\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/imodelId-Metro-Boulot/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/imodelId-Metro-Boulot/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/imodelId-Metro-Boulot/namedversions\"}}}," \
				"{\"id\":\"imodelId-Offshore-Rig\",\"displayName\":\"OffshoreRig\",\"dataCenterLocation\":\"East US\",\"name\":\"OffshoreRig\",\"description\":\"Bentley Omniverse Test Model\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:55:30.6200000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/imodelId-Offshore-Rig/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/imodelId-Offshore-Rig/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/imodelId-Offshore-Rig/namedversions\"}}}," \
				"{\"id\":\"imodelId-Turb-53\",\"displayName\":\"WindTurbine\",\"dataCenterLocation\":\"East US\",\"name\":\"WindTurbine\",\"description\":\"Omniverse Test Model\"," \
				"\"state\":\"initialized\",\"createdDateTime\":\"2021-09-28T19:19:44.8300000Z\",\"iTwinId\":\"itwinId-Cay-EA\",\"isSecured\":false,\"extent\":null,\"containersEnabled\":0," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"}," \
				"\"changesets\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets\"},\"namedVersions\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/namedversions\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/imodels?itwinId=itwinId-Cay-EA\u0026$skip=0\u0026$top=100\"},\"prev\":null,\"next\":null}}"
			);
		}
		else if (url.ends_with(IMODELID_WIND_TURBINE "/changesets" ))
		{
			//---------------------------------------------------------------------------
			// GetiModelChangesets
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"changesets\":[" \
				"{\"id\":\"changesetIdTheOneToTest\",\"displayName\":\"4\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - Initialization changes\",\"index\":4," \
				"\"parentId\":\"changesetIdOfTheParent\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:13.3530000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":0,\"fileSize\":599,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/changesetIdTheOneToTest.cs?sv=2019-07-07\u0026sr=b\u0026sig=TYtyeN3eMo0MfZ7dCWNkqA%2FSF4ZmyOiXaL3wZ5DOoYQ%3D\u0026st=2024-06-17T08%3A43%3A04.6502473Z\u0026se=2024-06-17T09%3A04%3A42.3118793Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/4\"}}}," \
				"{\"id\":\"changesetIdOfTheParent\",\"displayName\":\"3\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - BootstrapExternalSources\",\"index\":3," \
				"\"parentId\":\"changesetIdOfTheGrandPa\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:10.9100000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":0,\"fileSize\":229,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/changesetIdOfTheParent.cs?sv=2019-07-07\u0026sr=b\u0026sig=IZneO860eH1uYMqrNsaeTZ3SepPkardVBDc2NEdGsI0%3D\u0026st=2024-06-17T08%3A41%3A24.6846254Z\u0026se=2024-06-17T09%3A04%3A42.3118999Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/3\"}}}," \
				"{\"id\":\"changesetIdOfTheGrandPa\",\"displayName\":\"2\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - initalLoad - Domain schema upgrade\",\"index\":2," \
				"\"parentId\":\"changesetIdOfTheGrandGrandPa\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:08.7300000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":1,\"fileSize\":3791,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/changesetIdOfTheGrandPa.cs?sv=2019-07-07\u0026sr=b\u0026sig=4OQNPY4%2BHVfRPdwi6sSrv20L5RYrawyhg2GT637f11s%3D\u0026st=2024-06-17T08%3A41%3A33.4453273Z\u0026se=2024-06-17T09%3A04%3A42.3119214Z\u0026sp=r\"}," \
				"\"namedVersion\":null,\"currentOrPrecedingCheckpoint\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/0/checkpoint\"},\"creator\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/users/102f4511-1838\"},\"self\":{\"href\":\"https://api.test.com/imodels/d66fcd8c/changesets/2\"}}}," \
				"{\"id\":\"changesetIdOfTheGrandGrandPa\",\"displayName\":\"1\",\"application\":{\"id\":\"imodel-bridge-administrator\",\"name\":\"iTwin Synchronizer\"}," \
				"\"synchronizationInfo\":{\"taskId\":\"02a0e54e\",\"changedFiles\":null},\"description\":\"MicroStation Connector - Domain schema upgrade\",\"index\":1," \
				"\"parentId\":\"\",\"creatorId\":\"102f4511-1838\",\"pushDateTime\":\"2021-09-30T06:06:04.5700000Z\"," \
				"\"state\":\"fileUploaded\",\"containingChanges\":1,\"fileSize\":6384,\"briefcaseId\":2,\"groupId\":null," \
				"\"_links\":{\"download\":{\"href\":\"https://ihub.blob.net/imodelhub-d66fcd8c/changesetIdOfTheGrandGrandPa.cs?sv=2019-07-07\u0026sr=b\u0026sig=h3Fy8Kw9JHxCU6zBgeBAAOBiXUneLbFoT7C71z6B0WY%3D\u0026st=2024-06-17T08%3A42%3A16.1500756Z\u0026se=2024-06-17T09%3A04%3A42.3119433Z\u0026sp=r\"}," \
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
				"{\"export\":{\"id\":\"ExportId-Just-Started\",\"displayName\":\"SS_Stadium\",\"status\":\"NotStarted\"," \
				"\"lastModified\":\"2024-06-18T14:12:30.905Z\",\"request\":{\"iModelId\":\"imodelId-Stadium-023\",\"changesetId\":\"changesetIdStadium\"," \
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
				"{\"id\":\"expId-Turb-53\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-03-29T10:20:57.606Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-Turb-53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"iTwinId\":\"itwinId-Cay-EA\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/expId-Turb-53?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D\"}}}," \
				"{\"id\":\"87316e15-3d1e-436f-bc7d-b22521f67aff\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-01-29T08:39:07.737Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-Turb-53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"3DFT\",\"geometryOptions\":{},\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Cay-EA\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/87316e15-3d1e-436f-bc7d-b22521f67aff?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sO3bvUtBCrmQS1n8jvgcNNm5k8UOzKmP%2BGtOGBZ3DwM%3D\"}}}," \
				"{\"id\":\"a8d9806f-42e1-4523-aa25-0ba0b7f87e5c\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-05-02T13:00:11.999Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-Turb-53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"IMODEL\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Cay-EA\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/a8d9806f-42e1-4523-aa25-0ba0b7f87e5c?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Va1c8owVNySpR7IFb4Q0A1%2FDqZn%2BD5B4T9%2F%2Fru8PFEM%3D\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/mesh-export/?$skip=0&$top=100&iModelId=imodelId-Turb-53&changesetId=9641026f8e6370db8cc790fab8943255af57d38e\"}}}"
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
				"{\"id\":\"ExportId-PhotoReal-Cesium\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-20T15:06:47.548Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-PhotoReal-Render-97\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Tests-Plop\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ExportId-PhotoReal-Cesium?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=1pievrXlFCSwmErxnSsIS4STny9y9oz%2B3P5j%2FsbPkgA%3D\"}}}," \
				"{\"id\":\"ExportId-PhotoReal-Cesium-Bis\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:37:17.574Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-PhotoReal-Render-97\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Tests-Plop\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ExportId-PhotoReal-Cesium-Bis?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=sgi1%2F26Szx6zUezikckec3l0285RRw3A1k948KBAjsU%3D\"}}}," \
				"{\"id\":\"ExportId-PhotoReal-Cesium-Ter\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-18T15:00:19.179Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-PhotoReal-Render-97\",\"changesetId\":\"\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Tests-Plop\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ExportId-PhotoReal-Cesium-Ter?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=jAKM4lsaO0THXKe6Au9jOoqb4CUaAOVGy6hCf%2BGCO9s%3D\"}}}," \
				"{\"id\":\"ExportId-PhotoReal-GLTF\",\"displayName\":\"PhotoRealisticRendering\",\"status\":\"Complete\",\"lastModified\":\"2024-06-05T13:51:14.999Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-PhotoReal-Render-97\",\"changesetId\":\"\"," \
				"\"exportType\":\"GLTF\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.0\",\"currentExporterVersion\":\"1.0\",\"contextId\":\"itwinId-Tests-Plop\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/ExportId-PhotoReal-GLTF?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=alK49gFhKRILyHFf%2FFRgVl3Lr1ARN%2Bkg8KFrLxomjqE%3D\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/mesh-export/?$skip=0&$top=100&iModelId=imodelId-PhotoReal-Render-97\"}}}"
			);
		}
		else if (url.ends_with(EXPORTID_WIND_TURBINE_CESIUM))
		{
			//---------------------------------------------------------------------------
			// GetExportInfo
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"export\":" \
				"{\"id\":\"expId-Turb-53\",\"displayName\":\"WindTurbine\",\"status\":\"Complete\",\"lastModified\":\"2024-03-29T10:20:57.606Z\"," \
				"\"request\":{\"iModelId\":\"imodelId-Turb-53\",\"changesetId\":\"9641026f8e6370db8cc790fab8943255af57d38e\"," \
				"\"exportType\":\"CESIUM\",\"exporterVersion\":\"1.0\",\"exportTypeVersion\":\"1.1\",\"currentExporterVersion\":\"1.0\"," \
				"\"contextId\":\"itwinId-Cay-EA\"}," \
				"\"_links\":{\"mesh\":{\"href\":\"https://gltf59.blob.net/expId-Turb-53?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D\"}}}}"
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

#define ADD_SAVEDVIEW_02_DATA "{\"iTwinId\":\"itwinId-Tests-Plop\",\"iModelId\":\"imodelId-PhotoReal-Render-97\"," SAVEDVIEW_02_DATA "}"
#define ADD_GROUP_02_DATA "{\"iTwinId\":\"" ITWINID_CAYMUS_EAP "\",\"iModelId\":\"" IMODELID_BUILDING "\",\"displayName\":\"Group02\",\"shared\":false}"

		if (argMap["iTwinId"] == ITWINID_TESTS_ALEXW
			&& argMap["iModelId"] == IMODELID_PHOTO_REALISTIC_RENDERING
			&& argMap["$skip"] == "0" && argMap["$top"] == "100"
			&& url.ends_with("/savedviews"))
		{
			//---------------------------------------------------------------------------
			// GetAllSavedViews
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"savedViews\":[" \
				"{\"id\":\"SavedViewIDPlopPhotoRealistic01\",\"shared\":true,\"creationTime\":\"2024-06-13T10:07:29.897Z\",\"lastModified\":\"2024-06-13T12:25:19.239Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-3.12,7.39,2.2],\"extents\":[0,0,0],\"angles\":{\"yaw\":176.41,\"pitch\":-41.52,\"roll\":84.6},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-3.12,7.39,2.2]}}}," \
				"\"displayName\":\"view01\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic01/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic01/image\"}}}," \
				"{\"id\":\"SVIdPhotoRealisticView02\",\"shared\":true,\"creationTime\":\"2024-06-13T10:08:34.797Z\",\"lastModified\":\"2024-06-13T12:26:35.678Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0,0,0],\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.79,-0.69,1.59]}}}," \
				"\"displayName\":\"view02\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SVIdPhotoRealisticView02/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SVIdPhotoRealisticView02/image\"}}}," \
				"{\"id\":\"SavedViewIDPlopPhotoRealistic03\",\"shared\":true,\"creationTime\":\"2024-06-13T10:43:36.006Z\",\"lastModified\":\"2024-06-18T07:27:58.423Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.6,6.77,10.89],\"extents\":[0,0,0],\"angles\":{\"yaw\":156.52,\"pitch\":-22.47,\"roll\":41.34},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.6,6.77,10.89]}}}," \
				"\"displayName\":\"view03 - top\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic03/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic03/image\"}}}," \
				"{\"id\":\"SavedViewIDPlop_PhotoRealistic04\",\"shared\":true,\"creationTime\":\"2024-06-13T12:16:16.765Z\",\"lastModified\":\"2024-06-13T12:17:04.237Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-2.67,3.17,1.9],\"extents\":[0,0,0],\"angles\":{\"yaw\":-170.55,\"pitch\":-86.22,\"roll\":99.47},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-2.67,3.17,1.9]}}}," \
				"\"displayName\":\"view04\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlop_PhotoRealistic04/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlop_PhotoRealistic04/image\"}}}," \
				"{\"id\":\"SavedViewIDPlop-PhotoRealistic05\",\"shared\":true,\"creationTime\":\"2024-06-18T07:33:29.596Z\",\"lastModified\":\"2024-06-18T07:33:29.596Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.23,-0.78,1.46],\"extents\":[0,0,0],\"angles\":{\"yaw\":0.04,\"pitch\":-0.53,\"roll\":-85.38},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.23,-0.78,1.46]}}}," \
				"\"displayName\":\"view05\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlop-PhotoRealistic05/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlop-PhotoRealistic05/image\"}}}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.test.com/savedviews?iTwinId=itwinId-Tests-Plop&iModelId=imodelId-PhotoReal-Render-97&$top=100\"}}}"
			);
		}
		else if (url.ends_with("/savedviews")
			&& argMap["groupId"] == SAVEDVIEWGROUPID_TESTRENAMEGROUP
			&& argMap["$skip"] == "0" && argMap["$top"] == "100")
		{
			return Response(cpr::status::HTTP_OK, "{\"savedViews\":[" \
				"{\"id\":\"SavedViewIDPlopPhotoRealistic01\",\"shared\":true,\"creationTime\":\"2024-06-13T10:07:29.897Z\",\"lastModified\":\"2024-06-13T12:25:19.239Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-3.12,7.39,2.2],\"extents\":[0,0,0],\"angles\":{\"yaw\":176.41,\"pitch\":-41.52,\"roll\":84.6},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-3.12,7.39,2.2]}}}," \
				"\"displayName\":\"view01\",\"tags\":[],\"extensions\":[],\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"},\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"},\"image\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic01/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SavedViewIDPlopPhotoRealistic01/image\"}}}]}");
		}
		else if (url.ends_with("/savedviews")
			&& argMap["$skip"] == "100" && argMap["$top"] == "100")
		{
			return Response(cpr::status::HTTP_OK, "{\"savedViews\":[]}");
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
				"{\"id\":\"SVIdPhotoRealisticView02\",\"shared\":true,\"creationTime\":\"2024-06-13T10:08:34.797Z\",\"lastModified\":\"2024-06-13T12:26:35.678Z\"," \
				"\"savedViewData\":{\"itwin3dView\":{\"origin\":[-1.79,-0.69,1.59],\"extents\":[0,0,0]," \
				"\"angles\":{\"yaw\":-1.69,\"pitch\":-50.43,\"roll\":-92.19},\"camera\":{\"lens\":0,\"focusDist\":0,\"eye\":[-1.79,-0.69,1.59]}}}," \
				"\"displayName\":\"view02\",\"tags\":[],\"extensions\":[]," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Tests-Plop/members/abcdefabcdef\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Tests-Plop\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Tests-Plop\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/imodelId-PhotoReal-Render-97\"}," \
				"\"image\":{\"href\":\"https://api.test.com/savedviews/SVIdPhotoRealisticView02/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SVIdPhotoRealisticView02/image\"}}}}"
			);
		}
		else if (url.ends_with(SAVEDVIEWID_BUILDING_ALEXVIEW2) && method == "GET")
		{
			// GetSavedView with only 'roll' angle
			return Response(cpr::status::HTTP_OK, "{\"savedView\":" \
				"{\"id\":\"SVIdBuildingView_Nm02\",\"shared\":true,\"creationTime\":\"2024-08-21T08:31:17.000Z\",\"lastModified\":\"2024-08-21T08:31:17.000Z\"," \
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
				"\"legacyView\":{\"id\":\"8ce6a267-10a8-43f5-b6b8-0c1fb5d97973\",\"is2d\":false,\"groupId\":\"-1\",\"name\":\"AlexView2\",\"userId\":\"owner-identifier-059\"," \
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
				"\"_links\":{\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"savedView\":{\"href\":\"https://api.test.com/savedviews/SVIdBuildingView_Nm02\"}}}," \
				"{\"extensionName\":\"PerModelCategoryVisibility\",\"markdownUrl\":\"https://www.test.com/\"," \
				"\"schemaUrl\":\"https://www.test.com/\",\"data\":{\"perModelCategoryVisibilityProps\":[]},\"_links\":{\"iTwin\":{" \
				"\"href\":\"https://api.test.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"savedView\":{\"href\":\"https://api.test.com/savedviews/SVIdBuildingView_Nm02\"}}}]," \
				"\"_links\":{\"creator\":{\"href\":\"https://api.test.com/accesscontrol/iTwins/itwinId-Cay-EA/members/owner-identifier-059\"}," \
				"\"iTwin\":{\"href\":\"https://api.test.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.test.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.test.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\"}," \
				"\"image\":{\"href\":\"https://api.test.com/savedviews/SVIdBuildingView_Nm02/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.test.com/savedviews/SVIdBuildingView_Nm02/image\"}}}}"
			);
		}
		else if (url.ends_with(SAVEDVIEWID_BUILDING_CONSTRUCTION) && method == "GET")
		{
			// GetSavedView with hidden elements/models/categories + synchro
			return Response(cpr::status::HTTP_OK, "{\"savedView\":{\"id\":\"SVIdBuildingConstruction\",\"displayName\":\"Construction\",\"shared\":false,\"tags\":[],\"extensions\":" \
				"[{\"extensionName\":\"EmphasizeElements\",\"href\":\"https://api.bentley.com/savedviews/SVIdBuildingConstruction/extensions/EmphasizeElements\"},{\"extensionName\":\"PerModelCategoryVisibility\",\"href\":\"https://api.bentley.com/savedviews/SVIdBuildingConstruction/extensions/PerModelCategoryVisibility\"}],\"creationTime\":\"2025-01-10T10:02:00.089Z\"," \
				"\"lastModified\":\"2025-01-10T10:02:05.352Z\",\"savedViewData\":{\"itwin3dView\":{\"origin\":[45.48796771467186,19.17567984963212,-6.282902298539785],\"extents\":[41.75008017237279,26.716783202281604,21.223040086186096],\"angles\":{\"yaw\":30.000000000000114,\"pitch\":-35.264389682754434,\"roll\":-44.99999999999979}," \
				"\"camera\":{\"lens\":89.99999999999949,\"focusDist\":20.875040086186583,\"eye\":[53.361505503969084,-2.472547166037531,16.96506391818422]},\"categories\":{\"enabled\":[\"0x20000000057\",\"0x200000000d5\",\"0x200000000d7\",\"0x200000000d9\",\"0x200000000df\",\"0x200000000e1\",\"0x200000000e3\",\"0x200000000e5\",\"0x200000000e7\"," \
				"\"0x200000000e9\",\"0x200000000eb\",\"0x200000000ed\",\"0x200000000f1\",\"0x200000000f9\",\"0x200000000fd\",\"0x200000000ff\",\"0x20000000101\",\"0x20000000103\",\"0x20000000105\",\"0x2000000010d\",\"0x2000000010f\",\"0x20000000111\",\"0x20000000113\",\"0x20000000115\",\"0x20000000117\",\"0x20000000119\",\"0x2000000011b\",\"0x2000000011d\"," \
				"\"0x2000000011f\",\"0x20000000121\",\"0x20000000123\",\"0x20000000125\",\"0x20000000127\",\"0x20000000129\",\"0x2000000012b\",\"0x2000000012f\",\"0x20000000131\"],\"disabled\":[\"0x200000000e3\"]},\"models\":{\"enabled\":[\"0x2000000007f\",\"0x20000000134\",\"0x20000000136\",\"0x20000000138\",\"0x2000000013a\",\"0x2000000013c\",\"0x2000000013e\",\"0x20000000140\"," \
				"\"0x20000000142\",\"0x20000000144\",\"0x20000000146\",\"0x20000000148\",\"0x2000000014a\",\"0x2000000014c\",\"0x2000000014e\",\"0x20000000150\",\"0x20000000152\",\"0x20000000154\",\"0x20000000156\",\"0x20000000158\",\"0x2000000015a\",\"0x2000000015c\",\"0x2000000015e\",\"0x20000000160\",\"0x20000000162\",\"0x20000000164\",\"0x20000000166\"," \
				"\"0x20000000168\",\"0x2000000016a\",\"0x2000000016c\",\"0x2000000016e\",\"0x20000000170\",\"0x20000000172\",\"0x20000000174\",\"0x20000000176\",\"0x20000000178\",\"0x2000000017a\",\"0x2000000017c\",\"0x2000000017e\",\"0x20000000180\",\"0x20000000182\",\"0x20000000184\",\"0x20000000186\",\"0x20000000188\",\"0x2000000018a\",\"0x2000000018c\"," \
				"\"0x2000000018e\",\"0x20000000190\",\"0x20000000192\"],\"disabled\":[\"0x20000000134\",\"0x20000000186\",\"0x2000000018c\",\"0x20000000192\"]},\"displayStyle\":{\"renderTimeline\":\"0x20000003cda\",\"timePoint\":1758013200,\"viewflags\":{\"renderMode\":6,\"ambientOcclusion\":true},\"mapImagery\":{\"backgroundBase\":{\"visible\":true,\"name\":\"Bing Maps: Aerial Imagery with labels\",\"transparentBackground\":false," \
				"\"url\":\"https://dev++dot++virtualearth++dot++net/REST/v1/Imagery/Metadata/AerialWithLabels?o=json++and++incl=ImageryProviders++and++key={bingKey}\",\"formatId\":\"BingMaps\",\"provider\":{\"name\":\"BingProvider\",\"type\":3}}}," \
				"\"environment\":{\"ground\":{\"display\":false,\"elevation\":-0.01,\"aboveColor\":{\"red\":0,\"green\":100,\"blue\":0},\"belowColor\":{\"red\":101,\"green\":67,\"blue\":33}},\"sky\":{\"display\":true,\"twoColor\":true,\"skyColor\":{\"red\":222,\"green\":242,\"blue\":255}," \
				"\"groundColor\":{\"red\":240,\"green\":236,\"blue\":232},\"zenithColor\":{\"red\":222,\"green\":242,\"blue\":255},\"nadirColor\":{\"red\":240,\"green\":236,\"blue\":232}}},\"lights\":{\"portrait\":{\"intensity\":0.8},\"solar\":{\"intensity\":0},\"ambient\":{\"intensity\":0.55},\"specularIntensity\":0}}}}," \
				"\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/iTwins/itwinId-Cay-EA\"},\"project\":{\"href\":\"https://api.bentley.com/projects/itwinId-Cay-EA\"},\"imodel\":{\"href\":\"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/a1094ec1-5165-4550-b717-d5859d527938\"},\"image\":{\"href\":\"https://api.bentley.com/savedviews/SVIdBuildingConstruction/image?size=full\"}," \
				"\"thumbnail\":{\"href\":\"https://api.bentley.com/savedviews/SVIdBuildingConstruction/image\"}}}}"
			);
		}
		else if (url.ends_with("/extensions/EmphasizeElements") 
				 && method == "GET"
				 && url.find("/" SAVEDVIEWID_BUILDING_ALEXVIEW2 "/") != std::string::npos)
		{
			return Response(cpr::status::HTTP_OK, "{"\
				"\"extension\": {"\
				"\"extensionName\": \"EmphasizeElements\","\
				"\"markdownUrl\" : \"https://www.bentley.com/\","\
				"\"schemaUrl\" : \"https://www.bentley.com/\","\
				"\"data\" : \"{\\\"emphasizeElementsProps\\\":{}}\","\
				"\"_links\" : {"\
				"\"iTwin\": {"\
				"\"href\": \"https://api.bentley.com/iTwins/itwinId-Cay-EA\""\
				"},"\
				"\"project\" : {"\
				"\"href\": \"https://api.bentley.com/projects/itwinId-Cay-EA\""\
				"},"\
				"\"imodel\" : {"\
				"\"href\": \"https://api.bentley.com/imodels/ce302230-920b-464a-a7e0-e57aed2a3f37\""\
				"},"\
				"\"savedView\" : {"\
				"\"href\": \"https://api.bentley.com/savedviews/SVIdBuildingView_Nm02\"}}}}"
			);
		}
		else if (url.ends_with("/extensions/EmphasizeElements") 
				 && method == "GET"
				 && url.find("/" SAVEDVIEWID_BUILDING_CONSTRUCTION "/") != std::string::npos)
		{
			return Response(cpr::status::HTTP_OK, "{"\
				"\"extension\": {"\
				"\"extensionName\": \"EmphasizeElements\","\
				"\"markdownUrl\" : \"https://www.bentley.com/\","\
				"\"schemaUrl\" : \"https://www.bentley.com/\","\
				"\"data\" : \"{\\\"emphasizeElementsProps\\\":{\\\"neverDrawn\\\":[\\\"0x2000000028c\\\",\\\"0x2000000028b\\\"]}}\","\
				"\"_links\" : {"\
				"\"iTwin\": {"\
				"\"href\": \"https://api.bentley.com/iTwins/itwinId-Cay-EA\""\
				"},"\
				"\"project\" : {"\
				"\"href\": \"https://api.bentley.com/projects/itwinId-Cay-EA\""\
				"},"\
				"\"imodel\" : {"\
				"\"href\": \"https://api.bentley.com/imodels/imodelId-Building\""\
				"},"\
				"\"savedView\" : {"\
				"\"href\": \"https://api.bentley.com/savedviews/SVIdBuildingConstruction\"}}}}"
			);
		}
		else if (url.ends_with("/image")
				 && url.find("/" SAVEDVIEWID_BUILDING_CONSTRUCTION "/") != std::string::npos
				 && method == "GET")
		{
			return Response(cpr::status::HTTP_OK, "{"\
				"\"href\": \"" SAVEDVIEW_THUMBNAILURL "\"}"
			);
		}
		else if (url.ends_with("/image")
				 && url.find("/" SAVEDVIEWID_BUILDING_CONSTRUCTION "/") != std::string::npos
				 && method == "PUT" && data == "{\"image\":\"" SAVEDVIEW_THUMBNAILURL "\"}")
		{
			return Response(cpr::status::HTTP_OK, "");
		}
		else if (url.ends_with("/image")
			&& url.find("/" SAVEDVIEWID_BUILDING_TEST "/") != std::string::npos
			&& method == "PUT" && data == "{\"image\":\"" SAVEDVIEW_THUMBNAILURL_ERROR "\"}")
		{
			// Error 422
			return Response(cpr::status::HTTP_UNPROCESSABLE_ENTITY,
				"{\"error\":{\"code\":\"InvalidSavedviewsRequest\",\"message\":\"Cannot update savedview.\",\"details\":[{\"code\":\"InvalidRequestBody\",\"message\":\"image must be a base64Image.\",\"target\":\"image\"}]}}"
			);
		}
		else if (url.ends_with("/groups")
				 && method == "POST"
				 && data == ADD_GROUP_02_DATA)
		{
			return Response(cpr::status::HTTP_OK, "{" \
				"\"group\": {" \
				"\"id\": \"SVGroupId-Group02\"," \
				"\"displayName\" : \"Group02\"," \
				"\"shared\" : false," \
				"\"_links\" : {" \
				"\"iTwin\": {" \
					"\"href\": \"https://api.bentley.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\" : {" \
					"\"href\": \"https://api.bentley.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\" : {" \
					"\"href\": \"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\" : {" \
					"\"href\": \"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/a1094ec1-5165-4550-b717-d5859d527938\"}," \
				"\"savedViews\" : {" \
					"\"href\": \"https://api.bentley.com/savedviews?groupId=SVGroupId-Group02\"}}," \
				"\"readOnly\": false}}"
			);
		}
		else if (url.ends_with("/savedviews/groups") && ((argMap["iTwinId"] == ITWINID_CAYMUS_EAP
			&& argMap["iModelId"] == IMODELID_BUILDING) || (argMap["iTwinId"] == ITWINID_TESTS_ALEXW
			&& argMap["iModelId"] == IMODELID_PHOTO_REALISTIC_RENDERING))
			)
		{
			//---------------------------------------------------------------------------
			// GetSavedViewsGroups
			//---------------------------------------------------------------------------
			return Response(cpr::status::HTTP_OK, "{\"groups\":[{\"id\":\"SVGroupIdTest01\",\"displayName\":\"Advanced Visualization\"," \
				"\"shared\":true,\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.bentley.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/a1094ec1-5165-4550-b717-d5859d527938\"}," \
				"\"savedViews\":{\"href\":\"https://api.bentley.com/savedviews?groupId=SVGroupIdTest01\"}}," \
				"\"readOnly\":false},{\"id\":\"SVGroupIdTest02\",\"displayName\":\"Group 2\"," \
				"\"shared\":false,\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.bentley.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/a1094ec1-5165-4550-b717-d5859d527938\"}," \
				"\"savedViews\":{\"href\":\"https://api.bentley.com/savedviews?groupId=SVGroupIdTest02\"}}," \
				"\"readOnly\":false},{\"id\":\"SVGroupIdTest03\",\"displayName\":\"New Group 1\"," \
				"\"shared\":true,\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.bentley.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/0a483d73-ffce-4d52-9af4-a9927d07aa82\"}," \
				"\"savedViews\":{\"href\":\"https://api.bentley.com/savedviews?groupId=SVGroupIdTest03\"}}," \
				"\"readOnly\":false},{\"id\":\"SVGroupIdRenameTest\",\"displayName\":\"testRenameGroup\"," \
				"\"shared\":true,\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/iTwins/itwinId-Cay-EA\"}," \
				"\"project\":{\"href\":\"https://api.bentley.com/projects/itwinId-Cay-EA\"}," \
				"\"imodel\":{\"href\":\"https://api.bentley.com/imodels/imodelId-Building\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/accesscontrol/iTwins/itwinId-Cay-EA/members/a1094ec1-5165-4550-b717-d5859d527938\"}," \
				"\"savedViews\":{\"href\":\"https://api.bentley.com/savedviews?groupId=SVGroupIdRenameTest\"}}," \
				"\"readOnly\":false}]," \
				"\"_links\":{\"self\":{\"href\":\"https://api.bentley.com/savedviews/groups?iTwinId=itwinId-Cay-EA&iModelId=imodelId-Building\"}}}"
			);
		}
		else if (url.ends_with("/savedviews/groups") 
				 && argMap["iTwinId"] == ITWINID_CAYMUS_EAP
				 && argMap["iModelId"].empty())
		{
			return Response(cpr::status::HTTP_OK, "{\"groups\":[{\"id\":\"SVGroupIdCaymusTestName\",\"displayName\":\"Test Name\"," \
				"\"shared\":true,\"_links\":{\"iTwin\":{\"href\":\"https://api.bentley.com/path1/path2/id\"}," \
				"\"project\":{\"href\":\"https://api.bentley.com/path1/path2/id\"}," \
				"\"imodel\":{\"href\":\"https://api.bentley.com/path1/path2/id\"}," \
				"\"creator\":{\"href\":\"https://api.bentley.com/path1/path2/id\"}," \
				"\"savedViews\":{\"href\":\"https://api.bentley.com/path1/path2/id\"}}," \
				"\"readOnly\":false}]}");
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
			int const HeaderStatus = CheckRequiredHeaders(headers, {
				{ "Accept", "application/vnd.bentley.itwin-platform.v1+json" },
				{ "Prefer", "return=minimal" },
				{ "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN }
			});
			if (HeaderStatus != cpr::status::HTTP_OK)
			{
				return Response(HeaderStatus, "Error in headers.");
			}
			if (iTwinId == ITWINID_CAYMUS_EAP)
			{
				return Response(cpr::status::HTTP_OK,
					"{\r\n  \"realityData\": [\r\n    {\r\n      \"id\": \"realityData-Id-Orlando-Magic\",\r\n " \
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
				"{\r\n  \"realityData\": {\r\n    \"id\": \"realityData-Id-Orlando-Magic\",\r\n    \"displayName\": \"Orlando_CesiumDraco_LAT\"," \
				"\r\n    \"classification\": \"Model\",\r\n    \"type\": \"Cesium3DTiles\"," \
				"\r\n    \"rootDocument\": \"Orlando_CesiumDraco_LAT.json\"," \
				"\r\n    \"dataCenterLocation\" : \"East US\",\r\n    \"authoring\" : false,\r\n    \"size\" : 3164951," \
				"\r\n    \"extent\" : {\r\n      \"southWest\": {\r\n        \"latitude\": 28.496424905782874,\r\n        \"longitude\" : -81.42035061172474\r\n      }," \
				"\r\n      \"northEast\" : {\r\n        \"latitude\": 28.587753137096165,\r\n        \"longitude\" : -81.33756635398319\r\n      }\r\n    }," \
				"\r\n    \"accessControl\": \"ITwin\",\r\n    \"modifiedDateTime\" : \"2024-05-27T12:20:01Z\"," \
				"\r\n    \"lastAccessedDateTime\" : \"2024-06-18T08:07:48Z\"," \
				"\r\n    \"createdDateTime\" : \"2024-03-19T12:39:00Z\",\r\n    \"ownerId\" : \"owner-identifier-059\"}}"
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
				"\"href\": \"https://realityblob59.blob.core.net/realityData-Id-Orlando-Magic?skoid=6db55139-0f1c-467a-95b4-5009c17c1bf0" \
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
					"\"high\":[409.678652192302,249.78031406156776,33.397180631459555]},\"globalOrigin\":[0,0,0],\"key\":\"imodelId-Building:changesetidbuilding59\"," \
					"\"iTwinId\":\"itwinId-Cay-EA\",\"iModelId\":\"imodelId-Building\",\"changeset\":{\"id\":\"changesetidbuilding59\",\"index\":12}}"
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
					"\"key\":\"imodelId-Stadium-023:changesetIdStadium\",\"iTwinId\":\"itwinId-Stadium-Ouh-QA\"," \
					"\"iModelId\":\"imodelId-Stadium-023\",\"changeset\":{\"id\":\"changesetIdStadium\",\"index\":63}}"
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
	IMPLEMENT_OBS_CALLBACK(OnSavedViewGroupInfosRetrieved, FSavedViewGroupInfos);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewRetrieved, FSavedView, FSavedViewInfo);
	IMPLEMENT_OBS_CALLBACK(OnSavedViewAdded, FSavedViewInfo);
	IMPLEMENT_OBS_CALLBACK(OnSavedViewGroupAdded, FSavedViewGroupInfo);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewDeleted, FString, FString);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewEdited, FSavedView, FSavedViewInfo);

	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewExtensionRetrieved, FString, FString);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewThumbnailRetrieved, FString, TArray<uint8_t>);
	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnSavedViewThumbnailUpdated, FString, FString);


	IMPLEMENT_OBS_CALLBACK(OnRealityDataRetrieved, FITwinRealityDataInfos);
	IMPLEMENT_OBS_CALLBACK(OnRealityData3DInfoRetrieved, FITwinRealityData3DInfo);

	IMPLEMENT_OBS_CALLBACK_TWO_ARGS(OnElementPropertiesRetrieved, FElementProperties, FString);

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


FITwinAPITestHelperBase::~FITwinAPITestHelperBase()
{

}

bool FITwinAPITestHelperBase::InitServer(MockServerPtr Server)
{
	MockServer = std::move(Server);
	if (!ensureMsgf(MockServer && MockServer->isRunning(), TEXT("Mock Server not started!")))
	{
		return false;
	}
	return true;
}

bool FITwinAPITestHelperBase::Init(AdvViz::SDK::EITwinEnvironment Env /*= EITwinEnvironment::Prod*/)
{
	if (bInitDone)
		return true;
	if (!DoInit(Env))
	{
		return false;
	}
	static bool bHasSetTestToken = false;
	ensureMsgf(IsInGameThread(), TEXT("UT should be initialized in game thread"));
	if (!bHasSetTestToken)
	{
		AdvViz::SDK::ITwinAuthManager::GetInstance(Env)->SetOverrideAccessToken(ITWINTEST_ACCESS_TOKEN);
		bHasSetTestToken = true;
	}
	bInitDone = true;
	return true;
}

void FITwinAPITestHelperBase::Cleanup()
{
	MockServer.reset();

	DoCleanup();
}

std::string FITwinAPITestHelperBase::GetServerUrl() const
{
	if (MockServer)
	{
		std::ostringstream url;
		url << "http://localhost:" << MockServer->getPort();
		return url.str();
	}
	return {};
}


bool FITwinAPITestHelperBase::PostCondition() const
{
	if (MockServer)
	{
		FITwinMockServerBase const* pServer = static_cast<FITwinMockServerBase const*>(MockServer.get());
		if (!pServer->PostCondition())
		{
			BE_LOGE("ITwinAPI", "Server post-condition not met");
			return false;
		}
	}
	return true;
}


class FITwinAPITestHelper : public FITwinAPITestHelperBase
{
public:
	static FITwinAPITestHelper& Instance();
	~FITwinAPITestHelper();

	TestObserverPtr const& GetObserver() const { return Observer; }
	TObjectPtr<UITwinWebServices> const& GetWebServices() const { return WebServices; }

protected:
	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment Env) override;
	virtual void DoCleanup() override;

private:
	FITwinAPITestHelper() {}

	TObjectPtr<UITwinWebServices> WebServices;
	TObjectPtr<AITwinServerConnection> ServerConnection;
	TestObserverPtr Observer;
};

/*static*/
FITwinAPITestHelper& FITwinAPITestHelper::Instance()
{
	static FITwinAPITestHelper instance;
	return instance;
}

bool FITwinAPITestHelper::DoInit(AdvViz::SDK::EITwinEnvironment Env)
{
	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8080;

	if (!InitServer(FITwinMockServer::MakeServer(DEFAULT_SERVER_PORT)))
	{
		return false;
	}

	// totally disable error logs (even though SuppressLogErrors avoids making the unit-test fail, the test
	// target still fails at the end because of the logs...)
	UITwinWebServices::SetLogErrors(false);

	const std::string url = GetServerUrl();

	WebServices = NewObject<UITwinWebServices>();
	ServerConnection = NewObject<AITwinServerConnection>();
	ServerConnection->Environment = static_cast<EITwinEnvironment>(Env);
	WebServices->SetServerConnection(ServerConnection);
	WebServices->SetTestServerURL(url.c_str());
	Observer = std::make_shared<ITwinTestWebServicesObserver>();
	WebServices->SetObserver(Observer.get());
	return true;
}

void FITwinAPITestHelper::DoCleanup()
{
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


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForMockServerResponse, TestObserverPtr, Observer);

bool FNUTWaitForMockServerResponse::Update()
{
	return !Observer || !Observer->IsWaitingForServerResponse();
}


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

			UTEST_EQUAL("Id", iTwins[0].Id, TEXT("itwinId-Tests-Plop"));
			UTEST_EQUAL("DisplayName", iTwins[0].DisplayName, TEXT("Tests_AlexW"));
			UTEST_EQUAL("Status", iTwins[0].Status, TEXT("Active"));

			UTEST_EQUAL("Id", iTwins[1].Id, TEXT(ITWINID_CAYMUS_EAP));
			UTEST_EQUAL("DisplayName", iTwins[1].DisplayName, TEXT("Bentley Caymus EAP"));
			UTEST_EQUAL("Status", iTwins[1].Status, TEXT("Active"));

			UTEST_EQUAL("Id", iTwins[2].Id, TEXT("itwinId-Another-Project"));
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

				UTEST_EQUAL("Id", iModels[0].Id, TEXT("imodelId-Building"));
				UTEST_EQUAL("DisplayName", iModels[0].DisplayName, TEXT("Building"));

				UTEST_EQUAL("Id", iModels[1].Id, TEXT("imodelId-Another-World"));
				UTEST_EQUAL("DisplayName", iModels[1].DisplayName, TEXT("Hatch Terrain Model"));

				UTEST_EQUAL("Id", iModels[2].Id, TEXT("imodelId-Highway-66"));
				UTEST_EQUAL("DisplayName", iModels[2].DisplayName, TEXT("Highway"));

				UTEST_EQUAL("Id", iModels[3].Id, TEXT("imodelId-Metro-Boulot"));
				UTEST_EQUAL("DisplayName", iModels[3].DisplayName, TEXT("MetroStation"));

				UTEST_EQUAL("Id", iModels[4].Id, TEXT("imodelId-Offshore-Rig"));
				UTEST_EQUAL("DisplayName", iModels[4].DisplayName, TEXT("OffshoreRig"));

				UTEST_EQUAL("Id", iModels[5].Id, TEXT("imodelId-Turb-53"));
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

			UTEST_EQUAL("Id", Changesets[0].Id, TEXT("changesetIdTheOneToTest"));
			UTEST_EQUAL("DisplayName", Changesets[0].DisplayName, TEXT("4"));
			UTEST_EQUAL("Description", Changesets[0].Description, TEXT("MicroStation Connector - initalLoad - Initialization changes"));
			UTEST_EQUAL("Index", Changesets[0].Index, 4);

			UTEST_EQUAL("Id", Changesets[1].Id, TEXT("changesetIdOfTheParent"));
			UTEST_EQUAL("DisplayName", Changesets[1].DisplayName, TEXT("3"));
			UTEST_EQUAL("Description", Changesets[1].Description, TEXT("MicroStation Connector - initalLoad - BootstrapExternalSources"));
			UTEST_EQUAL("Index", Changesets[1].Index, 3);

			UTEST_EQUAL("Id", Changesets[2].Id, TEXT("changesetIdOfTheGrandPa"));
			UTEST_EQUAL("DisplayName", Changesets[2].DisplayName, TEXT("2"));
			UTEST_EQUAL("Description", Changesets[2].Description, TEXT("MicroStation Connector - initalLoad - Domain schema upgrade"));
			UTEST_EQUAL("Index", Changesets[2].Index, 2);

			UTEST_EQUAL("Id", Changesets[3].Id, TEXT("changesetIdOfTheGrandGrandPa"));
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
		TEXT("https://gltf59.blob.net/expId-Turb-53/tileset.json?sv=2024-05-04&spr=https&se=2024-06-22T23%3A59%3A59Z&sr=c&sp=rl&sig=Nq%2B%2FPjEXu64kgPsYVBjuxTV44Zq4GfsSxqTDDygD4oI%3D");

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
				// we only keep one now in AdvViz::SDK::ITwinWebServices::GetExports
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
				UTEST_EQUAL("Id 0", ExportInfos[0].Id, TEXT("ExportId-PhotoReal-Cesium"));
				//UTEST_EQUAL("Id 1", ExportInfos[1].Id, TEXT("ExportId-PhotoReal-Cesium-Ter"));
				// I left the old export version 0.2[.0] for this one, instead of 0.2.8.1, so that it was
				// filtered out by WebServices, but we can no longer (and should not) test versions
				//UTEST_EQUAL("Id 2", ExportInfos[2].Id, TEXT("ExportId-PhotoReal-Cesium-Bis"));
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
			UTEST_EQUAL("ExportId", InExportId, TEXT("ExportId-Just-Started"));
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
			UTEST_EQUAL("ITwinId", Infos.ITwinId, ITWINID_TESTS_ALEXW);
			UTEST_EQUAL("IModelId", Infos.IModelId, IMODELID_PHOTO_REALISTIC_RENDERING);

			TArray<FSavedViewInfo> const& SavedViews(Infos.SavedViews);
			if (Infos.GroupId.IsEmpty())
			{
				UTEST_EQUAL("Num", SavedViews.Num(), 5);

				UTEST_EQUAL("Id", SavedViews[0].Id, TEXT("SavedViewIDPlopPhotoRealistic01"));
				UTEST_EQUAL("DisplayName", SavedViews[0].DisplayName, TEXT("view01"));
				UTEST_EQUAL("Shared", SavedViews[0].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[0].CreationTime, TEXT("2024-06-13T10:07:29.897Z"));
				TestTrue("Extensions", SavedViews[0].Extensions.IsEmpty());

				UTEST_EQUAL("Id", SavedViews[1].Id, TEXT("SVIdPhotoRealisticView02"));
				UTEST_EQUAL("DisplayName", SavedViews[1].DisplayName, TEXT("view02"));
				UTEST_EQUAL("Shared", SavedViews[1].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[1].CreationTime, TEXT("2024-06-13T10:08:34.797Z"));
				TestTrue("Extensions", SavedViews[1].Extensions.IsEmpty());

				UTEST_EQUAL("Id", SavedViews[2].Id, TEXT("SavedViewIDPlopPhotoRealistic03"));
				UTEST_EQUAL("DisplayName", SavedViews[2].DisplayName, TEXT("view03 - top"));
				UTEST_EQUAL("Shared", SavedViews[2].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[2].CreationTime, TEXT("2024-06-13T10:43:36.006Z"));
				TestTrue("Extensions", SavedViews[2].Extensions.IsEmpty());

				UTEST_EQUAL("Id", SavedViews[3].Id, TEXT("SavedViewIDPlop_PhotoRealistic04"));
				UTEST_EQUAL("DisplayName", SavedViews[3].DisplayName, TEXT("view04"));
				UTEST_EQUAL("Shared", SavedViews[3].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[3].CreationTime, TEXT("2024-06-13T12:16:16.765Z"));
				TestTrue("Extensions", SavedViews[3].Extensions.IsEmpty());

				UTEST_EQUAL("Id", SavedViews[4].Id, TEXT("SavedViewIDPlop-PhotoRealistic05"));
				UTEST_EQUAL("DisplayName", SavedViews[4].DisplayName, TEXT("view05"));
				UTEST_EQUAL("Shared", SavedViews[4].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[4].CreationTime, TEXT("2024-06-18T07:33:29.596Z"));
				TestTrue("Extensions", SavedViews[4].Extensions.IsEmpty());
			}
			else if (Infos.GroupId == TEXT(SAVEDVIEWGROUPID_TESTRENAMEGROUP))
			{
				UTEST_EQUAL("Num", SavedViews.Num(), 1);
				UTEST_EQUAL("Id", SavedViews[0].Id, TEXT("SavedViewIDPlopPhotoRealistic01"));
				UTEST_EQUAL("DisplayName", SavedViews[0].DisplayName, TEXT("view01"));
				UTEST_EQUAL("Shared", SavedViews[0].bShared, true);
				UTEST_EQUAL("CreationTime", SavedViews[0].CreationTime, TEXT("2024-06-13T10:07:29.897Z"));
			}
			else if (Infos.GroupId == TEXT(SAVEDVIEWGROUPID_GROUP02))
			{
				TestTrue("Num", SavedViews.IsEmpty());
			}

			return true;
		};
		//case itwinid/imodelid
		WebServices->GetAllSavedViews(ITWINID_TESTS_ALEXW, IMODELID_PHOTO_REALISTIC_RENDERING);
		//case groupid
		Observer->AddPendingRequest();
		WebServices->GetAllSavedViews(ITWINID_TESTS_ALEXW, IMODELID_PHOTO_REALISTIC_RENDERING, SAVEDVIEWGROUPID_TESTRENAMEGROUP);
		//case pagination
		Observer->AddPendingRequest();
		WebServices->GetAllSavedViews(ITWINID_TESTS_ALEXW, IMODELID_PHOTO_REALISTIC_RENDERING, SAVEDVIEWGROUPID_GROUP02, 100, 100);
	}

	SECTION("GetSavedViewsGroups")
	{
		Observer->AddPendingRequest();
		Observer->OnSavedViewGroupInfosRetrievedFunc = [this](bool bSuccess, FSavedViewGroupInfos const& Infos)
			{
				UTEST_TRUE("Get Saved Views Groups request result", bSuccess);

				if (!Infos.IModelId.IsEmpty())
				{
					TArray<FSavedViewGroupInfo> const& Groups(Infos.SavedViewGroups);
					UTEST_EQUAL("Num", Groups.Num(), 4);

					UTEST_EQUAL("Id", Groups[0].Id, TEXT("SVGroupIdTest01"));
					UTEST_EQUAL("DisplayName", Groups[0].DisplayName, TEXT("Advanced Visualization"));
					UTEST_EQUAL("Shared", Groups[0].bShared, true);
					UTEST_EQUAL("ReadOnly", Groups[0].bReadOnly, false);

					UTEST_EQUAL("Id", Groups[1].Id, TEXT("SVGroupIdTest02"));
					UTEST_EQUAL("DisplayName", Groups[1].DisplayName, TEXT("Group 2"));
					UTEST_EQUAL("Shared", Groups[1].bShared, false);
					UTEST_EQUAL("ReadOnly", Groups[0].bReadOnly, false);

					UTEST_EQUAL("Id", Groups[2].Id, TEXT("SVGroupIdTest03"));
					UTEST_EQUAL("DisplayName", Groups[2].DisplayName, TEXT("New Group 1"));
					UTEST_EQUAL("Shared", Groups[2].bShared, true);
					UTEST_EQUAL("ReadOnly", Groups[2].bReadOnly, false);

					UTEST_EQUAL("Id", Groups[3].Id, TEXT("SVGroupIdRenameTest"));
					UTEST_EQUAL("DisplayName", Groups[3].DisplayName, TEXT("testRenameGroup"));
					UTEST_EQUAL("Shared", Groups[3].bShared, true);
					UTEST_EQUAL("ReadOnly", Groups[3].bReadOnly, false);
				}
				else
				{
					TArray<FSavedViewGroupInfo> const& Groups(Infos.SavedViewGroups);
					UTEST_EQUAL("Num", Groups.Num(), 1);

					UTEST_EQUAL("Id", Groups[0].Id, TEXT("SVGroupIdCaymusTestName"));
					UTEST_EQUAL("DisplayName", Groups[0].DisplayName, TEXT("Test Name"));
					UTEST_EQUAL("Shared", Groups[0].bShared, true);
					UTEST_EQUAL("ReadOnly", Groups[0].bReadOnly, false);
				}

				return true;
			};
		//case itwinid/imodelid
		WebServices->GetSavedViewGroups(ITWINID_CAYMUS_EAP, IMODELID_BUILDING);
		//case itwinid
		Observer->AddPendingRequest();
		WebServices->GetSavedViewGroups(ITWINID_CAYMUS_EAP);
	}

	// Get/Edit/Add SavedView both expect the same kind of response, so we share the same callback
	auto CheckSavedView = [this](FSavedView const& SV)
	{
		UTEST_TRUE("Origin", FVector::PointsAreNear(SV.Origin, FVector(-1.79, -0.69, 1.59), UE_SMALL_NUMBER));
		UTEST_TRUE("Extents", FVector::PointsAreNear(SV.Extents, FVector(0, 0, 0), UE_SMALL_NUMBER));
		UTEST_TRUE("Angles", FMath::IsNearlyEqual(SV.Angles.Yaw, -1.69, UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(SV.Angles.Pitch, -50.43, UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(SV.Angles.Roll, -92.19, UE_SMALL_NUMBER));
		TestTrue("HiddenElements should be empty", SV.HiddenElements.IsEmpty());
		return true;
	};
	auto CheckSavedViewInfo = [this](FSavedViewInfo const& Info)
	{
		UTEST_EQUAL("Id", Info.Id, TEXT(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02));
		UTEST_EQUAL("DisplayName", Info.DisplayName, TEXT("view02"));
		UTEST_EQUAL("Shared", Info.bShared, true);
		TestTrue("Extensions should be empty", Info.Extensions.IsEmpty());
		return true;
	};
	auto CheckEmptySVValues = [this](FSavedView const& SV)
	{
		TestTrue("HiddenElements should be empty", SV.HiddenElements.IsEmpty());
		TestTrue("HiddenModels should be empty", SV.HiddenModels.IsEmpty());
		TestTrue("HiddenCategories should be empty", SV.HiddenCategories.IsEmpty());
		TestTrue("Synchro - RenderTimeline should be empty", SV.DisplayStyle.RenderTimeline.IsEmpty());
		TestTrue("Synchro - TimePoint = 0.0", FMath::IsNearlyEqual(SV.DisplayStyle.TimePoint, 0.));
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
				UTEST_TRUE("CheckEmptySVValues", CheckEmptySVValues(SavedView));
			}
			else if (SavedViewInfo.Id == TEXT(SAVEDVIEWID_BUILDING_ALEXVIEW2))
			{
				UTEST_EQUAL("Id", SavedViewInfo.Id, TEXT(SAVEDVIEWID_BUILDING_ALEXVIEW2));
				UTEST_EQUAL("DisplayName", SavedViewInfo.DisplayName, TEXT("AlexView2"));
				UTEST_EQUAL("Shared", SavedViewInfo.bShared, true);

				UTEST_TRUE("Origin", FVector::PointsAreNear(SavedView.Origin, FVector(62.47373320977305, -7.5267036440751, 7.8815683208719705), UE_SMALL_NUMBER));
				UTEST_TRUE("Extents", FVector::PointsAreNear(SavedView.Extents, FVector(2.5791900968344437, 1.8184076521127042, 1.2895950484174423), UE_SMALL_NUMBER));
				UTEST_TRUE("Angles", FMath::IsNearlyEqual(SavedView.Angles.Yaw, 0., UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Pitch, 0., UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Roll, -90., UE_SMALL_NUMBER));
				UTEST_EQUAL("ExtensionName[0]", SavedViewInfo.Extensions[0], TEXT("EmphasizeElements"));
				UTEST_EQUAL("ExtensionName[1]", SavedViewInfo.Extensions[1], TEXT("PerModelCategoryVisibility"));
				UTEST_TRUE("CheckEmptySVValues", CheckEmptySVValues(SavedView));
			}
			else
			{
				UTEST_EQUAL("Id", SavedViewInfo.Id, TEXT(SAVEDVIEWID_BUILDING_CONSTRUCTION));
				UTEST_EQUAL("DisplayName", SavedViewInfo.DisplayName, TEXT("Construction"));
				UTEST_EQUAL("Shared", SavedViewInfo.bShared, false);

				UTEST_TRUE("Origin", FVector::PointsAreNear(SavedView.Origin, FVector(53.361505503969084, -2.472547166037531, 16.96506391818422), UE_SMALL_NUMBER));
				UTEST_TRUE("Extents", FVector::PointsAreNear(SavedView.Extents, FVector(41.75008017237279, 26.716783202281604, 21.223040086186096), UE_SMALL_NUMBER));
				UTEST_TRUE("Angles", FMath::IsNearlyEqual(SavedView.Angles.Yaw, 30.000000000000114, UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Pitch, -35.264389682754434, UE_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(SavedView.Angles.Roll, -44.99999999999979, UE_SMALL_NUMBER));
				UTEST_EQUAL("ExtensionName[0]", SavedViewInfo.Extensions[0], TEXT("EmphasizeElements"));
				UTEST_EQUAL("ExtensionName[1]", SavedViewInfo.Extensions[1], TEXT("PerModelCategoryVisibility"));
				UTEST_EQUAL("HiddenElements[0]", SavedView.HiddenElements[0], TEXT("0x2000000028c"));
				UTEST_EQUAL("HiddenElements[1]", SavedView.HiddenElements[1], TEXT("0x2000000028b"));
				UTEST_EQUAL("Synchro - RenderTimeline", SavedView.DisplayStyle.RenderTimeline, TEXT("0x20000003cda"));
				UTEST_EQUAL("Synchro - TimePoint", SavedView.DisplayStyle.TimePoint, 1758013200.0);
				UTEST_EQUAL("HiddenModels[0]", SavedView.HiddenModels[0], TEXT("0x20000000134"));
				UTEST_EQUAL("HiddenModels[1]", SavedView.HiddenModels[1], TEXT("0x20000000186"));
				UTEST_EQUAL("HiddenModels[2]", SavedView.HiddenModels[2], TEXT("0x2000000018c"));
				UTEST_EQUAL("HiddenModels[3]", SavedView.HiddenModels[3], TEXT("0x20000000192"));
				UTEST_EQUAL("HiddenCategories[3]", SavedView.HiddenCategories[0], TEXT("0x200000000e3"));
			}
			return true;
		};
		Observer->AddPendingRequest();
		WebServices->GetSavedView(SAVEDVIEWID_PHOTO_REALISTIC_RENDERING_VIEW02);

		Observer->AddPendingRequest();
		WebServices->GetSavedView(SAVEDVIEWID_BUILDING_ALEXVIEW2);
		//case hidden elements/categories/models + synchro
		Observer->AddPendingRequest();
		WebServices->GetSavedView(SAVEDVIEWID_BUILDING_CONSTRUCTION);
	}

	SECTION("GetSavedViewExtension")
	{
		Observer->OnSavedViewExtensionRetrievedFunc = [&, this]
		(bool bSuccess, FString const& SavedViewId, FString const& data)
			{
				UTEST_TRUE("Get Saved View Extension request result", bSuccess);
				UTEST_EQUAL("SavedViewId", SavedViewId, TEXT(SAVEDVIEWID_BUILDING_CONSTRUCTION));
				UTEST_EQUAL("Data", data, "{\"emphasizeElementsProps\":{\"neverDrawn\":[\"0x2000000028c\",\"0x2000000028b\"]}}");
				return true;
			};
		Observer->AddPendingRequest();
		WebServices->GetSavedViewExtension(SAVEDVIEWID_BUILDING_CONSTRUCTION, TEXT("EmphasizeElements"));
	}

	SECTION("GetSavedViewThumbnail")
	{
		Observer->OnSavedViewThumbnailRetrievedFunc = [&, this]
		(bool bSuccess, FString const& SavedViewId, TArray<uint8_t> const& RawData)
			{
				UTEST_TRUE("Get Saved View Thumbnail request result", bSuccess);
				UTEST_EQUAL("SavedViewId", SavedViewId, TEXT(SAVEDVIEWID_BUILDING_CONSTRUCTION));
				UTEST_TRUE("Texture2D", FImageUtils::ImportBufferAsTexture2D(RawData) != nullptr);
				UTEST_TRUE("RawData", !RawData.IsEmpty());
				return true;
			};
		Observer->AddPendingRequest();
		WebServices->GetSavedViewThumbnail(SAVEDVIEWID_BUILDING_CONSTRUCTION);
	}

	SECTION("UpdateSavedViewThumbnail")
	{
		Observer->OnSavedViewThumbnailUpdatedFunc = [&, this]
		(bool bSuccess, FString const& SavedViewId, FString const& Response)
			{
				if (SavedViewId == SAVEDVIEWID_BUILDING_CONSTRUCTION)
				{
					UTEST_TRUE("Update Saved View Thumbnail request result", bSuccess);
					UTEST_EQUAL("SavedViewId", SavedViewId, TEXT(SAVEDVIEWID_BUILDING_CONSTRUCTION));
					UTEST_TRUE("Empty Response", Response.IsEmpty());
				}
				else if (SavedViewId == SAVEDVIEWID_BUILDING_TEST)
				{
					UTEST_EQUAL("Update Saved View Thumbnail should fail", bSuccess, false);
					UTEST_EQUAL("ErrorMessage", Response,
						TEXT("[UpdateSavedViewThumbnail] code 422: Unknown\n\tError [InvalidSavedviewsRequest]: Cannot update savedview.\n\tDetails: [InvalidRequestBody] image must be a base64Image. (target: image)"));
				}
				else
				{
					ensureMsgf(false, TEXT("Unexpected SavedView ID: %s"), *SavedViewId);
				}
				return true;
			};
		Observer->AddPendingRequest();
		WebServices->UpdateSavedViewThumbnail(SAVEDVIEWID_BUILDING_CONSTRUCTION, SAVEDVIEW_THUMBNAILURL);
		Observer->AddPendingRequest();
		WebServices->UpdateSavedViewThumbnail(SAVEDVIEWID_BUILDING_TEST, SAVEDVIEW_THUMBNAILURL_ERROR);
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
			{
				FVector(-1.79, -0.69, 1.59),
				FVector(0.0, 0.0, 0.0),
				FRotator(-50.43, -1.69, -92.19)
			},
			{
				TEXT(""),
				TEXT("view02"),
				true
			},
			TEXT(IMODELID_PHOTO_REALISTIC_RENDERING));
	}

	SECTION("AddSavedViewGroup")
	{
		Observer->AddPendingRequest();
		Observer->OnSavedViewGroupAddedFunc = [&, this]
		(bool bSuccess, FSavedViewGroupInfo const& GroupInfo)
			{
				UTEST_TRUE("Add Saved View Group request result", bSuccess);
				UTEST_EQUAL("Id", GroupInfo.Id, TEXT(SAVEDVIEWGROUPID_GROUP02));
				UTEST_EQUAL("DisplayName", GroupInfo.DisplayName, TEXT("Group02"));
				UTEST_EQUAL("Shared", GroupInfo.bShared, false);
				UTEST_EQUAL("ReadOnly", GroupInfo.bReadOnly, false);
				return true;
			};
		WebServices->AddSavedViewGroup(
			TEXT(ITWINID_CAYMUS_EAP),
			TEXT(IMODELID_BUILDING),
			{
				TEXT(""),
				TEXT("Group02"),
				false,
				false
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
				TEXT("https://realityblob59.blob.core.net/realityData-Id-Orlando-Magic/Orlando_CesiumDraco_LAT.json?skoid=6db55139-0f1c-467a-95b4-5009c17c1bf0&sktid=067e9632-ea4c-4ed9-9e6d-e294956e284b&skt=2024-06-18T17%3A42%3A00Z&ske=2024-06-21T17%3A42%3A00Z&sks=b&skv=2024-05-04&sv=2024-05-04&st=2024-06-18T20%3A11%3A05Z&se=2024-06-19T23%3A59%3A59Z&sr=c&sp=rl&sig=0qSqX3OF4qlyYeHUc8hT61NCI%3D")
			);
			return true;
		};
		WebServices->GetRealityData3DInfo(ITWINID_CAYMUS_EAP, REALITYDATAID_ORLANDO);
	}

	SECTION("GetElementProperties")
	{
		Observer->AddPendingRequest();
		Observer->OnElementPropertiesRetrievedFunc = [this](bool bSuccess, FElementProperties const& InProps, FString const& InElementId)
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

			UTEST_EQUAL("Element Id", InElementId, TEXT("0x20000001baf"));
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
				UTEST_TRUE("CartographicOrigin Lat.", FMath::IsNearlyEqual(EcefLocation.CartographicOrigin.Latitude, FMath::RadiansToDegrees(0.022790512521193126), UE_SMALL_NUMBER));
				UTEST_TRUE("CartographicOrigin Long.", FMath::IsNearlyEqual(EcefLocation.CartographicOrigin.Longitude, FMath::RadiansToDegrees(1.8129729494684641), UE_SMALL_NUMBER));

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
