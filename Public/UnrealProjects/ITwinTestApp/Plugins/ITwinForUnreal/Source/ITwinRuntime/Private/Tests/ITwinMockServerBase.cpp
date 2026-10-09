/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMockServerBase.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS 

#include <Tests/ITwinMockServerBase.h>

#include <mutex>
#include <optional>
#include <set>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace
{
	static std::vector<FITwinMockServerBase*> Instances;
	static std::mutex InstancesMutex;
}

/*static*/ bool FITwinMockServerBase::HasRunningInstance()
{
	std::lock_guard<std::mutex> lock(InstancesMutex);
	for (auto const* instance : Instances)
	{
		if (instance && instance->isRunning())
			return true;
	}
	return false;
}

/// Mock server implementation for request-based tests (iTwin services, iModel, Decoration service...)

FITwinMockServerBase::FITwinMockServerBase(int port)
	: httpmock::MockServer(port)
{
	std::lock_guard<std::mutex> lock(InstancesMutex);
	Instances.push_back(this);
}

FITwinMockServerBase::~FITwinMockServerBase()
{
	std::lock_guard<std::mutex> lock(InstancesMutex);
	auto it = std::find(Instances.begin(), Instances.end(), this);
	if (it != Instances.end())
	{
		Instances.erase(it);
	}
}

FString FITwinMockServerBase::GetUrl() const
{
	return FString::Format(TEXT("http://localhost:{0}"), { getPort() });
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
					headerError = MHD_HTTP_UNAUTHORIZED;
				else
					headerError = MHD_HTTP_BAD_REQUEST;
				break;
			}
		}
	}

	if (!headerError
		&& matchedHeaders.size() != requiredHeaders.size())
	{
		errorInfo = " - missing header(s): [";
		int missingKeyIndex(0);
		for (auto const& [key, _] : requiredHeaders)
		{
			if (!matchedHeaders.contains(key))
			{
				if (missingKeyIndex > 0)
					errorInfo += ", ";
				errorInfo += key;
				missingKeyIndex++;
			}
		}
		errorInfo += "]";
		headerError = MHD_HTTP_BAD_REQUEST;
	}
	if (headerError)
	{
		BE_LOGE("ITwinAPI", "Not the expected headers (" << *headerError << errorInfo << ") -> " << ToString(headers));
		return *headerError;
	}

	return MHD_HTTP_OK;
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

#endif // WITH_TESTS
