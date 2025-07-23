cmake_minimum_required (VERSION 3.28)

enable_testing()
include ("Public/SDK/cmake/HandleVCPKG.cmake")
# Use "Private" dir as main dir if it exists, otherwise "Public".
foreach (dir Private Public)
	if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${dir}")
		continue ()
	endif ()
	message ("Using \"${dir}\" as main dir.")
	# CMake expects a call to project(), done by the root CMakeLists.txt file.
	# So we first include a cmake file whose sole purpose is to give us the project name.
	if (NOT BE_PROJECT_NAME)
		include ("${dir}/CMake/projectName.cmake")
	endif ()
	message ("Using BE_PROJECT_NAME: ${BE_PROJECT_NAME}")
	project (${BE_PROJECT_NAME})
	# Now include the main cmake file (which is processed as if it was in the root dir).
	include ("${dir}/CMake/main.cmake")
	# Exit the file so that we don't raise the error below.
	return ()
endforeach ()
message (FATAL_ERROR "No main dir found.")
