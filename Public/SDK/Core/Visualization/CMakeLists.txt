
add_library(Visualization 
		Visualization.h
		Decoration.h
		Decoration.cpp
		Config.h
		Config.cpp
		Instance.h
		Instance.cpp
		InstancesGroup.h
		InstancesGroup.cpp
		InstancesManager.h
		InstancesManager.cpp
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
						Catch2::Catch2WithMain
					)

include(CTest)
include(Catch)
catch_discover_tests(VisualizationTest)
