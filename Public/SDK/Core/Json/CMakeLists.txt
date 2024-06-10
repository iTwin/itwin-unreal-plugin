


#### =========  Unit Tests ===========

add_executable(JsonTest JsonTest.cpp)
target_compile_features(JsonTest PRIVATE ${DefaultCXXSTD})
set_target_properties(JsonTest PROPERTIES FOLDER "SDK/Core/Tests")

#add_test(NAME JsonTest COMMAND JsonTest)

target_link_libraries(JsonTest PRIVATE 
						reflectcpp
						GTest::gtest 
						GTest::gtest_main 
						GTest::gmock 
						GTest::gmock_main
					)

include(GoogleTest)
gtest_discover_tests(JsonTest)
