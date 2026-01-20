/*--------------------------------------------------------------------------------------+
|
|     $Source: WebTestHelpers.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#if WITH_TESTS

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinEnvironment.h>
#	include <cpr/cpr.h>
#	include <httpmockserver/mock_server.h>
#	include <httpmockserver/port_searcher.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <map>


// Special token used in both WebServices and MaterialPErsistence tests
#define ITWINTEST_ACCESS_TOKEN "ThisIsATestITwinAccessToken"



/// Base class for mock servers used in iTwin services tests
class FITwinMockServerBase : public httpmock::MockServer
{
public:
	explicit FITwinMockServerBase(int port);

	virtual bool PostCondition() const { return true; }

protected:
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

	int CheckRequiredHeaders(const std::vector<Header>& headers,
		std::map<std::string, std::string> const& requiredHeaders) const;

	std::string ToString(const std::vector<Header>& headers) const;

	/// Process /arg_test request
	Response ProcessArgTest(const std::vector<UrlArg>& urlArguments) const;
};


class FITwinAPITestHelperBase
{
public:
	using MockServerPtr = std::unique_ptr<httpmock::MockServer>;

	virtual ~FITwinAPITestHelperBase();
	bool Init(AdvViz::SDK::EITwinEnvironment Env = AdvViz::SDK::EITwinEnvironment::Prod);
	void Cleanup();

	/// Return URL server is listening at. E.g.: http://localhost:8080
	std::string GetServerUrl() const;

	bool HasMockServer() const { return !!MockServer; }

	/// Check conditions that should be met once all the tests have been run.
	virtual bool PostCondition() const;

protected:
	FITwinAPITestHelperBase() {}

	bool InitServer(MockServerPtr Server);

	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment) { return true; }
	virtual void DoCleanup() {}

private:
	MockServerPtr MockServer;
	bool bInitDone = false;
};


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


#endif // WITH_TESTS
