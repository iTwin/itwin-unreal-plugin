/*--------------------------------------------------------------------------------------+
|
|     $Source: Main.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[])
{
	std::cout << "Test work dir = " << BEUTILS_WORK_DIR << std::endl;
	std::error_code ec;
	const std::filesystem::path workDir(BEUTILS_WORK_DIR);
	if (std::filesystem::exists(workDir, ec)
		&& std::filesystem::is_directory(workDir, ec)
		&& workDir.root_path() != workDir /* basic protection to avoid deleting root directory */)
	{
		std::filesystem::remove_all(workDir, ec);
	}
	return Catch::Session().run(argc, argv);
}
