
set(libtype STATIC)
if (ADVVIZDLL)
    set(libtype SHARED)
endif()

be_add_library(Visualization ${libtype} 
		Visualization.h
		Decoration.h
		Decoration.cpp
		Config.h
		Config.cpp
		ConstantIDs.h
		Cutout.h
		Cutout.cpp
		Instance.h
		Instance.cpp
		InstancesGroup.h
		InstancesGroup.cpp
		InstancesManager.h
		InstancesManager.cpp
		AnnotationsManager.h
		AnnotationsManager.cpp
		AsyncHelpers.cpp
		AsyncHelpers.h
		AsyncHttp.inl
		MaterialPersistence.cpp
		MaterialPersistence.h
		ScenePersistence.cpp
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
		PathAnimation.h
		PathAnimation.cpp 
		PopulationPersistence.h
		PopulationPersistence.cpp
		RefID.h
		RefID.cpp
		SavableItem.h
		SavableItemManager.cpp
		SavableItemManager.inl
		SavableItemManager.h
		SaveStatus.h
)
target_compile_features(Visualization PRIVATE ${DefaultCXXSTD})
if ( MSVC )
	set_source_files_properties ( ScenePersistenceAPI.cpp PROPERTIES COMPILE_FLAGS " /bigobj" )
endif ()
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
configure_advviz_library(Visualization)
set_target_properties(Visualization PROPERTIES FOLDER "SDK/Core") 

if(SDK_ADDUNITTEST)
	be_add_executable(VisualizationTest 
		Tests/VisualizationTest.cpp 
		Tests/VizKeyframeTests.cpp
		Tests/SceneAPITest.cpp
		Tests/RefIDTest.cpp
		Tests/AnnotationTest.cpp
		Tests/InstancesTest.cpp
		Tests/SplinesTest.cpp
		Tests/PathAnimTest.cpp
		Tests/TimelineTest.cpp
	)
	target_compile_features(VisualizationTest PRIVATE ${DefaultCXXSTD})
	target_compile_definitions(VisualizationTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

	set_target_properties(VisualizationTest PROPERTIES FOLDER "SDK/Core/Tests")

	if (MSVC AND DEFINED fmt_LIB_DIR ) # quick fix for link error when embedded in Unreal
		set ( fmt_lib "${fmt_LIB_DIR}fmt.lib" )
	else ()
		set ( fmt_lib "${ADVVIZ_FMT_DEP}" )
	endif()
	target_link_libraries(VisualizationTest PRIVATE
							Visualization
							${fmt_lib}
							NetworkMockServer
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
	configure_advviz_library(VisualizationTest)

	be_add_sdk_test (VisualizationTest
		COVERED_TARGETS Visualization
	)
endif()
