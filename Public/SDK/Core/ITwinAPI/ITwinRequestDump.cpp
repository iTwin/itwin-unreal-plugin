/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRequestDump.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinRequestDump.h"

#include <Core/Json/Json.h>

namespace AdvViz::SDK::RequestDump
{
	namespace
	{
		//! Set this variable to true in the debugger to dump all requests & responses.
		//! The generated files can then be used in automatic tests, to mock the web services.
		//! See For example IModelRenderTest.cpp. 
		static bool g_ShouldDumpRequests = false;
	} // unnamed namespace

	bool ShouldDumpRequests()
	{
		return g_ShouldDumpRequests;
	}

	void DumpRequest(const std::string& urlSuffix, const std::string& body, std::filesystem::path& requestDumpPath)
	{
		// Dump request to temp folder.
		requestDumpPath = std::filesystem::temp_directory_path() / "iTwinRequestDump" /
			RequestDump::GetRequestHash(urlSuffix, body);
		std::filesystem::remove_all(requestDumpPath);
		std::filesystem::create_directories(requestDumpPath);
		std::ofstream(requestDumpPath / "request.json") << rfl::json::write(
			RequestDump::Request{ urlSuffix, body }, YYJSON_WRITE_PRETTY);
	}

	void DumpResponse(long status, const std::string& body, const std::filesystem::path& requestDumpPath)
	{
		if (!requestDumpPath.empty())
		{
			// Dump response to temp folder.
			std::ofstream(requestDumpPath / "response.json") << rfl::json::write(
				RequestDump::Response{ status, body }, YYJSON_WRITE_PRETTY);
			if (!body.empty())
				std::ofstream(requestDumpPath / "response.bin").write(
					(const char*)body.data(), body.size());
		}
	}

} // namespace AdvViz::SDK::RequestDump
