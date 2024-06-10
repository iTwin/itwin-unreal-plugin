
add_library(Tools OBJECT
		Tools.h
		Extension.h
		Extension.cpp
		FactoryClass.h
		FactoryClass.cpp
)
target_compile_features(Tools PRIVATE ${DefaultCXXSTD})
set_target_properties(Tools PROPERTIES FOLDER "SDK/Core") 


####  Unit Tests
add_executable(ToolsTest ToolsTest.cpp)
target_compile_features(ToolsTest PRIVATE ${DefaultCXXSTD})
target_compile_definitions(ToolsTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
set_target_properties(ToolsTest PROPERTIES FOLDER "SDK/Core/Tests")
target_link_libraries(ToolsTest PRIVATE 
						Tools
						GTest::gtest 
						GTest::gtest_main 
						GTest::gmock 
						GTest::gmock_main
					)

include(GoogleTest)
gtest_discover_tests(ToolsTest)
