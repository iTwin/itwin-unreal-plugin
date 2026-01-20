/*--------------------------------------------------------------------------------------+
|
|     $Source: UnrealGuid.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Misc/Guid.h>

namespace std {

template <>
struct hash<FGuid>
{
public:
	size_t operator()(FGuid const& key) const
	{
		return GetTypeHash(key);
	}
};

}

inline std::size_t hash_value(FGuid const& v) noexcept
{
	return GetTypeHash(v);
}
