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
	std::filesystem::remove_all(BEUTILS_WORK_DIR);
	return Catch::Session().run(argc, argv);
}
