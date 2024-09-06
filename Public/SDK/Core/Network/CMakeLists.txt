
add_library(Network OBJECT
		Network.h
		http.h
		http.cpp
		httpCprImpl.h
		httpCprImpl.cpp
		HttpRequest.h
		HttpRequest.cpp
)
target_compile_features(Network PRIVATE ${DefaultCXXSTD})
target_link_libraries(Network PRIVATE 
						cpr::cpr
						reflectcpp
						Json
					 ) 

set_target_properties(Network PROPERTIES FOLDER "SDK/Core") 


if(SDK_ADDUNITTEST)
	add_executable(NetworkTest NetworkHttpTest.cpp)
	target_compile_features(NetworkTest PRIVATE ${DefaultCXXSTD})
	set_target_properties(NetworkTest PROPERTIES FOLDER "SDK/Core/Tests")
	target_link_libraries(NetworkTest PRIVATE 
							Network 
							Catch2::Catch2WithMain
							httpmockserver
						)
	if (MSVC)
		target_compile_options(NetworkTest PRIVATE "/Zc:preprocessor")
	endif()
	include(CTest)
	include(Catch)
	catch_discover_tests(NetworkTest)
endif()