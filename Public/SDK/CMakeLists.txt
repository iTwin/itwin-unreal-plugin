cmake_minimum_required(VERSION 3.18)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../CMake")
# Including this twice is a problem: will delete buildtrees when using BE_REBUILD_ALL_VCPKG_FOR_MEND
# after vcpkg has rebuilt everything from scratch but before we've had a chance to use be_vcpkg_buildtrees_for_mend
if (NOT HAS_INCLUDED_HANDLEVCPKG)
	include(cmake/HandleVCPKG.cmake)
	set(HAS_INCLUDED_HANDLEVCPKG ON)
endif()

option(SDK_ADDUNITTEST "Generate Unit tests for SDK" ON)
option(SINGLETONDLL "Use singleton dll if you need to link the static sdk several times." OFF)
option (BE_CODE_COVERAGE "Measure code coverage when running unit tests" OFF)
option(WITH_HTTPCPR "Use CPR library" ON)

if("${VIZSDK_BUILD_CONTEXT}" STREQUAL "Unreal")
	message("Using Unreal build configuration")
	if(WIN32)
		set (CMAKE_CONFIGURATION_TYPES Debug UnrealDebug Release)
		include(cmake/CustomConfig.cmake)
	endif()
	set(SINGLETONDLL ON)
	set(WITH_HTTPCPR OFF)
endif()

if(WIN32)
	SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MP /Zi /Od")
	SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MP /Zi /Od")
	SET(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /debug /INCREMENTAL" )
	SET(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /debug /INCREMENTAL" )

	SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MP")
	SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MP")
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
	add_compile_definitions(
		$<$<CONFIG:Debug>:DEBUG_CONFIG>
		$<$<CONFIG:UnrealDebug>:UNREALDEBUG_CONFIG>
		$<$<CONFIG:Release>:RELEASE_CONFIG>
	)
else()
	add_compile_definitions(
		$<$<CONFIG:Debug>:DEBUG_CONFIG>
		$<$<CONFIG:Release>:RELEASE_CONFIG>
		$<$<CONFIG:RelWithDebInfo>:RELWITHDEBINFO_CONFIG>
	)
endif()

include(cmake/External.cmake)

include (be_setup_native_lib_flags)
include (be_coverage)
if (BE_CODE_COVERAGE)
	find_package (Python3 REQUIRED COMPONENTS Interpreter)
	# A rather recent version of python is needed, eg. for pathlib.Path.readlink() etc.
	set (beRequiredPythonVersion "3.9")
	if("${Python3_VERSION}" VERSION_LESS "${beRequiredPythonVersion}")
		message (FATAL_ERROR "Python version required is ${beRequiredPythonVersion}, but found ${Python3_VERSION}.")
	endif ()
endif ()

function (be_add_test target)
	# When code coverage is enabled, we have to filter the stdout and exit code of OpenCppCoverage.
	# We use FixStdoutForVS.py to do this.
	# Since add_test() does not support redirection, we use a cmd file.
	# It also allows us to create the test output folder before running the test (needed also when coverage is disabled).
	set (cmdFile "${CMAKE_CURRENT_BINARY_DIR}/RunTests_${target}_$<CONFIG>.cmd")
	set (testOutDir "${CMAKE_BINARY_DIR}/Testing/$<CONFIG>")
	set (runCommand "\"$<TARGET_FILE:${target}>\" --reporter \"JUnit::out=${testOutDir}/${target}.result.xml\" --reporter console")
	if (BE_CODE_COVERAGE)
		set (runCommand "\
\"${BE_COVERAGE_TOOL_EXE}\"\
 --export_type \"html:${CMAKE_BINARY_DIR}/CodeCoverage/$<CONFIG>/${target}\"\
 --export_type \"cobertura:${CMAKE_BINARY_DIR}/CodeCoverage/$<CONFIG>/${target}/report.cobertura.xml\"\
 --modules \"$<SHELL_PATH:$<TARGET_FILE_DIR:${target}>>\"\
 --sources \"$<SHELL_PATH:${CMAKE_CURRENT_FUNCTION_LIST_DIR}>\"\
 -- ${runCommand}\
 2>&1 | \"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../CMake/FixStdoutForVS.py\"\
"
	)
	endif ()
	file (GENERATE OUTPUT "${cmdFile}" CONTENT "\
\"${CMAKE_COMMAND}\" -E make_directory \"${testOutDir}\"
${runCommand}\
"
	)
	add_test(NAME ${target} COMMAND "${cmdFile}")
endfunction ()

add_custom_target (RunSdkTests "${CMAKE_CTEST_COMMAND}" -C $<CONFIG> -V)
set_target_properties (RunSdkTests PROPERTIES FOLDER "SDK")
add_subdirectory(Core)
