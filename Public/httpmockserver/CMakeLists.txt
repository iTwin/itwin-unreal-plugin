if( TARGET httpmockserver)
	Message("httpmockserver alreay added")
else ()
	# *Begin section*
	# Consistency with <Carrot.git>/Public/CMake/main.cmake.
	# Indeed in Carrot, this CMakeLists.txt may be included early as part of the SDK,
	# thus before the following flags are set.
	set (CMAKE_CXX_STANDARD 20)
	set (CMAKE_UNREALDEBUG_POSTFIX d)
	# *End section* 
	include (be_setup_native_lib_flags)
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

	find_package(MHD MODULE REQUIRED)
	set (HTTPMOCKSERVER_LIBRARIES httpmockserver ${MHD_LIBRARIES} CACHE INTERNAL "")
	set (HTTPMOCKSERVER_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "")

	find_package(CPR REQUIRED)
	set (CPR_FOUND ${CPR_FOUND} CACHE INTERNAL "")
	set (CPR_LIBRARIES ${CPR_LIBRARIES} CACHE INTERNAL "")
	set (CPR_INCLUDE_DIRS ${CPR_INCLUDE_DIRS} CACHE INTERNAL "")

	add_library (httpmockserver
		STATIC
		mock_holder.h
		mock_server.cpp
		mock_server.h
		port_searcher.h
		test_environment.h
	)
	target_link_libraries(httpmockserver PUBLIC 
							${MHD_LIBRARIES}
							${CPR_LIBRARIES}
						)

	target_include_directories (httpmockserver PUBLIC
		${MHD_INCLUDE_DIRS}
	)
endif()
