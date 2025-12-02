cmake_host_system_information(RESULT _hostname QUERY HOSTNAME)
if(BE_FILL_SHARED_VCPKG_BINARY_CACHE)
	set(ENV{VCPKG_BINARY_SOURCES} "clear;files,V:\\vcpkg_cache,readwrite")
	message("BE_FILL_SHARED_VCPKG_BINARY_CACHE is ON: vcpkg binary-caching setup as '$ENV{VCPKG_BINARY_SOURCES}'")
elseif(BE_VCPKG_BINARY_CACHE)
	message("vcpkg binary-caching set from cmake option as path '${BE_VCPKG_BINARY_CACHE}'")
	set(ENV{VCPKG_BINARY_SOURCES} "files,${BE_VCPKG_BINARY_CACHE},read")
elseif(DEFINED ENV{VCPKG_BINARY_SOURCES})
	message("vcpkg binary-caching set up from environment variable: '$ENV{VCPKG_BINARY_SOURCES}'")
else()
	set(ENV{VCPKG_BINARY_SOURCES} "files,V:\\vcpkg_cache,read")
	message("vcpkg binary-caching set up by default as '$ENV{VCPKG_BINARY_SOURCES}'")
endif()

set(VCPKG_INSTALL_OPTIONS --no-print-usage)

# SELECTED_PRESET preset is a custom variable from our presets, it helps detect when preset has not been selected
if(DEFINED SELECTED_PRESET)
	# Note: displaying it here also avoids the "Manually-specified variables were not used by the project" warning...
	message("Using selected preset ${SELECTED_PRESET}")
else()
	message(FATAL_ERROR "Selecting a preset is mandatory for vcpkg,  please pass one when running CMake, eg. '--preset win64Unreal' on Windows")
endif()

# use environment variable VCPKG_ROOT in priority, otherwise check cmake variable of the same name
if (NOT DEFINED ENV{VCPKG_ROOT})
	if (NOT DEFINED VCPKG_ROOT)
		#there is no cmake variable either, create it with an empty path so that we can browse it in cmake-gui
		set (VCPKG_ROOT "" CACHE PATH "Set VCKPG REPO PATH"  )
		message(FATAL_ERROR "No VCPKG_ROOT environment or CMake variable: make sure to define one of them!")
	else()
		string (REPLACE "\\" "/" VCPKG_ROOT "${VCPKG_ROOT}") # otw errors in cesium-native's generated cmake_install.cmake files!
		set(ENV{VCPKG_ROOT} "${VCPKG_ROOT}")
		message("No VCPKG_ROOT in environment but exists as a CMake variable, using it and setting env: ${VCPKG_ROOT}")
	endif()
else()
	set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
	string (REPLACE "\\" "/" VCPKG_ROOT "${VCPKG_ROOT}") # otw errors in cesium-native's generated cmake_install.cmake files!
	set(ENV{VCPKG_ROOT} "${VCPKG_ROOT}")
	message("VCPKG_ROOT defined in environment, copying it as a CMake variable: $ENV{VCPKG_ROOT}")
endif()

include(be_find_git)
be_find_git()
if (NOT EXISTS "${VCPKG_ROOT}")
	file(MAKE_DIRECTORY "${VCPKG_ROOT}")
elseif (THIRDPARTYLIBS_CLEAN_BUILD_PRODUCTS_ASAP AND (EXISTS "${VCPKG_ROOT}/buildtrees" OR EXISTS "${VCPKG_ROOT}/packages"))
	# Clean all build products (but not the vcpkg executable) but do not redownload everything (which would have
	# been the consequence of a "git clean -dfx")
	message("CLEANING all build products inside ${VCPKG_ROOT}... ('buildtrees', 'packages')")
	file(REMOVE_RECURSE "${VCPKG_ROOT}/buildtrees")
	file(REMOVE_RECURSE "${VCPKG_ROOT}/packages")
endif()
if (WIN32)
	set(bootstrapVcpkgScript "bootstrap-vcpkg.bat")
	set(scriptLauncher "cmd.exe /C")
else()
	set(bootstrapVcpkgScript "bootstrap-vcpkg.sh")
	set(scriptLauncher)
endif()
# Differences can break binary caching, so enforce a specific commit:
set(BE_VCPKG_REPO_HASH "c9122c23d24ffd5b64459406bdfa99f847f9f562")
if (NOT EXISTS "${VCPKG_ROOT}/${bootstrapVcpkgScript}")
	execute_process(COMMAND ${GITCOMMAND}
		clone https://github.com/Microsoft/vcpkg --revision=${BE_VCPKG_REPO_HASH} "${VCPKG_ROOT}")
else()
	execute_process(COMMAND ${GITCOMMAND} rev-parse HEAD RESULT_VARIABLE git_res
		OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE git_out ERROR_VARIABLE git_err
		WORKING_DIRECTORY "${VCPKG_ROOT}"
	)
	if (git_res EQUAL 0)
		if (NOT git_out STREQUAL BE_VCPKG_REPO_HASH)
			execute_process(COMMAND ${GITCOMMAND} diff --no-ext-diff --quiet --exit-code
				RESULT_VARIABLE git_res WORKING_DIRECTORY "${VCPKG_ROOT}"
			)
			if (git_res EQUAL 0)
				message("Wrong HEAD in ${VCPKG_ROOT}, got ${git_out} instead of ${BE_VCPKG_REPO_HASH}: repo is clean, will now check out the desired commit and delete ${VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}.")
				file (REMOVE "${VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}")
				execute_process(
					COMMAND ${GITCOMMAND} fetch
					COMMAND ${GITCOMMAND} checkout ${BE_VCPKG_REPO_HASH}
					WORKING_DIRECTORY "${VCPKG_ROOT}"
					COMMAND_ERROR_IS_FATAL ANY
				)
			else()
				message(FATAL_ERROR "Wrong HEAD in ${VCPKG_ROOT}, got ${git_out} instead of ${BE_VCPKG_REPO_HASH}, but the repo is not clean, so not checking out automatically: please check your vcpkg repo and do a 'git checkout ${BE_VCPKG_REPO_HASH}' there (and delete ${VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}).")
			endif()
		endif()
	else()
		message(FATAL_ERROR "Error getting vcpkg clone's HEAD in ${VCPKG_ROOT}: ${git_err}")
	endif()
endif()
if (NOT EXISTS "${VCPKG_ROOT}/vcpkg${CMAKE_HOST_EXECUTABLE_SUFFIX}")
	execute_process(
		COMMAND ${scriptLauncher} ${bootstrapVcpkgScript} -disableMetrics
		WORKING_DIRECTORY "${VCPKG_ROOT}"
	)
endif()

if (Z_VCPKG_ROOT_DIR AND NOT Z_VCPKG_ROOT_DIR STREQUAL VCPKG_ROOT)
	message(FATAL_ERROR "Changing VCPKG_ROOT from ${Z_VCPKG_ROOT_DIR} to ${VCPKG_ROOT} in ${CMAKE_BINARY_DIR}: please clean the build directory first, then try again.")
endif()

# to make VCPKG works, we need the toolchain file (vcpkg.cmake) and a triplet. if we have those, the configuration is good
if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}" OR NOT DEFINED VCPKG_TARGET_TRIPLET)
	#replace placeholder $<VCPKG_ROOT> in default CMAKE_TOOLCHAIN_FILE from preset by the content of VCPKG_ROOT cmake variable
	STRING(REPLACE $<VCPKG_ROOT> "${VCPKG_ROOT}" CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
	set( "CMAKE_TOOLCHAIN_FILE" "${CMAKE_TOOLCHAIN_FILE}" CACHE PATH "path to vcpkg toolchain cmake"  FORCE)
	
	#final check if the CMAKE_TOOLCHAIN_FILE exists
	if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
		message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE does not exists '${CMAKE_TOOLCHAIN_FILE}'")
	endif()
endif()

if(APPLE)
	#on mac, we need PKG_CONFIG env var to point to a pkgconf executable
	if (NOT DEFINED ENV{PKG_CONFIG})
		#in case of install pkg_conf from vcpkg
		if( NOT EXISTS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf")
			message("installing pkgconf from VCPKG")
			execute_process ( COMMAND "${VCPKG_ROOT}/vcpkg" "install" "pkgconf" "--triplet" ${VCPKG_TARGET_TRIPLET} )
		endif()
		#use pkgconf installed from vcpkg
		SET( ENV{PKG_CONFIG} "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf")
	endif()
endif()