/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinElementID.h>
#include <Hashing/UnrealGuid.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/container_hash/hash.hpp>
	#include <boost/functional/hash.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <unordered_set>
#include <variant>

using FElementsGroup = std::unordered_set<ITwinElementID>;

class FIModelElementsKey
{
public:
	static FIModelElementsKey NOT_ANIMATED;
	using KeyType = std::variant<ITwinElementID, size_t/*GroupInVec*/, FGuid>;
	KeyType Key;
	explicit FIModelElementsKey(ITwinElementID const& ElementID) : Key(ElementID) {}
	explicit FIModelElementsKey(size_t const& GroupIndex) : Key(GroupIndex) {}
	explicit FIModelElementsKey(FGuid const& Guid) : Key(Guid) {}
};

inline bool operator ==(FIModelElementsKey const& A, FIModelElementsKey const& B)
{
	return A.Key == B.Key;
}

template <>
struct std::hash<FIModelElementsKey>
{
public:
	size_t operator()(FIModelElementsKey const& ElementsKey) const
	{
		return std::hash<FIModelElementsKey::KeyType>()(ElementsKey.Key);
	}
};

template <>
struct std::hash<FElementsGroup>
{
public:
	size_t operator()(FElementsGroup const& Elements) const
	{
		size_t h = 0;
		for (typename FElementsGroup::value_type const& ElemID : Elements)
			boost::hash_combine(h, ElemID.value());
		return h;
	}
};
