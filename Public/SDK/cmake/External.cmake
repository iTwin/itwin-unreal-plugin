
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

### =============== glm =============== 
if ( NOT DEFINED glm_INCLUDE_DIR )
	FetchContent_Declare(
		glm
		GIT_REPOSITORY	https://github.com/g-truc/glm.git
		GIT_TAG 0.9.9.8 
		EXCLUDE_FROM_ALL
	)

	FetchContent_MakeAvailable(glm)

	set_target_properties(glm PROPERTIES FOLDER "SDK/External")
	
	set ( glm_INCLUDE_DIR ${glm_SOURCE_DIR} )
endif ()

include_directories(${glm_INCLUDE_DIR})


find_package(cpr REQUIRED)
find_package(Catch2 3 REQUIRED)


