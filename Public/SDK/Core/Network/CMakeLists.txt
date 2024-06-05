
add_library(Network 
		Network.ixx
		http.ixx
		http.cpp
		httpCprImpl.ixx
		httpImpl.cpp 
)
target_compile_features(Network PRIVATE ${DefaultCXXSTD})
target_link_libraries(Network PRIVATE 
						cpr::cpr 
						Json
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
