
add_library(Tools OBJECT
		Tools.h
		TypeId.h
		Types.h
		Extension.h
		Assert.h
		DelayedCall.h
		DelayedCallEnums.h
		Error.h
		Log.h
		Log.cpp
		Hash.h
		AssertHandler.h
		AssertHandler.cpp
		Extension.cpp
		FactoryClass.h
		FactoryClassInternalHelper.h
		FactoryClass.cpp
		fnv1ahash.h
		IDelayedCallHandler.h
		IDelayedCallHandler.cpp
		JsonCacheUtilities.h
		JsonCacheUtilities.cpp
		StrongTypeId.h
		LockableObject.h
		CommonInterfaceClass.h
		TaskManager.h
		TaskManager.cpp
		../AdvVizLinkType.h
		GCSTransform.h
		GCSTransform.cpp
		CrashInfo.h
		CrashInfo.cpp
)
target_compile_features(Tools PRIVATE ${DefaultCXXSTD})
set_target_properties(Tools PROPERTIES FOLDER "SDK/Core") 
target_link_libraries(Tools PUBLIC libassert::assert Singleton)

if ( DEFINED glm_INCLUDE_DIR )
	target_include_directories(Tools PUBLIC ${glm_INCLUDE_DIR} )
endif ()


if(SDK_ADDUNITTEST)
	add_executable(ToolsTest ToolsTest.cpp)
	target_compile_features(ToolsTest PRIVATE ${DefaultCXXSTD})
	target_compile_definitions(ToolsTest PRIVATE "CMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
	set_target_properties(ToolsTest PROPERTIES FOLDER "SDK/Core/Tests")
	target_link_libraries(ToolsTest PRIVATE 
							Tools
							Catch2::Catch2
						)
	if (SINGLETONDLL)
		add_custom_command(TARGET ToolsTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Singleton>
				$<TARGET_FILE_DIR:ToolsTest>)
	endif()
	if (ADVVIZDLL)
		add_custom_command(TARGET ToolsTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Visualization>
				$<TARGET_FILE_DIR:ToolsTest>)
	endif()
	if (MSVC)
		target_compile_options(ToolsTest PRIVATE "/Zc:preprocessor")
	endif()

	include(CTest)
	include(Catch)
	catch_discover_tests(ToolsTest)
endif()