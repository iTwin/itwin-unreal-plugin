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
	#include <BeHeaders/Compil/AlwaysFalse.h>
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
	/// Maps replies to queries contents
	using FQueryKey = std::pair<FString/*url*/, FString/*payload*/>;
	using FSessionMap
		= std::unordered_map<std::variant<FString/*url*/, FQueryKey>, FString/*reply filepath*/>;
	/// Map of the queries/replies sent/received in the order in which it happened during a session
	using FReplayMap = std::map<int32/*Timestamp*/,
		std::variant</*get or post query:*/FString/*url*/, FQueryKey,
					 /*or reply:*/int32/*timestamp of query this is the reply to*/>>;

} // namespace QueriesCache

template <>
struct std::hash<std::variant<FString, QueriesCache::FQueryKey>>
{
public:
	size_t operator()(std::variant<FString, QueriesCache::FQueryKey> const& Key) const
	{
		size_t Res = 9876;
		std::visit([&Res](auto&& StrOrPair)
			{
				using T = std::decay_t<decltype(StrOrPair)>;
				if constexpr (std::is_same_v<T, FString>)
					boost::hash_combine(Res, GetTypeHash(StrOrPair));
				else if constexpr (std::is_same_v<T, QueriesCache::FQueryKey>)
				{
					boost::hash_combine(Res, GetTypeHash(StrOrPair.first));
					boost::hash_combine(Res, GetTypeHash(StrOrPair.second));
				}
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			Key);
		return Res;
	}
};
