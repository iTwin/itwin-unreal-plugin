/*--------------------------------------------------------------------------------------+
|
|     $Source: SplinesTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../SplinesManager.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;

bool CompareSpline(const ISpline& s1, const ISpline& s2)
{
	auto const SamePoints = [](const ISpline& s1, const ISpline& s2) -> bool
	{
		auto const numPts = s1.GetNumberOfPoints();
		if (s2.GetNumberOfPoints() != numPts)
			return false;
		for (size_t i(0); i < numPts; ++i)
		{
			auto pt1 = s1.GetPoint(i)->GetRAutoLock();
			auto pt2 = s2.GetPoint(i)->GetRAutoLock();
			if (pt1->GetPosition() != pt2->GetPosition())
				return false;
		}
		return true;
	};
	return s1.GetDBIdentifier() == s2.GetDBIdentifier() /* only because the splines have been saved! */
		&& s1.GetName() == s2.GetName()
		&& s1.GetUsage() == s2.GetUsage()
		&& SamePoints(s1, s2);
}

bool CompareSplines(ISplinesManager const& mngr1, ISplinesManager const& mngr2)
{
	if (mngr1.GetNumberOfSplines() != mngr2.GetNumberOfSplines())
	{
		return false;
	}
	size_t const numSplines = mngr1.GetNumberOfSplines();
	for (size_t i(0); i < numSplines; ++i)
	{
		auto const spline1_ptr = mngr1.GetSpline(i);
		auto spline1 = spline1_ptr->GetRAutoLock();
		bool found = false;
		for (size_t j(0); j < numSplines; ++j)
		{
			auto const spline2_ptr = mngr2.GetSpline(j);
			auto spline2 = spline2_ptr->GetRAutoLock();
			found = CompareSpline(*spline1, *spline2);
			if (found)
				break;
		}
		if (!found)
			return false;
	}
	return true;
}
bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);
void SetDefaultConfig();

TEST_CASE("Splines Saving")
{
	SECTION("manager")
	{
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);

			std::vector<std::string> splinepoints_server;
			std::vector<std::string> splins_server;
			size_t receivedRequests = 0;
			std::mutex serverMutex;

			auto respKeyPostPoints = std::pair("POST", "/advviz/v1/decorations/TEST_SPLINES_ID/splinepoints");
			mock->responseFctWithData_[respKeyPostPoints] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				// points should be posted before splines
				++receivedRequests;
				if (receivedRequests != 1)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data ==	"{\"splinePoints\":[" \
					"{\"position\":[0.0,0.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[0.0,1.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[1.0,1.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[1.0,0.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[0.0,0.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[0.0,2.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[2.0,2.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"position\":[2.0,0.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}]}")
				{
					return HTTPMock::Response2(201,
						"{\"ids\" : [\"pt1\",\"pt2\",\"pt3\",\"pt4\",\"pt5\",\"pt6\",\"pt7\",\"pt8\"]}");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline points data");
				}
			};

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations/TEST_SPLINES_ID/splines");
			mock->responseFctWithData_[respKeyPost] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				// points should be posted before splines
				++receivedRequests;
				if (receivedRequests != 2)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"splines\":[" \
					"{\"name\":\"spline_1\",\"usage\":\"AnimPath\",\"pointIDs\":[\"pt1\",\"pt2\",\"pt3\",\"pt4\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":false}," \
					"{\"name\":\"spline_2\",\"usage\":\"MapCutout\",\"pointIDs\":[\"pt5\",\"pt6\",\"pt7\",\"pt8\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":true}" \
					"]}")
				{
					return HTTPMock::Response2(201,
						"{\"ids\" : [\"spl1\",\"spl2\"]}");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline data");
				}
			};

			std::shared_ptr<ISplinesManager> splinesManager(ISplinesManager::New());
			std::atomic_bool saveFinished = false;

			auto SaveSplinesAndWait = [&]()
			{
				saveFinished = false;
				REQUIRE(splinesManager->HasSplinesToSave());
				splinesManager->AsyncSaveDataOnServer("TEST_SPLINES_ID",
					[&saveFinished](bool bSuccess)
				{
					REQUIRE(bSuccess);
					saveFinished = true;
				});
				REQUIRE(WaitForAsyncTask(saveFinished, 10));
				REQUIRE(!splinesManager->HasSplinesToSave());
			};

			// Add 2 splines and save
			auto spline1_ptr = splinesManager->AddSpline();
			REQUIRE(spline1_ptr.get() != nullptr);
			{
				auto spline1 = spline1_ptr->GetAutoLock();
				spline1->SetName("spline_1");
				spline1->SetUsage(ESplineUsage::AnimPath);
				auto pt1_spl1 = spline1->AddPoint()->GetAutoLock();
				auto pt2_spl1 = spline1->AddPoint()->GetAutoLock();
				auto pt3_spl1 = spline1->AddPoint()->GetAutoLock();
				auto pt4_spl1 = spline1->AddPoint()->GetAutoLock();
				pt1_spl1->SetPosition({ 0., 0., 0. });
				pt2_spl1->SetPosition({ 0., 1., 0. });
				pt3_spl1->SetPosition({ 1., 1., 0. });
				pt4_spl1->SetPosition({ 1., 0., 0. });
			}

			auto spline2_ptr = splinesManager->AddSpline();
			REQUIRE(spline2_ptr.get() != nullptr);
			{
				auto spline2 = spline2_ptr->GetAutoLock();
				spline2->SetName("spline_2");
				spline2->SetUsage(ESplineUsage::MapCutout);
				auto pt1_spl2 = spline2->AddPoint()->GetAutoLock();
				auto pt2_spl2 = spline2->AddPoint()->GetAutoLock();
				auto pt3_spl2 = spline2->AddPoint()->GetAutoLock();
				auto pt4_spl2 = spline2->AddPoint()->GetAutoLock();
				pt1_spl2->SetPosition({ 0., 0., 2. });
				pt2_spl2->SetPosition({ 0., 2., 2. });
				pt3_spl2->SetPosition({ 2., 2., 2. });
				pt4_spl2->SetPosition({ 2., 0., 2. });
			}

			SaveSplinesAndWait();

			// Modify spline and save
			{
				auto spline1 = spline1_ptr->GetAutoLock();
				auto pt3_spl1 = spline1->GetPoint(2)->GetAutoLock();
				pt3_spl1->SetPosition({ 2., 2., 0. });
				pt3_spl1->InvalidateDB();
			}

			auto respKeyPutPoints = std::pair("PUT", "/advviz/v1/decorations/TEST_SPLINES_ID/splinepoints");
			mock->responseFctWithData_[respKeyPutPoints] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 3)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"splinePoints\":[" \
					"{\"position\":[2.0,2.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0],\"id\":\"pt3\"}]}")
				{
					return HTTPMock::Response2(200,
						"{\"numUpdated\":1}");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline points update data");
				}
			};
			SaveSplinesAndWait();


			// Remove spline point and save
			{
				auto spline1 = spline1_ptr->GetAutoLock();
				spline1->RemovePoint(2);
			}

			// The removal triggers two requests: one to remove the point from the spline (PUT) and one to
			// delete the point.
			auto respKeyPutSpline = std::pair("PUT", "/advviz/v1/decorations/TEST_SPLINES_ID/splines");
			mock->responseFctWithData_[respKeyPutSpline] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 4 && receivedRequests != 5)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"splines\":[" \
						"{\"id\":\"spl1\",\"name\":\"spline_1\",\"usage\":\"AnimPath\",\"pointIDs\":[\"pt1\",\"pt2\",\"pt4\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":false}" \
						"]}")
				{
					return HTTPMock::Response2(200,
						"{\"numUpdated\":1}");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline update data");
				}
			};
			auto respKeyDeletePoints = std::pair("DELETE", "/advviz/v1/decorations/TEST_SPLINES_ID/splinepoints");
			mock->responseFctWithData_[respKeyDeletePoints] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 4 && receivedRequests != 5)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"ids\":[\"pt3\"]}")
				{
					return HTTPMock::Response2(200, "");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline points delete data");
				}
			};
			SaveSplinesAndWait();

			// Load splines.

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/TEST_SPLINES_ID/splines");
			mock->responseFct_[respKeyGet] = [&]
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 6 && receivedRequests != 9 && receivedRequests != 11)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (receivedRequests == 6)
				{
					return HTTPMock::Response2(200,
						"{\"total_rows\":2,\"rows\":[" \
						"{\"id\":\"spl1\",\"name\":\"spline_1\",\"usage\":\"AnimPath\",\"pointIDs\":[\"pt1\",\"pt2\",\"pt4\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":false}," \
						"{\"id\":\"spl2\",\"name\":\"spline_2\",\"usage\":\"MapCutout\",\"pointIDs\":[\"pt5\",\"pt6\",\"pt7\",\"pt8\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":true}" \
						"],\"_links\":{}}");
				}
				else
				{
					return HTTPMock::Response2(200,
						"{\"total_rows\":1,\"rows\":[" \
						"{\"id\":\"spl2\",\"name\":\"spline_2\",\"usage\":\"MapCutout\",\"pointIDs\":[\"pt5\",\"pt6\",\"pt7\",\"pt8\"],\"transform\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"closedLoop\":true}" \
						"],\"_links\":{}}");
				}
			};

			auto respKeyGetPoints = std::pair("GET", "/advviz/v1/decorations/TEST_SPLINES_ID/splinepoints");
			mock->responseFct_[respKeyGetPoints] = [&]
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 7 && receivedRequests != 10 && receivedRequests != 12)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				// Note that when we remove a spline, its points are not deleted on the server.
				// This could be something to do in the future...
				return HTTPMock::Response2(200,
					"{\"total_rows\":7,\"rows\":[" \
					"{\"id\":\"pt1\",\"position\":[0.0,0.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt2\",\"position\":[0.0,1.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt4\",\"position\":[1.0,0.0,0.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt5\",\"position\":[0.0,0.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt6\",\"position\":[0.0,2.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt7\",\"position\":[2.0,2.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}," \
					"{\"id\":\"pt8\",\"position\":[2.0,0.0,2.0],\"upVector\":[0.0,0.0,0.0],\"inTangentMode\":\"Linear\",\"inTangent\":[0.0,0.0,0.0],\"outTangentMode\":\"Linear\",\"outTangent\":[0.0,0.0,0.0]}" \
					"],\"_links\":{}}");
			};

			std::shared_ptr<ISplinesManager> splinesManager2(ISplinesManager::New());
			splinesManager2->LoadDataFromServer("TEST_SPLINES_ID");
			REQUIRE(CompareSplines(*splinesManager, *splinesManager2));


			// Remove spline and save

			auto respKeyDelete = std::pair("DELETE", "/advviz/v1/decorations/TEST_SPLINES_ID/splines");
			mock->responseFctWithData_[respKeyDelete] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 8)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"ids\":[\"spl1\"]}")
				{
					return HTTPMock::Response2(200, "");
				}
				else
				{
					return HTTPMock::Response2(504, "unexpected spline delete data");
				}
			};

			splinesManager->RemoveSpline(spline1_ptr);
			SaveSplinesAndWait();

			// Load splines and compare again after removal.
			std::shared_ptr<ISplinesManager> splinesManager3(ISplinesManager::New());
			splinesManager3->LoadDataFromServer("TEST_SPLINES_ID");
			CHECK(CompareSplines(*splinesManager, *splinesManager3));

			// Test asynchronous Load as well.
			std::shared_ptr<ISplinesManager> splinesManager4(ISplinesManager::New());
			std::atomic_bool asyncLoadFinished = false;
			splinesManager4->AsyncLoadDataFromServer("TEST_SPLINES_ID",
				[](ISplinePtr&) {},
				[](ISplinePointPtr&) {},
				[&asyncLoadFinished, splinesManager, splinesManager4](AdvViz::expected<void, std::string> const& exp)
			{
				REQUIRE(exp);
				CHECK(CompareSplines(*splinesManager, *splinesManager4));
				asyncLoadFinished = true;
			});
			REQUIRE(WaitForAsyncTask(asyncLoadFinished, 10));
		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}

