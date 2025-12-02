/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinConsoleCommandUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <optional>

namespace ITwin
{
	/// Try to parse the argument of given index as a boolean value.
	/// Expects the value to be interpretable as a boolean (1-0/true-false/on-off, case
	/// insensitive).
	ITWINRUNTIME_API std::optional<bool> ToggleFromCmdArg(const TArray<FString>& Args, int Idx);

	template <typename EnumType>
	std::optional<EnumType> GetEnumFromCmdArg(const TArray<FString>& Args, int Idx);
}
