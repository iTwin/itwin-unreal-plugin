
add_library(Visualization STATIC
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
		MaterialPersistence.cpp
		MaterialPersistence.h
		ScenePersistence.cpp
		ScenePersistence.h
		TextureKey.h
		Timeline.cpp
		Timeline.h
)
target_compile_features(Visualization PRIVATE ${DefaultCXXSTD})
target_link_libraries(Visualization PUBLIC
						ITwinAPI
						Network
						Tools
					 )
target_link_libraries(Visualization PRIVATE
						fmt::fmt-header-only
						Singleton
					 )
set_target_properties(Visualization PROPERTIES FOLDER "SDK/Core") 

if(SDK_ADDUNITTEST)
	add_executable(VisualizationTest VisualizationTest.cpp)
	target_compile_features(VisualizationTest PRIVATE ${DefaultCXXSTD})
	target_compile_definitions(VisualizationTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

	set_target_properties(VisualizationTest PROPERTIES FOLDER "SDK/Core/Tests")

	#add_test(NAME VisualizationTest COMMAND VisualizationTest)

	target_link_libraries(VisualizationTest PRIVATE 
							Visualization
							httpmockserver
							Catch2::Catch2WithMain
						)

	add_custom_command(TARGET VisualizationTest POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			$<TARGET_FILE:Singleton>
			$<TARGET_FILE_DIR:VisualizationTest>)

	if (MSVC)
		target_compile_options(VisualizationTest PRIVATE "/Zc:preprocessor")
	endif()

	include(CTest)
	include(Catch)
	catch_discover_tests(VisualizationTest)
endif()