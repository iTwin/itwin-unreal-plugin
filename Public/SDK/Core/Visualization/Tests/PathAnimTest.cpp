/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../PathAnimation.h"
#include "../SplinesManager.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>

using namespace AdvViz::SDK;

bool ComparePathAnimInfo(const IAnimationPathInfoPtr& p1Ptr, const IAnimationPathInfoPtr& p2Ptr)
{
	auto p1 = p1Ptr->GetRAutoLock();
	auto p2 = p2Ptr->GetRAutoLock();

	std::vector<std::string> p1Objects, p2Objects;
	p1->GetObjects(p1Objects);
	p2->GetObjects(p2Objects);

	return p1->GetSplineId() == p2->GetSplineId()
		&& p1->GetSpeed() == p2->GetSpeed()
		&& p1->GetStartTime() == p2->GetStartTime()
		&& p1->IsLooping() == p2->IsLooping()
		&& p1->IsEnabled() == p2->IsEnabled()
		&& p1->HasInvDir() == p2->HasInvDir()
		&& p1->GetRepeatMode() == p2->GetRepeatMode()
		&& p1->IsOneWay() == p2->IsOneWay()
		&& p1->GetLaneCount() == p2->GetLaneCount()
		&& p1->GetLaneWidth() == p2->GetLaneWidth()
		&& p1->GetDensity() == p2->GetDensity()
		&& p1->GetSepWidth() == p2->GetSepWidth()
		&& p1->GetMinSpeed() == p2->GetMinSpeed()
		&& p1->GetMaxSpeed() == p2->GetMaxSpeed()
		&& p1Objects == p2Objects
		&& p1->GetDBIdentifier() == p2->GetDBIdentifier();
}

bool ComparePathAnimInfos(const std::vector<IAnimationPathInfoPtr>& ll1, const std::vector<IAnimationPathInfoPtr>& ll2)
{
	if (ll1.size() != ll2.size())
	{
		return false;
	}
	for (auto p1 : ll1)
	{
		bool found = false;
		for (auto p2 : ll2)
		{
			found = ComparePathAnimInfo(p1, p2);
			if (found)
				break;
		}
		if (!found)
			return false;
	}
	return true;
}

void SetDefaultConfig();

TEST_CASE("PathAnimation") {

	SECTION("path animation properties") {
		try {
			SetDefaultConfig();

			// Test individual path animation object
			auto pathInfo = IAnimationPathInfo::New();
			REQUIRE(pathInfo != nullptr);

			{
				// Test all setters and getters
				RefID refId;
				pathInfo->SetSplineId(refId);
				CHECK(pathInfo->GetSplineId() == refId);

				pathInfo->SetSpeed(12.5);
				CHECK(pathInfo->GetSpeed() == 12.5);

				pathInfo->SetOffsetX(10.0);
				CHECK(pathInfo->GetOffsetX() == 10.0);

				pathInfo->SetOffsetY(20.0);
				CHECK(pathInfo->GetOffsetY() == 20.0);

				pathInfo->SetStartTime(5.0);
				CHECK(pathInfo->GetStartTime() == 5.0);

				pathInfo->SetIsLooping(true);
				CHECK(pathInfo->IsLooping() == true);

				pathInfo->SetIsEnabled(false);
				CHECK(pathInfo->IsEnabled() == false);

				pathInfo->SetInvDir(true);
				CHECK(pathInfo->HasInvDir() == true);

				pathInfo->SetRepeatMode(3);
				CHECK(pathInfo->GetRepeatMode() == 3);

				pathInfo->SetOneWay(true);
				CHECK(pathInfo->IsOneWay() == true);

				pathInfo->SetLaneCount(6);
				CHECK(pathInfo->GetLaneCount() == 6);

				pathInfo->SetLaneWidth(3.7);
				CHECK(pathInfo->GetLaneWidth() == 3.7);

				pathInfo->SetDensity(0.75);
				CHECK(pathInfo->GetDensity() == 0.75);

				pathInfo->SetSepWidth(1.5);
				CHECK(pathInfo->GetSepWidth() == 1.5);

				pathInfo->SetMinSpeed(3.0);
				CHECK(pathInfo->GetMinSpeed() == 3.0);

				pathInfo->SetMaxSpeed(20.0);
				CHECK(pathInfo->GetMaxSpeed() == 20.0);

				std::vector<std::string> testObjects = {"test1", "test2", "test3"};
				pathInfo->SetObjects(testObjects);
				std::vector<std::string> retrievedObjects;
				pathInfo->GetObjects(retrievedObjects);
				CHECK(retrievedObjects == testObjects);
			}
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("manager operations") {
		try {
			SetDefaultConfig();

			auto pathAnimManager = IPathAnimManager::New();
			REQUIRE(pathAnimManager != nullptr);

			// Test initial empty state
			CHECK(pathAnimManager->GetNumberOfPaths() == 0);

			// Add path animations
			auto path1 = pathAnimManager->AddAnimationPathInfo();
			CHECK(pathAnimManager->GetNumberOfPaths() == 1);

			RefID path1Id;
			RefID path1SplineId;
			{
				auto path1Locked = path1->GetAutoLock();
				path1Locked->SetSplineId(path1SplineId);
				path1Id = path1Locked->GetId();
			}

			auto path2 = pathAnimManager->AddAnimationPathInfo();
			CHECK(pathAnimManager->GetNumberOfPaths() == 2);

			RefID path2Id;
			RefID path2SplineId;
			{
				auto path2Locked = path2->GetAutoLock();
				path2Locked->SetSplineId(path2SplineId);
				path2Id = path2Locked->GetId();
			}

			// Test retrieval by ID
			auto retrievedPath1 = pathAnimManager->GetAnimationPathInfo(path1Id);
			REQUIRE(retrievedPath1 != nullptr);
			CHECK(retrievedPath1->GetRAutoLock()->GetSplineId() == path1SplineId);

			// Test retrieval by spline ID
			auto pathBySplineId = pathAnimManager->FindAnimationPathInfoBySplineRefId(path2SplineId);
			REQUIRE(pathBySplineId != nullptr);
			CHECK(pathBySplineId->GetRAutoLock()->GetId() == path2Id);

			// Test removal
			pathAnimManager->RemoveAnimationPathInfo(path1Id);
			CHECK(pathAnimManager->GetNumberOfPaths() == 1);

			auto removedPath = pathAnimManager->GetAnimationPathInfo(path1Id);
			CHECK(removedPath == nullptr);

			// Test GetAnimationPathIds
			std::set<RefID> ids;
			pathAnimManager->GetAnimationPathIds(ids);
			CHECK(ids.size() == 1);
			CHECK(ids.count(path2Id) == 1);
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}


namespace
{
	static const std::string kDecorationId = "67a0e122f9f9158c2e60d7ba";
	static const std::string kAnimPathsUrlPath = "/advviz/v1/decorations/" + kDecorationId + "/animationpaths";

	static const std::string kPathDBId = "67a0e124f9f9158c2e60d001";
	static const std::string kSplineDBId = "67a0e124f9f9158c2e60d002";

	// GET decorations/<id>/animationpaths (HttpGetWithLink envelope)
	static const std::string kAnimPathsListPayload =
		"{\"total_rows\":1,\"rows\":["
		"{\"id\":\"67a0e124f9f9158c2e60d001\",\"splineId\":\"67a0e124f9f9158c2e60d002\",\"speed\":42.5,"
		"\"startTime\":3.0,\"hasLoop\":true,\"isEnabled\":false,\"invDir\":true,\"repeatMode\":2,"
		"\"oneWay\":false,\"laneCount\":4,\"laneWidth\":350.0,\"density\":0.5,\"sepWidth\":150.0,"
		"\"minSpeed\":1000.0,\"maxSpeed\":2000.0,\"objects\":[\"obj1\",\"obj2\"]}"
		"],\"_links\":{\"self\":\"http:///advviz/v1/decorations/67a0e122f9f9158c2e60d7ba/animationpaths\"}}";

	// POST / PUT / DELETE answers
	static const std::string kPostIdsPayload = "{\"ids\":[\"67a0e124f9f9158c2e60d001\"]}";
	static const std::string kNumUpdated1Payload = "{\"numUpdated\":1}";

	static constexpr int kAsyncTimeoutSeconds = 10;

	//! Common setup: log channels, config, mock server and access token.
	//! Note: the Http instance is captured by PathAnimManager at construction time,
	//! so this must be called *before* creating the manager.
	HTTPMock& InitPathAnimTest()
	{
		Tools::CreateAdvVizLogChannels();
		SetDefaultConfig();
		HTTPMock* mock = GetHttpMock();
		REQUIRE(mock != nullptr);
		std::shared_ptr<ThreadSafeAccessToken> token = std::make_shared<ThreadSafeAccessToken>();
		token->Set("abcd");
		GetDefaultHttp()->SetAccessToken(token);
		return *mock;
	}

	//! Registers a one-shot response, and unregisters it at end of scope.
	class [[nodiscard]] ScopedPathResponse
	{
	public:
		ScopedPathResponse(HTTPMock& mock, const HTTPMock::RequestKey& key, long code, const std::string& payload)
			: mock_(mock), key_(key)
		{
			mock_.SetResponseFunction(key_, [code, payload] {
				return HTTPMock::Response2(code, std::string(payload));
			});
		}
		~ScopedPathResponse() { mock_.ResetResponseFunction(key_); }
	private:
		HTTPMock& mock_;
		HTTPMock::RequestKey key_;
	};

	//! Adds a spline holding the given server identifier, and returns its (unique) RefID.
	RefID AddSplineWithDBId(const std::shared_ptr<ISplinesManager>& splinesManager, const std::string& dbId)
	{
		ISplinePtr splinePtr = splinesManager->AddSpline();
		REQUIRE(splinePtr != nullptr);
		auto spline = splinePtr->GetAutoLock();
		spline->SetDBIdentifier(dbId);
		return spline->GetId();
	}
}

TEST_CASE("AnimationPathInfoSplineDBId") {

	SetDefaultConfig();

	auto pathInfo = IAnimationPathInfo::New();
	REQUIRE(pathInfo != nullptr);

	// a spline RefID carrying a database identifier is propagated to the server side data
	RefID splineRefId;
	splineRefId.SetDBIdentifier(kSplineDBId);
	pathInfo->SetSplineId(splineRefId);

	CHECK(pathInfo->GetSplineId() == splineRefId);
	REQUIRE(pathInfo->GetServerSideData().splineId.has_value());
	CHECK(pathInfo->GetServerSideData().splineId.value() == kSplineDBId);
	// setting a DB-backed spline id invalidates the item
	CHECK(pathInfo->ShouldSave() == true);

	// a spline RefID without database identifier leaves the server side data untouched
	pathInfo->SetShouldSave(false);
	RefID plainRefId;
	pathInfo->SetSplineId(plainRefId);
	CHECK(pathInfo->GetSplineId() == plainRefId);
	CHECK(pathInfo->GetServerSideData().splineId.value() == kSplineDBId);
	CHECK(pathInfo->ShouldSave() == false);
}

TEST_CASE("PathAnimManagerLookupAndDirtyState") {

	SetDefaultConfig();

	auto pathAnimManager = IPathAnimManager::New();
	REQUIRE(pathAnimManager != nullptr);

	SECTION("FindAnimationPathInfoByDBId") {
		auto path1 = pathAnimManager->AddAnimationPathInfo();
		auto path2 = pathAnimManager->AddAnimationPathInfo();
		REQUIRE(pathAnimManager->GetNumberOfPaths() == 2);

		RefID path2Id;
		{
			auto path2Locked = path2->GetAutoLock();
			path2Locked->SetDBIdentifier(kPathDBId);
			path2Id = path2Locked->GetId();
		}

		auto found = pathAnimManager->FindAnimationPathInfoByDBId(kPathDBId);
		REQUIRE(found != nullptr);
		CHECK(found->GetRAutoLock()->GetId() == path2Id);

		// unknown identifier -> null pointer
		CHECK(pathAnimManager->FindAnimationPathInfoByDBId("000000000000000000000000") == nullptr);
		// path1 has no DB identifier at all, it must never match
		CHECK(path1->GetRAutoLock()->HasDBIdentifier() == false);
	}

	SECTION("HasAnimPathsToSave") {
		// empty manager
		CHECK(pathAnimManager->HasAnimPathsToSave() == false);

		auto path = pathAnimManager->AddAnimationPathInfo();
		RefID pathId;
		{
			auto pathLocked = path->GetAutoLock();
			pathId = pathLocked->GetId();
			// a brand new path is 'NeverSaved', not 'ShouldSave'
			CHECK(pathLocked->ShouldSave() == false);
		}
		CHECK(pathAnimManager->HasAnimPathsToSave() == false);

		SECTION("a modified path must be saved") {
			{
				auto pathLocked = path->GetAutoLock();
				pathLocked->SetSpeed(50.0); // invalidates the item
			}
			CHECK(pathAnimManager->HasAnimPathsToSave() == true);

			{
				auto pathLocked = path->GetAutoLock();
				pathLocked->SetShouldSave(false);
			}
			CHECK(pathAnimManager->HasAnimPathsToSave() == false);
		}

		SECTION("a removed path known by the server must be saved") {
			{
				auto pathLocked = path->GetAutoLock();
				pathLocked->SetDBIdentifier(kPathDBId);
				pathLocked->SetShouldSave(false);
			}
			pathAnimManager->RemoveAnimationPathInfo(pathId);
			CHECK(pathAnimManager->GetNumberOfPaths() == 0);
			// the deletion still has to be sent to the server
			CHECK(pathAnimManager->HasAnimPathsToSave() == true);
		}

		SECTION("a removed path unknown by the server is just dropped") {
			pathAnimManager->RemoveAnimationPathInfo(pathId);
			CHECK(pathAnimManager->GetNumberOfPaths() == 0);
			CHECK(pathAnimManager->HasAnimPathsToSave() == false);
		}
	}
}

TEST_CASE("PathAnimManagerLoadDataFromServer") {

	HTTPMock& mock = InitPathAnimTest();

	std::shared_ptr<ISplinesManager> splinesManager(ISplinesManager::New());
	REQUIRE(splinesManager != nullptr);
	const RefID splineRefId = AddSplineWithDBId(splinesManager, kSplineDBId);

	auto pathAnimManager = IPathAnimManager::New();
	REQUIRE(pathAnimManager != nullptr);
	pathAnimManager->SetSplinesManager(splinesManager);

	const HTTPMock::RequestKey getKey = std::pair("GET", kAnimPathsUrlPath);

	SECTION("synchronous") {
		ScopedPathResponse resp(mock, getKey, 200, kAnimPathsListPayload);

		pathAnimManager->LoadDataFromServer(kDecorationId);

		REQUIRE(pathAnimManager->GetNumberOfPaths() == 1);
		auto pathPtr = pathAnimManager->FindAnimationPathInfoByDBId(kPathDBId);
		REQUIRE(pathPtr != nullptr);

		auto path = pathPtr->GetRAutoLock();
		CHECK(path->GetSpeed() == 42.5);
		CHECK(path->GetStartTime() == 3.0);
		CHECK(path->IsLooping() == true);
		CHECK(path->IsEnabled() == false);
		CHECK(path->HasInvDir() == true);
		CHECK(path->GetRepeatMode() == 2);
		CHECK(path->IsOneWay() == false);
		CHECK(path->GetLaneCount() == 4);
		// the spline RefID is resolved through the splines manager
		CHECK(path->GetSplineId() == splineRefId);
		// freshly loaded data is not dirty
		CHECK(path->ShouldSave() == false);

		// the path can also be retrieved by its spline RefID
		CHECK(pathAnimManager->FindAnimationPathInfoBySplineRefId(splineRefId) != nullptr);
	}

	SECTION("synchronous - server error leaves the manager empty") {
		ScopedPathResponse resp(mock, getKey, 404, std::string("{}"));
		pathAnimManager->LoadDataFromServer(kDecorationId);
		CHECK(pathAnimManager->GetNumberOfPaths() == 0);
	}

	SECTION("asynchronous") {
		ScopedPathResponse resp(mock, getKey, 200, kAnimPathsListPayload);

		std::mutex resultMutex;
		std::vector<IAnimationPathInfoPtr> loadedPaths;
		std::atomic_bool bFinished = false;
		bool bFinishedOk = false;

		std::function<void(IAnimationPathInfoPtr&)> onPathLoaded =
			[&resultMutex, &loadedPaths](IAnimationPathInfoPtr& pathPtr) {
				std::lock_guard<std::mutex> lock(resultMutex);
				loadedPaths.push_back(pathPtr);
			};
		std::function<void(AdvViz::expected<void, std::string> const&)> onComplete =
			[&bFinished, &bFinishedOk](AdvViz::expected<void, std::string> const& ret) {
				bFinishedOk = ret.has_value();
				bFinished = true;
			};

		pathAnimManager->AsyncLoadDataFromServer(kDecorationId, onPathLoaded, onComplete);

		REQUIRE(WaitForAsyncTask(bFinished, kAsyncTimeoutSeconds));
		CHECK(bFinishedOk);

		std::lock_guard<std::mutex> lock(resultMutex);
		REQUIRE(loadedPaths.size() == 1);
		REQUIRE(pathAnimManager->GetNumberOfPaths() == 1);

		auto pathPtr = pathAnimManager->FindAnimationPathInfoByDBId(kPathDBId);
		REQUIRE(pathPtr != nullptr);
		auto path = pathPtr->GetRAutoLock();
		CHECK(path->GetSplineId() == splineRefId);
		CHECK(path->GetSpeed() == 42.5);
		CHECK(path->ShouldSave() == false);
	}

	SECTION("asynchronous - missing splines manager is reported") {
		ScopedPathResponse resp(mock, getKey, 200, kAnimPathsListPayload);

		auto managerWithoutSplines = IPathAnimManager::New();
		REQUIRE(managerWithoutSplines != nullptr);

		std::atomic_bool bFinished = false;
		std::atomic_bool bErrorReported = false; // onFinish may be called twice
		std::function<void(IAnimationPathInfoPtr&)> onPathLoaded;
		std::function<void(AdvViz::expected<void, std::string> const&)> onComplete =
			[&bFinished, &bErrorReported](AdvViz::expected<void, std::string> const& ret) {
				if (!ret.has_value())
					bErrorReported = true;
				bFinished = true;
			};

		managerWithoutSplines->AsyncLoadDataFromServer(kDecorationId, onPathLoaded, onComplete);

		REQUIRE(WaitForAsyncTask(bFinished, kAsyncTimeoutSeconds));
		CHECK(bErrorReported == true);
	}
}

TEST_CASE("PathAnimManagerAsyncSaveDataOnServer") {

	HTTPMock& mock = InitPathAnimTest();

	std::shared_ptr<ISplinesManager> splinesManager(ISplinesManager::New());
	REQUIRE(splinesManager != nullptr);
	const RefID splineRefId = AddSplineWithDBId(splinesManager, kSplineDBId);

	auto pathAnimManager = IPathAnimManager::New();
	REQUIRE(pathAnimManager != nullptr);
	pathAnimManager->SetSplinesManager(splinesManager);

	SECTION("new path is created (POST)") {
		ScopedPathResponse postResp(mock, std::pair("POST", kAnimPathsUrlPath), 201, kPostIdsPayload);

		auto pathPtr = pathAnimManager->AddAnimationPathInfo();
		{
			auto path = pathPtr->GetAutoLock();
			path->SetSplineId(splineRefId);
			path->SetSpeed(42.5);
			REQUIRE(path->HasDBIdentifier() == false);
		}

		std::atomic_bool bDone = false;
		bool bSaveOk = false;
		pathAnimManager->AsyncSaveDataOnServer(kDecorationId, [&](bool success) {
			bSaveOk = success;
			bDone = true;
		});

		REQUIRE(WaitForAsyncTask(bDone, kAsyncTimeoutSeconds));
		CHECK(bSaveOk);

		// the server identifier returned by the POST is assigned to the path
		auto path = pathPtr->GetRAutoLock();
		REQUIRE(path->HasDBIdentifier() == true);
		CHECK(path->GetDBIdentifier() == kPathDBId);
		CHECK(path->ShouldSave() == false);
		CHECK(pathAnimManager->FindAnimationPathInfoByDBId(kPathDBId) != nullptr);
	}

	SECTION("existing path is updated (PUT)") {
		ScopedPathResponse putResp(mock, std::pair("PUT", kAnimPathsUrlPath), 200, kNumUpdated1Payload);

		auto pathPtr = pathAnimManager->AddAnimationPathInfo();
		{
			auto path = pathPtr->GetAutoLock();
			path->SetSplineId(splineRefId);
			path->SetDBIdentifier(kPathDBId); // already known by the server
			path->SetSpeed(60.0);
			REQUIRE(path->ShouldSave() == true);
		}
		CHECK(pathAnimManager->HasAnimPathsToSave() == true);

		std::atomic_bool bDone = false;
		bool bSaveOk = false;
		pathAnimManager->AsyncSaveDataOnServer(kDecorationId, [&](bool success) {
			bSaveOk = success;
			bDone = true;
		});

		REQUIRE(WaitForAsyncTask(bDone, kAsyncTimeoutSeconds));
		CHECK(bSaveOk);
		CHECK(pathPtr->GetRAutoLock()->ShouldSave() == false);
		CHECK(pathAnimManager->HasAnimPathsToSave() == false);
	}

	SECTION("removed path is deleted (DELETE)") {
		ScopedPathResponse delResp(mock, std::pair("DELETE", kAnimPathsUrlPath), 204, std::string("{}"));

		auto pathPtr = pathAnimManager->AddAnimationPathInfo();
		RefID pathId;
		{
			auto path = pathPtr->GetAutoLock();
			path->SetSplineId(splineRefId);
			path->SetDBIdentifier(kPathDBId);
			path->SetShouldSave(false);
			pathId = path->GetId();
		}
		pathAnimManager->RemoveAnimationPathInfo(pathId);
		REQUIRE(pathAnimManager->HasAnimPathsToSave() == true);

		std::atomic_bool bDone = false;
		bool bSaveOk = false;
		pathAnimManager->AsyncSaveDataOnServer(kDecorationId, [&](bool success) {
			bSaveOk = success;
			bDone = true;
		});

		REQUIRE(WaitForAsyncTask(bDone, kAsyncTimeoutSeconds));
		CHECK(bSaveOk);
	}

	SECTION("server error is reported") {
		ScopedPathResponse postResp(mock, std::pair("POST", kAnimPathsUrlPath), 500, std::string("{}"));

		auto pathPtr = pathAnimManager->AddAnimationPathInfo();
		{
			auto path = pathPtr->GetAutoLock();
			path->SetSplineId(splineRefId);
			path->SetSpeed(42.5);
		}

		std::atomic_bool bDone = false;
		bool bSaveOk = true;
		pathAnimManager->AsyncSaveDataOnServer(kDecorationId, [&](bool success) {
			bSaveOk = success;
			bDone = true;
		});

		REQUIRE(WaitForAsyncTask(bDone, kAsyncTimeoutSeconds));
		CHECK(bSaveOk == false);
		// no DB identifier was assigned
		CHECK(pathPtr->GetRAutoLock()->HasDBIdentifier() == false);
	}

	SECTION("nothing to save still invokes the callback") {
		// no path at all: no request is registered
		std::atomic_bool bDone = false;
		bool bSaveOk = false;
		pathAnimManager->AsyncSaveDataOnServer(kDecorationId, [&](bool success) {
			bSaveOk = success;
			bDone = true;
		});
		REQUIRE(WaitForAsyncTask(bDone, kAsyncTimeoutSeconds));
		CHECK(bSaveOk);
	}
}
