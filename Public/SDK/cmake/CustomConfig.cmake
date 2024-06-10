# note: this file is included twice: 
# first call add the configuration but the *_RELWITHDEBINFO are not set
# second call set the properties

#reference: https://gitlab.kitware.com/cmake/community/-/wikis/FAQ#how-can-i-extend-the-build-modes-with-a-custom-made-one-

if(WIN32)
	SET(CMAKE_CXX_FLAGS_UNREALDEBUG "/Zi /Od /Ob1 /DNDEBUG" CACHE STRING "Custom debug builds for C++" FORCE)
	SET(CMAKE_C_FLAGS_UNREALDEBUG "/Zi /Od /Ob1 /DNDEBUG" CACHE STRING "Custom debug builds for C" FORCE)
	SET(CMAKE_EXE_LINKER_FLAGS_UNREALDEBUG "/debug /INCREMENTAL" CACHE STRING "Custom debug builds exe" FORCE)
	SET(CMAKE_SHARED_LINKER_FLAGS_UNREALDEBUG "/debug /INCREMENTAL" CACHE STRING "Custom debug builds shared" FORCE)

	MARK_AS_ADVANCED(
		CMAKE_CXX_FLAGS_UNREALDEBUG
		CMAKE_C_FLAGS_UNREALDEBUG
		CMAKE_EXE_LINKER_FLAGS_UNREALDEBUG
		CMAKE_SHARED_LINKER_FLAGS_UNREALDEBUG
	)

	list(APPEND CMAKE_MAP_IMPORTED_CONFIG_UNREALDEBUG "Release" "")
endif()
