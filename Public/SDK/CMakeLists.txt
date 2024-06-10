cmake_minimum_required(VERSION 3.18)

include(cmake/HandleVCPKG.cmake)


if(${UNREAL_BUILD})
	message("Using Unreal build configuration")
	set (CMAKE_CONFIGURATION_TYPES UnrealDebug Release)
	include(cmake/CustomConfig.cmake)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

enable_testing()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(DefaultCXXSTD cxx_std_20)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

project(CarrotSDK CXX)

include(cmake/External.cmake)

add_subdirectory(Core)
