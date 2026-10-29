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

/// Process /arg_test request (basic test to check that the mock server is answering)
FITwinMockServerBase::Response FITwinMockServerBase::ProcessArgTest(const std::vector<UrlArg>& urlArguments) const
{
	static const StringMap expectedArgs = { { "b", "2" }, { "x", "0" } };
	check(ToArgMap(urlArguments) == expectedArgs);
	return Response();
}

#endif // WITH_TESTS
