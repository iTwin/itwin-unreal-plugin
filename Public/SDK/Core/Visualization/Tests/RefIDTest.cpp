/*--------------------------------------------------------------------------------------+
|
|     $Source: RefIDTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "../RefID.h"

#include <catch2/catch_all.hpp>

#include <unordered_set>

using namespace AdvViz::SDK;


TEST_CASE("RefID:Uniqueness")
{
	RefID id1, id2;
	CHECK(id1.IsValid());
	CHECK(id2.IsValid());
	CHECK(id1 != id2);
	CHECK(!(id1 == id2));

	RefID const nullRef = RefID::Invalid();
	CHECK(!nullRef.IsValid());
	CHECK(id1 != nullRef);
	CHECK(id2 != nullRef);
}

TEST_CASE("RefID:Reset")
{
	RefID id1, id2;
	RefID id3 = id1;
	CHECK(id3.IsValid());
	CHECK(id3 == id1);
	CHECK(id3 != id2);

	id3.Reset();
	CHECK(id3.IsValid()); // should remain valid, but change value
	CHECK(id3 != id1);
	CHECK(id3 != id2);
}

TEST_CASE("RefID:ReadFromServer")
{
	RefID::DBToIDMap idMap;
	idMap.emplace("db_id_001", (uint64_t)123);
	idMap.emplace("db_id_002", (uint64_t)189);

	RefID id0;
	CHECK(!id0.HasDBIdentifier());
	RefID id1 = id0;
	CHECK(id1 == id0);
	id1 = RefID::FromDBIdentifier("db_id_001", idMap);
	CHECK(id1 != id0);
	CHECK(id1.HasDBIdentifier());
	CHECK(id1.GetDBIdentifier() == "db_id_001");
	CHECK(id1.ID() == (uint64_t)123);
	RefID id1_bis = RefID::FromDBIdentifier("db_id_001", idMap);
	CHECK(id1_bis == id1);

	RefID id3;
	CHECK(id3.IsValid());
	RefID id4 = RefID::FromDBIdentifier("db_id_004", idMap); // not in map
	// => the identifier should have a new value, and this value should be added tp the map
	CHECK(idMap.contains("db_id_004"));
	CHECK(id4 != id3);
	CHECK(id4 != id1);
	CHECK(id4.IsValid());

	RefID id4_bis = RefID::FromDBIdentifier("db_id_004", idMap);
	CHECK(id4_bis == id4);
}

TEST_CASE("RefID:ComparisonAfterReading")
{
	std::vector<RefID> splineIds;
	splineIds.resize(10);

	std::set<RefID> splineIds_set;
	std::vector<std::string> splineIds_server;
	std::unordered_set<RefID> splineIds_hashset;
	splineIds_server.reserve(splineIds.size());
	for (RefID& id : splineIds)
	{
		RefID const initial_id(id);
		CHECK(splineIds_set.insert(id).second); // check uniqueness
		std::string const db_identifier = std::string("decoration_spline_") + std::to_string(id.ID());
		splineIds_server.push_back(db_identifier);
		splineIds_hashset.emplace(id);
		id.SetDBIdentifier(db_identifier);
		CHECK(id.HasDBIdentifier());
		CHECK(initial_id == id); // DB identifier should not change comparison
	}
	CHECK(splineIds_hashset.size() == splineIds.size()); // check hash

	// Repeat a few IDs to check they compare equal once reloaded.
	splineIds.push_back(splineIds[1]);
	splineIds_server.push_back(splineIds[1].GetDBIdentifier());
	splineIds.push_back(splineIds[1]);
	splineIds_server.push_back(splineIds[1].GetDBIdentifier());
	splineIds.push_back(splineIds[7]);
	splineIds_server.push_back(splineIds[7].GetDBIdentifier());
	splineIds.push_back(splineIds[4]);
	splineIds_server.push_back(splineIds[4].GetDBIdentifier());


	// "read" identifiers from server (in a new session, typically).
	std::vector<RefID> reloadedSplineIds;
	reloadedSplineIds.reserve(splineIds.size());
	RefID::DBToIDMap idMap;
	for (std::string const& db_identifier : splineIds_server)
	{
		RefID const reloadedId = RefID::FromDBIdentifier(db_identifier, idMap);
		reloadedSplineIds.push_back(reloadedId);
		if (reloadedSplineIds.size() <= 10)
		{
			// reloaded IDs should be unique
			CHECK(splineIds_set.insert(reloadedId).second);
		}
		else
		{
			// except those which are copies from others
			CHECK(!splineIds_set.insert(reloadedId).second);
		}
	}
	// IDs which were the same in the initial session should still be equal after reading:
	CHECK(reloadedSplineIds[10] == reloadedSplineIds[1]);
	CHECK(reloadedSplineIds[11] == reloadedSplineIds[1]);
	CHECK(reloadedSplineIds[12] == reloadedSplineIds[7]);
	CHECK(reloadedSplineIds[13] == reloadedSplineIds[4]);
}
