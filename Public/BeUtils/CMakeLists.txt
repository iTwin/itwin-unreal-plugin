add_library (BeUtils
	STATIC
	Gltf/GltfBuilder.cpp
	Gltf/GltfBuilder.h
	Gltf/GltfBuilder.inl
	Gltf/GltfTuner.cpp
	Gltf/GltfTuner.h
	Gltf/GltfMaterialHelper.cpp
	Gltf/GltfMaterialHelper.h
)
target_include_directories (BeUtils PUBLIC
	"${CMAKE_BINARY_DIR}/Public/CesiumDependencies/Source/ThirdParty/include"
)
target_link_libraries (BeUtils
	Cesium3DTilesSelection
	fmt::fmt-header-only
	Visualization
)
# "${CMAKE_BINARY_DIR}/CesiumDependencies/Source" is created by target InstallCesiumDependencies.
add_dependencies (BeUtils InstallCesiumDependencies)
add_subdirectory (UnitTests)
