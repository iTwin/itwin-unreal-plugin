# When using an official Epic install (setting BE_USE_OFFICIAL_UNREAL to ON), or an already synchronized
# Bentley-Unreal fork, the UE root to use should ideally be registered in the Windows registry for the
# corresponding version (BE_CURRENT_UE_VERSION_BASED_ON or BE_CURRENT_BE_UE_VERSION).
# When you want to use UE build from the source and your Bentley-Unreal copy is not yet synchronized
# though, you need to pass a path exactly like we used to do with UE5.5 and earlier versions,
# several possibilities being considered in this order:
#  1. CMake variable BE_UNREAL_ENGINE_DIR
#  2. CMake variable UNREAL_ENGINE_ROOT
#  3. Environment variable UNREAL_ENGINE_ROOT
# You can omit the parameter once the synchronization is finished, as the UE root is automatically
# registered in Windows registry for you (see rsync_be_unreal).

function(check_ue_build_ver ue_root out_var message_severity)
	set(${out_var} "" PARENT_SCOPE)
	if (NOT EXISTS "${ue_root}" OR NOT EXISTS "${ue_root}/Engine/Build"
		OR NOT EXISTS "${ue_root}/Engine/Build/InstalledBuild.txt"
	)
		if (NOT "${message_severity}" STREQUAL "NO_MESSAGE")
			message(${message_severity} "${ue_root}/Engine/Build/InstalledBuild.txt not found")
		endif()
	else()
		file(READ "${ue_root}/Engine/Build/InstalledBuild.txt" beUECurVer)
		string (STRIP beUECurVer "${beUECurVer}")
		string (REPLACE "\n" "" beUECurVer "${beUECurVer}")
		set(${out_var} "${beUECurVer}" PARENT_SCOPE)
		message("Found ${beUECurVer} in ${ue_root}/Engine/Build/InstalledBuild.txt")
		if (NOT ${${out_var}} STREQUAL ${BE_UNREAL_VERSION_USED} AND NOT "${message_severity}" STREQUAL "NO_MESSAGE")
			message(${message_severity} "${ue_root}/Engine/Build/InstalledBuild.txt found but declared Unreal Engine build version '${beUECurVer}' does not match current expected version '${BE_UNREAL_VERSION_USED}'")
		endif()
	endif()
endfunction()

option(BE_USE_OFFICIAL_UNREAL "Use the official Unreal Engine from Epic instead of Bentley's fork" ON)
option_path(BE_RSYNC_CREDENTIALS_FILE "Credentials for rsync access to EONNAS, when using an Unreal Engine built from source: currently a file containing the password in clear text for the 'eon' user, until I can make rsync to the NAS work with an identity key" "")
option(BE_RSYNC_FORCE_NO_VERSION_CHECK "When using Bentley's fork of Unreal, force syncing with EONNAS (if BE_RSYNC_CREDENTIALS_FILE is set) without checking local and remote InstalledBuild.txt versions." OFF)
option(BE_RSYNC_DELETE_LOCAL "When using our Unreal fork and syncing with EONNAS (requires BE_RSYNC_CREDENTIALS_FILE to be set), delete local files that are not on the NAS. In particular, will wipe all your local build products." OFF)

# "BE_RSYNC_UE_EXCLUDE_*" options are dysfunctional for the moment:
# 	1. The need for a resync is based on Engine/Build/InstalledBuild.txt, which does not check whether any of the options below have changed since the last sync
#	2. Engine/Config/BaseEngine.ini should be adjusted specifically for each case (and the content used as a solution to fix issue 1.)
#	3. Some folders not related to the Debug build of the Engine are named "Debug" and thus cannot so easily discarded (but we could use "Win64/x64/*/Debug" if cwrsync supports it).
#	4. UnrealVersionSelector[-Win64-Shipping?].exe name may vary depending on the config (the Development one has no suffix at all but may be excluded)
# advanced_option(BE_RSYNC_UE_EXCLUDE_Debug "When using an Unreal Engine built from source, this will disable download of Debug build products" ON)
# advanced_option(BE_RSYNC_UE_EXCLUDE_Development "When using an Unreal Engine built from source, this will disable download of Development (for UnrealDebug) build products" OFF)
# advanced_option(BE_RSYNC_UE_EXCLUDE_Shipping "When using an Unreal Engine built from source, this will disable download of Shipping (for Release) build products" OFF)

include (be_file_utils)

set(BE_CURRENT_UE_VERSION_BASED_ON "UE_5.6")
set(BE_OPENSSL_VERSION_STRING_OFFICIAL_BASED_ON "1.1.1t")
set(BE_TINYXML2_VERSION_STRING_OFFICIAL_BASED_ON "9.0.0")
# Current build hash of Bentley's fork of Unreal: we can't just put any string in InstalledBuild.txt
# because UnrealVersionSelector.exe will use the build hash nonetheless to write in the Windows registry.
# Note: if the folder has already been registered, the build hash it is associated with is NOT updated
# when calling UnrealVersionSelector.exe in rsync_be_unreal again when 'incrementing' the build hash...
set(BE_CURRENT_BE_UE_VERSION "A5DAEB5B-4462-EC2C-0618-E5A4B687C453")
set(BE_OPENSSL_VERSION_STRING_BEUE "1.1.1zd")
set(BE_TINYXML2_VERSION_STRING_BEUE "9.0.0")
# List all previous versions here separated by semi-colons, in reverse chronological order... :-o
# Was used for when we just switched version (for a source build), so it is not yet registered...
# REMOVED - blame here - too hacky (would be needed too in cesium-unreal/extern/vcpkg-overlays/openssl/portfile.cmake.in ... :/)
#set(BE_PREVIOUS_BE_UE_VERSIONS "B79D9580-4636-69E5-D2AB-B3BB5FD07FA4;5E699E31-4C52-BB3F-DA2D-08B4FA498767")
set(BE_UNREAL_VERSION_USED) # will be set to one of the above

set(BE_CURRENT_UE_VERSION_IS_REGISTERED OFF)
if (BE_USE_OFFICIAL_UNREAL)
	message(WARNING "Using *official* Unreal Engine from Epic (version ${BE_CURRENT_UE_VERSION_BASED_ON}) instead of Bentley's fork! Some features may not work or not optimally.")
	set(BE_IS_USING_BENTLEY_UNREAL 0) # written in a .h
	mark_as_advanced(BE_RSYNC_CREDENTIALS_FILE) # Not needed for installed Engines
	set(BE_UNREAL_VERSION_USED ${BE_CURRENT_UE_VERSION_BASED_ON})
	string(REPLACE "UE_" "" ueOfficialRegValue ${BE_UNREAL_VERSION_USED})
	set(BE_UNREAL_ROOT_REG_KEY "HKEY_LOCAL_MACHINE/SOFTWARE/EpicGames/Unreal Engine/${ueOfficialRegValue}")
	set(BE_UNREAL_ROOT_REG_VALUE "InstalledDirectory")
	set(BE_OPENSSL_VERSION_STRING "${BE_OPENSSL_VERSION_STRING_OFFICIAL_BASED_ON}")
	set(BE_TINYXML2_VERSION_STRING "${BE_TINYXML2_VERSION_STRING_OFFICIAL_BASED_ON}")
else()
	set(BE_IS_USING_BENTLEY_UNREAL 1) # written in a .h
	if (BE_RSYNC_CREDENTIALS_FILE)
		message("Using Unreal Engine built from source (version ${BE_CURRENT_BE_UE_VERSION}), using ${BE_RSYNC_CREDENTIALS_FILE} for rsync credentials")
	else()
		message(WARNING "Using Unreal Engine built from source (version ${BE_CURRENT_BE_UE_VERSION}) but no credentials file supplied: proceeding assuming no sync is needed, UE version is OK and you know what you are doing...")
	endif()
	set(BE_UNREAL_ROOT_REG_KEY "HKEY_CURRENT_USER/Software/Epic Games/Unreal Engine/Builds")
	set(BE_UNREAL_VERSION_USED ${BE_CURRENT_BE_UE_VERSION})
	set(BE_UNREAL_ROOT_REG_VALUE "{${BE_UNREAL_VERSION_USED}}")
	# Used in cesium-unreal's overlay port of openssl that points at UE's version: don't forget to increment the port-version
	# (to rebuild dependees like s2geometry and gRPC ^^) whenever changes are made on BeUE's openssl 1.1.1 to fix CVEs... :-°
	# (only for header changes actually, since those are all static libs)
	set(BE_OPENSSL_VERSION_STRING "${BE_OPENSSL_VERSION_STRING_BEUE}")
	set(BE_TINYXML2_VERSION_STRING "${BE_TINYXML2_VERSION_STRING_BEUE}")
endif()
cmake_host_system_information(RESULT regResult QUERY WINDOWS_REGISTRY "${BE_UNREAL_ROOT_REG_KEY}"
							  VALUE ${BE_UNREAL_ROOT_REG_VALUE} VIEW 64 ERROR_VARIABLE regErr)
if (regErr)
	message("Reg key query for UE install failed with: ${regErr}")
	message("Note: \"cannot find the file specified\" really means the key or the value was not found.")
else()
	message("Reg key query for UE install returned: ${regResult}")
	if (EXISTS "${regResult}")
		string(STRIP "${regResult}" REGISTERED_UE_ROOT)
		string(REPLACE "\\" "/" REGISTERED_UE_ROOT "${REGISTERED_UE_ROOT}")
		set(BE_CURRENT_UE_VERSION_IS_REGISTERED ON)
		set(_UEChosenFrom "Windows registry key for the current UE version")
	else()
		message("Ignoring value returned by reg key query: path does not exist")
	endif()
endif()

if (BE_CURRENT_UE_VERSION_IS_REGISTERED)
	check_ue_build_ver("${REGISTERED_UE_ROOT}" ueBuildVerFound NO_MESSAGE)
	if (BE_USE_OFFICIAL_UNREAL AND NOT ${ueBuildVerFound} STREQUAL ${BE_CURRENT_UE_VERSION_BASED_ON})
		message(FATAL_ERROR "Version mismatch when checking the registered official Unreal found in registry at ${REGISTERED_UE_ROOT}/Engine/Build/InstalledBuild.txt: ${ueBuildVerFound} != ${BE_CURRENT_UE_VERSION_BASED_ON}")
	elseif (NOT BE_USE_OFFICIAL_UNREAL AND NOT ${ueBuildVerFound} STREQUAL ${BE_CURRENT_BE_UE_VERSION})
		# In fact this is the nominal situation when our Unreal fork has a new build version!
		# message(FATAL_ERROR "Version mismatch when checking the registered Bentley-Unreal found in registry at ${REGISTERED_UE_ROOT}/Engine/Build/InstalledBuild.txt: ${ueBuildVerFound} != ${BE_CURRENT_BE_UE_VERSION}")
	endif()
endif()

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
elseif (BE_CURRENT_UE_VERSION_IS_REGISTERED)
	set(UNREAL_ENGINE_ROOT "${REGISTERED_UE_ROOT}")
	option_path (BE_UNREAL_ENGINE_DIR "${_beUEDirOptionDoc}" "${UNREAL_ENGINE_ROOT}/Engine")
	set(ENV{UNREAL_ENGINE_ROOT} "${UNREAL_ENGINE_ROOT}")
else() # TODO Mac
	message(FATAL_ERROR "No UE root in Windows registry for the current version, NOT using the default installation path!\nPlease either register the version (run Engine/Binaries/UnrealVersionSelector.exe) or pass the path explicitly with UNREAL_ENGINE_ROOT or BE_UNREAL_ENGINE_DIR")
endif()

if (NOT "${UNREAL_ENGINE_ROOT}" STREQUAL "${REGISTERED_UE_ROOT}") # already checked
	check_ue_build_ver("${UNREAL_ENGINE_ROOT}" ueBuildVerFound NO_MESSAGE)
	if (BE_USE_OFFICIAL_UNREAL AND NOT ${ueBuildVerFound} STREQUAL ${BE_CURRENT_UE_VERSION_BASED_ON})
		message(FATAL_ERROR "Version mismatch when checking the official Unreal set via UNREAL_ENGINE_ROOT or BE_UNREAL_ENGINE_DIR (${UNREAL_ENGINE_ROOT}/Engine/Build/InstalledBuild.txt): ${ueBuildVerFound} != ${BE_CURRENT_UE_VERSION_BASED_ON}")
	endif()
endif()

if (WIN32)
	set(RSYNC_COMMAND "${CMAKE_SOURCE_DIR}/Private/CMake/cwrsync/rsync.exe")
else()
	set(RSYNC_COMMAND "rsync") # assumed in path (seems to be the case on tokamac)
endif()
if (BE_IS_USING_BENTLEY_UNREAL AND BE_RSYNC_CREDENTIALS_FILE)
	include(rsync_be_unreal)
endif()

if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}")
	set(BE_UNREAL_ENGINE_DIR "")
	message(FATAL_ERROR "Unreal's 'Engine' folder not found where it was expected: ${BE_UNREAL_ENGINE_DIR}, chosen from: ${_UEChosenFrom}")
endif()
# Note: testing an expected subdir is a way to make sure the right path component was set,
# because the error in case of mistake is not very explicit (even though we try to be robust above)
if (NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Build/BatchFiles" OR NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Source/ThirdParty"
	# Launcher app not automatically built by UBT, packaging will *succeed* even without it but Engage will fail to launch!
	# Note: 'BootstrapPackagedGame.exe' is the 'Development' executable and thus not used for 'Shipping'.
	OR NOT EXISTS "${BE_UNREAL_ENGINE_DIR}/Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe" # TODO_GCO Mac
)
	message(FATAL_ERROR "Cannot find 'Build/BatchFiles' and/or 'Source/ThirdParty' and/or 'Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe' where they are expected: ${BE_UNREAL_ENGINE_DIR}, chosen from: ${_UEChosenFrom}")
	set(BE_UNREAL_ENGINE_DIR "")
endif()
message("Using Unreal Engine from: ${BE_UNREAL_ENGINE_DIR}, chosen from: ${_UEChosenFrom}")

set(BE_IS_USING_BENTLEY_UNREAL_HEADER "${CMAKE_SOURCE_DIR}/Public/UnrealProjects/ITwinTestApp/Plugins/ITwinForUnreal/Source/ITwinRuntime/Private/Compil/IsUsingBentleyUnreal.h")
configure_file("${BE_IS_USING_BENTLEY_UNREAL_HEADER}.in" "${BE_IS_USING_BENTLEY_UNREAL_HEADER}" @ONLY)

# Publishing the SDK uses a repo with only the Public folder: creating the link in Private is OK as long as we don't do a Mend scan on published plugins
if (BE_PREPARE_FOR_MEND AND EXISTS "${CMAKE_SOURCE_DIR}/Private")
	if (BE_IS_USING_BENTLEY_UNREAL)
		# Moved to near bottom of root CMakeLists.txt because RunUAT will parse our Build.cs files and
		# error out when some folders do not exist, but some are links created during the cmake run itself
	else()
		set(dummyAddedFile)
		createSymlink("${BE_UNREAL_ENGINE_DIR}/Source/ThirdParty" "${CMAKE_SOURCE_DIR}/Private/_UEDepsLinkForMend" dummyAddedFile)
	endif()
	set(BE_PREPARE_FOR_MEND OFF CACHE BOOL FORCE "")
endif()
