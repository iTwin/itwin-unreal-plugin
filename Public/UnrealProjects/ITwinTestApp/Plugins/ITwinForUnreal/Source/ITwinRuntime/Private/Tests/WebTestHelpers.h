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
#include <Compil/AfterNonUnrealIncludes.h>

#include <atomic>
#include <memory>

namespace httpmock {
	class MockServer;
}

/// Helper class to track the number of requests started and completed in async tests.
class FITwinIOAsyncCallback
{
public:
	virtual ~FITwinIOAsyncCallback() = default;

	void OnRequestStarted()
	{
		NumRequestsStarted++;
	}
	void OnRequestDone()
	{
		NumRequestsDone++;
	}

	virtual bool IsDone() const { return NumRequestsDone == NumRequestsStarted; }

private:
	uint32 NumRequestsStarted = 0;
	uint32 NumRequestsDone = 0;
};
using FITwinIOAsyncCallbackPtr = std::shared_ptr<FITwinIOAsyncCallback>;


class FITwinAPITestHelperBase
{
public:
	using MockServerPtr = std::unique_ptr<httpmock::MockServer>;

	virtual ~FITwinAPITestHelperBase();
	bool Init(AdvViz::SDK::EITwinEnvironment Env = AdvViz::SDK::EITwinEnvironment::Prod);
	void Cleanup();

	/// Return URL server is listening at. E.g.: http://localhost:8080
	std::string GetServerUrl() const;
	int GetMockServerPort() const;
	bool HasMockServer() const { return !!MockServer; }

	FITwinIOAsyncCallbackPtr GetAsyncCallback() const { return AsyncCallback; }

	/// Check conditions that should be met once all the tests have been run.
	virtual bool PostCondition() const;

	/// Wait for a given task, for a maximum duration.
	static bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);

	/// Return the UWorld instance used for testing. It is expected to be valid when tests are run.
	static UWorld* GetTestWorld();

protected:
	FITwinAPITestHelperBase();

	bool InitServer(MockServerPtr Server);

	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment) { return true; }
	virtual void DoCleanup() {}

private:
	MockServerPtr MockServer;
	bool bInitDone = false;

	FITwinIOAsyncCallbackPtr AsyncCallback;
};

#endif // WITH_TESTS
