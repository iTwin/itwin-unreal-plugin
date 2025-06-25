include (be_setup_native_lib_flags)
add_library (BeUtils
	STATIC
	Gltf/ExtensionITwinMaterial.h
	Gltf/ExtensionITwinMaterialID.h
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
	Misc/Random.h
	Misc/RWLock.h
	SplineSampling/Basic2DMap.cpp
	SplineSampling/Basic2DMap.h
	SplineSampling/Basic2DMap.inl
	SplineSampling/ControlledCurve.cpp
	SplineSampling/ControlledCurve.h
	SplineSampling/MathTypes.h
	SplineSampling/OcclusionMap.cpp
	SplineSampling/OcclusionMap.h
	SplineSampling/Poisson2D.h
	SplineSampling/SplineDefines.h
	SplineSampling/SplineHelper.cpp
	SplineSampling/SplineHelper.h
	SplineSampling/SplinePattern.cpp
	SplineSampling/SplinePattern.h
	SplineSampling/SplineSampling.cpp
	SplineSampling/SplineSampling.h
	SplineSampling/SplineUtils.h
)
target_include_directories (BeUtils PUBLIC
	"${CMAKE_BINARY_DIR}/Public/CesiumDependencies/Source/ThirdParty/include"
)
target_link_libraries (BeUtils
	Cesium3DTilesSelection
	Visualization
	Network
)
# "${CMAKE_BINARY_DIR}/CesiumDependencies/Source" is created by target InstallCesiumDependencies.
add_dependencies (BeUtils InstallCesiumDependencies)
add_subdirectory (UnitTests)
