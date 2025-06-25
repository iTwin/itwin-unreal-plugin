cmake_minimum_required(VERSION 3.18)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../CMake")
include(cmake/HandleVCPKG.cmake)

option(SDK_ADDUNITTEST "Generate Unit tests for SDK" ON)
option(SINGLETONDLL "Use singleton dll if you need to link the static sdk several times." OFF)

if("${VIZSDK_BUILD_CONTEXT}" STREQUAL "Unreal")
	message("Using Unreal build configuration")
	if(WIN32)
		set (CMAKE_CONFIGURATION_TYPES UnrealDebug Release)
		include(cmake/CustomConfig.cmake)
	endif()
	set(SINGLETONDLL ON)
endif()

if (ADVVIZDLL)
add_definitions(-DADVVIZDLL -DADVVIZ_LINK_EXPORTS)
endif()

if (SINGLETONDLL)	
add_definitions("-DSINGLETONDLL")
endif()
		
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(SDK_ADDUNITTEST)
	enable_testing()
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(DefaultCXXSTD cxx_std_20)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

project(AdvVizSDK CXX)

if("${VIZSDK_BUILD_CONTEXT}" STREQUAL "Unreal")
add_compile_definitions($<$<CONFIG:UnrealDebug>:UNREALDEBUG_CONFIG>
	$<$<CONFIG:Release>:RELEASE_CONFIG>
)
else()
add_compile_definitions($<$<CONFIG:Debug>:DEBUG_CONFIG>
	$<$<CONFIG:Release>:RELEASE_CONFIG>
	$<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_CONFIG>
)
endif()

include(cmake/External.cmake)

include (be_setup_native_lib_flags)
add_subdirectory(Core)
