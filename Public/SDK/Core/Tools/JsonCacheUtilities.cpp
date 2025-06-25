/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonCacheUtilities.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


// This code was moved from JsonQueriesCacheInit.cpp to suppress a warning which was persisting despite the
// _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING macro, probably due to the way UE manages low-level
// includes.
//
// Initial author: Ghislain Cottat
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "JsonCacheUtilities.h"

#include "Assert.h"

#include <codecvt>
#include <locale>
#include <fstream>

namespace AdvViz::SDK::Tools
{

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
		uint8_t const Byte0 = static_cast<uint8_t>( (FileSize > 0) ? Ifs.get() : 0 );
		uint8_t const Byte1 = static_cast<uint8_t>( (FileSize > 1) ? Ifs.get() : 0 );
		uint8_t const Byte2 = static_cast<uint8_t>( (FileSize > 2) ? Ifs.get() : 0 );
		uint8_t const Byte3 = static_cast<uint8_t>( (FileSize > 3) ? Ifs.get() : 0 );
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

	std::string LoadCacheFileToStringWithoutReply(std::filesystem::path const& Filepath)
	{
		uint64_t FileSize;
		EFileBOM const BOM = HasBOM(Filepath, FileSize);
		switch (BOM)
		{
		case EFileBOM::UTF16_BE:
		case EFileBOM::UTF32_BE:
		case EFileBOM::UTF32_LE:
			BE_ISSUE("Unimplemented BOM");
			break;
		case EFileBOM::Unknown:
			BE_ISSUE("Unknown BOM");
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
				{
					FileString += Line;
				}
				else
				{
					// Need to overwrite the trailing comma, it is an error in JSON5 in dicts
					auto const Comma = FileString.find_last_of(',');
					if (std::string::npos == Comma)
					{
						BE_ISSUE("no comma");
					}
					else
					{
						FileString[Comma] = '}';
					}
					break;
				}
			}
			return FileString;
		}
		// this is the one I really need, at least on Windows, because apparently UE Json writer will use this
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
				{
					FileString += Line;
				}
				else
				{
					auto const Comma = FileString.find_last_of(L',');
					if (std::wstring::npos == Comma)
					{
						BE_ISSUE("no comma");
					}
					else
					{
						FileString[Comma] = '}';
					}
					break;
				}
			}
			return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.to_bytes(FileString);
		}
		}
		return {};
	}

} // ns. AdvViz::SDK::Tools
