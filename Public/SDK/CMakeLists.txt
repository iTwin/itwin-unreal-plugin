cmake_minimum_required(VERSION 3.18)

include ("HandleVCPKG.cmake")


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

enable_testing()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(DefaultCXXSTD cxx_std_20)

project(CarrotSDK CXX)

include(External.cmake)

add_subdirectory(Core/Tools)
add_subdirectory(Core/Json)
add_subdirectory(Core/Network)
add_subdirectory(Core/Visualization)
