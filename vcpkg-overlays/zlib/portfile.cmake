# When this port is updated, the minizip port should be updated at the same time
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO madler/zlib
    REF 09a1572aa624e5ddb6c075dc013880de70b1b9b9
    SHA512 8df6edcaf32252636da46730cffc8416d4a389c59f9c5b54f0162ccdcd35794a14e3d2c4937b973345797849ab8d2ae07420ebe6ece8febfc7da4ce6029df8f9
    HEAD_REF master
	PATCHES
		0001-zlib-static-output-name.patch
)

# This is (was?) generated during the cmake build
#file(REMOVE "${SOURCE_PATH}/zconf.h")

if (VCPKG_LIBRARY_LINKAGE STREQUAL dynamic)
	set(_linkageOpt -DZLIB_BUILD_SHARED=ON -DZLIB_BUILD_STATIC=OFF)
else()
	set(_linkageOpt -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_SHARED=OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DZLIB_BUILD_TESTING=OFF
		${_linkageOpt}
)

vcpkg_cmake_install()
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

# Install the pkgconfig file
if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
    if(VCPKG_TARGET_IS_WINDOWS)
        vcpkg_replace_string("${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/zlib.pc" "-lz" "-lzlib")
    endif()
    file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/zlib.pc" DESTINATION "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
endif()
if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
    if(VCPKG_TARGET_IS_WINDOWS)
        vcpkg_replace_string("${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/zlib.pc" "-lz" "-lzlibd")
    endif()
    file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/zlib.pc" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig")
endif()

vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/zconf.h" "ifdef ZLIB_DLL" "if 0")
else()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/zconf.h" "ifdef ZLIB_DLL" "if 1")
endif()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
