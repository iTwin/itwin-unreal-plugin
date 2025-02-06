
#to make VCPKG works, we need the toolchain file (vcpkg.cmake) and a triplet. if we have those, the configuration is good
if(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}" OR NOT DEFINED VCPKG_TARGET_TRIPLET)

	#SELECTED_PRESET preset is a custom variable from our presets, it helps detect when preset has not been selected
	if(NOT DEFINED SELECTED_PRESET)
		message(FATAL_ERROR "You forgot to select a preset. They are needed for VCPKG please select one")
	endif()
	
	#test presence of env VCPKG_ROOT 
	if(NOT DEFINED ENV{VCPKG_ROOT})
		#there is no env varbale, test cmake variable
		message ("ENV  VCPKG_ROOT not defined" )
		if( NOT DEFINED VCPKG_ROOT)
			#there is no cmake varbale, create it with an empty path so that we can browse it in cmake-gui
			set (VCPKG_ROOT "" CACHE PATH "Set VCKPG REPO PATH"  )
			message(FATAL_ERROR "Make sure environment variable VCPKG_ROOT is set or use VCPKG_ROOT cmake Variable")
		endif()
	else()
		#convert env variable to cmake variable 
		message ("ENV  VCPKG_ROOT is set $ENV{VCPKG_ROOT}" )
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