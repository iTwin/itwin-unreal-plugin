
add_library(Network OBJECT
		Network.h
		http.h
		http.cpp
		httpCprImpl.h
		httpCprImpl.cpp
		HttpRequest.h
		HttpRequest.cpp
		IHttpRouter.h
		IHttpRouter.cpp
		HttpGetWithLink.h
)
target_compile_features(Network PRIVATE ${DefaultCXXSTD})
target_link_libraries(Network PRIVATE 
						cpr::cpr
						reflectcpp
						Json
						Singleton
					 ) 

set_target_properties(Network PROPERTIES FOLDER "SDK/Core") 
target_link_libraries(Network PUBLIC libassert::assert)

if(SDK_ADDUNITTEST)
	add_executable(NetworkTest NetworkHttpTest.cpp)
	target_compile_features(NetworkTest PRIVATE ${DefaultCXXSTD})
	set_target_properties(NetworkTest PROPERTIES FOLDER "SDK/Core/Tests")
	target_link_libraries(NetworkTest PRIVATE 
							Network 
							Catch2::Catch2WithMain
							httpmockserver
						)

	add_custom_command(TARGET NetworkTest POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			$<TARGET_FILE:Singleton>
			$<TARGET_FILE_DIR:NetworkTest>)


	if (MSVC)
		target_compile_options(NetworkTest PRIVATE "/Zc:preprocessor")
	endif()
	include(CTest)
	include(Catch)
	catch_discover_tests(NetworkTest)
endif()