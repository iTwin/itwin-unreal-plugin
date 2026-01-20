/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineTypes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinElementID.h>

#include <unordered_set>
#include <variant>

using FElementsGroup = std::unordered_set<ITwinElementID>;

class FIModelElementsKey
{
public:
	static FIModelElementsKey NOT_ANIMATED;
	std::variant<ITwinElementID, size_t/*GroupInVec*/> Key;
	explicit FIModelElementsKey(ITwinElementID const& ElementID) : Key(ElementID) {}
	explicit FIModelElementsKey(size_t const& GroupIndex) : Key(GroupIndex) {}
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
		return std::hash<std::variant<ITwinElementID, size_t>>()(ElementsKey.Key);
	}
};
