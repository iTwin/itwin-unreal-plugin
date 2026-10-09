/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinFileBasedMockServer.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/ITwinMockServerBase.h>
#include <filesystem>

//! Generic mock server implementation for iTwin APIs, based on cached responses from the real use cases.
//! Use AdvViz::SDK::RequestDump to generate the cached responses from a real session.
class FITwinFileBasedMockServer : public FITwinMockServerBase
{
private:
	using Super = FITwinMockServerBase;
public:
	using Super::Super;

	//! Set the relative path to the folder containing the cached responses for this mock server.
	//! The path is relative to the plugin's Resources folder.
	void SetRelativeCachedResponsesFolder(std::filesystem::path const& InRelativeCachedResponsesFolder);

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override;

private:
	inline std::filesystem::path GetResponsePath(
		const std::string& url,
		const std::string& data) const;

private:
	//! Path to the folder containing the cached responses for this mock server.
	std::filesystem::path CachedResponsesFolder;
};

//! Get a mock server instance for the given relative folder containing the cached responses.
//! A default port can be specified, otherwise 8080 is used. The mock server will search for an available
//! port starting from the default port.
std::unique_ptr<FITwinFileBasedMockServer> GetITwinFileBasedMockServer(
	std::filesystem::path const& InRelativeCacheFolder, unsigned int DefaultPort = 8080);

#endif // WITH_TESTS
