# Here the goal is to create:
# - The targets that will build all the external dependencies of the cesium-unreal plugin.
#   This is done by simply calling add_subdirectory() on the appropriate directory.
# - A target that will install the external dependencies used by the cesium-unreal plugin.
#   This is needed because this will copy the header & lib files in the locations expected by the cesium-unreal plugin.
# The install script copies these files directly in the source dir, which is something we do not want.
# Instead, we want to have these files copied somewhere in our build dir.
# So we create a symlink, inside the build dir, that points to the source dir containing the root CMakeLists.txt.
file (CREATE_LINK "${CMAKE_SOURCE_DIR}/Public/Extern/cesium-unreal/extern" "${CMAKE_CURRENT_BINARY_DIR}/extern" SYMBOLIC)
set(CESIUM_UNREAL_NATIVE_DIRECTORY "cesium-native")
set(CESIUM_TESTS_ENABLED ON)
add_subdirectory ("${CMAKE_CURRENT_BINARY_DIR}/extern" "${CMAKE_CURRENT_BINARY_DIR}/build")
# Retrieve all the targets added by add_subdirectory() above.
be_get_targets (cesiumUnrealExternTargets "${CMAKE_CURRENT_BINARY_DIR}/extern")
foreach (target ${cesiumUnrealExternTargets})
	set_target_properties (${target} PROPERTIES FOLDER CesiumDependencies)
endforeach ()
# Ideally we could use add_custom_command with the appropriate dependencies, so that the target is built only when needed.
# But I cannot find a way to reliably retrieve all the headers/libs that will be installed,
# thus this custom target (that will be always be considered out of date).
# Fortunately, when installed files are already up to date it only take around one second to execute.
add_custom_target (InstallCesiumDependencies
	COMMAND "${CMAKE_COMMAND}" -DBUILD_TYPE=$<CONFIG> -P "${CMAKE_CURRENT_BINARY_DIR}/build/cmake_install.cmake"
)
set_target_properties (InstallCesiumDependencies PROPERTIES FOLDER CesiumDependencies)
# We have to make the "install" target depends on all the "active" targets added by the add_subdirectory() above.
# "active" target here means a target that would be built by the ALL_BUILD target,
# ie. a target that does not have the EXCLUDE_FROM_ALL flag, or whose ancestor directory has the EXCLUDE_FROM_ALL flag.
# Indeed we do not need to build the test targets and such.
be_get_targets (cesiumUnrealExternTargetsFiltered "${CMAKE_CURRENT_BINARY_DIR}/extern" SKIP_EXCLUDE_FROM_ALL)
foreach (target ${cesiumUnrealExternTargetsFiltered})
	add_dependencies (InstallCesiumDependencies ${target})
endforeach ()
# We want to run cesium-native-tests, so I modified cesium-native so that the tests are installed,
# thus we have the cesium-native-tests target thanks to the find_package in main.cmake
be_add_test (cesium-native-tests)