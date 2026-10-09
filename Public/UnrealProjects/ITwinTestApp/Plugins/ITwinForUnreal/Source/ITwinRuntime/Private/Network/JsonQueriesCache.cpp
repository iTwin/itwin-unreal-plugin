/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCache.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "JsonQueriesCache.h"
#include "JsonQueriesCacheInit.h"
#include <Hashing/UnrealString.h>
#include <ITwinIModelSettings.h>
#include <ITwinServerEnvironment.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Tools/Log.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <HAL/FileManager.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <algorithm>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_set>

#define DEBUG_DUMP_MRU() 0

namespace QueriesCache
{
	FString GetCacheFolder(ESubtype const Type, EITwinEnvironment const Environment, FString const& ITwinId,
		FString const& IModelId, FString const& ChangesetId, FString const& ExtraStr/* = {}*/)
	{
		// Note that an empty changeset is valid: it's the "baseline", although rare, it happens, eg. when
		// creating an iModel with https://developer.bentley.com/apis/imodels-v2/operations/create-imodel/
		// then https://developer.bentley.com/apis/imodels-v2/operations/complete-imodel-baseline-file-upload/
		ensureMsgf(ChangesetId.ToLower() != TEXT("latest"), TEXT("Need to pass the resolved changeset!"));
		FString SubtypeFolder;
		switch (Type)
		{
		case ESubtype::Schedules:						SubtypeFolder = TEXT("Schedules"); break;
		case ESubtype::DEPRECATED_ElementsHierarchies:	SubtypeFolder = TEXT("ElemTrees"); break;
		case ESubtype::DEPRECATED_ElementsSourceIDs:	SubtypeFolder = TEXT("ElemSrcID"); break;
		case ESubtype::ElementsMetadataCombined:		SubtypeFolder = TEXT("ElemMetadata"); break;
		case ESubtype::ElementsMetadataNoBBoxes:		SubtypeFolder = TEXT("ElemMetadataNoBBoxes"); break;
		case ESubtype::ElementsMetadataBBoxes:			SubtypeFolder = TEXT("ElemMetadataBBoxes"); break;
		case ESubtype::ConstructionDetailing:			SubtypeFolder = TEXT("ConstrDetailing"); break;
		default: ensure(false); return {};
		}
		FString const CacheFolder = FPaths::Combine(FPlatformProcess::UserSettingsDir(),
			TEXT("Bentley"), TEXT("Cache"), SubtypeFolder,
			ITwinServerEnvironment::ToName(Environment).ToString());
		if (ITwinId.IsEmpty())
			return CacheFolder;
		if (ESubtype::Schedules == Type)
		{
			FString Ret;
			if (ExtraStr.Contains(ITwinId)) // often the case at the moment for schedule Ids...
				Ret = FPaths::Combine(CacheFolder, ExtraStr);
			else
				Ret = FPaths::Combine(CacheFolder, ITwinId + TEXT("_") + ExtraStr);
			// NextGen are not tied to a specific changeset, thus it is passed empty
			if (!ChangesetId.IsEmpty())
				Ret += TEXT("_") + ChangesetId;
			return Ret;
		}
		else
		{
			return FPaths::Combine(CacheFolder, ITwinId, IModelId + TEXT("_") + ChangesetId);
		}
	}

} // ns QueriesCache

class FJsonQueriesCache;

namespace {

struct MRUEntry
{
	FString PathBase;
	FDateTime LastUse;
	int64 SizeOnDisk = 0;
	FJsonQueriesCache const* InUse = nullptr;
	FString DisplayName; ///< Informative only
};
using FCacheMRU = std::list<MRUEntry>;

class FJsonQueriesCacheManager
{
	std::mutex CommonMux;
	int64 MaxSize = 4 * 1073741824ULL; // 4GB;
	FCacheMRU MRU;
	std::unordered_map<FString, FCacheMRU::iterator> Caches;

public:
	static std::shared_ptr<FJsonQueriesCacheManager> Get()
	{
		static std::weak_ptr<FJsonQueriesCacheManager> Instance;
		if (Instance.expired()) // or empty
		{
			auto Shared = std::make_shared<FJsonQueriesCacheManager>();
			Instance = Shared;
			return Shared;
		}
		else
		{
			return Instance.lock();
		}
	}

	void InitializeMRU(EITwinEnvironment const Env)
	{
		MaxSize = GetDefault<UITwinIModelSettings>()->IModelMaximumCachedMegaBytes * (1024 * 1024ULL);
		std::vector<FString> const SubcacheFolders = {
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::DEPRECATED_ElementsHierarchies, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::DEPRECATED_ElementsSourceIDs, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::Schedules, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::ElementsMetadataCombined, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::ElementsMetadataNoBBoxes, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::ElementsMetadataBBoxes, Env, {}, {}, {}),
			QueriesCache::GetCacheFolder(QueriesCache::ESubtype::ConstructionDetailing, Env, {}, {}, {}),
		};
		for (FString const& Dir : SubcacheFolders)
		{
			std::unordered_set<FString> CacheDirs;
			IFileManager::Get().IterateDirectoryRecursively(*Dir,
				[&CacheDirs](const TCHAR* Filename, bool bIsDirectory)
				{
					if (!bIsDirectory) return true;
					CacheDirs.erase(FPaths::GetPath(Filename)); // ensure only leaves remain
					CacheDirs.insert(Filename);
					return true;
				});
			for (FString const& CacheDir : CacheDirs)
			{
				FString const TimestampFile = FPaths::Combine(CacheDir, QueriesCache::MRU_TIMESTAMP);
				if (IFileManager::Get().FileExists(*TimestampFile)) // nominal case
				{
					FString FileContent;
					if (!FFileHelper::LoadFileToString(FileContent, *TimestampFile))
						{ ensure(false); continue; }
					TArray<FString> OutArray;
					// MRU_TIMESTAMP file contains actually 2 or 3 pieces of data (info string is optional) with
					// pipe separators, ie: "TIMESTAMP_TICKS|DISKSIZE_BYTES|INFO_STRING"
					if (FileContent.ParseIntoArray(OutArray, TEXT("|")) < 2)
						{ ensure(false); continue; }
					errno = 0;
					int64 const Time = FCString::Strtoi64(*OutArray[0], nullptr, 10);
					if (errno != 0)
						{ ensure(false); continue; }
					int64 const SizeOnDisk = FCString::Strtoi64(*OutArray[1], nullptr, 10);
					if (errno != 0)
						{ ensure(false); continue; }
					MRU.push_back(MRUEntry{ CacheDir, FDateTime(Time), SizeOnDisk, nullptr,
											(OutArray.Num() == 3) ? OutArray[2] : FString() });
				}
				else // Old cache dir without a cache.txt: create it
				{
					int64 SizeOnDisk = 0;
					IFileManager::Get().IterateDirectory(*CacheDir,
						[&SizeOnDisk](const TCHAR* Filename, bool bIsDirectory)
						{
							if (ensure(!bIsDirectory)) // CacheDir should be a leaf dir
								SizeOnDisk += IFileManager::Get().FileSize(Filename);
							return true;
						});
					FDateTime const Now = FDateTime::UtcNow();
					MRU.push_back(MRUEntry{ CacheDir, FDateTime::UtcNow(), SizeOnDisk, nullptr, {} });
					FFileHelper::SaveStringToFile(
						FString::Printf(TEXT("%llu|%llu"), Now.GetTicks(), SizeOnDisk),
						*TimestampFile,
						FFileHelper::EEncodingOptions::ForceUTF8);
				}
			}
		}
		// Sort by access time with most recent accesses first
		MRU.sort([](MRUEntry const& X, MRUEntry const& Y) { return X.LastUse > Y.LastUse; });
		// Note: we could have created these entries in the loop above and sorted afterwards since list::sort
		// does not invalidate iterators (as opposed to std::sort)
		for (auto It = MRU.begin(), ItE = MRU.end(); It != ItE; ++It)
			Caches.insert(std::make_pair(It->PathBase, It));
	}

	bool CleanLeastRecentlyUsedOnePass()
	{
		int64 TotalSize = 0;
		FCacheMRU::iterator It, ItE;
		for (It = MRU.begin(), ItE = MRU.end(); It != ItE && TotalSize <= MaxSize; ++It)
		{
			if ((TotalSize + It->SizeOnDisk) <= MaxSize)
				TotalSize += It->SizeOnDisk;
			else
				break; // exit w/o incrementing It
		}
		bool bErasedSth = false;
		while (It != MRU.end()) // Note: no-op if TotalSize <= MaxSize
		{
			if (It->InUse == nullptr)
			{
				bErasedSth = true;
				Caches.erase(It->PathBase);
				if (!IFileManager::Get().DeleteDirectory(*It->PathBase, /*requiresExists*/true, /*tree*/true))
				{
					BE_LOGE("ITwinQuery", "Error deleting cache folder " << TCHAR_TO_UTF8(*It->PathBase));
				}
				It = MRU.erase(It);
			}
			else
			{
				TotalSize += It->SizeOnDisk;
				++It;
			}
		}
		return bErasedSth && (TotalSize > MaxSize);
	}

	void CleanLeastRecentlyUsed()
	{
#if DEBUG_DUMP_MRU()
		FString const CacheRoot = FPaths::Combine(FPlatformProcess::UserSettingsDir(),
			TEXT("Bentley"), TEXT("Cache"));
		FString Msg = FString::Printf(TEXT("MRU at %llu = { \n"), FDateTime::UtcNow().GetTicks());
		for (auto&& Entry : MRU)
		{
			Msg += FString::Printf(TEXT(" [ t=%llu, sz=%llu : \"%s\" - %s ]\n"), Entry.LastUse.GetTicks(),
				Entry.SizeOnDisk, *Entry.DisplayName, *Entry.PathBase.RightChop(CacheRoot.Len()));
		}
		Msg += TEXT("\n}");
		BE_LOG("ITwinQuery", TCHAR_TO_UTF8(*Msg));
#endif // DEBUG_DUMP_MRU()
		do { /*no-op*/ } while (CleanLeastRecentlyUsedOnePass());
	}

	enum class EUseFlag : uint8_t { Loading, Unloading };

	void MarkAsUsed(FJsonQueriesCache& Owner, FCacheMRU::iterator& Entry, EUseFlag const UseFlag)
	{
		std::lock_guard<std::mutex> Lock(CommonMux);
		if (Entry == MRU.end())
			{ ensure(false); return; }
		MRU.splice(MRU.begin(), MRU, Entry); // move Entry to front
		Entry->LastUse = FDateTime::UtcNow();
		Entry->InUse = (EUseFlag::Loading == UseFlag) ? (&Owner) : nullptr;
		if (!Owner.IsUnitTesting())
		{
			FFileHelper::SaveStringToFile(
				FString::Printf(TEXT("%llu|%llu|%s"),
								Entry->LastUse.GetTicks(), Entry->SizeOnDisk, *Entry->DisplayName),
				*FPaths::Combine(Entry->PathBase, QueriesCache::MRU_TIMESTAMP),
				FFileHelper::EEncodingOptions::ForceUTF8);
		}
		CleanLeastRecentlyUsed();
	}

	std::optional<FCacheMRU::iterator> InitializeThis(FString CacheFolder,
		EITwinEnvironment const Environment, FString const& DisplayName)
	{
		std::lock_guard<std::mutex> Lock(CommonMux);
		if (Caches.empty()) // first one => build MRU structures
		{
			InitializeMRU(Environment);
		}
		auto const Found = Caches.try_emplace(CacheFolder, FCacheMRU::iterator{});
		FCacheMRU::iterator& Entry = Found.first->second;
		if (Found.second) // was inserted, ie _not_ found -> we are initializing a new cache folder
		{
			Entry = MRU.insert(MRU.begin(),
				MRUEntry{ CacheFolder, FDateTime::UtcNow(), 0ULL, nullptr/*set in MarkAsUsed*/, DisplayName });
		}
		else
		{
			if (Entry->InUse != nullptr)
			{
				BE_LOGW("ITwinQuery", "Cache folder " << TCHAR_TO_UTF8(*CacheFolder) << " already in use!");
				return std::nullopt;
			}
			//Entry->InUse = this; will be set in MarkAsUsed below
			if (Entry->DisplayName.IsEmpty())
				Entry->DisplayName = DisplayName;
		}
		return Entry;
	}

}; // class FJsonQueriesCacheManager

} // anon. ns.

class FJsonQueriesCache::FImpl
{
public:
	FImpl(ITwinHttp::FMutex& InMutex) : Mutex(InMutex) {}
	ITwinHttp::FMutex& Mutex;
	FString PathBase;
	FCacheMRU::iterator Entry;
	std::shared_ptr<FJsonQueriesCacheManager> Manager;
	bool bIsRecordingForSimulation = false;
	bool bIsUnitTesting = false;
	int RecorderTimestamp = 0;
	QueriesCache::FCacheMap CacheMap;
};

FJsonQueriesCache::FJsonQueriesCache(UObject const& Owner, ITwinHttp::FMutex& Mutex) : Impl(MakePimpl<FImpl>(Mutex))
{
	//critical! CDOs are deleted after FImpl static members have been destroyed!
	check(!Owner.HasAnyFlags(RF_ClassDefaultObject));
}

FString FJsonQueriesCache::CacheFolder() const
{
	return Impl->PathBase;
}

bool FJsonQueriesCache::IsValid() const
{
	return !Impl->PathBase.IsEmpty();
}

bool FJsonQueriesCache::IsUnitTesting() const
{
	return Impl->bIsUnitTesting;
}

int FJsonQueriesCache::CurrentTimestamp() const
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	return (IsValid() ? Impl->RecorderTimestamp : (-1));
}

void FJsonQueriesCache::Uninitialize()
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (IsValid() && Impl->Manager)
	{
		Impl->Manager->MarkAsUsed(*this, Impl->Entry, FJsonQueriesCacheManager::EUseFlag::Unloading);
	}
	Impl->Manager.reset();
	Impl->PathBase.Empty();
	QueriesCache::FCacheMap Tmp;
	Impl->CacheMap.swap(Tmp);
	Impl->bIsRecordingForSimulation = false;
	Impl->RecorderTimestamp = 0;
}

void FJsonQueriesCache::ClearFromDisk()
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (IsValid())
		IFileManager::Get().DeleteDirectory(*Impl->PathBase, /*requireExists*/false, /*recurse*/true);
}

bool FJsonQueriesCache::Initialize(FString CacheFolder, EITwinEnvironment const Environment,
	FString const& DisplayName, bool const InbIsRecordingForSimulation/*= false*/,
	bool const bUnitTesting/*= false*/)
{
	Impl->bIsUnitTesting = bUnitTesting;
	FPaths::NormalizeDirectoryName(CacheFolder);
	FPaths::RemoveDuplicateSlashes(CacheFolder);
	// Absolute paths are required for safety against multiple cache initializations on the same folder through
	// different relative paths, but we use IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"))->GetBaseDir()
	// for the custom path passed by unit tests, and for some reason "now" it is relative :/ Just skip the test
	// and be careful with the cache folders used by unit tests (which can run concurrently!)
	if (!Impl->bIsUnitTesting && !FPaths::CollapseRelativeDirectories(CacheFolder))
	{
		BE_LOGE("ITwinQuery", "Cache folder path should be absolute: " << TCHAR_TO_UTF8(*CacheFolder));
		return false;
	}

	Impl->Manager = FJsonQueriesCacheManager::Get();
	auto Entry = Impl->Manager->InitializeThis(CacheFolder, Environment, DisplayName);
	if (Entry)
		Impl->Entry = *Entry;
	else
		return false; // error already logged

	Impl->bIsRecordingForSimulation = InbIsRecordingForSimulation;
	ensure(Impl->PathBase.IsEmpty() || Impl->PathBase == CacheFolder);
	if (!IFileManager::Get().DirectoryExists(*CacheFolder)
		&& !ensure(IFileManager::Get().MakeDirectory(*CacheFolder, /*recurse*/true)))
	{
		return false;
	}
	Impl->PathBase = CacheFolder;
	FString ParseError;
	QueriesCache::FRecordDirIterator DirIter(Impl->CacheMap, nullptr, ParseError, &Impl->RecorderTimestamp);
	if (IFileManager::Get().IterateDirectory(*CacheFolder, DirIter))
	{
		// set "InUse" and update timestamp (see also dtor...)
		Impl->Manager->MarkAsUsed(*this, Impl->Entry, FJsonQueriesCacheManager::EUseFlag::Loading);
		return true;
	}
	else
	{
		BE_LOGE("ITwinQuery", "Error loading cache: " << TCHAR_TO_UTF8(*ParseError)
			<< " --> Clearing cache folder " << TCHAR_TO_UTF8(*CacheFolder)
			<< " to avoid mixing corrupt and new data");
		IFileManager::Get().DeleteDirectory(*CacheFolder, /*requireExists*/false, /*recurse*/true);
		Uninitialize();
		// was reset and folder deleted, but set them up again to use for recording what we will (re-)download
		Impl->PathBase = CacheFolder;
		ensure(IFileManager::Get().MakeDirectory(*CacheFolder, /*recurse*/true));
		return false;
	}
}

FJsonQueriesCache::~FJsonQueriesCache()
{
	if (Impl->Manager) // this case happens when in Editor only (not PIE) then closing Unreal
		Uninitialize();
}

bool FJsonQueriesCache::LoadSessionSimulation(FString const& SimulateFromFolder)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid()) // may have been uninit from another thread
		return false;
	Impl->PathBase = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	if (ensure(IFileManager::Get().DirectoryExists(*Impl->PathBase)
		&& IFileManager::Get().DirectoryExists(*(Impl->PathBase += SimulateFromFolder))))
	{
		QueriesCache::FReplayMap ReplayMap;
		FString ParseError;
		QueriesCache::FRecordDirIterator DirIter(Impl->CacheMap, &ReplayMap, ParseError);
		if (IFileManager::Get().IterateDirectory(*Impl->PathBase, DirIter))
			return true;
		BE_LOGE("ITwinQuery", "Error parsing simulation data from " << TCHAR_TO_UTF8(*SimulateFromFolder)
			<< ": " << TCHAR_TO_UTF8(*ParseError));
	}
	return false;
}

void FJsonQueriesCache::Write(AdvViz::SDK::ITwinAPIRequestInfo const& CompletedRequest,
	FString const& QueryResult, ITwinHttp::ConnectionSuccess bConnectedSuccessfully,
	int const QueryTimestamp/*= -1*/)
{
	auto JsonObj = MakeShared<FJsonObject>();
	ToJson(CompletedRequest, JsonObj);
	Write(JsonObj,
		bConnectedSuccessfully ? 200 : 500/*we don't get the actual code from SDK...*/,
		bConnectedSuccessfully ? QueryResult : FString{},
		bConnectedSuccessfully, (bool)bConnectedSuccessfully,
		QueryTimestamp);
}

void FJsonQueriesCache::Write(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const Response,
	ITwinHttp::ConnectionSuccess bConnectedSuccessfully, int const QueryTimestamp/*= -1*/)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid()) // may have been uninit from another thread
		return;
	auto JsonObj = MakeShared<FJsonObject>();
	ToJson(CompletedRequest, JsonObj);
	if (Response)
	{
		FString Reply = Response->GetContentAsString();
		bool const bRequestSucceeded = EHttpRequestStatus::Succeeded == CompletedRequest->GetStatus();
		// See comment in FReusableJsonQueries::FImpl::FRequestHandler::ProcessResponse: write the response header
		// in the cache in a way that will put it directly in the JSON when reading from a cache entry.
		FString const ContinuationToken =
			(bConnectedSuccessfully && bRequestSucceeded) // check request success, otherwise reply may be html
				? Response->GetHeader(TEXT("Continuation-Token")) : FString{};
		if (!ContinuationToken.IsEmpty())
		{
			int32 Index;
			if (ensure(Reply.FindChar(TCHAR('{'), Index)))
			{
				Reply = FString("{\"nextPageToken\":\"") + ContinuationToken + FString("\",")
					+ Reply.RightChop(Index + 1);
			}
		}
		else // For NextGen schedules, last page of a request should contain a Delta-Token for incremental updates
		{
			FString const DeltaToken =
				(bConnectedSuccessfully && bRequestSucceeded) // same comment as above
					? Response->GetHeader(TEXT("Delta-Token")) : FString{};
			if (!DeltaToken.IsEmpty())
			{
				int32 Index;
				if (ensure(Reply.FindChar(TCHAR('{'), Index)))
				{
					Reply = FString("{\"deltaToken\":\"") + DeltaToken + FString("\",")
						+ Reply.RightChop(Index + 1);
				}
			}
		}
		Write(JsonObj, Response->GetResponseCode(), Reply, bConnectedSuccessfully, bRequestSucceeded, QueryTimestamp);
	}
	else
	{
		Write(JsonObj, 418/* https://en.wikipedia.org/wiki/HTTP_418 */, {}, bConnectedSuccessfully, false,
			  QueryTimestamp);
	}
}

void FJsonQueriesCache::Write(TSharedRef<FJsonObject>& JsonObj, int const ResponseCode,
	FString const& ContentAsString, ITwinHttp::ConnectionSuccess bConnectedSuccessfully, bool const bRequestSucceeded,
	int const QueryTimestamp)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid()) // may have been uninit from another thread
		return;
	if (Impl->bIsRecordingForSimulation)
	{
		ensure(QueryTimestamp != -1);
		JsonObj->SetNumberField(TEXT("toQuery"), QueryTimestamp);
	}
	else 
		ensure(bRequestSucceeded); // we shouldn't write unsuccessful replies in the cache...
	JsonObj->SetBoolField(TEXT("connectedSuccessfully"), bConnectedSuccessfully.value());
	JsonObj->SetNumberField(TEXT("responseCode"), ResponseCode);
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	if (bRequestSucceeded) // otherwise Response->GetContentAsString may be html, not json...
	{
		int32 FinalClosingBrace;
		if (JsonString.FindLastChar(TEXT('}'), FinalClosingBrace))
		{
			JsonString[FinalClosingBrace] = TEXT(',');
			JsonString.Reserve(JsonString.Len() + 20/*actually a bit less*/ + ContentAsString.Len());
			JsonString += LINE_TERMINATOR;
			JsonString += TEXT("\t\"reply\": ");
			JsonString += LINE_TERMINATOR;
			JsonString += ContentAsString.IsEmpty() ? FString(TEXT("{}")) : ContentAsString;
			JsonString += LINE_TERMINATOR;
			JsonString += TEXT('}');
		}
		else check(false);
	}
	FString const Path = FPaths::Combine(Impl->PathBase,
		Impl->bIsRecordingForSimulation
			? FString::Printf(TEXT("%08d_res_%08d.json"), Impl->RecorderTimestamp, QueryTimestamp)
			: FString::Printf(TEXT("%08d.json"), Impl->RecorderTimestamp));
	++Impl->RecorderTimestamp;
	if (FFileHelper::SaveStringToFile(JsonString, *Path, FFileHelper::EEncodingOptions::ForceUTF8))
	{
		Impl->Entry->SizeOnDisk += IFileManager::Get().FileSize(*Path);
	}
	else
	{
		BE_LOGE("ITwinQuery", "Could not write cache file: " << TCHAR_TO_UTF8(*Path));
	}
}

namespace
{
	inline FString ToUnrealString(AdvViz::SDK::Tools::StringWithEncoding const& ContentString)
	{
		if (ContentString.GetEncoding() == AdvViz::SDK::Tools::EStringEncoding::Utf8)
			return UTF8_TO_TCHAR(ContentString.str().c_str());
		else
			return ANSI_TO_TCHAR(ContentString.str().c_str());
	}
}

void FJsonQueriesCache::ToJson(AdvViz::SDK::ITwinAPIRequestInfo const& Req, TSharedRef<FJsonObject>& JsonObj) const
{
	JsonObj->SetStringField(TEXT("url"), Req.UrlSuffix.c_str());
	JsonObj->SetStringField(TEXT("verb"), ITwinHttp::GetVerbString(Req.Verb));
	if (AdvViz::SDK::EVerb::Post == Req.Verb)
	{
		if (Req.ContentString.empty())
			JsonObj->SetStringField(TEXT("payload"), TEXT("{}"));//otherwise won't work in Get(..)
		else
			JsonObj->SetStringField(TEXT("payload"), ToUnrealString(Req.ContentString));
	}
	// yes protect even this, against Uninitialize() being called from another thread while we are recording a query
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid())
		return;
	if (Impl->bIsRecordingForSimulation)
	{
		// see the other ToJson below, could factorize the Headers part with a template func...
		ensureMsgf(false, TEXT("Unimplemented"));
	}
}

void FJsonQueriesCache::ToJson(FHttpRequestPtr const& Req, TSharedRef<FJsonObject>& JsonObj) const
{
	JsonObj->SetStringField(TEXT("url"), Req->GetURL());
	JsonObj->SetStringField(TEXT("verb"), Req->GetVerb());
	if (Req->GetVerb() == TEXT("POST"))
	{
		auto&& ContentAsArray = Req->GetContent();
		FString const PostContentString(ContentAsArray.Num(), UTF8_TO_TCHAR(ContentAsArray.GetData()));
		if (PostContentString.IsEmpty())
			JsonObj->SetStringField(TEXT("payload"), TEXT("{}"));//otherwise won't work in Get(..)
		else
			JsonObj->SetStringField(TEXT("payload"), PostContentString);
	}
	// yes protect even this, against Uninitialize() being called from another thread while we are recording a query
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid())
		return;
	if (Impl->bIsRecordingForSimulation)
	{
		TArray<TSharedPtr<FJsonValue>> HeadersJson;
		// Headers are all the same anyway so let's save only for the very first query of each session.
		// Also, do not save Bearer token.
		if (0 == Impl->RecorderTimestamp)
		{
			for (auto&& Header : Req->GetAllHeaders())
			{
				static const FString BEARER = TEXT("bearer ");
				static const FString AUTH = TEXT("authorization:");
				int32 const BearerFound = Header.Find(BEARER);
				if (INDEX_NONE != BearerFound)
				{
					Header.LeftInline(BearerFound + BEARER.Len());
					Header += TEXT(" _bearer token expurgated from json_");
				}
				else
				{
					int32 const AuthFound = Header.Find(AUTH);
					if (INDEX_NONE != AuthFound)
					{
						Header.LeftInline(AuthFound + AUTH.Len());
						Header += TEXT(" _authorization expurgated from json_");
					}
				}
				HeadersJson.Add(MakeShared<FJsonValueString>(Header));
			}
		}
		if (!HeadersJson.IsEmpty())
			JsonObj->SetArrayField(TEXT("headers"), HeadersJson);
	}
}

void FJsonQueriesCache::RecordQuery(FHttpRequestPtr const& Request)
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid())
		return;
	auto JsonObj = MakeShared<FJsonObject>();
	ToJson(Request, JsonObj);
	FString const Path = FPaths::Combine(Impl->PathBase,
											FString::Printf(TEXT("%08d_req.json"), Impl->RecorderTimestamp));
	++Impl->RecorderTimestamp;
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	FFileHelper::SaveStringToFile(JsonString, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
}

std::optional<FString> FJsonQueriesCache::LookUp(AdvViz::SDK::ITwinAPIRequestInfo const& Request) const
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	if (!IsValid()) // may have been uninit from another thread
		return std::nullopt;
	QueriesCache::FCacheMap::const_iterator It;
	switch (Request.Verb)
	{
	case ITwinHttp::EVerb::Get:
		It = Impl->CacheMap.find(QueriesCache::FQueryKey(FString(Request.UrlSuffix.c_str()), {}));
		break;
	case ITwinHttp::EVerb::Post:
	{
		It = Impl->CacheMap.find(QueriesCache::FQueryKey(Request.UrlSuffix.c_str(),
														   ToUnrealString(Request.ContentString)));
		break;
	}
	default:
		ensure(false); // unimplemented
		return {};
	}
	if (Impl->CacheMap.end() != It)
		return It->second.ReplyFilepath;
	else return std::nullopt;
}

std::optional<QueriesCache::FCacheMap::const_iterator> FJsonQueriesCache::LookUp(ITwinHttp::FLock& Lock,
	FHttpRequestPtr const& Request, ITwinHttp::EVerb const Verb, bool bUnsetDroppableFlagOnHit,
	std::optional<FString> EraseParameter/*= {}*/) const
{
	if (!IsValid()) // may have been uninit from another thread
		return std::nullopt;
	QueriesCache::FCacheMap::iterator It; // not const_iterator, see bDroppable below
	QueriesCache::FQueryKey Key;
	TArray<uint8> const* pContentAsArray = nullptr;
	switch (Verb)
	{
	case ITwinHttp::EVerb::Post:
		pContentAsArray = &Request->GetContent();
		Key.second = FString(pContentAsArray->Num(), UTF8_TO_TCHAR(pContentAsArray->GetData()));
		[[fallthrough]];
	case ITwinHttp::EVerb::Get:
		Key.first = Request->GetURL();
		if (EraseParameter)
			ITwinHttp::EraseURLParameter(Key.first, *EraseParameter);
		It = Impl->CacheMap.find(Key);
		break;
	default:
		ensure(false); // unimplemented
		return {};
	}
	if (Impl->CacheMap.end() != It)
	{
		if (bUnsetDroppableFlagOnHit && It->second.bDroppable)
			It->second.bDroppable = false;
		return It;
	}
	else return std::nullopt;
}

std::optional<FString> FJsonQueriesCache::LookUp(FHttpRequestPtr const& Request, ITwinHttp::EVerb const Verb,
	bool bUnsetDroppableFlagOnHit, std::optional<FString> EraseParameter/*= {}*/) const
{
	ITwinHttp::FLock Lock(Impl->Mutex);
	auto Found = LookUp(Lock, Request, Verb, bUnsetDroppableFlagOnHit, EraseParameter);
	if (Found)
		return (*Found)->second.ReplyFilepath;
	else return std::nullopt;
}

/*static*/
TSharedPtr<FJsonObject> FJsonQueriesCache::Read(FString&& CacheEntryPath)
{
	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *CacheEntryPath))
	{
		auto Reader = TJsonReaderFactory<TCHAR>::Create(FileContent);
		TSharedPtr<FJsonObject> JsonObject;
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
			return {};
		TSharedPtr<FJsonObject> const* ReplyObj;
		if (JsonObject->TryGetObjectField(TEXT("reply"), ReplyObj))
			return (*ReplyObj);
		else
			return {};
	}
	else
	{
		return {};
	}
}

size_t FJsonQueriesCache::EraseEntry(QueriesCache::FCacheMap::const_iterator ToErase, bool bMarkSimilarAsDroppable,
	ITwinHttp::FLock& Lock, FString const& SimilarIgnoreParam/*= {}*/, int32 SimilarParamMaxLengthHint/*= -1*/)
{
	size_t MarkedDroppable = 0;
	if (bMarkSimilarAsDroppable && ensure(!SimilarIgnoreParam.IsEmpty()))
	{
		ensure(ToErase->first.second.IsEmpty()); // POST request?! Payload is ignored below!
		FString RedactedUrl;
		// +2 for '?' (or '&') and '='
		RedactedUrl.Reserve(ToErase->first.first.Len() + SimilarIgnoreParam.Len() + SimilarParamMaxLengthHint + 2);
		for (auto& Entry : Impl->CacheMap)
		{
			RedactedUrl.Reset();
			if (ITwinHttp::EraseURLParameter(Entry.first.first, SimilarIgnoreParam, &RedactedUrl))
			{
				if (RedactedUrl == ToErase->first.first && !Entry.second.bDroppable)
				{
					Entry.second.bDroppable = true;
					++MarkedDroppable;
				}
			}
			// But then Entry is necessarily the target of ToErase...
			// else if (Entry.first.first == ToErase->first.first) { Entry.second.bDroppable = ... }
		}
	}
	Impl->CacheMap.erase(ToErase);
	return MarkedDroppable;
}

size_t FJsonQueriesCache::EraseDroppableEntries()
{
	size_t Erased = 0;
	std::vector<FString> FilesToDel;
	{
		ITwinHttp::FLock Lock(Impl->Mutex);
		for (QueriesCache::FCacheMap::iterator It = Impl->CacheMap.begin(); It != Impl->CacheMap.end(); )
		{
			if (It->second.bDroppable)
			{
				FilesToDel.emplace_back(std::move(It->second.ReplyFilepath));
				It = Impl->CacheMap.erase(It);
				++Erased;
			}
			else
				++It;
		}
	}
	IFileManager& FileMgr = IFileManager::Get();
	for (auto&& FilePath : FilesToDel)
		FileMgr.Delete(*FilePath);
	return Erased;
}
