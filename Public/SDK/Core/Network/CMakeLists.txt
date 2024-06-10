
add_library(Network OBJECT
		Network.h
		http.h
		http.cpp
		httpCprImpl.h
		httpImpl.cpp 
)
target_compile_features(Network PRIVATE ${DefaultCXXSTD})
target_link_libraries(Network PRIVATE 
						cpr::cpr
						reflectcpp
					 ) 

set_target_properties(Network PROPERTIES FOLDER "SDK/Core") 


#### =========  Unit Tests ===========

add_executable(NetworkTest NetworkHttpTest.cpp)
target_compile_features(NetworkTest PRIVATE ${DefaultCXXSTD})
set_target_properties(NetworkTest PROPERTIES FOLDER "SDK/Core/Tests")
target_link_libraries(NetworkTest PRIVATE 
						Network 
						GTest::gtest 
						GTest::gtest_main 
						GTest::gmock 
						GTest::gmock_main
					)

include(GoogleTest)
gtest_discover_tests(NetworkTest)
