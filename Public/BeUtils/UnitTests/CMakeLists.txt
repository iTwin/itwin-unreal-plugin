add_executable (BeUtils_UnitTests
	Main.cpp
	TestGltfTuner.cpp
	TestMiscUtils.cpp
)
if (MSVC)
	# We should probably use find_package(fmt REQUIRED) instead because this compilation flag
	# is an INTERFACE requirement of fmt, but I had issues doing that - will see later...
	target_compile_options(BeUtils_UnitTests PRIVATE /utf-8) # Needed for "fmt" lib
endif ()
target_compile_definitions (BeUtils_UnitTests PRIVATE -DBEUTILS_WORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/$(Configuration)/Work")
target_link_libraries (BeUtils_UnitTests
	Catch2::Catch2
	BeUtils
	CesiumGltfWriter
)
be_add_test (BeUtils_UnitTests)

add_custom_command(TARGET BeUtils_UnitTests POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
		$<TARGET_FILE:Singleton>
		$<TARGET_FILE_DIR:BeUtils_UnitTests>)
