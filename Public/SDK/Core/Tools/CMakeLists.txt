
add_library(Tools OBJECT
		Tools.h
		TypeId.h
		Types.h
		Extension.h
		Extension.cpp
		FactoryClass.h
		FactoryClass.cpp
		fnv1ahash.h
)
target_compile_features(Tools PRIVATE ${DefaultCXXSTD})
set_target_properties(Tools PROPERTIES FOLDER "SDK/Core") 

if ( DEFINED glm_INCLUDE_DIR )
	target_include_directories(Tools PUBLIC ${glm_INCLUDE_DIR} )
endif ()


####  Unit Tests
add_executable(ToolsTest ToolsTest.cpp)
target_compile_features(ToolsTest PRIVATE ${DefaultCXXSTD})
target_compile_definitions(ToolsTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
set_target_properties(ToolsTest PROPERTIES FOLDER "SDK/Core/Tests")
target_link_libraries(ToolsTest PRIVATE 
						Tools
						Catch2::Catch2WithMain
					)

include(CTest)
include(Catch)
catch_discover_tests(ToolsTest)
