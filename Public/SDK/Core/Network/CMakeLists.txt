

be_add_library(Network OBJECT
		Network.h
		Network_api.h
		http.h
		http.cpp
		httpCprImpl.h
		httpCprImpl.cpp
		HttpError.h
		HttpRequest.h
		HttpRequest.cpp
		IHttpRouter.h
		IHttpRouter.cpp
		HttpGetWithLink.h
)
target_compile_features(Network PRIVATE ${DefaultCXXSTD})
if (WITH_HTTPCPR)
target_link_libraries(Network PRIVATE
						cpr::cpr
					 ) 
target_compile_definitions(Network PUBLIC "-DWITH_HTTPCPR")
endif()

target_link_libraries(Network PUBLIC
						reflectcpp::reflectcpp
						Json
						Singleton
						Tools
					 ) 
set_target_properties(Network PROPERTIES FOLDER "SDK/Core")
target_link_libraries(Network PUBLIC libassert::assert)

if(SDK_ADDUNITTEST)
	be_add_library(NetworkMockServer OBJECT
		Tests/AsyncTestHelpers.h
		Tests/AsyncTestHelpers.cpp
		Tests/HttpMock.h
		Tests/HttpMock.cpp
	)
	target_compile_features(NetworkMockServer PRIVATE ${DefaultCXXSTD})
	target_link_libraries(NetworkMockServer PRIVATE
						httpmockserver
					 )
	set_target_properties(NetworkMockServer PROPERTIES FOLDER "SDK/Core/Tests")

	be_add_executable(NetworkTest Tests/NetworkHttpTest.cpp)
	target_compile_features(NetworkTest PRIVATE ${DefaultCXXSTD})
	set_target_properties(NetworkTest PROPERTIES FOLDER "SDK/Core/Tests")
	target_link_libraries(NetworkTest PRIVATE
							Network
							Catch2::Catch2WithMain
							NetworkMockServer
							Tools
						)
	if (SINGLETONDLL)
		add_custom_command(TARGET NetworkTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Singleton>
				$<TARGET_FILE_DIR:NetworkTest>)
	endif()
	if (ADVVIZDLL)
		add_custom_command(TARGET NetworkTest POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				$<TARGET_FILE:Visualization>
				$<TARGET_FILE_DIR:NetworkTest>)
	endif()
	if (MSVC)
		target_compile_options(NetworkTest PRIVATE "/Zc:preprocessor")
	endif()
	be_add_sdk_test (NetworkTest
		COVERED_TARGETS Network
	)
endif()
