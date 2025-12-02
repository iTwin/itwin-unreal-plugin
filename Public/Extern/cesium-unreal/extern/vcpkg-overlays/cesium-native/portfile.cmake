vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
  FEATURES
  dependencies-only CESIUM_NATIVE_DEPS_ONLY
)

if(CESIUM_NATIVE_DEPS_ONLY)
  message(STATUS "Skipping installation of cesium-native vcpkg port (installing dependencies only)")
  set(VCPKG_POLICY_EMPTY_PACKAGE enabled)
  return()
endif()

# vcpkg_from_github(
#  OUT_SOURCE_PATH SOURCE_PATH
#  REPO CesiumGS/cesium-native
#  REF "v${VERSION}"
#  SHA512 xxxxxxxx
#  HEAD_REF main
#  PATCHES
#    config.patch <= AdvViz: applied in our submodule
# )
set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cesium-native")

# Rename sqlite3* symbols to cesium_sqlite3* so they don't conflict with UE's sqlite3,
# which has a bunch of limitations and is not considered public. The sqlite3 port is built this way.
vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
                -DCESIUM_USE_EZVCPKG=OFF
                -DPRIVATE_CESIUM_SQLITE=ON
                -DCESIUM_TESTS_ENABLED=ON
        )

vcpkg_cmake_install()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
vcpkg_cmake_config_fixup(CONFIG_PATH share/cesium-native/cmake PACKAGE_NAME cesium-native)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
