vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO GhisBntly/stb
    REF 319f7d3425449b521fc789385618a8f825b0b201
    SHA512 56f5ca1338d9828793412216d033f1b872632914a9e3fd36bb2c669ac48964046eec279165023f14cc02c4ea13b4a28f07c7a86d48fea134d5129c2f1100023e
    HEAD_REF advviz
)

# Exclude everything that's not used, in particular stb_vorbis.c because of one Critical-
# and 4 High-severity CVEs in Mend (stb_herringbone_wang_tile and stb_include also have
# Medium-severity CVEs)
file(REMOVE "${SOURCE_PATH}/stb_connected_components.h")
file(REMOVE "${SOURCE_PATH}/stb_c_lexer.h")
file(REMOVE "${SOURCE_PATH}/stb_divide.h")
file(REMOVE "${SOURCE_PATH}/stb_ds.h")
file(REMOVE "${SOURCE_PATH}/stb_dxt.h")
file(REMOVE "${SOURCE_PATH}/stb_easy_font.h")
file(REMOVE "${SOURCE_PATH}/stb_herringbone_wang_tile.h")
file(REMOVE "${SOURCE_PATH}/stb_hexwave.h")
file(REMOVE "${SOURCE_PATH}/stb_include.h")
file(REMOVE "${SOURCE_PATH}/stb_leakcheck.h")
file(REMOVE "${SOURCE_PATH}/stb_perlin.h")
file(REMOVE "${SOURCE_PATH}/stb_rect_pack.h")
file(REMOVE "${SOURCE_PATH}/stb_sprintf.h")
file(REMOVE "${SOURCE_PATH}/stb_textedit.h")
file(REMOVE "${SOURCE_PATH}/stb_tilemap_editor.h")
file(REMOVE "${SOURCE_PATH}/stb_truetype.h")
#file(REMOVE "${SOURCE_PATH}/stb_vorbis.c") <= already removed from my fork
file(REMOVE "${SOURCE_PATH}/stb_voxel_render.h")

file(GLOB HEADER_FILES "${SOURCE_PATH}/*.h")
file(COPY ${HEADER_FILES} DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/FindStb.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# Even when told to create static symbols, STB creates two symbols globally, which breaks packaging in UE 5.4. Fix that.
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/stb_image_resize2.h" "STBIR__SIMDI_CONST(stbir__s" "STBIRDEF STBIR__SIMDI_CONST(stbir__s")
