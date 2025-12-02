include("${CMAKE_CURRENT_LIST_DIR}/../Public/Extern/cesium-unreal/extern/vcpkg-overlays/triplets/x64-windows-unreal.cmake")

# AdvViz: despite setting CESIUM_VCPKG_RELEASE_ONLY in the CMake preset environment,
# and adding VCPKG_BUILD_TYPE in the preset's "cache variables", vcpkg was still
# building both Release and Debug :/
# Let's force it here for the time being...
set(VCPKG_BUILD_TYPE "release")