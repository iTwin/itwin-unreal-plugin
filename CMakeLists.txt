cmake_minimum_required (VERSION 3.28)

enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/Public/CMake")
include ("Public/SDK/cmake/HandleVCPKG.cmake")
set(HAS_INCLUDED_HANDLEVCPKG ON)
# Must now be done before the "project" call, because that's what triggers the installation
# of vcpkg packages, and some overlays and triplets use UNREAL_ENGINE_ROOT or BE_UNREAL_ENGINE_DIR
# (see the portfile.cmake.in below as well as
# Public/Extern/cesium-unreal/extern/vcpkg-overlays/triplets/x64-windows-unreal.cmake for example)
include (advanced_option)
include (detect_unreal_root)
# Configure a few files for vcpkg overlays that we have modified from cesium-unreal:
include("Public/CesiumDependencies/generate_overlay_files.cmake")

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
	if (dir STREQUAL Private AND BE_PREPARE_FOR_MEND)
		# (BeUE-only) Run Epic's "TPS audit" and prepare the folder sent to Mend for scanning
		if (BE_IS_USING_BENTLEY_UNREAL)
			include(be_ue_prepare_for_mend)
		endif()
		# Create and/or link the vcpkg buildtrees' filtered copy so that it is sent to Mend for
		# scanning, as the ThirdParty 'include' folder is not sufficient for a thorough scan!
		include(be_vcpkg_buildtrees_for_mend)
	endif()
	# To be called after all tests have been registered.
	# Creates a "merge reports" target for code coverage etc.
	FinalizeTests()
	# Exit the file so that we don't raise the error below.
	return ()
endforeach ()
message (FATAL_ERROR "No main dir found.")
