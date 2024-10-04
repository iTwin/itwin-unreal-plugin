
include(FetchContent)

### =========== reflect-cpp =========== 
FetchContent_Declare(reflect-cpp
    GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
	GIT_TAG v0.14.1
	GIT_SHALLOW ON
)

FetchContent_MakeAvailable(reflect-cpp)
set_target_properties(reflectcpp PROPERTIES FOLDER "SDK/External")
include_directories(${reflect-cpp_SOURCE_DIR}/include)

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

### =========== fmt =========== 
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
	GIT_TAG 11.0.2
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(fmt)
set_target_properties(fmt PROPERTIES FOLDER "SDK/External")
#message( " >>> fmt directory: ${fmt_SOURCE_DIR}")
include_directories(${fmt_SOURCE_DIR}/include)

### =========== cpr =========== 
find_package(cpr REQUIRED)

### =========== libassert =========== 
find_package(libassert CONFIG REQUIRED)

### =========== plog =========== 
FetchContent_Declare(plog
    GIT_REPOSITORY https://github.com/SergiusTheBest/plog.git
	GIT_TAG 1.1.10
	GIT_SHALLOW ON 
)

FetchContent_MakeAvailable(plog)
set_target_properties(plog PROPERTIES FOLDER "SDK/External")
include_directories(${plog_SOURCE_DIR}/include)
message("inc:${plog_SOURCE_DIR}/include")


### =============== expected =============== 
if ( NOT DEFINED expected_INCLUDE_DIR )
	FetchContent_Declare(
		expected
		GIT_REPOSITORY https://github.com/martinmoene/expected-lite.git
		GIT_TAG v0.8.0 
		EXCLUDE_FROM_ALL
	)

	FetchContent_MakeAvailable(expected)	
	set ( expected_INCLUDE_DIR ${expected_SOURCE_DIR}/include ) 
endif ()

include_directories(${expected_INCLUDE_DIR})
message("expected_INCLUDE_DIR: ${expected_INCLUDE_DIR}")

if(SDK_ADDUNITTEST)
	find_package(Catch2 3 REQUIRED)

	### =========== httpmockserver =========== 
	include_directories(${CMAKE_SOURCE_DIR}/../)
	add_subdirectory(../httpmockserver ${CMAKE_BINARY_DIR}/extern/httpmockserver) # for some unit tests only
	set_target_properties(httpmockserver PROPERTIES FOLDER "SDK/External")
endif()



