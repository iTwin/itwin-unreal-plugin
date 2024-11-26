/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonQueriesCacheInit.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "JsonQueriesCacheInit.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
	#include <Core/Json/Json.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <Misc/Paths.h>

#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>
#include <optional>
#include <string>

namespace QueriesCache {

struct FRflQuery
{
	std::string url;
	std::string verb;
	std::optional<std::string> payload; ///< For POST requests

	bool IsValid() const
	{
		if (verb == "GET")
			return true;
		if (verb == "POST")
		{
			return payload.has_value();
		}
		return false;
	}
};

/// "std::string reply;" is NOT included on purpose, hoping rfl::json would efficiently skip it!
/// Well, be it skipped or not, parsing is not what takes the most time by far :/ On HS2, reading all
/// cache files took almost 6s, but only one second less without any parsing at all...
/// I should save replies as separate files (and/or all request metadata in a single file/DB), but for now
/// I'm just truncating the files (see LoadCacheFileToStringWithoutReply)
struct FRflReply
{
	/// Replaces inheritance which is not supported out of the box - see:
	/// https://github.com/getml/reflect-cpp/blob/main/docs/c_arrays_and_inheritance.md
	rfl::Flatten<FRflQuery> Req;
	std::optional<int> toQuery; ///< "RecorderTimestamp" of query being replied to - simulation mode only
	bool connectedSuccessfully;
	int responseCode;
};

enum class EFileBOM { None, UTF8, UTF16_BE, UTF16_LE, UTF32_BE, UTF32_LE, Unknown };

/// Logic from FPlaylistReaderDASH::GetXMLResponseString
EFileBOM HasBOM(std::filesystem::path const& Filepath, uint64_t& FileSize)
{
    std::streamoff const FileOffset =
		std::ifstream(Filepath, std::ios_base::ate | std::ios_base::binary).tellg();
    if (FileOffset <= 0)
        return EFileBOM::Unknown;
    FileSize = static_cast<uint64_t>(FileOffset);
	std::fstream Ifs(Filepath);
    if (!Ifs.good())
        return EFileBOM::Unknown;
	unsigned char const Byte0 = (FileSize > 0) ? Ifs.get() : 0;
	unsigned char const Byte1 = (FileSize > 1) ? Ifs.get() : 0;
	unsigned char const Byte2 = (FileSize > 2) ? Ifs.get() : 0;
	unsigned char const Byte3 = (FileSize > 3) ? Ifs.get() : 0;
	if (FileSize > 3 && Byte0 == 0xEF && Byte1 == 0xBB && Byte2 == 0xBF)
	{
		return EFileBOM::UTF8;
	}
	else if (FileSize >= 2 && Byte0 == 0xFE && Byte1 == 0xFF)
	{
		return EFileBOM::UTF16_BE;
	}
	else if (FileSize >= 2 && Byte0 == 0xFF && Byte1 == 0xFE)
	{
		return EFileBOM::UTF16_LE;
	}
	else if (FileSize >= 4 && Byte0 == 0x00 && Byte1 == 0x00 && Byte2 == 0xFE && Byte3 == 0xFF)
	{
		return EFileBOM::UTF32_BE;
	}
	else if (FileSize >= 4 && Byte0 == 0xFF && Byte1 == 0xFE && Byte2 == 0x00 && Byte3 == 0x00)
	{
		return EFileBOM::UTF32_LE;
	}
	return EFileBOM::None;
}

std::string LoadCacheFileToStringWithoutReply(const TCHAR* Filename)
{
	auto const Filepath = std::filesystem::path(Filename);
	uint64_t FileSize;
	EFileBOM const BOM = HasBOM(Filepath, FileSize);
	switch (BOM)
	{
	case EFileBOM::UTF16_BE:
	case EFileBOM::UTF32_BE:
	case EFileBOM::UTF32_LE:
		ensureMsgf(false, TEXT("Unimplemented"));
		break;
	case EFileBOM::Unknown:
		ensure(false);
		break;
	case EFileBOM::UTF8:
	case EFileBOM::None:
	  {
		std::ifstream Ifs(Filepath);
		if (EFileBOM::UTF8 == BOM)
		{
			// No, codecvt_utf8 is for conversion to UCS2 or UTF-32...
			//Ifs.imbue(std::locale(Ifs.getloc(), new std::codecvt_utf8<char, std::consume_header>));
			Ifs.get(); Ifs.get(); Ifs.get(); // just skip the BOM
		}
		std::string FileString, Line;
		FileString.reserve(FileSize + 32);
		while (std::getline(Ifs, Line))
		{
			// cheat: don't read reply at all - see doc on FRflReply class
			if (std::string::npos == Line.find("\"reply\":"))
				FileString += Line;
			else
			{
				// Need to overwrite te trailing comma, it is an error in JSON5 in dicts
				auto const Comma = FileString.find_last_of(',');
				if (std::string::npos == Comma)
					ensure(false);
				else
					FileString[Comma] = '}';
				break;
			}
		}
		return FileString;
	  }
	// this is the one I really need, at least on Windows, because appparently UE Json writer will use this
	// assumedly when non-ascii characters are encountered.
	case EFileBOM::UTF16_LE:
	  {
		// open as a byte stream
		std::wifstream Ifs(Filepath, std::ios::binary);
		// apply BOM-sensitive UTF-16 facet
		Ifs.imbue(std::locale(Ifs.getloc(),
			new std::codecvt_utf16<wchar_t, /*MaxCode default:*/0x10ffff, std::consume_header>));
		std::wstring FileString, Line;
		FileString.reserve(FileSize / 2 + 32);
		while (std::getline(Ifs, Line))
		{
			if (std::wstring::npos == Line.find(L"\"reply\":"))
				FileString += Line;
			else
			{
				auto const Comma = FileString.find_last_of(L',');
				if (std::wstring::npos == Comma)
					ensure(false);
				else
					FileString[Comma] = '}';
				break;
			}
		}
		return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.to_bytes(FileString);
	  }
	}
	return {};
}

bool FRecordDirIterator::Visit(const TCHAR* Filename, bool bIsDirectory) /*override*/
{
	if (bIsDirectory || FPaths::GetCleanFilename(Filename).ToLower() == MRU_TIMESTAMP)
		return true;
	TArray<FString> OutArray;
	if (FPaths::GetBaseFilename(FString(Filename)).ParseIntoArray(OutArray, TEXT("_")) <= 1)
	{
		if (!ensure(!bSimulationMode))
			{ ensure(false); return false; }
	}
	errno = 0;
	auto const Timestamp = FCString::Strtoi(*OutArray[0], nullptr, 10);
	if (errno != 0)
		{ ensure(false); return false; }
	// +1 because pRecorderTimestamp should end up being the next available timestamp/filename
	if (pRecorderTimestamp)
		*pRecorderTimestamp = std::max(*pRecorderTimestamp, Timestamp + 1);
	bool bIsReply = false;
	std::string RflParseError;
	if (!std::filesystem::exists(std::filesystem::path(Filename)))
		{ ensure(false); return false; }
	std::string const FileString = LoadCacheFileToStringWithoutReply(Filename);
	if (FileString.empty())
		{ ensure(false); return false; }
	if (!bSimulationMode || (OutArray.Num() == 3 && OutArray[1] == TEXT("res"))) // reply
	{
		FRflReply Reply = { FRflQuery{} };
		if (!SDK::Core::Json::FromString(Reply, FileString, RflParseError))
		{
			ParsingError = RflParseError.c_str();
			ensure(false); return false;
		}
		if (!Reply.Req.value_.IsValid())
			{ ensure(false); return false; }

		bIsReply = true;
		errno = 0;
		FSessionMap::iterator SessionMapIt = SessionMap.end();
		if (bSimulationMode)
		{
			auto const ReplyToTimestamp = FCString::Strtoi(*OutArray[2], nullptr, 10);
			if (errno != 0 || ReplyToTimestamp >= Timestamp)
				{ ensure(false); return false; }
			if (!Reply.toQuery)
				{ ensure(false); return false; }
			if (ReplyToTimestamp != *Reply.toQuery)
				{ ensure(false); return false; }
			auto QueryInReplay = ReplayMap->find(ReplyToTimestamp);
			if (ReplayMap->end() == QueryInReplay)
				{ ensure(false); return false; }
			if (!ReplayMap->try_emplace(Timestamp, ReplyToTimestamp).second)
				{ ensure(false); return false; }
			bool bWrongType = false;
			std::visit([&bWrongType, &SessionMapIt, this](auto&& Var)
				{
					using T = std::decay_t<decltype(Var)>;
					if constexpr (std::is_same_v<T, FString>)
						SessionMapIt = SessionMap.find(Var);
					else if constexpr (std::is_same_v<T, FQueryKey>)
						SessionMapIt = SessionMap.find(Var);
					else if constexpr (std::is_same_v<T, int32>)
						bWrongType = true;//should have gotten a query, but this is a reply!
					else static_assert(always_false_v<T>, "non-exhaustive visitor!");
				},
				QueryInReplay->second);
			if (bWrongType || SessionMap.end() == SessionMapIt)
				{ ensure(false); return false; }
		}
		else
		{
			// do like in "query" case below, but only on SessionMap
			FSessionMap::key_type Key;
			if (Reply.Req.value_.verb == "POST")
				Key = FQueryKey(Reply.Req.value_.url.c_str(), Reply.Req.value_.payload->c_str());
			else // GET
				Key = FString(Reply.Req.value_.url.c_str());
			auto Inserted = SessionMap.try_emplace(Key, FString{});
			if (!Inserted.second) { ensure(false); return false; }
			SessionMapIt = Inserted.first;
		}
		if (Reply.connectedSuccessfully)
			SessionMapIt->second = Filename;
	}
	else if (/*bSimulationMode &&*/OutArray.Num() == 2 && OutArray[1] == TEXT("req")) // query
	{
		FRflQuery Query;
		if (!SDK::Core::Json::FromString(Query, FileString, RflParseError))
		{
			ParsingError = RflParseError.c_str();
			ensure(false); return false;
		}
		if (!Query.IsValid())
			{ ensure(false); return false; }
		FSessionMap::key_type SimuKey;
		FReplayMap::mapped_type Mapped;
		// SimuKey and Mapped are not the same variant type, hence the redundancy:
		if (Query.verb == "POST")
		{
			SimuKey = FQueryKey(Query.url.c_str(), Query.payload->c_str());
			Mapped  = FQueryKey(Query.url.c_str(), Query.payload->c_str());
		}
		else // GET
		{
			SimuKey = FString(Query.url.c_str());
			Mapped  = FString(Query.url.c_str());
		}
		if (!ReplayMap->try_emplace(Timestamp, Mapped).second)
			{ ensure(false); return false; }
		if (!SessionMap.try_emplace(SimuKey, FString{}).second)
			{ ensure(false); return false; }
	}
	else if (!ensure(bIsReply)) { return false; }
	return true;
}

} // ns JsonQueriesCache
