# AdvViz: avoid having vcpkg binary cache depend on Unreal installation root :-/
#set(VCPKG_ENV_PASSTHROUGH "UNREAL_ENGINE_ROOT")

if(DEFINED ENV{CESIUM_VCPKG_RELEASE_ONLY} AND "$ENV{CESIUM_VCPKG_RELEASE_ONLY}")
  set(VCPKG_BUILD_TYPE "release")
endif()
