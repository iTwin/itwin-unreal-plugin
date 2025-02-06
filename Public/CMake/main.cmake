# Due to Unreal and CMake having their own distinct configuration set,
# we have to map Unreal configs to CMake configs (see be_add_unreal_project).
# As a result, only a subset of CMake configs are supported.
set (CMAKE_CONFIGURATION_TYPES UnrealDebug Release)
# Add string view definition to fix include problems in Visual Studio 17.11
# Change some compiler/linker flags, applied to both our targets and the external (ceesium) targets.
# Enable debug info in Release config.
if(WIN32)
	add_definitions("-D_LEGACY_CODE_ASSUMES_STRING_VIEW_INCLUDES_XSTRING")
	set (CMAKE_CXX_FLAGS_RELEASE " ${CMAKE_CXX_FLAGS_RELEASE} /Zi")
	set (CMAKE_EXE_LINKER_FLAGS_RELEASE " ${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
	set (CMAKE_SHARED_LINKER_FLAGS_RELEASE " ${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
endif()
# Mute all useless "Up-to-date ..." messages produced by "cmake install".
set (CMAKE_INSTALL_MESSAGE LAZY)
find_package (Python3 REQUIRED COMPONENTS Interpreter)
# A rather recent version of python is needed, eg. for pathlib.Path.readlink() etc.
set (beRequiredPythonVersion "3.9")
if("${Python3_VERSION}" VERSION_LESS "${beRequiredPythonVersion}")
	message (FATAL_ERROR "Python version required is ${beRequiredPythonVersion}, but found ${Python3_VERSION}.")
endif ()
cmake_path(NATIVE_PATH Python3_EXECUTABLE NORMALIZE Python3_EXECUTABLE_NATIVE)
define_property (GLOBAL PROPERTY beExtraFoldersToSymlink
	BRIEF_DOCS "Contains extra folders to symlink - introduced for optional features"
	FULL_DOCS "Contains the absolute paths of extra folders to symlink")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
include (be_get_vcpkg_infos)
set ( glm_INCLUDE_DIR      "${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/extern/cesium-native/extern/glm" )
set ( expected_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/extern/cesium-native/extern/expected-lite/include" )
add_subdirectory(Public/SDK)
include (advanced_option)
include (be_add_feature_option)
include (options)
include (be_add_unreal_project)
include (be_get_targets)
include (be_add_test)
include (jsonUtils)
include (be_utils)
# Add all the targets for the cesium dependencies before changing the global include directories & co.
add_subdirectory (Public/CesiumDependencies)
set (CMAKE_CXX_STANDARD 20)
# Binaries used by Unreal apps/plugins are stored in the same folder "Binaries",
# independently of the config (UnrealDebug, Release...).
# So to allow building different configs, we have to set a different name for each config
# (eg. MyLib.dll in Release, and MyLibd.dll in UnrealDebug).
set (CMAKE_UNREALDEBUG_POSTFIX d)
set (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
include_directories (
	"${CMAKE_SOURCE_DIR}/Public"
	"${CMAKE_SOURCE_DIR}/Public/Extern" # for boost
)
add_subdirectory (Public/BeHeaders)
add_subdirectory (Public/Extern/boost) # Actually a small subset, see CMakeLists to expand it
add_subdirectory (Public/BeUtils)
add_subdirectory (Public/httpmockserver) # for some unit tests only
if (NOT ITWIN_TEST_APP_ID)
	# Configure your App ID here.
	set (ITWIN_TEST_APP_ID "")
endif ()
# Plugin ITwinForUnreal is used in several apps so we centralize its definition here.
set (ITwinForUnreal_PluginDef
		PLUGIN ITwinForUnreal
			PLUGIN_DEPENDS
				BeHeaders
				BeUtils
				Boost
				cpr::cpr
				httpmockserver
				libmicrohttpd
				Visualization
)
# Generate ITwinTestAppConfig.h (storing the app ID) in ThirdParty dir, so that it's ignored by git.
configure_file (Public/ITwinTestAppConfig/ITwinTestAppConfig.h.in
	"${CMAKE_SOURCE_DIR}/Public/UnrealProjects/ITwinTestApp/Source/ThirdParty/Include/ITwinTestAppConfig/ITwinTestAppConfig.h")
be_add_unreal_project ("Public/UnrealProjects/ITwinTestApp"
	MAIN_DEPENDS
		Visualization
	PLUGINS
		${ITwinForUnreal_PluginDef}
)
# Set ITwinTestApp_Editor as VS default startup project, that's what most users will want to run.
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ITwinTestApp_Editor)
