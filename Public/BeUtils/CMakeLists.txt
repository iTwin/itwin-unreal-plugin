include (be_setup_native_lib_flags)
add_library (BeUtils
	STATIC
	Gltf/GltfBuilder.cpp
	Gltf/GltfBuilder.h
	Gltf/GltfBuilder.inl
	Gltf/GltfTuner.cpp
	Gltf/GltfTuner.h
	Gltf/GltfMaterialHelper.cpp
	Gltf/GltfMaterialHelper.h
	Gltf/GltfMaterialTuner.cpp
	Gltf/GltfMaterialTuner.h
	Misc/MiscUtils.cpp
	Misc/MiscUtils.h
)
target_include_directories (BeUtils PUBLIC
	"${CMAKE_BINARY_DIR}/Public/CesiumDependencies/Source/ThirdParty/include"
)
target_link_libraries (BeUtils
	Cesium3DTilesSelection
	Visualization
)
# "${CMAKE_BINARY_DIR}/CesiumDependencies/Source" is created by target InstallCesiumDependencies.
add_dependencies (BeUtils InstallCesiumDependencies)
add_subdirectory (UnitTests)
