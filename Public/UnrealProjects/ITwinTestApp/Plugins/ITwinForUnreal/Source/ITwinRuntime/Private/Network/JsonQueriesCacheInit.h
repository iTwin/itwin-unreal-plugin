/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCacheInit.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "JsonQueriesCacheTypes.h"

#include <GenericPlatform/GenericPlatformFile.h>

namespace QueriesCache {

static const TCHAR* MRU_TIMESTAMP = TEXT("cache.txt");

class FRecordDirIterator : public IPlatformFile::FDirectoryVisitor
{
	FCacheMap& CacheMap;
	FReplayMap* ReplayMap = nullptr;
	bool bSimulationMode = false;
	FString& ParsingError;
	int* pRecorderTimestamp = nullptr;

public:
	FRecordDirIterator(FCacheMap& InCacheMap,
		FReplayMap* InReplayMap, FString& InParsingError, int* pInRecorderTimestamp = nullptr)
	:
		CacheMap(InCacheMap), ReplayMap(InReplayMap), bSimulationMode(InReplayMap != nullptr),
		ParsingError(InParsingError), pRecorderTimestamp(pInRecorderTimestamp)
	{
	}

	virtual bool Visit(const TCHAR* Filename, bool bIsDirectory) override;
};

class FChangesetFinderIterator : public IPlatformFile::FDirectoryVisitor
{
	FString const LatestChangesetJson;
	FString BaseFilename;
	TArray<FString> OtherChangesetJsons;

	virtual bool Visit(const TCHAR* Filename, bool bIsDirectory) override;

public:
	FChangesetFinderIterator(FString&& InLatestChangesetJson);

	TArray<FString> const& GetOtherChangesetJsonsFound() const;
	void DeleteOtherChangesetJsons() const;
};

} // ns QueriesCache
