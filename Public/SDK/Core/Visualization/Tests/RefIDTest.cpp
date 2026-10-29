/*--------------------------------------------------------------------------------------+
|
|     $Source: RefIDTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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

TEST_CASE("RefID:FindFromDBIdentifier")
{
	RefID::DBToIDMap idMap;
	idMap.emplace("db_id_001", (uint64_t)123);
	idMap.emplace("db_id_002", (uint64_t)189);

	SECTION("known identifier")
	{
		RefID const found = RefID::FindFromDBIdentifier("db_id_001", idMap);
		CHECK(found.IsValid());
		CHECK(found.ID() == (uint64_t)123);
		CHECK(found.HasDBIdentifier());
		CHECK(found.GetDBIdentifier() == "db_id_001");

		// two lookups of the same identifier give equal references
		RefID const found_bis = RefID::FindFromDBIdentifier("db_id_001", idMap);
		CHECK(found_bis == found);

		// and they match what the non-const variant returns
		RefID const fromDB = RefID::FromDBIdentifier("db_id_001", idMap);
		CHECK(fromDB == found);

		// different entries give different references
		RefID const other = RefID::FindFromDBIdentifier("db_id_002", idMap);
		CHECK(other.IsValid());
		CHECK(other != found);
	}

	SECTION("unknown identifier returns an invalid RefID")
	{
		RefID const notFound = RefID::FindFromDBIdentifier("db_id_004", idMap);
		CHECK(!notFound.IsValid());
		CHECK(notFound.ID() == RefID::INVALID_ID);
		// no db identifier is stored when the lookup fails
		CHECK(!notFound.HasDBIdentifier());
		CHECK(notFound.GetDBIdentifier().empty());

		// an invalid reference compares different from everything, including itself and another invalid one
		RefID const valid = RefID::FindFromDBIdentifier("db_id_001", idMap);
		CHECK(notFound != valid);
		CHECK(notFound != notFound);
		CHECK(notFound != RefID::Invalid());
	}

	SECTION("empty identifier returns an invalid RefID")
	{
		RefID const notFound = RefID::FindFromDBIdentifier("", idMap);
		CHECK(!notFound.IsValid());
		CHECK(!notFound.HasDBIdentifier());
	}

	SECTION("the map is left unchanged")
	{
		size_t const initialSize = idMap.size();

		RefID::FindFromDBIdentifier("db_id_001", idMap); // known
		RefID::FindFromDBIdentifier("db_id_004", idMap); // unknown

		// contrary to FromDBIdentifier, no entry is ever created
		CHECK(idMap.size() == initialSize);
		CHECK(!idMap.contains("db_id_004"));
		CHECK(idMap.at("db_id_001") == (uint64_t)123);
	}
}

TEST_CASE("RefID:FindFromDBIdentifierAfterReading")
{
	// typical usage: identifiers are first read with FromDBIdentifier (which populates the map),
	// then later looked up with FindFromDBIdentifier (e.g. to resolve a cross-reference).
	RefID::DBToIDMap idMap;

	std::vector<std::string> const db_identifiers = {
		"decoration_spline_1", "decoration_spline_2", "decoration_spline_3" };

	std::vector<RefID> loadedIds;
	loadedIds.reserve(db_identifiers.size());
	for (std::string const& db_identifier : db_identifiers)
	{
		loadedIds.push_back(RefID::FromDBIdentifier(db_identifier, idMap));
	}
	REQUIRE(idMap.size() == db_identifiers.size());

	for (size_t i = 0; i < db_identifiers.size(); ++i)
	{
		RefID const found = RefID::FindFromDBIdentifier(db_identifiers[i], idMap);
		CHECK(found.IsValid());
		CHECK(found == loadedIds[i]);
		CHECK(found.GetDBIdentifier() == loadedIds[i].GetDBIdentifier());
	}

	// a reference to an item which was not part of the loading cannot be resolved
	RefID const dangling = RefID::FindFromDBIdentifier("decoration_spline_42", idMap);
	CHECK(!dangling.IsValid());
	for (RefID const& id : loadedIds)
	{
		CHECK(dangling != id);
	}

	// an identifier created in session (not read from the server) is not in the map either
	RefID sessionId;
	sessionId.SetDBIdentifier("decoration_spline_99");
	CHECK(!RefID::FindFromDBIdentifier(sessionId.GetDBIdentifier(), idMap).IsValid());
}
