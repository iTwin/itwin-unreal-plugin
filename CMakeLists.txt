cmake_minimum_required (VERSION 3.28)

enable_testing()

if (DEFINED CARROT_BUILDNUM AND NOT ${CARROT_BUILDNUM} EQUAL "99.99.99") # Official release pipeline
	# Avoid Mend reports in internal tools
	file(REMOVE_RECURSE "${CMAKE_CURRENT_LIST_DIR}/Public/Extern/cesium-unreal/extern/cesium-native/tools")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/Public/CMake")
include ("Public/SDK/cmake/HandleVCPKG.cmake")
# Must now be done before the "project" call, because that's what triggers the installation
# of vcpkg packages, and some overlays and triplets use UNREAL_ENGINE_ROOT (see
# Public/Extern/cesium-unreal/extern/vcpkg-overlays/openssl/portfile.cmake as well as
# Public/Extern/cesium-unreal/extern/vcpkg-overlays/triplets/x64-windows-unreal.cmake for example)
include (advanced_option)
include (detect_unreal_root)
set(opensslOverlayPort "${CMAKE_CURRENT_LIST_DIR}/Public/Extern/cesium-unreal/extern/vcpkg-overlays/openssl/portfile.cmake")
configure_file("${opensslOverlayPort}.in" "${opensslOverlayPort}" @ONLY)

# Supposed to configure cesium-unreal's x64-windows-unreal triplet to build Release libs only
# (see Public/Extern/cesium-unreal/extern/vcpkg-overlays/triplets/shared/common.cmake)
# BUT in practice it did not work... (see our own vcpkg-triplets/x64-windows-unreal-release.cmake)
if (NOT DEFINED ENV{CESIUM_VCPKG_RELEASE_ONLY})
	set(ENV{CESIUM_VCPKG_RELEASE_ONLY} "ON")
endif()
# Avoid vcpkg binary cache not matching packages because of differing local CMake versions.
# Note: the version downloaded is forced by vcpkg (see vcpkg.git/scripts/vcpkg-tools.json),
# currently (Sept. 2025) it will be 3.30.1.
set(ENV{VCPKG_FORCE_DOWNLOADED_BINARIES} "ON")
# For debugging vcpkg in manifest mode (didn't work in CMakePresets.json nor on command line!)
#set(VCPKG_INSTALL_OPTIONS "--debug")

# Use "Private" dir as main dir if it exists, otherwise "Public".
foreach (dir Private Public)
	if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${dir}")
		continue ()
	endif ()
	message ("Using \"${dir}\" as main dir.")
	# CMake expects a call to project(), done by the root CMakeLists.txt file.
	# So we first include a cmake file whose sole purpose is to give us the project name.
	if (NOT BE_PROJECT_NAME)
		include ("${dir}/CMake/projectName.cmake")
	endif ()
	message ("Using BE_PROJECT_NAME: ${BE_PROJECT_NAME}")
	project (${BE_PROJECT_NAME})
	# Now include the main cmake file (which is processed as if it was in the root dir).
	include ("${dir}/CMake/main.cmake")
	# Exit the file so that we don't raise the error below.
	return ()
endforeach ()
message (FATAL_ERROR "No main dir found.")
