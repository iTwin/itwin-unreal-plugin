vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO protocolbuffers/protobuf
    REF "v${VERSION}"
    SHA512 46d60de626480f5bac256a09c57300fe5ec990664876edbe04c9385769b500ec88409da976acc28fcb2b2e987afc1bbbf5669f4fed4033c5464ab8bbd38723bc
    HEAD_REF main
    PATCHES
        fix-cmake.patch
)

# AdvViz: avoid sending thousands of useless files to Mend (protobuf already sends those it actually uses = not the whole repo)
file (GLOB rootPaths RELATIVE "${SOURCE_PATH}" "${SOURCE_PATH}/*")
foreach (rootPath ${rootPaths})
	if (NOT "${rootPath}" STREQUAL "third_party")
		file(REMOVE_RECURSE "${SOURCE_PATH}/${rootPath}")
	endif()
endforeach()
# AdvViz: same inside third_party
file (GLOB paths RELATIVE "${SOURCE_PATH}/third_party" "${SOURCE_PATH}/third_party/*")
foreach (thirdp ${paths})
	if (NOT "${thirdp}" STREQUAL "utf8_range")
		file(REMOVE_RECURSE "${SOURCE_PATH}/third_party/${thirdp}")
	endif()
endforeach()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/third_party/utf8_range"
    OPTIONS
        "-Dutf8_range_ENABLE_TESTS=off"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME "utf8_range" CONFIG_PATH "lib/cmake/utf8_range")

vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/third_party/utf8_range/LICENSE")
