/*--------------------------------------------------------------------------------------+
|
|     $Source: StdHash.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
