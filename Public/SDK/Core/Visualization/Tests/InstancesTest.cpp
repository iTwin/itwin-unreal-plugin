/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../InstancesManager.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>
#include "Mock.h"

using namespace AdvViz::SDK;

bool IdenticalInstance(const IInstance& inst1, const IInstance& inst2)
{
	// only test a few relevant settings (those modified in those tests)
	if (inst1.GetDBIdentifier() == inst2.GetDBIdentifier() /* only because the instances have been saved! */
		&& inst1.GetName() == inst2.GetName()
		&& inst1.GetColorShift().has_value() == inst2.GetColorShift().has_value())
	{
		if (inst1.GetColorShift().has_value())
		{
			auto const color1 = *inst1.GetColorShift();
			auto const color2 = *inst2.GetColorShift();
			return (fabs(color1[0] - color2[0]) < 1.f / 255.f)
				&& (fabs(color1[1] - color2[1]) < 1.f / 255.f)
				&& (fabs(color1[2] - color2[2]) < 1.f / 255.f);
		}
		else
		{
			return true;
		}
	}
	return false;
}

bool IdenticalInstanceVect(SharedInstVect const& vect1, SharedInstVect const& vect2)
{
	if (vect1.size() != vect2.size())
		return false;
	for (auto const& inst1Ptr : vect1)
	{
		auto inst1 = inst1Ptr->GetRAutoLock();
		auto it = std::find_if(vect2.begin(), vect2.end(),
			[&inst1](auto const& instPtr) {
			auto inst = instPtr->GetRAutoLock();
			return IdenticalInstance(*inst1, *inst);
		});
		if (it == vect2.end())
			return false;
	}
	return true;
}

bool IdenticalInstanceManagers(IInstancesManager const& mngr1, IInstancesManager const& mngr2)
{
	std::map<RefID, RefID> grouupId1To2;
	bool sameGroups = true;
	mngr1.IterateInstancesGroups([&](IInstancesGroup const& group1)
	{
		auto group2Ptr = mngr2.GetInstancesGroupByName(group1.GetName());
		if (!group2Ptr)
		{
			sameGroups = false;
			return;
		}
		auto group2 = group2Ptr->GetRAutoLock();
		grouupId1To2[group1.GetId()] = group2->GetId();
	});

	if (!sameGroups)
		return false;

	// Recover the RefID of the corresponding group in the initial manager
	auto const refs1 = mngr1.GetObjectReferences();
	for (auto const& refinfo : refs1)
	{
		auto const instances1 = mngr1.GetInstancesByObjectRef(refinfo.first, refinfo.second);
		auto const instances2 = mngr2.GetInstancesByObjectRef(refinfo.first, grouupId1To2[refinfo.second]);
		if (!IdenticalInstanceVect(instances1, instances2))
			return false;
	}
	return true;
}

bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);
void SetDefaultConfig();

namespace
{
	static const std::string TEST_GROUP_NAME = "test_group";

	std::shared_ptr<IInstancesManager> GetTestInstanceManager()
	{
		SetDefaultConfig();
		std::shared_ptr<IInstancesManager> instanceManager(IInstancesManager::New());
		CHECK(!instanceManager->HasInstances());

		// Add 4 instances
		IInstancesGroup* group = IInstancesGroup::New();
		group->SetName(TEST_GROUP_NAME);
		RefID const groupID = group->GetId();
		instanceManager->AddInstancesGroup(Tools::MakeSharedLockableDataPtr<IInstancesGroup>(group));

		auto inst0_ptr = instanceManager->AddInstance("Animals/Bird.uasset", groupID);
		CHECK(instanceManager->HasInstances());
		REQUIRE(inst0_ptr.get() != nullptr);
		auto inst0 = inst0_ptr->GetAutoLock();
		inst0->SetName("inst");
		inst0->SetColorShift({ 0.5f, 0.f, 0.5f });

		auto inst1_ptr = instanceManager->AddInstance("Animals/Bird.uasset", groupID);
		REQUIRE(inst1_ptr.get() != nullptr);
		auto inst1 = inst1_ptr->GetAutoLock();
		inst1->SetName("inst");
		inst1->SetColorShift({ 0.f, 1.f, 0.5f });

		auto inst2_ptr = instanceManager->AddInstance("Animals/Bird.uasset", groupID);
		REQUIRE(inst2_ptr.get() != nullptr);
		auto inst2 = inst2_ptr->GetAutoLock();
		inst2->SetName("inst");

		auto inst3_ptr = instanceManager->AddInstance("Animals/Bird.uasset", groupID);
		REQUIRE(inst3_ptr.get() != nullptr);
		auto inst3 = inst3_ptr->GetAutoLock();
		inst3->SetName("inst");

		auto inst4_ptr = instanceManager->AddInstance("Animals/Tiger.uasset", groupID);
		REQUIRE(inst4_ptr.get() != nullptr);
		auto inst4 = inst4_ptr->GetAutoLock();
		inst4->SetName("inst");

		return instanceManager;
	}
}

TEST_CASE("Instances Manager")
{
	SECTION("GetInstanceCountByObjectRef")
	{
		auto instanceManager = GetTestInstanceManager();
		REQUIRE(instanceManager.get() != nullptr);

		auto groupPtr = instanceManager->GetInstancesGroupByName(TEST_GROUP_NAME);
		REQUIRE(groupPtr.get() != nullptr);
		auto group = groupPtr->GetRAutoLock();

		// Test GetInstanceCountByObjectRef
		CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Bird.uasset", group->GetId()) == 4);
		CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Tiger.uasset", group->GetId()) == 1);
		CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Tiger.uasset", RefID()) == 0);
		CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Cat.uasset", group->GetId()) == 0);
	}

	SECTION("SetInstanceCountByObjectRef")
	{
		auto instanceManager = GetTestInstanceManager();
		REQUIRE(instanceManager.get() != nullptr);

		auto groupPtr = instanceManager->GetInstancesGroupByName(TEST_GROUP_NAME);
		REQUIRE(groupPtr.get() != nullptr);
		RefID groupId = RefID::Invalid();
		{
			auto group = groupPtr->GetRAutoLock();
			groupId = group->GetId();
		}

		// Shrink birds
		instanceManager->SetInstanceCountByObjectRef("Animals/Bird.uasset", groupId, 2);
		CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Bird.uasset", groupId) == 2);

		// Add a new population
		std::string const newObjectRef = "Animals/Gypaete_Barbu.uasset";
		instanceManager->SetInstanceCountByObjectRef(newObjectRef, groupId, 7);
		CHECK(instanceManager->GetInstanceCountByObjectRef(newObjectRef, groupId) == 7);

		auto const& population = instanceManager->GetInstancesByObjectRef(newObjectRef, groupId);
		CHECK(population.size() == 7);
		for (auto const& instPtr : population)
		{
			REQUIRE(instPtr.get() != nullptr);
			auto inst = instPtr->GetRAutoLock();
			CHECK(inst->GetObjectRef() == newObjectRef);
			CHECK(inst->GetGroup() == groupPtr);
		}
	}

	SECTION("save and load")
	{
		try {
			SetDefaultConfig();
			HTTPMock* mock = GetHttpMock();
			REQUIRE(mock != nullptr);

			REQUIRE(GetDefaultHttp().get() != nullptr);

			size_t receivedRequests = 0;
			std::mutex serverMutex;

			auto respKeyPostGroups = std::pair("POST", "/advviz/v1/decorations/TEST_INSTANCES_ID/instancesgroups");
			mock->responseFctWithData_[respKeyPostGroups] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				// group should be posted before instances
				++receivedRequests;
				if (receivedRequests != 1)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data ==	"{\"name\":\"test_group\"}")
				{
					return HTTPMock::Response2(201,
						"{\"id\":\"gp1\"}");
				}
				else
				{
					return HTTPMock::Response2(505, "unexpected instance group data");
				}
			};

			auto respKeyPost = std::pair("POST", "/advviz/v1/decorations/TEST_INSTANCES_ID/instances");
			mock->responseFctWithData_[respKeyPost] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				// groups should be posted before instances
				++receivedRequests;
				if (receivedRequests != 2 && receivedRequests != 3)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}

				static auto const buildBatchOfInstanceIds = [](size_t firstIndex, size_t numInstances)
				{
					std::string str;
					for (size_t i(0); i < numInstances; ++i)
					{
						if (i > 0)
							str += ",";
						str += std::string("\"instId_") + std::to_string(firstIndex + i) + "\"";
					}
					return str;
				};

				if (data == "{\"instances\":[" \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#7f007f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#00ff7f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\"}" \
					"]}")
				{
					// To have a deterministic result, make sure the birds are assigned the initial indices
					return HTTPMock::Response2(201,
						std::string("{\"ids\" : [") + buildBatchOfInstanceIds(0, 4) + "]}");
				}
				else if (data == "{\"instances\":[" \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"groupid\":\"gp1\",\"objref\":\"Animals/Tiger.uasset\"}" \
					"]}")
				{
					return HTTPMock::Response2(201,
						std::string("{\"ids\" : [") + buildBatchOfInstanceIds(4, 1) + "]}");
				}
				else
				{
					return HTTPMock::Response2(505, "unexpected instance POST data");
				}
			};

			auto instanceManager = GetTestInstanceManager();
			auto groupPtr = instanceManager->GetInstancesGroupByName(TEST_GROUP_NAME);
			REQUIRE(groupPtr.get() != nullptr);
			RefID groupId = RefID::Invalid();
			{
				auto group = groupPtr->GetRAutoLock();
				groupId = group->GetId();
			}

			std::atomic_bool saveFinished = false;

			auto SaveInstancesAndWait = [&]()
			{
				saveFinished = false;
				CHECK(instanceManager->HasInstancesToSave());
				instanceManager->AsyncSaveDataOnServer("TEST_INSTANCES_ID",
					[&saveFinished](bool bSuccess)
				{
					CHECK(bSuccess);
					saveFinished = true;
				});
				REQUIRE(WaitForAsyncTask(saveFinished, 10));
				CHECK(!instanceManager->HasInstancesToSave());
			};


			SaveInstancesAndWait();

			// Modify instances and save
			auto const& instances = instanceManager->GetInstancesByObjectRef("Animals/Bird.uasset", groupId);
			REQUIRE(instances.size() == 4);
			{
				auto inst0 = instances[0]->GetAutoLock();
				auto inst2 = instances[2]->GetAutoLock();

				inst0->SetName("modified name");
				inst0->InvalidateDB();

				inst2->SetColorShift({ 0.f, 1.f, 0.5f });
				inst2->InvalidateDB();
			}

			auto respKeyPutInstances = std::pair("PUT", "/advviz/v1/decorations/TEST_INSTANCES_ID/instances");
			mock->responseFctWithData_[respKeyPutInstances] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 4)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"instances\":[" \
					"{\"name\":\"modified name\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#7f007f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\",\"id\":\"instId_0\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#00ff7f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\",\"id\":\"instId_2\"}" \
					"]}")
				{
					return HTTPMock::Response2(200,
						"{\"numUpdated\":2}");
				}
				else
				{
					return HTTPMock::Response2(505, "unexpected instance PUT data");
				}
			};
			SaveInstancesAndWait();


			// Remove instance and save
			instanceManager->RemoveInstancesByObjectRef("Animals/Bird.uasset", groupId, { 3, 1 }, /*bUseRemoveAtSwap*/false);
			CHECK(instanceManager->GetInstanceCountByObjectRef("Animals/Bird.uasset", groupId) == 2);

			auto respKeyDeleteInstances = std::pair("DELETE", "/advviz/v1/decorations/TEST_INSTANCES_ID/instances");
			mock->responseFctWithData_[respKeyDeleteInstances] = [&](const std::string& data)
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 5)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				if (data == "{\"ids\":[\"instId_3\",\"instId_1\"]}"
				 || data == "{\"ids\":[\"instId_1\",\"instId_3\"]}")
				{
					return HTTPMock::Response2(200, "");
				}
				else
				{
					return HTTPMock::Response2(505, "unexpected instance DELETE data");
				}
			};
			SaveInstancesAndWait();


			// Load instances.

			auto respKeyGetGroups = std::pair("GET", "/advviz/v1/decorations/TEST_INSTANCES_ID/instancesgroups");
			mock->responseFct_[respKeyGetGroups] = [&]
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 6 && receivedRequests != 8)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				return HTTPMock::Response2(200,
					"{\"total_rows\":1,\"rows\":[" \
					"{\"name\":\"test_group\",\"id\":\"gp1\"}" \
					"],\"_links\":{}}");
			};

			auto respKeyGet = std::pair("GET", "/advviz/v1/decorations/TEST_INSTANCES_ID/instances");
			mock->responseFct_[respKeyGet] = [&]
			{
				std::unique_lock<std::mutex> lock(serverMutex);
				++receivedRequests;
				if (receivedRequests != 7 && receivedRequests != 9)
				{
					return HTTPMock::Response2(504, "wrong request order");
				}
				return HTTPMock::Response2(200,
					"{\"total_rows\":3,\"rows\":[" \
					"{\"name\":\"modified name\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#7f007f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\",\"id\":\"instId_0\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"colorshift\":\"#00ff7f\",\"groupid\":\"gp1\",\"objref\":\"Animals/Bird.uasset\",\"id\":\"instId_2\"}," \
					"{\"name\":\"inst\",\"matrix\":[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0],\"groupid\":\"gp1\",\"objref\":\"Animals/Tiger.uasset\",\"id\":\"instId_4\"}" \
					"],\"_links\":{}}");
			};

			std::shared_ptr<IInstancesManager> instanceManager2(IInstancesManager::New());
			instanceManager2->LoadDataFromServer("TEST_INSTANCES_ID");
			CHECK(IdenticalInstanceManagers(*instanceManager, *instanceManager2));

			// Test asynchronous Load as well.
			std::shared_ptr<IInstancesManager> instanceManager3(IInstancesManager::New());
			std::atomic_bool asyncLoadFinished = false;
			instanceManager3->AsyncLoadDataFromServer("TEST_INSTANCES_ID",
				[](IInstancePtr&) {},
				[](IInstancesGroupPtr&) {},
				[&asyncLoadFinished, instanceManager, instanceManager3](AdvViz::expected<void, std::string> const& exp)
			{
				REQUIRE(exp);
				CHECK(IdenticalInstanceManagers(*instanceManager, *instanceManager3));
				asyncLoadFinished = true;
			},
				{} /*defaultGroup*/);
			REQUIRE(WaitForAsyncTask(asyncLoadFinished, 10));

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}

