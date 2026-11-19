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
		std::error_code ec;
		requestDumpPath = std::filesystem::temp_directory_path(ec) / "iTwinRequestDump" /
			RequestDump::GetRequestHash(urlSuffix, body);
		std::filesystem::remove_all(requestDumpPath, ec);
		std::filesystem::create_directories(requestDumpPath, ec);
		std::ofstream(requestDumpPath / "request.json") << rfl::json::write(
			RequestDump::Request{ urlSuffix, body }, YYJSON_WRITE_PRETTY);
	}

	void DumpResponse(const HttpResponse& response, const std::filesystem::path& requestDumpPath)
	{
		if (!requestDumpPath.empty())
		{
			// Dump response to temp folder.
			const long status = response.first;
			const std::string& body = response.second;
			std::ofstream(requestDumpPath / "response.json") << rfl::json::write(
				RequestDump::Response{ status, body }, YYJSON_WRITE_PRETTY);

			// If the response has raw binary content, dump it instead of the body string.
			// This is the case for example for iModel textures.
			if (response.rawdata_ && response.rawdata_->size() > 0)
			{
				std::ofstream out(requestDumpPath / "response.bin", std::ios::binary);
				out.write(
					reinterpret_cast<const char*>(response.rawdata_->data()),
					static_cast<std::streamsize>(response.rawdata_->size()));
			}
			else if (!body.empty())
			{
				std::ofstream(requestDumpPath / "response.bin").write(
					(const char*)body.data(), body.size());
			}
		}
	}

} // namespace AdvViz::SDK::RequestDump
