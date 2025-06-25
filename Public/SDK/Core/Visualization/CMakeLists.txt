
set(libtype STATIC)
if (ADVVIZDLL)
    set(libtype SHARED)
endif()

add_library(Visualization ${libtype} 
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
		ScenePersistenceDS.cpp
		ScenePersistenceDS.h
		ScenePersistence.h
		ScenePersistenceAPI.cpp
		ScenePersistenceAPI.h
		TextureKey.h
		TextureUsage.h
		Spline.h
		Spline.cpp
		SplinesManager.h
		SplinesManager.cpp
		Timeline.cpp
		Timeline.h
		KeyframeAnimation.h
		KeyframeAnimation.cpp 
		KeyframeAnimator.h
		KeyframeAnimator.cpp 
		RefID.h
		RefID.cpp
)
target_compile_features(Visualization PRIVATE ${DefaultCXXSTD})
target_link_libraries(Visualization PUBLIC
						ITwinAPI
						Network
						Tools
						Singleton
					 )
target_link_libraries(Visualization PRIVATE
						${ADVVIZ_FMT_DEP}
						reflectcpp::reflectcpp
					 )
if (MSVC)
	target_compile_options(Visualization PRIVATE /utf-8) # Needed for "fmt" lib when ADVVIZ_FMT_DEP is empty.
endif ()
set_target_properties(Visualization PROPERTIES FOLDER "SDK/Core") 

if(SDK_ADDUNITTEST)
	add_executable(VisualizationTest 
		Tests/VisualizationTest.cpp 
		Tests/VizKeyframeTests.cpp
		Tests/SceneAPITest.cpp
		Tests/Mock.h
		Tests/RefIDTest.cpp
	)
	target_compile_features(VisualizationTest PRIVATE ${DefaultCXXSTD})
	target_compile_definitions(VisualizationTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

	set_target_properties(VisualizationTest PROPERTIES FOLDER "SDK/Core/Tests")

	#add_test(NAME VisualizationTest COMMAND VisualizationTest)

	if (MSVC AND DEFINED fmt_LIB_DIR ) # quick fix for link error when embedded in Unreal
		set ( fmt_lib "${fmt_LIB_DIR}fmt.lib" )
	else ()
		set ( fmt_lib "${ADVVIZ_FMT_DEP}" )
	endif()
	target_link_libraries(VisualizationTest PRIVATE 
							Visualization
							${fmt_lib}
							httpmockserver
							Catch2::Catch2WithMain
						)
	if (SINGLETONDLL)
		add_custom_command(TARGET VisualizationTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Singleton>
				$<TARGET_FILE_DIR:VisualizationTest>)
	endif()

	if (ADVVIZDLL)
		add_custom_command(TARGET VisualizationTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Visualization>
				$<TARGET_FILE_DIR:VisualizationTest>)
	endif()

	if (MSVC)
		target_compile_options(VisualizationTest PRIVATE "/Zc:preprocessor")
	endif()

	include(CTest)
	include(Catch)
	catch_discover_tests(VisualizationTest)
endif()