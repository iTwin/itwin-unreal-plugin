
include(FetchContent)

### =========== reflect-cpp =========== 
FetchContent_Declare(reflect-cpp
    GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
	GIT_TAG v0.10.0
	GIT_SHALLOW ON
)

FetchContent_MakeAvailable(reflect-cpp)
set_target_properties(reflectcpp PROPERTIES FOLDER "SDK/External")
include_directories(${reflect-cpp_SOURCE_DIR}/include)

### =========== tiny-process =========== 
set(BUILD_TESTING OFF CACHE INTERNAL "set ON to build library tests")
FetchContent_Declare(tiny-process
    GIT_REPOSITORY http://gitlab.com/eidheim/tiny-process-library
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(tiny-process)
set_target_properties(tiny-process-library PROPERTIES FOLDER "SDK/External") 
include_directories(${tiny-process_SOURCE_DIR})


find_package(cpr REQUIRED)
find_package(Catch2 3 REQUIRED)
