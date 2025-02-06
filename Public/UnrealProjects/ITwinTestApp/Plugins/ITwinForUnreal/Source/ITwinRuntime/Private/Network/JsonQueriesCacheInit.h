/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCacheInit.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "JsonQueriesCacheTypes.h"

#include <GenericPlatform/GenericPlatformFile.h>

namespace QueriesCache {

static const TCHAR* MRU_TIMESTAMP = TEXT("cache.txt");

class FRecordDirIterator : public IPlatformFile::FDirectoryVisitor
{
	FSessionMap& SessionMap;
	FReplayMap* ReplayMap = nullptr;
	bool bSimulationMode = false;
	FString& ParsingError;
	int* pRecorderTimestamp = nullptr;

public:
	FRecordDirIterator(FSessionMap& InSessionMap,
		FReplayMap* InReplayMap, FString& InParsingError, int* pInRecorderTimestamp = nullptr)
	:
		SessionMap(InSessionMap), ReplayMap(InReplayMap), bSimulationMode(InReplayMap != nullptr),
		ParsingError(InParsingError), pRecorderTimestamp(pInRecorderTimestamp)
	{
	}

	virtual bool Visit(const TCHAR* Filename, bool bIsDirectory) override;
};

} // ns QueriesCache
