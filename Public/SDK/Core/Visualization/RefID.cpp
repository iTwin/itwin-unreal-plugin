/*--------------------------------------------------------------------------------------+
|
|     $Source: RefID.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "RefID.h"

#include <mutex>

namespace AdvViz::SDK
{
	namespace
	{
		std::mutex nextIDMutex_;
		uint64_t nextID_(0);
	}

	/*static*/
	uint64_t RefID::NextID()
	{
		std::unique_lock lock(nextIDMutex_);
		return ++nextID_;
	}

	/*static*/
	RefID RefID::FromDBIdentifier(std::string const& readID, DBToIDMap& classIDMap)
	{
		RefID refId = Invalid();
		refId.db_Identifier_ = readID;

		auto const it = classIDMap.find(readID);
		if (it == classIDMap.end())
		{
			refId.id_ = NextID();
			classIDMap.emplace(readID, refId.id_);
		}
		else
		{
			refId.id_ = it->second;
		}
		return refId;
	}

	/*static*/
	RefID RefID::FindFromDBIdentifier(std::string const& readID, DBToIDMap const& classIDMap)
	{
		RefID refId = Invalid();
		auto const it = classIDMap.find(readID);
		if (it != classIDMap.end())
		{
			refId.id_ = it->second;
			refId.db_Identifier_ = readID;
		}
		return refId;
	}

	void RefID::SetDBIdentifier(std::string const& idOnServer)
	{
		db_Identifier_ = idOnServer;
	}
}
