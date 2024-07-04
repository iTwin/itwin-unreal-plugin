add_library(Json INTERFACE
	Json.h
)
target_link_libraries (Json INTERFACE
	reflectcpp
)
set_target_properties(Json PROPERTIES FOLDER "SDK/Core") 

#### =========  Unit Tests ===========

add_executable(JsonTest JsonTest.cpp)
target_compile_features(JsonTest PRIVATE ${DefaultCXXSTD})
set_target_properties(JsonTest PROPERTIES FOLDER "SDK/Core/Tests")

#add_test(NAME JsonTest COMMAND JsonTest)

target_link_libraries(JsonTest PRIVATE 
						Json
						Catch2::Catch2WithMain
					)
include(CTest)
include(Catch)
catch_discover_tests(JsonTest)