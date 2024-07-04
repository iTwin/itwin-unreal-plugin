# Find microhttpd. From nanopool/genoil-ethereum under MIT license
#
# Find the microhttpd includes and library
#
# if you need to add a custom library search path, do it via via CMAKE_PREFIX_PATH
#
# This module defines
#  MHD_INCLUDE_DIRS, where to find header, etc.
#  MHD_LIBRARIES, the libraries needed to use jsoncpp.
#  MHD_FOUND, If false, do not try to use jsoncpp.

find_path(
    MHD_INCLUDE_DIR
    NAMES microhttpd.h
    DOC "microhttpd include dir"
)

find_library(
    MHD_LIBRARY
    NAMES microhttpd microhttpd-10 libmicrohttpd libmicrohttpd-dll
    DOC "microhttpd library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(mhd DEFAULT_MSG
    MHD_INCLUDE_DIR MHD_LIBRARY)

mark_as_advanced(MHD_INCLUDE_DIR MHD_LIBRARY)

set(MHD_INCLUDE_DIRS ${MHD_INCLUDE_DIR})
set(MHD_LIBRARIES ${MHD_LIBRARY})
