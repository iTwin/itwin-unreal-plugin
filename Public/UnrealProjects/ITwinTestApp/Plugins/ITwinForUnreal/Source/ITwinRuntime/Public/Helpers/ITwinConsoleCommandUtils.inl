/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinConsoleCommandUtils.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Helpers/ITwinConsoleCommandUtils.h>
#include <ITwinLogCategory.h> // for LogITwin

namespace ITwin
{
	template <typename EnumType>
	std::optional<EnumType> GetEnumFromCmdArg(const TArray<FString>& Args, int Idx)
	{
		static UEnum* UnrealEnum = StaticEnum<EnumType>();
		if (!ensure(UnrealEnum))
		{
			UE_LOG(LogITwin, Error, TEXT("Unable to recover UEnum"));
			return {};
		}

		if (Args.Num() <= Idx)
		{
			UE_LOG(LogITwin, Error, TEXT("Need at least %d args to parse enum"), Idx + 1);
			return {};
		}

		int32 EnumIndex = UnrealEnum->GetIndexByName(FName(*Args[Idx]), EGetByNameFlags::None);
		if (EnumIndex == INDEX_NONE)
		{
			UE_LOG(LogITwin, Error, TEXT("Unknown enum name: %s"), *Args[Idx]);
			return {};
		}
		return static_cast<EnumType>(UnrealEnum->GetValueByIndex(EnumIndex));
	}
}
