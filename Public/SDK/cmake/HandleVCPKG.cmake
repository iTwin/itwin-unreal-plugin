cmake_host_system_information(RESULT _hostname QUERY HOSTNAME)
if(_hostname MATCHES "PARCLUSTER*")
	set(ENV{VCPKG_BINARY_SOURCES} "clear;files,V:\\vcpkg_cache,readwrite")
	message("Running on build agent: vcpkg binary-caching setup as '$ENV{VCPKG_BINARY_SOURCES}'")
elseif(BE_VCPKG_BINARY_CACHE)
	message("Running on non-ADO machine: vcpkg binary-caching from cmake option as path '${BE_VCPKG_BINARY_CACHE}'")
	set(ENV{VCPKG_BINARY_SOURCES} "files,${BE_VCPKG_BINARY_CACHE},read")
elseif(DEFINED ENV{VCPKG_BINARY_SOURCES})
	message("Running on non-ADO machine: vcpkg binary-caching setup from environment variable: '$ENV{VCPKG_BINARY_SOURCES}'")
else()
	set(ENV{VCPKG_BINARY_SOURCES} "files,V:\\vcpkg_cache,read")
	message("Running on non-ADO machine: vcpkg binary-caching default setup as '$ENV{VCPKG_BINARY_SOURCES}'")
endif()

set(VCPKG_INSTALL_OPTIONS --no-print-usage)

#to make VCPKG works, we need the toolchain file (vcpkg.cmake) and a triplet. if we have those, the configuration is good
if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}" OR NOT DEFINED VCPKG_TARGET_TRIPLET)

	#SELECTED_PRESET preset is a custom variable from our presets, it helps detect when preset has not been selected
	if(NOT DEFINED SELECTED_PRESET)
		message(FATAL_ERROR "You forgot to select a preset. They are needed for VCPKG please select one")
	endif()
	
	# use environment variable VCPKG_ROOT in priority, otherwise check cmake variable of the same name
	if (NOT DEFINED ENV{VCPKG_ROOT})
		if (NOT DEFINED VCPKG_ROOT)
			#there is no cmake variable either, create it with an empty path so that we can browse it in cmake-gui
			set (VCPKG_ROOT "" CACHE PATH "Set VCKPG REPO PATH"  )
			message(FATAL_ERROR "No VCPKG_ROOT environment or CMake variable: make sure to define one of them!")
		else()
			message("No VCPKG_ROOT in environment but exists as a CMake variable, using it: ${VCPKG_ROOT}")
		endif()
	else()
		message("VCPKG_ROOT defined in environment, copying it as a CMake variable: ${VCPKG_ROOT}")
		cmake_path(SET VCPKG_ROOT $ENV{VCPKG_ROOT})
	endif()
	
	#replace placeholder $<VCPKG_ROOT> in default CMAKE_TOOLCHAIN_FILE from preset by the content of VCPKG_ROOT cmake variable
	STRING(REPLACE $<VCPKG_ROOT> "${VCPKG_ROOT}" CMAKE_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
	set( "CMAKE_TOOLCHAIN_FILE" "${CMAKE_TOOLCHAIN_FILE}" CACHE PATH "path to vcpkg toolchain cmake"  FORCE)
	
	#final check if the CMAKE_TOOLCHAIN_FILE exists
	if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
		message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE does not exists '${CMAKE_TOOLCHAIN_FILE}'")
	endif()
	
endif()

if(APPLE)
	#on mac, we need PKG_CONFIG env var to point to at
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