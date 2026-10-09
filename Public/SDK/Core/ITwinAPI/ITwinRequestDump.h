/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRequestDump.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once


#ifndef SDK_CPPMODULES
#	include <filesystem>
#	include <sstream>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Hash.h>

namespace AdvViz::SDK::RequestDump
{

struct Request
{
	std::string urlSuffix;
	std::string body;
};

struct Response
{
	long status;
	std::string body;
};

//! Returns a stringized hash of the given request.
//! Can be used as a folder name where to write or read the request & response content.
inline std::string GetRequestHash(const std::string& urlSuffix, const std::string& body)
{
	return (std::stringstream() << std::hex << Tools::GenHash((urlSuffix+";"+body).c_str())).str();
}

} // namespace AdvViz::SDK::RequestDump


MODULE_EXPORT namespace AdvViz::SDK::RequestDump
{
	//! Returns true if the request content should be dumped in a temporary folder for debugging purposes.
	bool ShouldDumpRequests();

	//! Dumps the request content in a temporary folder named from the request hash.
	void DumpRequest(const std::string& urlSuffix, const std::string& body, std::filesystem::path& requestDumpPath);
	//! Dumps the response content in a folder named after the request hash, under the given path.
	void DumpResponse(long status, const std::string& body, const std::filesystem::path& requestDumpPath);

} // namespace AdvViz::SDK::RequestDump
