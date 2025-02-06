cmake_minimum_required(VERSION 3.18)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../CMake")
include(cmake/HandleVCPKG.cmake)

set(SDK_ADDUNITTEST ON)
if("${VIZSDK_BUILD_CONTEXT}" STREQUAL "Unreal")
	message("Using Unreal build configuration")
	if(WIN32)
		set (CMAKE_CONFIGURATION_TYPES UnrealDebug Release)
		include(cmake/CustomConfig.cmake)
	endif()
	set(SDK_ADDUNITTEST OFF)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(SDK_ADDUNITTEST)
	enable_testing()
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(DefaultCXXSTD cxx_std_20)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

project(CarrotSDK CXX)

include(cmake/External.cmake)

include (be_setup_native_lib_flags)
add_subdirectory(Core)
