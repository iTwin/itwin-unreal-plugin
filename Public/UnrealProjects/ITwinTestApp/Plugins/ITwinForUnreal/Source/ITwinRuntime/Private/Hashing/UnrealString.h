/*--------------------------------------------------------------------------------------+
|
|     $Source: UnrealString.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

namespace std {

template <>
struct hash<FString>
{
public:
	size_t operator()(FString const& key) const
	{
		return GetTypeHash(key);
	}
};

}

inline std::size_t hash_value(FString const& v) noexcept
{
	return GetTypeHash(v);
}
