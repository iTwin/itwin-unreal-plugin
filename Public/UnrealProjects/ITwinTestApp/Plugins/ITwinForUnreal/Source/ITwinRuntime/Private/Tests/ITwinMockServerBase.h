/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMockServerBase.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <CoreMinimal.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <httpmockserver/mock_server.h>
#	include <httpmockserver/port_searcher.h>
#	include <microhttpd.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <map>


/// Base class for mock servers used in iTwin services tests
class FITwinMockServerBase : public httpmock::MockServer
{
public:
	explicit FITwinMockServerBase(int port);
	virtual ~FITwinMockServerBase();

	virtual bool PostCondition() const { return true; }

	//! Return URL server is listening at. E.g.: http://localhost:8080
	FString GetUrl() const;

	//! Return true if there is at least one running instance of mock server, false otherwise.
	static bool HasRunningInstance();

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


// Special test token used in both WebServices and MaterialPersistence tests
#define ITWINTEST_ACCESS_TOKEN "ThisIsATestITwinAccessToken"


#define CHECK_DECORATION_HEADERS()										\
{																		\
	const int HeaderStatus = CheckRequiredHeaders(headers,				\
		{{ "accept", "application/json" },								\
		 { "Content-Type", "application/json; charset=UTF-8" },			\
		 { "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN } });		\
	if (HeaderStatus != MHD_HTTP_OK)									\
	{																	\
		return Response(HeaderStatus, "Error in headers.");				\
	}																	\
}

#endif // WITH_TESTS
