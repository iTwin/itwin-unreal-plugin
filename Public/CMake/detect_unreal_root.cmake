include (be_file_utils)
set(BE_CURRENT_UE_VERSION "5.5")

if (APPLE)
	if(EXISTS "/Users/Shared/Epic Games/UE_${BE_CURRENT_UE_VERSION}" AND NOT EXISTS "/Applications/Epic Games/UE_${BE_CURRENT_UE_VERSION}")
		set (default_UE_ROOT "/Users/Shared/Epic Games/UE_${BE_CURRENT_UE_VERSION}")
	else ()
		set (default_UE_ROOT "/Applications/Epic Games/UE_${BE_CURRENT_UE_VERSION}")
	endif ()
else ()
	set (default_UE_ROOT "C:/Program Files/Epic Games/UE_${BE_CURRENT_UE_VERSION}")
endif ()
mark_as_advanced (default_UE_ROOT)

# Supports several possibilities, in order of precedence:
#  1. CMake variable BE_UNREAL_ENGINE_DIR
#  2. CMake variable UNREAL_ENGINE_ROOT
#  3. Environment variable UNREAL_ENGINE_ROOT
#  4. default_UE_ROOT as set above
set (_beUEDirOptionDoc "Path to Unreal's 'Engine' directory")
if (DEFINED BE_UNREAL_ENGINE_DIR)
	string(STRIP "${BE_UNREAL_ENGINE_DIR}" BE_UNREAL_ENGINE_DIR)
	# cesium-unreal uses UNREAL_ENGINE_ROOT, but possibly there could be other uses,
	# so define it globally (it was formerly only in Public/CesiumDependencies/CMakeLists.txt).
	# Define both CMake an Env, who knows...
	if (BE_UNREAL_ENGINE_DIR MATCHES "Engine$" OR BE_UNREAL_ENGINE_DIR MATCHES "Engine/$")
		cmake_path(GET BE_UNREAL_ENGINE_DIR PARENT_PATH UNREAL_ENGINE_ROOT)
	else()
		set(UNREAL_ENGINE_ROOT "${BE_UNREAL_ENGINE_DIR}")
		set(BE_UNREAL_ENGINE_DIR "${BE_UNREAL_ENGINE_DIR}/Engine")
		message(WARNING "BE_UNREAL_ENGINE_DIR was passed without the 'Engine' component, it was appended for you, now: ${BE_UNREAL_ENGINE_DIR}")
	endif()
	set(ENV{UNREAL_ENGINE_ROOT} "${UNREAL_ENGINE_ROOT}")
	set(_UEChosenFrom "BE_UNREAL_ENGINE_DIR CMake variable")
elseif (DEFINED UNREAL_ENGINE_ROOT)
	string(STRIP "${UNREAL_ENGINE_ROOT}" UNREAL_ENGINE_ROOT)
	option_path (BE_UNREAL_ENGINE_DIR "${_beUEDirOptionDoc}" "${UNREAL_ENGINE_ROOT}/Engine")
	set(ENV{UNREAL_ENGINE_ROOT} "${UNREAL_ENGINE_ROOT}")
	set(_UEChosenFrom "UNREAL_ENGINE_ROOT CMake variable")
elseif (DEFINED ENV{UNREAL_ENGINE_ROOT})
	string(STRIP "$ENV{UNREAL_ENGINE_ROOT}" UNREAL_ENGINE_ROOT)
	option_path (BE_UNREAL_ENGINE_DIR "${_beUEDirOptionDoc}" "${UNREAL_ENGINE_ROOT}/Engine")
	set(_UEChosenFrom "UNREAL_ENGINE_ROOT environment variable")
else()
	option_path (BE_UNREAL_ENGINE_DIR "${_beUEDirOptionDoc}" "${default_UE_ROOT}/Engine")
	set(UNREAL_ENGINE_ROOT "${default_UE_ROOT}")
	set(ENV{UNREAL_ENGINE_ROOT} "${UNREAL_ENGINE_ROOT}")
	set(_UEChosenFrom "default Unreal installation folder for the platform")
endif()
if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}")
	set(BE_UNREAL_ENGINE_DIR "")
	message(FATAL_ERROR "Unreal's 'Engine' folder not found where it was expected: ${BE_UNREAL_ENGINE_DIR}, chosen from: ${_UEChosenFrom}")
endif()
# Note: testing an expected subdir is a way to make sure the right path component was set,
# because the error in case of mistake is not very explicit (even though we try to be robust above)
if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles" OR NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Source/ThirdParty")
	set(BE_UNREAL_ENGINE_DIR "")
	message(FATAL_ERROR "Can't find 'Build/BatchFiles' and/or 'Source/ThirdParty' subfolders where they are expected: ${BE_UNREAL_ENGINE_DIR}, chosen from: ${_UEChosenFrom}")
endif()

# DISABLED to allow releases until (but excluding) the Limited Availability
#
# Publishing the SDK uses a repo with only the Public folder: this is OK as long
# as we don't do a Mend scan on published plugins
#if (EXISTS "${CMAKE_SOURCE_DIR}/Private")
#	set(dummyAddedFile)
#	createSymlink("${BE_UNREAL_ENGINE_DIR}/Source/ThirdParty" "${CMAKE_SOURCE_DIR}/Private/_UEDepsLinkForMend" dummyAddedFile)
#endif()
