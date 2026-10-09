/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../PathAnimation.h"
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