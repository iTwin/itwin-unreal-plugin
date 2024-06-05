
add_library(Visualization 
		Visualization.ixx
		Scene.ixx
		Scene.cpp
		Config.ixx
		Config.cpp
)
target_compile_features(Visualization PRIVATE ${DefaultCXXSTD})


target_link_libraries(Visualization PRIVATE 
						Network
						Tools
					 ) 

set_target_properties(Visualization PROPERTIES FOLDER "SDK/Core") 


####  Unit Tests
add_executable(VisualizationTest VisualizationTest.cpp)
target_compile_features(VisualizationTest PRIVATE ${DefaultCXXSTD})
target_compile_definitions(VisualizationTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

set_target_properties(VisualizationTest PROPERTIES FOLDER "SDK/Core/Tests")

#add_test(NAME VisualizationTest COMMAND VisualizationTest)

target_link_libraries(VisualizationTest PRIVATE 
						Visualization
						tiny-process-library
						GTest::gtest 
						GTest::gtest_main 
						GTest::gmock 
						GTest::gmock_main
					)

include(GoogleTest)
gtest_discover_tests(VisualizationTest)
