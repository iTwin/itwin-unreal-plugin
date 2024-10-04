
add_library ( ITwinAPI OBJECT
	ITwinAuthManager.cpp
	ITwinAuthManager.h
	ITwinEnvironment.h
	ITwinMaterial.cpp
	ITwinMaterial.h
	ITwinTypes.h
	ITwinWebServices.cpp
	ITwinWebServices.h
	ITwinWebServicesObserver.cpp
	ITwinWebServicesObserver.h
)
target_compile_features ( ITwinAPI PRIVATE ${DefaultCXXSTD} )
if ( MSVC )
	set_source_files_properties ( ITwinWebServices.cpp PROPERTIES COMPILE_FLAGS " /bigobj" )
endif ()
target_link_libraries( ITwinAPI PRIVATE 
					   Network
					   Tools
					   fmt::fmt-header-only
					  )
set_target_properties ( ITwinAPI PROPERTIES FOLDER "SDK/Core" ) 
