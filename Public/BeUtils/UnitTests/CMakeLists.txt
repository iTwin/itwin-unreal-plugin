add_executable (BeUtils_UnitTests
	Main.cpp
	TestGltfTuner.cpp
	TestMiscUtils.cpp
)
target_compile_definitions (BeUtils_UnitTests PRIVATE -DBEUTILS_WORK_DIR="${CMAKE_CURRENT_BINARY_DIR}/$(Configuration)/Work")
target_link_libraries (BeUtils_UnitTests
	Catch2
	BeUtils
	CesiumGltfWriter
)
be_add_test (BeUtils_UnitTests)

add_custom_command(TARGET BeUtils_UnitTests POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
		$<TARGET_FILE:Singleton>
		$<TARGET_FILE_DIR:BeUtils_UnitTests>)
