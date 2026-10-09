/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCacheTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Hashing/UnrealString.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/container_hash/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <Containers/UnrealString.h>

#include <map>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace QueriesCache
{
	using FQueryKey = std::pair<FString/*url*/, FString/*payload*/>;
	/// Maps replies to queries contents
	struct FEntry
	{
		FString ReplyFilepath; ///< Path to the file containing the reply
		bool bDroppable = false; ///< \see FReusableJsonQueries::OnDeltaTokenExpired
	};
	using FCacheMap = std::unordered_map<FQueryKey, FEntry>;
	/// Map of the queries/replies sent/received in the order in which it happened during a session
	using FReplayMap = std::map<int32/*Timestamp*/,
		std::variant</*get or post query:*/FString/*url*/, FQueryKey,
					 /*or reply:*/int32/*timestamp of query this is the reply to*/>>;

} // namespace QueriesCache

template <>
struct std::hash<QueriesCache::FQueryKey>
{
public:
	size_t operator()(QueriesCache::FQueryKey const& Entry) const
	{
		size_t Res = 9876;
		boost::hash_combine(Res, GetTypeHash(Entry.first));
		boost::hash_combine(Res, GetTypeHash(Entry.second));
		return Res;
	}
};
